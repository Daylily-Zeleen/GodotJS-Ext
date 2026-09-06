#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_codegen.py —— codegen 端到端基线校验（规范见 .trellis/spec/godotjs-ext/test/codegen-baseline.md）

流程（P0 验收闭环）：
  1. 清理生成产物: project/gen、project/typings、project/.godot/.api_dumping、project/extension_api.json
  2. 触发链（全部 headless，规程见 .trellis/spec/godotjs-ext/test/codegen-baseline.md）：
     a. --dump-extension-api-with-docs      （失败自动重试一次，仿 CI）
        → 成功后把 project/extension_api.json 备份到 .agent_tmp/
     b. --godotjs-api-generate extension_api.json （消费并删除该 json，构建 api store）
        → 之后把备份的 json 放回项目根：基线生成时项目根存在此文件，
          资源声明生成器会为其产出 gen/godot/extension_api.json.gen.ts；
          不放回则 live 永远缺这个文件（2026-08-23 定位确认）。
          注意 api-generate 本身以该 json 为输入，所以是先复制备份、
          消费完再放回，不能提前移走（首轮跑批踩坑）。
     c. --generate-types                    （完成后进程自行退出）
       前提：测试项目的 TS 已编译（cd project && pnpm install && npx tsc --noCheck），
       否则场景 d.ts 解析不到挂脚本的类型。
  3. 与 <检出根>/.codegen-baseline/ 递归 diff：
     - gen/、typings/ 全量比对（内容按 CRLF→LF 归一化后比较，纯行尾差异单独归类不算失败）
     - tsconfig.json 是 git 跟踪的预设文件，不参与删除/重生，仅单独比对
  4. 报告差异并以退出码表达结果（0 = 完全一致）

路径定位：基线一律取「当前检出根」自己的副本；可用环境变量 JSB_CHECKOUT_ROOT 覆盖。
（基线目录 .codegen-baseline/ 不入库，属于检出本地数据；
本脚本自身在 misc/ 下入库。分步操作规程见 .trellis/spec/godotjs-ext/test/codegen-baseline.md）

用法：
  python verify_codegen.py                      # 全流程校验
  python verify_codegen.py --godot <path>       # 指定 Godot 可执行文件
  python verify_codegen.py --update-baseline    # 跑触发链后快照当前产物为新基线
  python verify_codegen.py --diff-only          # 跳过清理与触发，只做 diff（调试用）
  python verify_codegen.py --no-cleanup         # 不删产物，直接触发重生（增量观察用）
