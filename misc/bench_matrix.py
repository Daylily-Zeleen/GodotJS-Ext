#!/usr/bin/env python3
"""Two-leg benchmark matrix runner: static vs dynamic x (--gc / no --gc).

Hardens every step of the data-collection workflow that has historically gone
wrong when done by hand. Each guard exists because of a concrete incident:

1.  DLL identity (md5) is snapshotted BEFORE and AFTER every single run and
    must be identical -- a background `scons` finishing mid-collection used to
    silently swap the deployed DLL mid-matrix (v3_dyn_nogc incident).
2.  Leg identity is asserted from the LOG ITSELF, not from what we believe we
    deployed: the static leg prints "static binding not found" fallback
    warnings (>=1), the dynamic leg prints none (==0). A mislabeled leg is a
    hard error, not a post-hoc discovery.
3.  Both gdextension deployment targets (main + editor) are md5-compared;
    replacing only one of the two used to poison runs.
4.  `--gc` effectiveness is asserted via the report's `gcRequested` field;
    argument-position mistakes (-- --bench) used to silently run the full TS
    suite instead (detected via the "Loading scene" marker).
5.  Round count, COMPLETED sentinel, exit code and invalid=0 are checked per
    run; any failure aborts the matrix before more time is wasted.

Usage:
    python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix
    python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix --leg static --gc-only
    python misc/bench_matrix.py --report .agent_tmp/matrix   # summarize only

Outputs (under --out):
    <leg>_<gc|nogc>_r<i>.log   raw engine logs (evidence, never hand-edited)
    manifest.json              per-round md5 + log fingerprint metadata
    report.md                  median tables (per-case + per-group)
"""

import argparse
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

# Release-flavor engine: a 4.8.dev template_release build placed next to the
# editor build (godot.windows.template_release.x86_64.exe). Benchmarks and
# size comparisons MUST use it -- the editor build carries editor-only code
# (debug helpers, TOOLS_ENABLED paths) that skews both timing and size.
GODOT = "D:/Dev/godot/godot/bin/godot.windows.template_release.x86_64.exe"
SCONS_TARGET = "template_release"
BENCH_JSON_RE = re.compile(r'BENCH_JSON (\{.*?\})\s*(?:\r?\n|$)', re.S)



def md5(path: str) -> str:
    import hashlib
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


# Release flavor: gdextension resolves windows.release.x86_64 -> the MAIN dll
# only; the editor-extension DLL is never loaded by a template_release engine.
DLL_MAIN = "bin/windows/godotjs-ext.windows.template_release.x86_64.dll"
DEPLOY_MAIN = "project/addons/godotjs-ext.daylily-zeleen/bin/windows/godotjs-ext.windows.template_release.x86_64.dll"


def check_deploy(log) -> dict:
    """Each of the two gdextension targets (main + editor) is a separate DLL
    with its own deployment copy under addons/; the pair `bin/<X>` vs
    `addons/bin/<X>` must match byte-for-byte. Returns {dll_path: {"md5": ...,
    "size": bytes}}."""
    import os
    pairs = [(DLL_MAIN, DEPLOY_MAIN)]
    sums = {}
    for src, dst in pairs:
        if not (os.path.exists(src) and os.path.exists(dst)):
            raise SystemExit(f"FATAL: missing DLL(s): {src} / {dst} -- run scons first")
        s_src, s_dst = md5(src), md5(dst)
        if s_src != s_dst:
            log(f"  MISMATCH {s_src[:8]} {src} vs {s_dst[:8]} {dst}")
            raise SystemExit("FATAL: deployment md5 mismatch -- copy the fresh DLL into addons/")
        sums[src] = {"md5": s_src, "size": os.path.getsize(src)}
    return sums



def run_bench(out_path: Path, use_gc: bool, log) -> dict:
    # --bench is a USER argument (start.ts reads get_cmdline_user_args);
    # everything goes after `--`.
    user_args = ["--bench"] + (["--gc"] if use_gc else [])
    cmd = [GODOT, "--headless", "--path", "project", "--"] + user_args
    log(f"  run: {' '.join(cmd[1:])}")
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, text=True)
    txt = out_path.read_text(encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"FATAL: exit={proc.returncode} -- see {out_path}")
    if "COMPLETED" not in txt:
        raise SystemExit(f"FATAL: no COMPLETED sentinel -- see {out_path}")
    m = BENCH_JSON_RE.search(txt)
    if not m:
        raise SystemExit(f"FATAL: no BENCH_JSON in output -- see {out_path}")
    report = json.loads(m.group(1))
    if report.get("invalid", 1) != 0:
        raise SystemExit(f"FATAL: {report['invalid']} invalid cases -- see {out_path}")
    want_gc = "true" if use_gc else "false"
    got_gc = str(report.get("gcRequested", "")).lower()
    if got_gc != want_gc:
        raise SystemExit(f"FATAL: gcRequested={got_gc}, wanted {want_gc} -- --gc did not "
                         f"take effect (argument position?) -- see {out_path}")
    # Wrong-entry guard: `-- --bench` (user arg) must load the benchmark
    # scene via start.ts; if the log's "Loading scene" line shows anything
    # other than the benchmark scene, the regular TS test suite ran instead.
    for line in txt.splitlines():
        if "Loading scene" in line:
            if "tests/benchmark" not in line:
                raise SystemExit(f"FATAL: full TS suite ran instead of benchmark "
                                 f"(--bench not seen as a user arg?) -- see {out_path}")
            break
    return report


