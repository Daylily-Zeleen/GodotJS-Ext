#!/usr/bin/env python
"""Package the CI build artifacts into one release archive per JS engine.

This script is the SINGLE derivation point between the build matrix in
`.github/workflows/ci.yml` and the release assets:

    ci.yml build matrix  ->  plan  ->  the set of artifacts a package contains
                             |
                             +----> assemble -> package contents + .gdextension files
                             |
                             +----> verify   -> release gate (no engine may go missing)

Both the release workflow (`misc_release.yml`) and the CI gate
(`ci.yml` -> `verify-release-artifacts`) call into here, so a change to the
matrix is automatically reflected in the packages and in the gate. Do not add
a second list of platforms/engines anywhere.

Engines published (see the task PRD): v8, qjs-ng, jsc, node and web (the
browser host JS engine). The original quickjs is superseded by quickjs-ng and
is deliberately not packaged. Desktop legs only exist for `target=editor`, so
a desktop entry is always `*.debug.editor.*`; the mobile/web legs are
`template_release` only.

Subcommands:

    plan     [--engine E | --all]                     print expected artifact names
    fetch    --engine E --run <run-id> --dir D        download the legs of one package
    assemble --engine E --artifacts D --out O         build <O>/<name>/ and <O>/<name>.zip
    verify   --all [--artifacts-json F | --run ID]    gate: every engine present and complete

`assemble` self-checks that every `[libraries]` / `[dependencies]` entry it
wrote exists inside the package and that no packaged library is left
unreferenced; a mismatch fails the run loudly.
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover - the CI images ship PyYAML
    print("PyYAML is required (pip install pyyaml)", file=sys.stderr)
    raise SystemExit(2)

ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
ADDON_DIR = ROOT / "project" / "addons" / "godotjs-ext.daylily-zeleen"

# Publish order is also the order assets are reported in.
ENGINE_ORDER = ["v8", "qjs-ng", "node", "jsc", "web"]
# Platform token order inside a package name (matches the historical
# "windows-linux-macos" phrasing, extended with the mobile/web platforms).
PLATFORM_ORDER = ["windows", "linux", "macos", "android", "ios", "web"]

ZIP_STEM = "godotjs-ext"
# One library, two products: `target=editor` and `target=template_*` build the
# same GDExtension target with different macros, so a package carries a single
# `.gdextension` whose desktop keys point at the editor/product flavor built
# for that leg (see `.github/workflows/ci.yml` matrix).
GEXTENSION = "godotjs-ext.gdextension"

# A packaged file is a loadable library when it carries the extension for its
# platform.
LIBRARY_SUFFIXES = (".dll", ".so", ".dylib", ".wasm")
XCFRAMEWORK_SUFFIX = ".xcframework"

# Linker by-products that end users can never use: static archives (MinGW `.a`),
# import libraries / export files (`.lib`/`.exp`), debug sidecars (`.pdb`),
# incremental-link leftovers (`.ilk`) and module-definition files (`.def`, the
# N-API export lists consumed when linking `node.dll`/`godotjs-ext.exe`). The
# engine loads none of them; they were packed only because the build directory
# was copied wholesale.
LINK_JUNK_SUFFIXES = (".a", ".lib", ".exp", ".pdb", ".ilk", ".def")

# The uploaded zips are distribution artifacts: strip the loadable libraries.
# MinGW PE and ELF builds carry full DWARF (a debug_symbols=yes editor DLL was
# 282 MiB shipped as-is) *and* a symbol/string table that survives
# `--strip-debug` - measured at 22.1 MiB of the 38.6 MiB shipped Windows DLL
# and 5.1 MiB of the 21.5 MiB shipped Linux .so. `--strip-all` removes both;
# exported symbols (PE export table, ELF dynsym, Mach-O export trie) are
# separate and stay intact.
STRIP_FLAG = "--strip-all"


# ---------------------------------------------------------------------------
# matrix -> plan
# ---------------------------------------------------------------------------


def load_matrix():
    workflow = yaml.safe_load(CI_WORKFLOW.read_text(encoding="utf-8"))
    return workflow["jobs"]["build"]["strategy"]["matrix"]["include"]


def artifact_name(leg):
    """Artifact name a leg uploads, mirroring ci.yml's upload step."""
    name = "{platform}-{target}-{arch}-{engine}".format(**leg)
    if leg["platform"] == "web" and leg.get("threads") is False:
        name += "-nothreads"
    return name