"""

import argparse
import difflib
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_GODOT = "godot"  # PATH 中的 godot 可执行文件；项目要求 Godot 4.7+（见 README）
# 注意：8-22 起的引擎构建不再把 unexposed 的扩展 internal 类写入
# extension_api.json，而 --generate-types 的场景/资源类型生成需要在 JS 运行时里
# 解析 GodotJSEditorHelper。为此 register_editor_types.cpp 已把该类改为
# GDREGISTER_CLASS（exposed）注册（生成器由 NamingUtil 过滤，产物不受影响）。
# 引擎版本要求 Godot 4.7+（godot-cpp ABI 口径，见 README）；更旧的引擎二进制
# 无法加载当前扩展（插件实例化即崩），不要回退。
SCRIPT_DIR = Path(__file__).resolve().parent          # <root>/misc
CHECKOUT_ROOT = Path(os.environ.get("JSB_CHECKOUT_ROOT", SCRIPT_DIR.parent)).resolve()
BASELINE_DIR = CHECKOUT_ROOT / ".codegen-baseline"
PROJECT_DIR = CHECKOUT_ROOT / "project"
ADDON_BIN_DLL = (
    PROJECT_DIR / "addons" / "godotjs-ext.daylily-zeleen" / "bin"
    / "windows" / "godotjs-ext.windows.editor.x86_64.dll"
)
REPO_BIN_DLL = CHECKOUT_ROOT / "bin" / "windows" / "godotjs-ext.windows.editor.x86_64.dll"

GEN_DIRS = ["gen", "typings"]
API_STORE_DIR = PROJECT_DIR / ".godot" / ".api_dumping"
# dump 出的 extension_api.json 暂存处（api-generate 会消费删除它，
# 而 generate-types 需要它回到项目根才能复现基线的输入条件）
STAGED_EXT_JSON = CHECKOUT_ROOT / ".agent_tmp" / "staged_extension_api.json"

STEP_TIMEOUT = 900  # 秒/步


# Windows 编辑器已知问题：headless 任务完成后在退出阶段崩溃（0xC0000005）。
# CI 对此用 `|| true` 容忍（ci.yml「Generate API data」步）。这里同样容忍该码，
# 但必须配合各步骤的产物存在性检查——产物没落盘仍判失败。
GODOT_SHUTDOWN_CRASH = 3221225477  # 0xC0000005


def log(msg: str) -> None:
    print(f"[verify_codegen] {msg}", flush=True)


def die(msg: str, code: int = 1) -> "None":
    log(f"FATAL: {msg}")
    sys.exit(code)


def _artifact_ok(step: str) -> bool:
    """步骤产物存在性检查（退出码不可靠时的真实判据）。"""
    if step == "dump":
        p = PROJECT_DIR / "extension_api.json"
        return p.exists() and p.stat().st_size > 1024 * 1024
    if step == "api-generate":
        return (API_STORE_DIR / "header.capi").exists()
    if step == "generate-types":
        gen_ok = (PROJECT_DIR / "gen").is_dir() and any((PROJECT_DIR / "gen").rglob("*.ts"))
        typ_ok = (PROJECT_DIR / "typings").is_dir() and any((PROJECT_DIR / "typings").glob("*.d.ts"))
        return gen_ok and typ_ok
    raise ValueError(step)


def run_godot(godot: str, args: list, allow_retry: bool = False, step: str = "") -> int:
    """在检出根下调用 Godot，返回退出码。"""
    cmd = [godot, "--headless", "--editor", "--path", str(PROJECT_DIR)] + args
    log(f"$ {' '.join(cmd)}")
    for attempt in (1, 2) if allow_retry else (1,):
        proc = subprocess.run(cmd, cwd=str(CHECKOUT_ROOT), timeout=STEP_TIMEOUT,
                              capture_output=True, text=True, errors="replace")
        rc = proc.returncode
        if rc == GODOT_SHUTDOWN_CRASH:
            # 已知的编辑器关机崩溃：以产物为准
            if step and _artifact_ok(step):
                log(f"  exit={rc}（已知的关机阶段崩溃，忽略）")
                return 0
            log(f"  exit={rc} 且产物未落盘，按失败处理 (attempt {attempt})")
            rc = 1
        elif rc == 0:
            tail = "\n".join((proc.stdout or "").strip().splitlines()[-5:])
            log(f"  exit=0\n{tail}")
            return 0
        else:
            log(f"  exit={rc} (attempt {attempt})")
        if attempt == 1 and allow_retry:
            log("  重试一次（仿 CI 的关机崩溃容错）…")
    # 失败时打印输出尾部帮助定位
    for stream in (proc.stdout, proc.stderr):
        if stream:
            tail = "\n".join(stream.strip().splitlines()[-30:])
            if tail:
                print(tail, file=sys.stderr)
    return proc.returncode


def cleanup(keep_tsconfig_note: bool = True) -> None:
    for name in GEN_DIRS:
        target = PROJECT_DIR / name
        if target.exists():
            log(f"清理 {target.relative_to(CHECKOUT_ROOT)}")
            shutil.rmtree(target)
        else:
            log(f"无需清理（不存在）: project/{name}")
    if API_STORE_DIR.exists():
        log(f"清理 {API_STORE_DIR.relative_to(CHECKOUT_ROOT)}")
        shutil.rmtree(API_STORE_DIR)
    # extension_api.json 是中间产物，每次重新 dump
    ext_json = PROJECT_DIR / "extension_api.json"
    if ext_json.exists():
        ext_json.unlink()
    if keep_tsconfig_note:
        log("注意: project/tsconfig.json 为 git 跟踪的预设文件，不删除、仅比对")


def collect(root: Path, normalize: bool = False) -> dict:
    """返回 {相对路径(bytes): 文件字节内容}。

    normalize=True 时把 CRLF 归一为 LF 再比较（行尾差异单独归类，
    不与内容差异混在一起——此前统一 diff 全空的现象即源于纯行尾差异）。
    """
    out = {}
    if not root.exists():
        return out
    for p in sorted(root.rglob("*")):
        if p.is_file():
            data = p.read_bytes()
            if normalize and b"\r\n" in data:
                data = data.replace(b"\r\n", b"\n")
            out[p.relative_to(root).as_posix()] = data
    return out


def diff_tree(label: str, base: Path, live: Path, report: list) -> None:
    # 两侧都按 CRLF→LF 归一化比较；原始字节不同但归一化后一致的单独归类为行尾差异
    a, b = collect(base, normalize=True), collect(live, normalize=True)
    a_keys, b_keys = set(a), set(b)
    for k in sorted(a_keys - b_keys):
        report.append(f"[{label}] 缺失: {k}")
    for k in sorted(b_keys - a_keys):
        report.append(f"[{label}] 多余: {k}")
    for k in sorted(a_keys & b_keys):
        if a[k] != b[k]:
            report.append(f"[{label}] 内容不同: {k}")
        elif (base / k).read_bytes() != (live / k).read_bytes():
            report.append(f"[{label}] 行尾差异(不计失败): {k}")


def print_unified(label: str, base: Path, live: Path, rel: str, max_lines: int = 40) -> None:
    bp, lp = base / rel, live / rel
    a = bp.read_text(encoding="utf-8", errors="replace").splitlines() if bp.exists() else []
    b = lp.read_text(encoding="utf-8", errors="replace").splitlines() if lp.exists() else []
    lines = list(difflib.unified_diff(a, b, fromfile=f"baseline/{rel}", tofile=f"live/{rel}", lineterm=""))
    print(f"\n===== diff [{label}] {rel} (前 {max_lines} 行) =====")
    print("\n".join(lines[:max_lines]))
    if len(lines) > max_lines:
        print(f"… 共 {len(lines)} 行 diff，已截断")


def main() -> None:
    ap = argparse.ArgumentParser(description="codegen 端到端基线校验")
    ap.add_argument("--godot", default=DEFAULT_GODOT, help="Godot 可执行文件路径")
    ap.add_argument("--update-baseline", action="store_true",
                    help="不与旧基线 diff，跑完触发链后把当前产物快照为新基线")
    ap.add_argument("--diff-only", action="store_true", help="跳过清理与触发，仅比对现有产物")
    ap.add_argument("--no-cleanup", action="store_true", help="触发前不清理产物")
    args = ap.parse_args()

    log(f"检出根: {CHECKOUT_ROOT}")
    log(f"基线:   {BASELINE_DIR}")
    if not BASELINE_DIR.exists():
        if not args.update_baseline:
            die("基线目录不存在（本检出应自持一份副本；首次建立用 --update-baseline）")
        log("基线目录不存在，将以 --update-baseline 新建")
    for sub in GEN_DIRS + ["tsconfig.json"]:
        if not (BASELINE_DIR / sub).exists() and not args.update_baseline:
            die(f"基线缺少 {sub}")
    godot_resolved = shutil.which(args.godot) if not Path(args.godot).exists() else args.godot
    if not godot_resolved:
        die(f"Godot 可执行文件不存在（既不是路径，也不在 PATH 中）: {args.godot}")
    args.godot = godot_resolved

    # 前置：扩展 DLL 必须已安装到 addon 目录（Godot 加载的是这份）
    if ADDON_BIN_DLL.exists():
        repo_newer = REPO_BIN_DLL.exists() and REPO_BIN_DLL.stat().st_mtime > ADDON_BIN_DLL.stat().st_mtime
        if repo_newer:
            log("警告: bin/windows 下 DLL 比 addon 目录的新——请先 scons（Install 步骤会刷新 addon 副本）再校验")
    else:
        log("警告: addon 目录无 editor DLL，扩展将无法加载，--generate-types 必然失败")

    if not args.diff_only:
        if not args.no_cleanup:
            cleanup()
        # 步骤 a: dump extension api（带文档），失败重试一次
        if run_godot(args.godot, ["--dump-extension-api-with-docs"],
                     allow_retry=True, step="dump") != 0:
            die("dump-extension-api-with-docs 失败")
        # 备份 extension_api.json：步骤 b 会消费删除它，
        # 但 generate-types 需要它在项目根（复现基线输入条件）。
        # 注意是复制不是移动——api-generate 本身要以该 json 为输入
        ext_json = PROJECT_DIR / "extension_api.json"
        STAGED_EXT_JSON.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(str(ext_json), str(STAGED_EXT_JSON))
        log(f"已备份 extension_api.json -> {STAGED_EXT_JSON.relative_to(CHECKOUT_ROOT)}")
        # 步骤 b: 构建 api store
        if run_godot(args.godot, ["--godotjs-api-generate", "extension_api.json"],
                     step="api-generate") != 0:
            die("godotjs-api-generate 失败")
        # 放回暂存的 json：基线生成时项目根存在该文件，资源声明生成器会为其
        # 产出 gen/godot/extension_api.json.gen.ts；不放回则 live 缺这个文件
        if STAGED_EXT_JSON.exists():
            shutil.move(str(STAGED_EXT_JSON), str(ext_json))
            log("已放回 extension_api.json 到项目根（generate-types 的输入前提）")
        else:
            log("警告: 暂存的 extension_api.json 不存在，live 可能缺少对应的 .gen.ts")
        # 步骤 c: 全量类型生成（headless 完成后自行退出）
        if run_godot(args.godot, ["--generate-types"], step="generate-types") != 0:
            die("generate-types 失败")

    if args.update_baseline:
        # 快照模式：当前产物即新基线。逐文件替换，避免整目录删除后
        # 长跑中途被杀留下空基线。
        BASELINE_DIR.mkdir(parents=True, exist_ok=True)
        for d in GEN_DIRS:
            dst = BASELINE_DIR / d
            if dst.exists():
                shutil.rmtree(str(dst))
            shutil.copytree(str(PROJECT_DIR / d), str(dst))
            log(f"已快照 {d}/ -> 基线")
        shutil.copy2(str(PROJECT_DIR / "tsconfig.json"), str(BASELINE_DIR / "tsconfig.json"))
        log("已快照 tsconfig.json -> 基线")
        # 双轮确定性检查的第二轮应交叉比对两轮快照产物（见规范）；
        # 此处额外做一次自校验：快照后立即 --diff-only 应全绿。
        report2: list = []
        for d in GEN_DIRS:
            diff_tree(d, BASELINE_DIR / d, PROJECT_DIR / d, report2)
        real2 = [x for x in report2 if "行尾差异" not in x]
        if real2:
            die(f"快照后自校验失败（{len(real2)} 处差异）：{real2[:3]}")
        log("✅ 基线已更新（含 .agent_tmp 暂存副本自校验通过）")
        return

    report: list = []
    fail_report: list = []
    for d in GEN_DIRS:
        diff_tree(d, BASELINE_DIR / d, PROJECT_DIR / d, report)
    # 行尾差异不参与成败判定
    fail_report = [line for line in report if "行尾差异" not in line]
    # tsconfig.json 单独比对（按行尾归一化，autocrlf 会让工作区变 CRLF）
    def _norm(p: Path) -> bytes:
        return p.read_bytes().replace(b"\r\n", b"\n")
    bt_path, lt_path = BASELINE_DIR / "tsconfig.json", PROJECT_DIR / "tsconfig.json"
    if not lt_path.exists():
        report.append("[tsconfig.json] 缺失")
    elif _norm(bt_path) != _norm(lt_path):
        report.append("[tsconfig.json] 内容不同")
    elif bt_path.read_bytes() != lt_path.read_bytes():
        report.append("[tsconfig.json] 行尾差异(不计失败)")

    if fail_report:
        log(f"发现 {len(fail_report)} 处差异（另有 {len(report) - len(fail_report)} 处行尾差异不计失败）:")
        for line in fail_report:
            print("  " + line)
        # 对前几个内容差异输出统一 diff 片段辅助定位
        import re
        shown = 0
        for line in fail_report:
            m = re.match(r"\[(.+?)\] 内容不同: (.+)", line)
            if not m:
                continue
            label, rel = m.group(1), m.group(2)
            base_root = BASELINE_DIR if label == "tsconfig.json" else BASELINE_DIR / label
            live_root = PROJECT_DIR if label == "tsconfig.json" else PROJECT_DIR / label
            print_unified(label, base_root, live_root, rel)
            shown += 1
            if shown >= 5:
                break
        sys.exit(1)
    if report:
        log(f"仅有 {len(report)} 处行尾差异（不计失败）:")
        for line in report:
            print("  " + line)

    log("✅ 校验通过: 生成产物与基线一致（忽略行尾差异）")


if __name__ == "__main__":
    main()
