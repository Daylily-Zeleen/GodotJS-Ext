#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
One-time batch script: scan project source files and add LGPL 2.1 copyright headers.

Reads the core copyright text from misc/copyright_header.txt (single source of truth)
and wraps it in the appropriate comment syntax for each file type.

Usage:
    python misc/copyright_headers.py              # batch add headers
    python misc/copyright_headers.py --check      # CI: exit non-zero if any file lacks header
    python misc/copyright_headers.py --dry-run    # preview changes without modifying files
"""

import argparse
import os
import sys
from pathlib import Path

from copyright import read_copyright_text, generate_copyright_header_cpp

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

# (directory, list_of_extensions_to_collect)
COLLECT_PATHS = [
    ("src", [".cpp", ".h", ".hpp", ".c", ".cc", ".cxx"]),
    ("scripts/jsb.runtime/src", [".ts"]),
    ("scripts/jsb.editor/src", [".ts"]),
    (".github/workflows", [".yml", ".yaml"]),
    (".github/actions", [".yml", ".yaml"]),
]

# =============================================================================
# TS / YAML header builders (C++ uses shared module above)
# =============================================================================

def make_header_ts(filename: str, copyright_text: str) -> str:
    """Build a TS/JS block comment header."""
    lines = copyright_text.split("\n")
    result_lines = ["/*"]
    result_lines.append(f" *  {filename}")
    result_lines.append(" *")
    for line in lines:
        result_lines.append(f" *  {line}" if line.strip() else " *")
    result_lines.append(" */")
    return "\n".join(result_lines) + "\n"


def make_header_yaml(filename: str, copyright_text: str) -> str:
    """Build a YAML comment header."""
    lines = copyright_text.split("\n")
    result_lines = [f"#  {filename}"]
    result_lines.append("#")
    for line in lines:
        result_lines.append(f"#  {line}" if line.strip() else "#")
    return "\n".join(result_lines) + "\n"


# Extension -> (format_name, make_header_fn)
EXTENSION_MAP = {
    ".cpp": ("cpp", generate_copyright_header_cpp), ".h": ("cpp", generate_copyright_header_cpp),
    ".hpp": ("cpp", generate_copyright_header_cpp), ".c": ("cpp", generate_copyright_header_cpp),
    ".cc": ("cpp", generate_copyright_header_cpp), ".cxx": ("cpp", generate_copyright_header_cpp),
    ".ts": ("ts", make_header_ts), ".js": ("ts", make_header_ts),
    ".mts": ("ts", make_header_ts), ".mjs": ("ts", make_header_ts),
    ".yml": ("yaml", make_header_yaml), ".yaml": ("yaml", make_header_yaml),
}

# Directories to always skip
EXCLUDE_DIRS = {"third", "node_modules", "_NOT_FOR_INCLUDE_", "__pycache__", ".git"}

# =============================================================================
# Detection helpers
# =============================================================================

COPYRIGHT_MARKERS = [
    "Copyright (c)",
    "This file is part of",
    "GNU Lesser General Public",
    "Permission is hereby granted",
    "SPDX-License-Identifier",
    "licensed under",
]


def has_copyright_header(content: str) -> bool:
    """Check if a file already has a copyright/license header (scan first 2KB)."""
    head = content[:2000]
    return any(marker in head for marker in COPYRIGHT_MARKERS)


def is_godot_mit_file(filepath: Path) -> bool:
    """Detect files copied from Godot Engine (MIT license) that must not be modified."""
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            head = f.read(2000)
    except OSError:
        return False
    return "GODOT ENGINE" in head


# =============================================================================
# File collection
# =============================================================================

def collect_files() -> list[tuple[Path, str]]:
    """
    Walk configured directories and return (filepath, template) pairs.
    Skips excluded directories, third-party code, and files that already have headers.
    """
    collected = []

    for rel_dir, extensions in COLLECT_PATHS:
        abs_dir = PROJECT_ROOT / rel_dir
        if not abs_dir.is_dir():
            continue

        for root, dirs, files in os.walk(abs_dir):
            dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]

            for fname in sorted(files):
                fpath = Path(root) / fname

                if fpath.suffix.lower() not in extensions:
                    continue
                if is_godot_mit_file(fpath):
                    continue

                try:
                    content = fpath.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue

                if has_copyright_header(content):
                    continue

                entry = EXTENSION_MAP.get(fpath.suffix.lower())
                if entry is None:
                    continue

                collected.append((fpath, entry[0]))

    return collected


# =============================================================================
# Header replacement logic
# =============================================================================

def add_header_to_file(filepath: Path, format_name: str, copyright_text: str, dry_run: bool = False) -> bool:
    """
    Add a copyright header to a file.
    If the file starts with a Godot-style /********** block, replace it.
    Otherwise, prepend the header.
    Preserves UTF-8 BOM if present.
    Returns True if the file was modified (or would be modified in dry-run).
    """
    BOM = b'\xef\xbb\xbf'
    try:
        raw = filepath.read_bytes()
    except OSError:
        return False

    has_bom = raw.startswith(BOM)
    if has_bom:
        raw = raw[len(BOM):]

    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError:
        return False

    # Select the right header builder
    entry = EXTENSION_MAP.get(filepath.suffix.lower())
    if entry is None:
        return False
    _, make_header_fn = entry
    new_header = make_header_fn(filepath.name, copyright_text)

    # Check if file starts with a Godot-style header block
    stripped = content.lstrip("\ufeff")
    if stripped.startswith("/**********"):
        lines = stripped.split("\n")
        idx = 0
        while idx < len(lines) and lines[idx].strip() == "":
            idx += 1
        if idx < len(lines) and "/**********" in lines[idx]:
            while idx < len(lines):
                if not lines[idx].startswith("/*"):
                    break
                idx += 1
            while idx < len(lines) and lines[idx].strip() == "":
                idx += 1
        rest = "\n".join(lines[idx:])
        new_content = new_header + "\n" + rest
    else:
        new_content = new_header + "\n" + content

    if new_content == content and not has_bom:
        return False

    if not dry_run:
        out = new_content.encode("utf-8")
        if has_bom:
            out = BOM + out
        filepath.write_bytes(out)

    return True


# =============================================================================
# CLI
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Add LGPL 2.1 copyright headers to project source files."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Exit non-zero if any file is missing a copyright header (CI use).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="List files that would be modified without changing them.",
    )
    args = parser.parse_args()

    copyright_text = read_copyright_text()
    files = collect_files()

    if not files:
        print("All source files already have copyright headers.")
        return 0

    if args.check:
        print(f"{len(files)} file(s) missing copyright headers:")
        for fpath, _ in files:
            print(f"  {fpath.relative_to(PROJECT_ROOT)}")
        return 1

    modified = 0
    for fpath, format_name in files:
        rel = fpath.relative_to(PROJECT_ROOT)
        if args.dry_run:
            print(f"  would add header: {rel}")
        else:
            if add_header_to_file(fpath, format_name, copyright_text):
                print(f"  added header: {rel}")
                modified += 1

    if args.dry_run:
        print(f"\n{len(files)} file(s) would be modified.")
    else:
        print(f"\nDone. Modified {modified} file(s).")

    return 0


if __name__ == "__main__":
    sys.exit(main())