def plan(engines=None):
    """Return {engine: [artifact names]} for the requested (or all) engines."""
    matrix = load_matrix()
    wanted = engines if engines is not None else ENGINE_ORDER
    result = {engine: [] for engine in ENGINE_ORDER}
    for leg in matrix:
        engine = leg.get("engine")
        if engine in wanted:
            result[engine].append(artifact_name(leg))
    if engines is None:
        return result
    return {engine: result[engine] for engine in wanted}


def legs_of(engine):
    return [leg for leg in load_matrix() if leg.get("engine") == engine]


# ---------------------------------------------------------------------------
# leg -> key / file
# ---------------------------------------------------------------------------


def _library_suffix(platform):
    return {"windows": ".dll", "linux": ".so", "macos": ".dylib", "android": ".so", "ios": ".dylib"}.get(platform, "")


def leg_library_file(leg, bin_dir):
    """Relative path (inside bin/) of the library a leg produces.

    One library, two products: every leg builds the same GDExtension target,
    so a leg yields exactly one packaged library regardless of `target`.

    `bin_dir` is the package's merged `bin/` directory, needed because the iOS
    leg publishes an xcframework only for the engines that also build the
    simulator variant (v8 has no iOS simulator prebuilt, so it ships the device
    dylib alone). The choice is made from what is actually present, never from
    the engine name.
    """
    platform, target, arch = leg["platform"], leg["target"], leg["arch"]
    if platform == "ios":
        xcframework = f"{ZIP_STEM}.ios.{target}{XCFRAMEWORK_SUFFIX}"
        if (bin_dir / platform / xcframework).is_dir():
            return f"{platform}/{xcframework}"
        return f"{platform}/{ZIP_STEM}.ios.{target}.{arch}.dylib"
    if platform == "web":
        nothreads = ".nothreads" if leg.get("threads") is False else ""
        return f"{platform}/{ZIP_STEM}.web.{target}.{arch}{nothreads}.wasm"
    return f"{platform}/{ZIP_STEM}.{platform}.{target}.{arch}{_library_suffix(platform)}"


def leg_library_key(leg):
    """`.gdextension` `[libraries]` key for a leg's library.

    Feature tags are emitted as `<platform>.<debug|release>.<editor|threads>.<arch>`;
    Godot requires every tag to be a real feature of the running engine
    (`gdextension_library_loader.cpp: match_all_tags`), so we emit only tags the
    leg really has:

    * `editor` legs are debug builds -> `debug` + `editor`
    * `template_release` legs -> `release`
    * the arch tag is omitted where the artifact covers more than one arch
      (macOS `universal`) or where the container is arch-agnostic
      (iOS xcframework, which carries both the device and simulator slices)
    * web keeps its `wasm32` tag and adds `threads` for the threaded variant.
    """
    platform, target, arch = leg["platform"], leg["target"], leg["arch"]
    tags = [platform]
    if target == "editor":
        tags += ["debug", "editor"]
    elif target == "template_debug":
        tags += ["debug"]
    else:
        tags += ["release"]
    if platform == "web" and leg.get("threads") is not False:
        tags.append("threads")
    arch_tagged = not (platform == "ios" or (platform == "macos" and arch == "universal"))
    if arch_tagged:
        tags.append(arch)
    return ".".join(tags)


# ---------------------------------------------------------------------------
# assemble
# ---------------------------------------------------------------------------


