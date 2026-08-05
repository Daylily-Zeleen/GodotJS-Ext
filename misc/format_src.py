#!/usr/bin/env python3
"""
Batch format C/C++ source files under src/ using clang-format.

Uses the project root .clang-format configuration. Excludes:
  - src/impl/jsc/_NOT_FOR_INCLUDE_/   (third-party JavaScriptCore headers)
  - *.gen.* files                     (build-generated files, see .github/instructions)
  - *.def.* files                     (special definition files)

Usage:
    python misc/format_src.py                # format all files in place
    python misc/format_src.py --dry-run      # only list files that would change
    python misc/format_src.py --check        # exit non-zero if any file would change
    python misc/format_src.py --verbose      # print every file being processed
    python misc/format_src.py --clang-format /path/to/clang-format
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Directories that must never be touched (third-party code).
EXCLUDE_DIRS = {
    "_NOT_FOR_INCLUDE_",
}

# File extensions considered as C/C++ sources.
SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inc"}

ANSI_RED = "\x1b[31m"
ANSI_RESET = "\x1b[0m"


def print_error(*values: object) -> None:
    """Print an error message to stderr."""
    print("ERROR:", *values, file=sys.stderr)


def is_generated_file(path: Path) -> bool:
    """Return True if the file is a build-generated file (*.gen.*)."""
    return ".gen." in path.name


def is_def_file(path: Path) -> bool:
    """Return True if the file is a special definition file (*.def.*)."""
    return ".gen." in path.name


def find_clang_format(custom: str | None) -> str:
    """Locate a usable clang-format executable."""
    if custom:
        if shutil.which(custom):
            return custom
        print_error(f"clang-format not found at '{custom}'")
        sys.exit(1)

    # 1. VS Code C/C++ extension bundled LLVM (reliable, known-good version).
    vscode_ext = Path(os.path.expandvars(r"%USERPROFILE%\.vscode\extensions"))
    if vscode_ext.is_dir():
        for ext in sorted(vscode_ext.iterdir(), reverse=True):
            if ext.name.startswith("ms-vscode.cpptools"):
                candidate = ext / "LLVM" / "bin" / "clang-format.exe"
                if candidate.is_file():
                    return str(candidate)

    # 2. PATH lookup, but skip .bat/.cmd shims (e.g. depot_tools wrappers
    #    that do not forward --version output correctly).
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        if not directory:
            continue
        for name in ("clang-format.exe", "clang-format"):
            candidate = Path(directory) / name
            if candidate.is_file() and candidate.suffix.lower() not in {".bat", ".cmd"}:
                return str(candidate)

    # 3. Common install locations (Windows).
    candidates = [
        r"C:\Program Files\LLVM\bin\clang-format.exe",
        r"C:\Program Files (x86)\LLVM\bin\clang-format.exe",
    ]
    for candidate in candidates:
        if Path(candidate).is_file():
            return candidate

    print_error(
        "clang-format not found. Install LLVM (https://llvm.org) or pass "
        "--clang-format /path/to/clang-format"
    )
    sys.exit(1)


def check_clang_format_version(clang_format: str) -> None:
    """Verify the clang-format version is recent enough."""
    try:
        result = subprocess.run(
            [clang_format, "--version"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        version_line = (result.stdout or result.stderr).strip()
        print(f"Using: {version_line}")
    except OSError:
        print_error(f"Failed to run clang-format: {clang_format}")
        sys.exit(1)


def collect_sources(src_dir: Path) -> list[Path]:
    """Collect all format-eligible source files under src_dir."""
    sources = []
    for path in sorted(src_dir.rglob("*")):
        if not path.is_file():
            continue
        if any(part in EXCLUDE_DIRS for part in path.parts):
            continue
        if is_generated_file(path) or is_def_file(path):
            continue
        if path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        sources.append(path)
    return sources


def format_files(clang_format: str, files: list[Path], style: str, verbose: bool = False) -> int:
    """Format files in batches to stay under command-line length limits."""
    failed = 0
    batch_size = 50
    for i in range(0, len(files), batch_size):
        batch = files[i : i + batch_size]
        if verbose:
            for f in batch:
                print(f"  {f}")
        cmd = [clang_format, "-i", f"-style={style}"] + [str(f) for f in batch]
        result = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False
        )
        if result.returncode != 0:
            failed += len(batch)
            for line in (result.stderr or "").splitlines():
                print_error(line)
    return failed


def find_unformatted(clang_format: str, files: list[Path], style: str) -> list[Path]:
    """
    Return the subset of files that would be reformatted.

    clang-format >= 15 reports violations to stderr in the form:
      <path>:<line>:<col>: error: code should be clang-formatted [-Wclang-format-violations]
    (older versions printed "would reformat <file>" to stdout). Both are handled.
    """
    needs_formatting: list[Path] = []
    seen: set[str] = set()
    pattern_werror = re.compile(r"^(.*?):\d+:\d+: error: code should be clang-formatted")
    batch_size = 50
    for i in range(0, len(files), batch_size):
        batch = files[i : i + batch_size]
        cmd = [
            clang_format,
            "--dry-run",
            "--Werror",
            f"-style={style}",
        ] + [str(f) for f in batch]
        result = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False
        )
        stderr = result.stderr or ""
        stdout = result.stdout or ""
        for line in stderr.splitlines():
            m = pattern_werror.match(line)
            if m and m.group(1) not in seen:
                seen.add(m.group(1))
                needs_formatting.append(Path(m.group(1)))
        for line in stdout.splitlines():
            if line.startswith("would reformat"):
                file_name = line.split("would reformat ", 1)[-1].strip()
                if file_name not in seen:
                    seen.add(file_name)
                    needs_formatting.append(Path(file_name))
    return needs_formatting


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Batch format C/C++ sources under src/ with clang-format."
    )
    parser.add_argument(
        "--clang-format",
        help="Path to the clang-format executable (auto-detected if omitted).",
    )
    parser.add_argument(
        "--style",
        default="file",
        help="Style string passed to clang-format (default: 'file' = .clang-format).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only list files that would be reformatted, do not modify anything.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Exit with code 1 if any file is not formatted (CI use).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print every file being processed.",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    src_dir = project_root / "src"
    if not src_dir.is_dir():
        print_error(f"src directory not found: {src_dir}")
        return 1

    clang_format = find_clang_format(args.clang_format)
    check_clang_format_version(clang_format)

    sources = collect_sources(src_dir)
    if not sources:
        print("No source files found under src/.")
        return 0

    print(f"Found {len(sources)} source files.")

    if args.dry_run or args.check:
        needs_formatting = find_unformatted(clang_format, sources, args.style)
        if needs_formatting:
            print(f"{len(needs_formatting)} file(s) need formatting:")
            for f in needs_formatting:
                print(f"  {f.relative_to(project_root)}")
            return 1 if args.check else 0
        print("All files are already formatted.")
        return 0

    failed = format_files(clang_format, sources, args.style, verbose=args.verbose)
    if failed:
        print_error(f"{failed} file(s) failed to format.")
        return 1

    print(f"Formatted {len(sources)} file(s) in place.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
