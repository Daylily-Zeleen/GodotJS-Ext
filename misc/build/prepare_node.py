#!/usr/bin/env python3
"""Prepare the libnode prebuilt archive for the node mode of GodotJS-Ext.

libnode is Node.js compiled as a static library (it embeds V8, so the standalone
v8_monolith is NOT linked in node mode). We pull the prebuilt archive from the
moluopro/libnode GitHub releases (same source gode uses) and repack it into the
layout expected by SConstruct:

    third/node/
        include/                          (node.h, node_api.h, uv.h, ...)
        <platform>_<arch>_release/
            libnode.{lib,a}

Source archive layout (https://github.com/moluopro/libnode/releases):

    libnode.zip
        include/                          (headers; possibly under a libnode/ prefix)
        windows/x64/libnode.lib
        linux/x64/libnode.a
        macos/arm64/libnode.a
        android/arm64/libnode.a
        ios/arm64/libnode.a

The same repacked layout is what gets published to the GodotJS-Dependencies
release as `node_<version>.zip` (zip with a top-level `node/` directory), which
then enables the auto-download path in SConstruct (`download_dependency`).

Usage:
    python misc/build/prepare_node.py [--version 24.18.0] [--platform windows] [--output third/node]
"""

import argparse
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

DEFAULT_VERSION = "24.18.0"
DEFAULT_URL = "https://github.com/moluopro/libnode/releases/download/{version}/libnode.zip"

# zip layout dir -> SConstruct layout dir (platform_base() naming)
PLATFORM_MAP = {
    "windows": {
        "x64": "windows_x86_64_release",
        "arm64": "windows_arm64_release",
    },
    "linux": {
        "x64": "linux_x86_64_release",
        "arm64": "linux_arm64_release",
    },
    "macos": {
        "x64": "macos_x86_64_release",
        "arm64": "macos_arm64_release",
    },
    "android": {
        "x64": "android_x86_64_release",
        "arm64": "android_arm64_release",
    },
    "ios": {
        "arm64": "ios_arm64_release",
    },
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=DEFAULT_VERSION, help="libnode version tag (default: %(default)s)")
    parser.add_argument("--url", default=None, help="override the download URL (default: moluopro/libnode release)")
    parser.add_argument("--platform", default=None, help="only repack this platform (windows/linux/macos/android/ios); default: all found in the archive")
    parser.add_argument("--output", default="third/node", help="destination directory (default: %(default)s)")
    parser.add_argument("--keep-archive", action="store_true", help="keep the downloaded .zip in the output directory")
    args = parser.parse_args()

    version = args.version
    url = args.url or DEFAULT_URL.format(version=version)
    output_dir = os.path.abspath(args.output)
    platform_filter = args.platform

    if os.path.exists(os.path.join(output_dir, "include", "node.h")) and any(
        os.path.exists(os.path.join(output_dir, sub, "libnode.lib")) or os.path.exists(os.path.join(output_dir, sub, "libnode.a"))
        for sub in [v for v in PLATFORM_MAP.get(platform_filter, {}).values()] if platform_filter
    ) if platform_filter else os.path.exists(os.path.join(output_dir, "include", "node.h")):
        print(f"libnode already present at '{output_dir}' (include/node.h found); skipping.")
        return 0

    print(f"Downloading {url} ...")
    with tempfile.TemporaryDirectory(prefix="godotjs-node-") as tmp:
        archive_path = os.path.join(tmp, "libnode.zip")
        try:
            urllib.request.urlretrieve(url, archive_path)
        except Exception as e:  # pragma: no cover
            print(f"ERROR: failed to download libnode: {e}", file=sys.stderr)
            return 1

        extract_dir = os.path.join(tmp, "extract")
        os.makedirs(extract_dir, exist_ok=True)
        with zipfile.ZipFile(archive_path) as zf:
            zf.extractall(extract_dir)

        # The archive may or may not have a top-level "libnode" directory.
        source_dir = None
        for candidate in (os.path.join(extract_dir, "libnode"), extract_dir):
            if os.path.isfile(os.path.join(candidate, "include", "node.h")):
                source_dir = candidate
                break
        if source_dir is None:
            print("ERROR: the archive does not contain 'include/node.h'.", file=sys.stderr)
            return 1

        include_src = os.path.join(source_dir, "include")
        include_dst = os.path.join(output_dir, "include")
        os.makedirs(include_dst, exist_ok=True)
        shutil.copytree(include_src, include_dst, dirs_exist_ok=True)
        print(f"headers -> {include_dst}")

        copied = []
        for platform, arch_map in PLATFORM_MAP.items():
            if platform_filter and platform != platform_filter:
                continue
            plat_src = os.path.join(source_dir, platform)
            if not os.path.isdir(plat_src):
                continue
            for arch, subdir in arch_map.items():
                lib_name = "libnode.lib" if platform == "windows" else "libnode.a"
                lib_src = os.path.join(plat_src, arch, lib_name)
                if not os.path.isfile(lib_src):
                    continue
                lib_dst_dir = os.path.join(output_dir, subdir)
                os.makedirs(lib_dst_dir, exist_ok=True)
                shutil.copy2(lib_src, os.path.join(lib_dst_dir, lib_name))
                copied.append(f"{platform}/{arch} -> {subdir}/{lib_name}")

        if not copied:
            print("ERROR: no platform libraries found in the archive.", file=sys.stderr)
            return 1

        print("libnode is ready:")
        for entry in copied:
            print(f"  {entry}")

        if args.keep_archive:
            os.makedirs(output_dir, exist_ok=True)
            shutil.copy2(archive_path, os.path.join(output_dir, "libnode.zip"))
            print(f"archive kept at '{output_dir}/libnode.zip'")

    return 0


if __name__ == "__main__":
    sys.exit(main())