class AssembleError(RuntimeError):
    pass


def package_name(engine, legs):
    platforms = sorted({leg["platform"] for leg in legs}, key=PLATFORM_ORDER.index)
    tokens = [engine]
    if platforms != [engine]:
        tokens += platforms
    return f"{ZIP_STEM}-" + "-".join(tokens)


def _skip_link_junk(directory, names):
    """shutil.copytree ignore= hook: keep linker by-products out of the package."""
    return [name for name in names if Path(name).suffix.lower() in LINK_JUNK_SUFFIXES]


# Namespaces this project ships itself. The OS can never provide them, so an
# undefined symbol in one of these families means the library is missing a
# static archive it was supposed to link.
ENGINE_SYMBOL_PREFIXES = ("_ZN2v8", "_ZN3jsb", "_ZN5godot", "_ZN6godot")
# Mach-O adds one more leading underscore to the Itanium-mangled name.
_MACHO_PREFIXES = tuple("_" + p for p in ENGINE_SYMBOL_PREFIXES)


def _nm_tool_candidates():
    tools = []
    found = shutil.which("llvm-nm")
    if found:
        tools.append(found)
    for pattern in ("/usr/bin/llvm-nm-*", "/usr/lib/llvm-*/bin/llvm-nm"):
        tools.extend(sorted(glob.glob(pattern), reverse=True))
    found = shutil.which("nm")
    if found:
        tools.append(found)
    return tools


def _undefined_engine_symbols(path: Path, tool: str):
    """Undefined symbols of `path` that belong to an engine namespace we ship.

    Returns None when the tool cannot read this file (caller tries the next
    candidate); an empty list means "read fine, nothing suspicious".
    """
    prefixes = ENGINE_SYMBOL_PREFIXES + _MACHO_PREFIXES
    for args in (["-D", "-u"], ["-u"], ["--dynamic", "--undefined-only"]):
        proc = subprocess.run([tool, *args, str(path)], capture_output=True, text=True)
        if proc.returncode != 0:
            continue
        names = []
        for line in proc.stdout.splitlines():
            parts = line.split()
            if parts and parts[-1].startswith(prefixes):
                names.append(parts[-1])
        return names
    return None


