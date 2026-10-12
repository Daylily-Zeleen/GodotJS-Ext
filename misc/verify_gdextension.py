#!/usr/bin/env python3
"""校验 `.gdextension` 与工程内 addon 目录的一致性。

`.gdextension` 会声明**所有**平台的库路径（打包时按腿裁剪），而开发构建只产出当前平台的
那一个；因此不能要求"声明的都存在"。能查、也确实出过问题的是另外两条：

1. **残留的扩展登记**：`extension_list.cfg` 里登记了磁盘上已不存在的 `.gdextension`
   （换平台或改了 addon 目录名之后残留）。引擎会报
   "GDExtension configuration file not found"，但没人断言过。
2. **当前平台该有的库存在**：`.gdextension` 里与 `--platform/--arch/--target` 匹配的
   `[libraries]` 项必须在 addon 目录里真实存在，否则运行期才发现。
   若该平台在此 `.gdextension` 里**完全没有**匹配项（例如 node 腿的模板构建），
   属于预期（本平台无对应产物），跳过而不是失败。

用法：
    python misc/verify_gdextension.py <project_dir> --platform <p> --arch <a> --target <t>
    python misc/verify_gdextension.py <project_dir>            # 只查登记残留

退出码：0 通过；1 有问题。
"""

import argparse
import os
import re
import sys

# 与引擎一致的可加载库后缀（含 macOS 的 xcframework 目录）。
LIBRARY_SUFFIXES = (".dll", ".so", ".dylib")
XCFRAMEWORK_SUFFIX = ".xcframework"


def find_addon_dirs(project_dir):
    """返回 (addon_dir, gdextension_files) 列表：addons/ 下的每个扩展目录及其 .gdextension。"""
    addons_root = os.path.join(project_dir, "addons")
    if not os.path.isdir(addons_root):
        return []
    found = []
    for entry in sorted(os.listdir(addons_root)):
        addon_dir = os.path.join(addons_root, entry)
        if not os.path.isdir(addon_dir):
            continue
        files = sorted(f for f in os.listdir(addon_dir) if f.endswith(".gdextension"))
        found.append((addon_dir, files))
    return found


def check_extension_list(project_dir, problems):
    """`extension_list.cfg` 里登记的每个 .gdextension 都必须真实存在。"""
    cfg = os.path.join(project_dir, ".godot", "extension_list.cfg")
    if not os.path.isfile(cfg):
        return
    with open(cfg, "rt", encoding="utf-8") as f:
        for line in f:
            entry = line.strip()
            if not entry.startswith("res://"):
                continue
            # res:// -> 工程根
            rel = entry[len("res://"):]
            if not os.path.isfile(os.path.join(project_dir, rel)):
                problems.append(
                    f"{os.path.relpath(cfg, project_dir)}: 登记了不存在的扩展 '{entry}'"
                )


def parse_library_entries(path):
    """读取 `[libraries]` 段的 `<tag> = "<path>"`。"""
    entries = []
    in_section = False
    with open(path, "rt", encoding="utf-8", errors="ignore") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("["):
                in_section = stripped == "[libraries]"
                continue
            if not in_section or "=" not in stripped or stripped.startswith(";"):
                continue
            key, _, value = stripped.partition("=")
            value = value.strip().strip('"')
            entries.append((key.strip(), value))
    return entries


def tag_matches(key, platform, arch, target):
    """该 key 是否是运行环境会选中的候选。

    key 形如 `<platform>.<debug|release>.<editor>.<arch>`，各段可选。这里只做宽松匹配：
    platform 必须相同，arch 若出现必须相同，debug/release 与 target 一致，editor 段与
    target == editor 一致。宽松是刻意的——目的是发现"该平台声明的库不存在"，
    不是复刻引擎的精确 tag 求解。
    """
    tags = key.split(".")
    if not tags or tags[0] != platform:
        return False
    want_release = target != "editor" and target != "template_debug"
    if "debug" in tags and want_release:
        return False
    if "release" in tags and not want_release:
        return False
    if "editor" in tags and target != "editor":
        return False
    arch_tags = [t for t in tags[1:] if t not in ("debug", "release", "editor", "threads")]
    if arch_tags and arch not in arch_tags:
        return False
    return True


def check_declared_libraries(addon_dir, gdextension, platform, arch, target, problems):
    """匹配当前平台的 [libraries] 项必须存在。"""
    entries = parse_library_entries(gdextension)
    if not entries:
        problems.append(f"{gdextension}: [libraries] 为空")
        return
    matched = [(k, v) for k, v in entries if tag_matches(k, platform, arch, target)]
    if not matched:
        # 该平台在本文件里没有声明项：预期（例如 node 腿的模板构建）。
        return
    for key, value in matched:
        if not os.path.exists(os.path.join(addon_dir, value)):
            problems.append(
                f"{os.path.relpath(gdextension)}: [{key}] 声明的 '{value}' 不存在"
            )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("project_dir", help="Godot 工程目录（含 addons/ 与 .godot/）")
    parser.add_argument("--platform", default=None, help="当前平台（windows/linux/macos/...）；不给则只查登记残留")
    parser.add_argument("--arch", default="x86_64", help="当前架构")
    parser.add_argument("--target", default="editor", help="editor / template_debug / template_release")
    args = parser.parse_args()

    if not os.path.isdir(args.project_dir):
        print(f"[gdextension] 工程目录不存在: {args.project_dir}", file=sys.stderr)
        return 1

    problems = []
    check_extension_list(args.project_dir, problems)

    addons = find_addon_dirs(args.project_dir)
    if not addons:
        print(f"[gdextension] WARNING: {args.project_dir}/addons 下没有扩展目录", file=sys.stderr)

    if args.platform is not None:
        for addon_dir, files in addons:
            if not files:
                problems.append(f"{os.path.relpath(addon_dir)}: 没有 .gdextension 文件")
                continue
            for name in files:
                check_declared_libraries(
                    addon_dir, os.path.join(addon_dir, name),
                    args.platform, args.arch, args.target, problems,
                )

    if problems:
        print("[gdextension] FAIL:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    checked = "登记残留" + (f" + {args.platform}/{args.arch}/{args.target} 的库声明" if args.platform else "")
    print(f"[gdextension] OK: {checked}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