def assert_leg(log_path: Path, expect_static: bool) -> None:
    """Leg identity from the log itself: static binding falls back to dynamic
    for 5 class methods not in the generated table (OS.get_preferred_locales,
    ResourceLoader.get_resource_type, ...); the dynamic leg has no static
    table to consult and never prints this warning."""
    txt = log_path.read_text(encoding="utf-8", errors="replace")
    n = len(re.findall(r"static binding not found", txt))
    hint = ("\nSwitch legs with: scons target=template_release ... [static_binding=no] -j6, "
            "copy both DLLs into addons/, then re-run.")
    if expect_static and n < 1:
        raise SystemExit(f"FATAL: {log_path.name} claims static but shows no "
                         f"'static binding not found' fallback -- DLL is NOT the static leg.{hint}")
    if not expect_static and n != 0:
        raise SystemExit(f"FATAL: {log_path.name} claims dynamic but shows {n} "
                         f"'static binding not found' fallbacks -- DLL is NOT the dynamic leg.{hint}")


def build_and_deploy(leg: str, log) -> None:
    """scons the requested leg and copy both fresh DLLs into the addons
    deployment tree. With --build this runs automatically between legs;
    without it the caller must have already built+deployed the right leg
    (the log-fingerprint guard verifies it either way)."""
    flag = "static_binding=yes" if leg == "static" else "static_binding=no"
    cmd = ["scons", f"target={SCONS_TARGET}", "compiledb=no", "debug_symbols=no",
           "dev_build=no", "verbose=no", flag, "-j6"]
    log(f"  scons {flag} ...")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        log(proc.stdout[-2000:])
        raise SystemExit(f"FATAL: scons failed for {leg} leg")
    import shutil
    shutil.copyfile(DLL_MAIN, DEPLOY_MAIN)
    info = check_deploy(log)
    log(f"  deployed: " + ", ".join(
            f"{k.split('/')[-1]} {v['md5'][:8]} ({v['size'] / 1024 / 1024:.1f} MiB)"
            for k, v in info.items()))


def collect(args, log):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    manifest_path = out / "manifest.json"

    # Stale-round guard: re-collecting fewer rounds into the same directory
    # leaves r<N+1..> logs behind. They are NOT read by --report (manifest.json
    # is the single source of truth), but their presence invites confusion --
    # warn loudly and offer the reason.
    if manifest_path.exists():
        try:
            old = json.loads(manifest_path.read_text(encoding="utf-8"))["runs"]
            old_rounds = max((r["round"] for r in old), default=0)
            if old_rounds > args.rounds:
                log(f"WARNING: this directory already holds a matrix with {old_rounds} "
                    f"rounds; re-collecting with --rounds {args.rounds} will overwrite "
                    f"manifest.json and r1..r{args.rounds} logs. Leftover r{args.rounds + 1}..r{old_rounds} "
                    f"files stay on disk but are EXCLUDED from future reports. "
                    f"Use a fresh --out directory for a clean matrix.")
        except (json.JSONDecodeError, KeyError):
            pass

    manifest = {"runs": []}
    legs = ["static", "dynamic"] if args.leg in (None, "both") else [args.leg]
    gcs = [True, False] if not args.gc_only else [True]

    if args.build:
        build_and_deploy(legs[0], log)
    else:
        check_deploy(log)

    for leg_index, leg in enumerate(legs):
        if args.build and leg_index > 0:
            build_and_deploy(leg, log)
        for use_gc in gcs:
            tag = f"{leg}_{'gc' if use_gc else 'nogc'}"
            for rnd in range(1, args.rounds + 1):
                # Guard 1: identity before AND after each run. A background
                # scons completing mid-matrix changes the md5 -> abort.
                before = check_deploy(log)
                log_path = out / f"{tag}_r{rnd}.log"
                log(f"[{tag} r{rnd}]")
                report = run_bench(log_path, use_gc, log)
                after = check_deploy(log)
                if before[DLL_MAIN]["md5"] != after[DLL_MAIN]["md5"]:
                    raise SystemExit(f"FATAL: dll changed DURING run {tag} r{rnd} "
                                     f"({before[DLL_MAIN]['md5'][:8]} -> {after[DLL_MAIN]['md5'][:8]}) -- "
                                     f"rebuild finished mid-run; matrix aborted, discard this batch")
                assert_leg(log_path, expect_static=(leg == "static"))
                manifest["runs"].append({
                    "leg": leg, "gc": use_gc, "round": rnd,
                    "log": str(log_path), "dll_md5": after[DLL_MAIN]["md5"][:8],
                    "dll_size_main": after[DLL_MAIN]["size"],
                    "dll_size_main": after[DLL_MAIN]["size"],
                    "invalid": report["invalid"], "cases": len(report["results"]),
                })
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    log(f"OK: {len(manifest['runs'])} runs collected, manifest written")
    # keep a single leg-md5 summary
    legs_md5 = sorted({r["dll_md5"] for r in manifest["runs"]})
    log(f"dll md5s seen: {legs_md5}")
    summarize(args.out, log)