def verify_no_unresolved_engine_symbols(bin_dir: Path):
    """Refuse a package whose shared libraries reference engine symbols nothing provides.

    A shared-library link is allowed to keep undefined symbols (unlike an
    executable), so a missing `libv8_monolith.a` does not fail the build - it
    silently produces a library with no engine inside that cannot be dlopen'ed.
    That is exactly how the android v8 package shipped a 10 MiB `.so` with 144
    undefined `v8::` symbols. `llvm-nm -D -u` catches it before packing.
    """
    tools = _nm_tool_candidates()
    if not tools:
        print("  verify: no llvm-nm/nm on PATH; skipping the unresolved-symbol check")
        return
    problems = []
    for path in sorted(bin_dir.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in (".so", ".dylib"):
            continue
        for tool in tools:
            names = _undefined_engine_symbols(path, tool)
            if names is None:
                continue
            if names:
                problems.append((path, names))
            break
    if problems:
        details = "\n  ".join(
            f"{path.relative_to(bin_dir).as_posix()}: {len(names)} undefined engine symbol(s), "
            f"e.g. {names[0]}"
            for path, names in problems
        )
        raise AssembleError(
            f"libraries reference engine symbols that nothing provides "
            f"({len(problems)} file(s)); they cannot be loaded:\n  {details}"
        )


def _strip_tool_candidates():
    """Strip binaries we can actually use on this runner.

    `llvm-strip` (multi-target: ELF/PE/Mach-O) is preferred - GNU `strip` cannot
    read Mach-O, so the macOS/iOS dylibs need LLVM. Both may be version-suffixed
    on the runner images.
    """
    tools = []
    for name in ("llvm-strip",):
        found = shutil.which(name)
        if found:
            tools.append(found)
    for pattern in ("/usr/bin/llvm-strip-*", "/usr/lib/llvm-*/bin/llvm-strip"):
        tools.extend(sorted(glob.glob(pattern), reverse=True))
    found = shutil.which("strip")
    if found:
        tools.append(found)
    return tools


def _strip_libraries(bin_dir: Path):
    """Remove embedded debug info from the packaged loadable libraries.

    The CI editor profile builds with `debug_symbols=yes`; MinGW/ELF builds embed
    the DWARF in the binary, so a shipped editor DLL reached 282 MiB. Stripping is
    best-effort: a file whose format no available tool understands stays as-is
    (reported), it never fails the release.
    """
    tools = _strip_tool_candidates()
    if not tools:
        print("  strip: no llvm-strip/strip on PATH; shipping libraries as-is")
        return
    targets = [
        path
        for path in sorted(bin_dir.rglob("*"))
        if path.is_file() and path.suffix.lower() in (".dll", ".so", ".dylib")
    ]
    if not targets:
        return
    print(f"  strip: {len(targets)} library file(s) using {Path(tools[0]).name}")
    for path in targets:
        before = path.stat().st_size
        for tool in tools:
            if path.suffix.lower() == ".dylib" and "llvm-strip" not in Path(tool).name:
                # GNU strip cannot read Mach-O; do not corrupt the file trying.
                continue
            proc = subprocess.run(
                [tool, STRIP_FLAG, str(path)],
                capture_output=True,
                text=True,
            )
            if proc.returncode == 0:
                break
        else:
            print(f"  strip: {path.name} left as-is (no tool accepted it)")
            continue
        after = path.stat().st_size
        rel = path.relative_to(bin_dir).as_posix()
        print(f"  strip: {rel} {before / 1048576:.1f} -> {after / 1048576:.1f} MiB")


def merge_leg(artifact_dir: Path, bin_dir: Path, pkg_dir: Path):
    """Merge one leg's artifact tree into the package.

    An artifact holds `<platform>/...` plus `addons/<addon>/LICENSE`. The
    platform trees land under `bin/`, the license is normalized to the package
    root (the old flow dropped it into `bin/addons/...`). Anything else is
    unexpected and fails loudly rather than silently shipping junk.
    """
    if not artifact_dir.is_dir():
        raise AssembleError(f"artifact directory not found: {artifact_dir}")
    for entry in sorted(artifact_dir.iterdir()):
        if entry.name in PLATFORM_ORDER and entry.is_dir():
            target = bin_dir / entry.name
            target.mkdir(parents=True, exist_ok=True)
            for item in sorted(entry.iterdir()):
                if item.is_file() and item.suffix.lower() in LINK_JUNK_SUFFIXES:
                    print(f"  drop {item.name} (linker by-product)")
                    continue
                dest = target / item.name
                if item.is_dir():
                    if dest.exists():
                        shutil.rmtree(dest)
                    shutil.copytree(item, dest, ignore=_skip_link_junk)
                else:
                    shutil.copy2(item, dest)
        elif entry.name == "LICENSE" or (entry.name == "addons" and entry.is_dir()):
            _copy_license(entry, pkg_dir)
        else:
            raise AssembleError(
                f"unexpected top-level entry {entry.name!r} in {artifact_dir}; "
                "the artifact layout changed, teach merge_leg() about it"
            )


def _copy_license(entry: Path, pkg_dir: Path):
    candidates = []
    if entry.is_file():
        candidates = [entry]
    else:
        candidates = sorted(p for p in entry.rglob("LICENSE") if p.is_file())
    if not candidates:
        return
    shutil.copy2(candidates[0], pkg_dir / "LICENSE")


def _drop_superseded_ios_dylibs(bin_dir: Path, legs):
    """Drop the loose iOS dylibs when the xcframework (their container) is shipped.

    The iOS leg uploads the device dylib, the simulator dylib and the
    xcframework built from both. The xcframework is what `.gdextension` points
    at, so the two loose copies are redundant payload of the very same slices.
    """
    for leg in legs:
        if leg["platform"] != "ios":
            continue
        ios_dir = bin_dir / "ios"
        xcframework = ios_dir / f"{ZIP_STEM}.ios.{leg['target']}{XCFRAMEWORK_SUFFIX}"
        if not xcframework.is_dir():
            continue
        pattern = re.compile(
            rf"^{re.escape(ZIP_STEM)}\.ios\.{re.escape(leg['target'])}\..*\.dylib$"
        )
        for path in sorted(ios_dir.glob(f"{ZIP_STEM}.ios.{leg['target']}*.dylib")):
            if pattern.match(path.name):
                print(f"  drop redundant {path.relative_to(bin_dir.parent)} (covered by {xcframework.name})")
                path.unlink()


def _is_library(path: Path) -> bool:
    if path.is_dir():
        return path.name.endswith(XCFRAMEWORK_SUFFIX)
    return path.name.endswith(LIBRARY_SUFFIXES)


def _section_lines(text, name):
    """Yield the raw lines inside `[name]`, excluding the header itself."""
    header = f"[{name}]"
    inside = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            inside = stripped == header
            continue
        if inside:
            yield line


def read_icons(source: Path):
    """Icon paths declared in the source's `[icons]`, in file order.

    `[icons]` is read by the engine: `gdextension_library_loader.cpp` maps each
    key to `class_icon_paths`, so `GodotJSScript = "icons/GodotJSScript.svg"`
    is what gives the script class its icon. The paths are relative to the
    `.gdextension` itself, which is why the packager has to carry the files
    along - an entry pointing at a file the archive never shipped is exactly
    the kind of "declared but absent" defect the library keys are checked for.
    """
    icons = []
    for line in _section_lines(source.read_text(encoding="utf-8"), "icons"):
        stripped = line.split(";", 1)[0].split("#", 1)[0].strip()
        if not stripped or "=" not in stripped:
            continue
        value = stripped.split("=", 1)[1].strip().strip('"').strip("'")
        if value:
            icons.append(value)
    return icons


def copy_icons(pkg_dir: Path, source: Path, icons):
    """Stage the packaged copies of the `[icons]` files, preserving rel paths."""
    for rel in icons:
        if Path(rel).is_absolute() or rel.startswith("res://"):
            raise AssembleError(
                f"{source.name}: icon {rel!r} is not a path inside the addon; "
                "the packaged copy cannot be resolved"
            )
        origin = source.parent / rel
        if not origin.is_file():
            raise AssembleError(f"{source.name}: declared icon {rel!r} does not exist at {origin}")
        destination = pkg_dir / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, destination)
    return icons


def set_information_version(text: str, version: str):
    """Rewrite `version` inside `[information]`, leaving every other section alone.

    `[information]` is this project's own metadata, not part of the engine's
    format (the engine reads `[configuration]`, `[libraries]`, `[dependencies]`
    and `[icons]`). It is not parsed, so it must not be rebuilt by a generic
    INI writer - the original lines are kept, and only the `version` value is
    swapped, so typos and extra keys survive untouched.
    """
    header = "[information]"
    if header not in text:
        return text
    lines = text.splitlines()
    output = []
    inside = False
    replaced = False
    insert_at = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if inside and not replaced:
                # section ended without a version key: add one before the next header
                output.append(f'version = "{version}"')
                replaced = True
            inside = stripped == header
            output.append(line)
            if inside:
                insert_at = len(output)
            continue
        if inside and not replaced:
            key = stripped.split("=", 1)[0].strip() if "=" in stripped else ""
            if key == "version":
                indent = line[: len(line) - len(line.lstrip())]
                output.append(f'{indent}version = "{version}"')
                replaced = True
                continue
        output.append(line)
    if inside and not replaced:
        output.append(f'version = "{version}"')
    return "\n".join(output) + "\n"


def write_gdextension(pkg_dir: Path, source: Path, entries, dependencies, version=None):
    """Rewrite a `.gdextension`, keeping everything above `[libraries]` verbatim.

    `[configuration]` (entry_symbol, compatibility_minimum, reloadable, ...) is
    the source file's business, not the packager's; copying it byte for byte
    keeps the packaged file in sync with the repo's superset file and cannot
    silently drop a key such as compatibility_minimum. `[information]` and
    `[icons]` ride along in that same prefix; only the `[information]` version
    is refreshed, because it has to track the release being built.
    """
    source_text = source.read_text(encoding="utf-8")
    head = source_text.split("[libraries]", 1)[0].rstrip() + "\n"
    if "[configuration]" not in head:
        raise AssembleError(f"{source.name}: no [configuration] section to preserve")
    if version is not None:
        head = set_information_version(head, version)

    lines = [head.rstrip("\n"), "", "[libraries]"]
    for key, value in entries:
        lines.append(f'{key} = "{value}"')
    if dependencies:
        lines += ["", "[dependencies]"]
        for key, dep in dependencies:
            lines.append(f"{key} = {{")
            lines.append(f'  "{dep}": ""')
            lines.append("}")
    lines.append("")
    out = pkg_dir / source.name
    out.write_text("\n".join(lines), encoding="utf-8")
    return out