def summarize(matrix_dir, log):
    out = Path(matrix_dir)
    runs = json.loads((out / "manifest.json").read_text(encoding="utf-8"))["runs"]

    def load():
        by_tag = {}
        for r in runs:
            t = f"{r['leg']}_{'gc' if r['gc'] else 'nogc'}"
            by_tag.setdefault(t, []).append(r["log"])
        return by_tag

    by_tag = load()
    def med_map(tag):
        vals = {}
        if tag not in by_tag:
            return vals
        for p in by_tag[tag]:
            txt = Path(p).read_text(encoding="utf-8", errors="replace")
            d = json.loads(BENCH_JSON_RE.search(txt).group(1))
            for res in d["results"]:
                vals.setdefault(res["name"], []).append(res["nsPerCall"])
        return {k: statistics.median(v) for k, v in vals.items()}

    missing = [t for t in ("static_gc", "static_nogc", "dynamic_gc", "dynamic_nogc")
               if t not in by_tag]
    if missing:
        print(f"NOTE: incomplete matrix, missing cells: {missing}")
    sgc = med_map("static_gc")
    sgn = med_map("static_nogc")
    dgc = med_map("dynamic_gc")
    dgn = med_map("dynamic_nogc")
    if not sgc and not dgc:
        raise SystemExit("FATAL: no complete cells (static_gc / dynamic_gc) to report")
    keys = sorted(sgc)

    def pct(vals, p):
        v = sorted(vals)
        i = (len(v) - 1) * p
        lo, hi = int(i), min(int(i) + 1, len(v) - 1)
        return v[lo] + (v[hi] - v[lo]) * (i - lo)

    lines = []
    lines.append("# Two-leg benchmark matrix report\n")
    rounds_by_cell = {}
    for r in runs:
        t = f"{r['leg']}_{'gc' if r['gc'] else 'nogc'}"
        rounds_by_cell[t] = rounds_by_cell.get(t, 0) + 1
    cell_desc = ", ".join(f"{k}×{v}" for k, v in sorted(rounds_by_cell.items()))
    lines.append(f"rounds actually counted: {cell_desc} "
                 f"(from manifest.json; --rounds on the command line does NOT change this); "
                 f"all runs dll-verified (md5 stable per run) + log-fingerprint verified\n")
    lines.append("## GC impact (no-gc/gc, >1 = running without --gc is slower)\n")
    for tag, mno, mgc in [("static", sgn, sgc), ("dynamic", dgn, dgc)]:
        if not mno or not mgc:
            continue
        common = [k for k in mgc if k in mno]
        rs = [mno[k] / mgc[k] for k in common]
        n_slow = sum(1 for r in rs if r > 1.05)
        lines.append(f"- {tag}: median {pct(rs, .5):.2f}x  P90 {pct(rs, .9):.2f}x  "
                     f"(no-gc slower >5%: {n_slow}/{len(rs)})")
    lines.append("\n## Legs (S/D by gc condition)\n")
    for cond, s_map, d_map in [("gc", sgc, dgc), ("no-gc", sgn, dgn)]:
        common = [k for k in s_map if k in d_map]
        rs = [s_map[k] / d_map[k] for k in common]
        if not rs:
            lines.append(f"- {cond}: (no comparable cells)")
            continue
        lines.append(f"- {cond}: median {pct(rs, .5):.2f}x  P10 {pct(rs, .1):.2f}x  "
                     f"P90 {pct(rs, .9):.2f}x  (static slower >15%: {sum(1 for r in rs if r > 1.15)}, "
                     f"static faster >13%: {sum(1 for r in rs if r < 0.87)})")
    lines.append("\n## Per-case (medians, ns)\n")
    lines.append("| case | S-gc | D-gc | S/D | S-nogc | D-nogc | S/D |")
    lines.append("|---|---|---|---|---|---|---|")
    for k in keys:
        s, d = sgc.get(k), dgc.get(k)
        sn, dn = sgn.get(k), dgn.get(k)
        ratio = f"{s / d:.2f}x" if s and d else "n/a"
        ratio_n = f"{sn / dn:.2f}x" if sn and dn else "n/a"
        def fmt(v):
            return f"{v:.1f}" if v is not None else "n/a"
        lines.append(f"| {k} | {fmt(s)} | {fmt(d)} | {ratio} | {fmt(sn)} | {fmt(dn)} | {ratio_n} |")

    # DLL size comparison: static linking inlines the operator/method tables
    # into the DLL; the dynamic leg ships none of them. Manifest entries carry
    # the sizes captured per run (all rounds of one leg share one build).
    sizes = {}
    for r in runs:
        leg = r["leg"]
        if leg not in sizes:
            sizes[leg] = r.get("dll_size_main")
    if sizes and any(sizes.values()):
        lines.append("\n## DLL size (release flavor, main gdextension)\n")
        lines.append("| leg | main dll |")
        lines.append("|---|---|")
        for leg in ("static", "dynamic"):
            if leg not in sizes or sizes[leg] is None:
                continue
            lines.append(f"| {leg} | {sizes[leg] / 1024 / 1024:.2f} MiB |")
        if "static" in sizes and "dynamic" in sizes and sizes["static"] and sizes["dynamic"]:
            st, dy = sizes["static"], sizes["dynamic"]
            lines.append(f"\nstatic/dynamic size ratio: **{st / dy:.2f}x** "
                         f"(static {st - dy:+.0f} bytes vs dynamic)")
    report = "\n".join(lines) + "\n"

    # per-case round-by-round dispersion: makes "adding rounds didn't move the
    # median" visible as stability instead of looking like a stale report.
    import math
    disp = ["\n## Per-case dispersion (per-round nsPerCall across rounds)\n"]
    disp.append("| case | S-gc min~max | D-gc min~max | S-gc stdev | D-gc stdev |")
    disp.append("|---|---|---|---|---|")
    raw = {}
    for tag, logs in by_tag.items():
        for p in logs:
            d = json.loads(BENCH_JSON_RE.search(Path(p).read_text(encoding="utf-8", errors="replace")).group(1))
            for res in d["results"]:
                raw.setdefault((tag, res["name"]), []).append(res["nsPerCall"])
    for k in keys:
        def cell(tag):
            v = raw.get((tag, k), [])
            if not v:
                return "n/a"
            sd = statistics.stdev(v) if len(v) > 1 else 0.0
            return f"{min(v):.0f}~{max(v):.0f} (sd {sd:.1f})"
        disp.append(f"| {k} | {cell('static_gc')} | {cell('dynamic_gc')} | "
                    f"{cell('static_nogc')} | {cell('dynamic_nogc')} |")
    report += "\n".join(disp) + "\n"
    report_path = (out / "report.md").resolve()
    (out / "report.md").write_text(report, encoding="utf-8")
    log(f"OK: {len(runs)} runs summarized -> {report_path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rounds", type=int, default=4, help="runs per matrix cell (default 4)")
    ap.add_argument("--out", default=".agent_tmp/matrix", help="output directory")
    ap.add_argument("--report", help="summarize an existing matrix directory, no runs")
    ap.add_argument("--leg", choices=["static", "dynamic", "both"], default="both")
    ap.add_argument("--build", action="store_true",
                    help="full pipeline: scons each leg (--leg both = build static, "
                         "collect, rebuild dynamic, collect) and deploy before collecting")
    ap.add_argument("--gc-only", action="store_true", help="skip the no-gc cells")
    args = ap.parse_args()

    if args.report:
        if args.rounds != 4:
            print("NOTE: --rounds has NO effect in --report mode -- it only re-summarizes "
                  "the logs already collected in the directory. To collect fresh data with "
                  "N rounds: python misc/bench_matrix.py --rounds N --build --out <dir>")
        if args.leg != "both" or args.build or args.gc_only:
            print("NOTE: --leg/--build/--gc-only are collection flags; ignored in --report mode.")
        summarize(args.report, print)
        return 0

    if not Path(GODOT).exists():
        raise SystemExit(f"FATAL: engine not found: {GODOT}")
    if not Path("project/project.godot").exists():
        raise SystemExit("FATAL: run from the repository root (./project missing)")
    collect(args, print)
    return 0


if __name__ == "__main__":
    sys.exit(main())