def assemble(engine, artifacts_dir: Path, out_dir: Path, version=None):
    legs = legs_of(engine)
    if not legs:
        raise AssembleError(f"engine {engine!r} has no build legs in {CI_WORKFLOW}")

    name = package_name(engine, legs)
    print(f"=== {name} ({len(legs)} legs) ===")
    pkg_dir = out_dir / name
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    bin_dir = pkg_dir / "bin"
    bin_dir.mkdir(parents=True)

    for leg in legs:
        artifact = artifact_name(leg)
        print(f"  merge {artifact}")
        merge_leg(artifacts_dir / artifact, bin_dir, pkg_dir)
    _drop_superseded_ios_dylibs(bin_dir, legs)
    _strip_libraries(bin_dir)

    # Each packaged library key is derived from a leg that really produced it:
    # no "supported platform" is declared without its binary in the box, and -
    # the converse that matters just as much - a leg that produced nothing must
    # not be swallowed silently. Every leg builds the one library, so every leg
    # must yield its file.
    entries = []
    for leg in legs:
        library_rel = leg_library_file(leg, bin_dir)
        if not (bin_dir / library_rel).exists():
            raise AssembleError(
                f"{engine}: leg {artifact_name(leg)} did not produce its library "
                f"({library_rel}); refusing to ship a package that silently drops a platform"
            )
        entries.append((leg_library_key(leg), f"bin/{library_rel}"))

    # node.dll is a Node-engine-only sidecar: the Node-API forwarder that native
    # `.node` addons import (it forwards to the main DLL). Only the Node engine
    # links libnode, so only a package whose windows leg ships node.dll declares
    # the dependency; the other engines must not.
    dependencies = []
    if (bin_dir / "windows" / "node.dll").is_file():
        node_dependency = "bin/windows/node.dll"
        for key, _ in entries:
            if key.split(".")[0] == "windows":
                dependencies.append((key, node_dependency))

    source = ADDON_DIR / GEXTENSION
    if not source.is_file():
        raise AssembleError(f"missing {source}")
    # `[icons]` entries point at files relative to the `.gdextension`; ship them
    # in the same shape, otherwise the engine resolves a path that is not there.
    icons = copy_icons(pkg_dir, source, read_icons(source))
    written = write_gdextension(pkg_dir, source, entries, dependencies, version=version)
    uid = source.with_name(source.name + ".uid")
    if uid.is_file():
        shutil.copy2(uid, pkg_dir / uid.name)
    print(f"  wrote {written.name}: {len(entries)} library key(s)")

    verify_package(pkg_dir, written, entries, dependencies, icons)
    if not (pkg_dir / "LICENSE").is_file():
        raise AssembleError(f"{name}: LICENSE was not staged by any leg")

    zip_path = out_dir / f"{name}.zip"
    if zip_path.exists():
        zip_path.unlink()
    zip_package(pkg_dir, zip_path)
    print(f"  packed {zip_path.name} ({zip_path.stat().st_size / 1024 / 1024:.1f} MiB)")
    _print_plan_summary(name, legs, entries)
    print(f"PACKAGE_NAME={name}")
    print(f"PACKAGE_ZIP={zip_path.resolve()}")
    return zip_path


def _print_plan_summary(name, legs, entries):
    platforms = sorted({leg["platform"] for leg in legs}, key=PLATFORM_ORDER.index)
    print(f"  platforms: {' '.join(platforms)}")
    print(f"  libraries: {', '.join(key for key, _ in entries)}")


def verify_package(pkg_dir: Path, written: Path, entries, dependencies, icons=()):
    """Assert declared <-> packaged 1:1 for the generated .gdextension."""
    declared = set()
    declared.update(value for _, value in entries)
    declared.update(value for _, value in dependencies)
    declared.update(icons)

    missing = sorted(value for value in declared if not (pkg_dir / value).exists())
    if missing:
        raise AssembleError("declared but not packaged:\n  " + "\n  ".join(missing))

    # Every loadable library under bin/ must be referenced by the .gdextension
    # (as a library or a dependency). The rule is deliberately name-agnostic: a
    # hardcoded prefix list is how an unexpected file sneaks into a package
    # unnoticed.
    unreferenced = []
    for path in sorted((pkg_dir / "bin").rglob("*")):
        rel = path.relative_to(pkg_dir).as_posix()
        # A packaged xcframework is declared as a single directory entry; its
        # slices are the container's payload, not separate libraries.
        if any(part.endswith(XCFRAMEWORK_SUFFIX) for part in rel.split("/")[:-1]):
            continue
        if not _is_library(path):
            continue
        if rel not in declared:
            unreferenced.append(rel)
    if unreferenced:
        raise AssembleError("packaged but not declared by any .gdextension:\n  " + "\n  ".join(unreferenced))

    text = written.read_text(encoding="utf-8")
    match = re.search(r'^\s*entry_symbol\s*=\s*"([^"]+)"', text, re.M)
    if match is None:
        raise AssembleError(f"{written.name}: entry_symbol missing from [configuration]")

    verify_no_unresolved_engine_symbols(pkg_dir / "bin")


def zip_package(pkg_dir: Path, zip_path: Path):
    # The archive's inner root is the plain addon name: the platform/engine
    # suffixes belong to the asset file name, not to the folder users drop into
    # their project.
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(pkg_dir.rglob("*")):
            arcname = (Path(ZIP_STEM) / path.relative_to(pkg_dir)).as_posix()
            if path.is_dir():
                info = zipfile.ZipInfo(arcname + "/")
                info.external_attr = (0o40755 << 16) | 0x10
                zf.writestr(info, b"")
                continue
            info = zipfile.ZipInfo.from_file(path, arcname)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as src, zf.open(info, "w") as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)


# ---------------------------------------------------------------------------
# fetch
# ---------------------------------------------------------------------------


def fetch(engine, run_id, dest: Path, repo=None):
    names = plan([engine])[engine]
    if not names:
        raise AssembleError(f"engine {engine!r} has no build legs in {CI_WORKFLOW}")
    for name in names:
        target = dest / name
        if target.is_dir() and any(target.iterdir()):
            print(f"  cached {name}")
            continue
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True)
        cmd = ["gh", "run", "download", str(run_id), "-n", name, "-D", str(target)]
        if repo:
            cmd += ["--repo", repo]
        print(f"  fetch {name}")
        subprocess.run(cmd, check=True)
    return names


# ---------------------------------------------------------------------------
# verify (release gate)
# ---------------------------------------------------------------------------


def actual_artifacts(run_id=None, repo=None, artifacts_json=None):
    if artifacts_json:
        payload = json.loads(Path(artifacts_json).read_text(encoding="utf-8"))
        if isinstance(payload, dict):
            return [entry["name"] for entry in payload.get("artifacts", [])]
        return list(payload)
    if run_id is None:
        run_id = os.environ.get("GITHUB_RUN_ID")
        if not run_id:
            raise AssembleError("pass --run <id> or set GITHUB_RUN_ID")
    # `gh api` has no --repo flag: the repository goes into the endpoint path
    # ({owner}/{repo} resolves from the checkout, which is why the CI gate can
    # call this without arguments there).
    scope = repo or "{owner}/{repo}"
    cmd = [
        "gh",
        "api",
        "--paginate",
        f"repos/{scope}/actions/runs/{run_id}/artifacts",
        "-q",
        ".artifacts[].name",
    ]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line.strip()]


def verify(run_id=None, repo=None, artifacts_json=None):
    expected = plan()
    actual = set(actual_artifacts(run_id, repo, artifacts_json))

    problems = []
    for engine in ENGINE_ORDER:
        names = expected[engine]
        if not names:
            problems.append(f"{engine}: no build legs found in {CI_WORKFLOW} (engine would be silently dropped)")
            continue
        missing = [name for name in names if name not in actual]
        if missing:
            problems.append(f"{engine}: expected artifacts missing from the run:\n    " + "\n    ".join(missing))
        print(f"  {engine}: {len(names)} legs declared, {len(names) - len(missing)} present")

    print("=== actual artifacts ===")
    for name in sorted(actual):
        print(f"  {name}")
    if problems:
        print("::error::release packaging would silently skip the following:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(f"All {sum(len(names) for names in expected.values())} release legs are present across "
          f"{len(ENGINE_ORDER)} engines.")
    return 0


# ---------------------------------------------------------------------------
# cli
# ---------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("plan", help="print the expected artifact names")
    p.add_argument("--engine", choices=ENGINE_ORDER)
    p.add_argument("--all", action="store_true")

    p = sub.add_parser("fetch", help="download one package's legs")
    p.add_argument("--engine", required=True, choices=ENGINE_ORDER)
    p.add_argument("--run", required=True)
    p.add_argument("--dir", required=True)
    p.add_argument("--repo")

    p = sub.add_parser("assemble", help="build a package and its .gdextension files")
    p.add_argument("--engine", required=True, choices=ENGINE_ORDER)
    p.add_argument("--artifacts", required=True)
    p.add_argument("--out", required=True)
    # `[information] version` tracks the release; pass it so a packaged copy
    # cannot advertise a version other than the tag it is attached to.
    p.add_argument("--version", help="value for [information] version (e.g. the release tag)")

    p = sub.add_parser("verify", help="release gate over every published engine")
    p.add_argument("--all", action="store_true")
    p.add_argument("--run")
    p.add_argument("--repo")
    p.add_argument("--artifacts-json")

    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            if args.all or not args.engine:
                for engine, names in plan().items():
                    for name in names:
                        print(name)
            else:
                for name in plan([args.engine])[args.engine]:
                    print(name)
            return 0
        if args.command == "fetch":
            fetch(args.engine, args.run, Path(args.dir), args.repo)
            return 0
        if args.command == "assemble":
            out = Path(args.out)
            out.mkdir(parents=True, exist_ok=True)
            assemble(args.engine, Path(args.artifacts), out, version=args.version)
            return 0
        if args.command == "verify":
            return verify(args.run, args.repo, args.artifacts_json)
    except AssembleError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
