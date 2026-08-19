"""
Shared copyright header generation for GodotJS-Ext.

Single source of truth for C++ block-comment copyright headers.
Used by SConstruct, generate_templates_header.py, and copyright_headers.py.

The column width is computed dynamically from the longest line in the
copyright template, so changing misc/copyright_header.txt never requires
touching any code.
"""

import unicodedata
from pathlib import Path

_TEMPLATE_PATH = Path(__file__).resolve().parent / "copyright_header.txt"


def read_copyright_text() -> str:
    """Return the raw copyright text from misc/copyright_header.txt."""
    return _TEMPLATE_PATH.read_text(encoding="utf-8").strip()


def display_width(text: str) -> int:
    """Calculate the display width of *text* in a CJK-aware monospace font.

    East-Asian-Wide (``W``) and East-Asian-Fullwidth (``F``) characters
    occupy 2 columns; everything else occupies 1.  This matches the
    rendering behaviour of most terminals and monospace fonts.
    """
    width = 0
    for ch in text:
        eaw = unicodedata.east_asian_width(ch)
        width += 2 if eaw in ("W", "F") else 1
    return width


def _pad_to_width(text: str, target_width: int) -> str:
    """Right-pad *text* with spaces so that its display width equals *target_width*."""
    return text + " " * max(0, target_width - display_width(text))


def generate_copyright_header_cpp(filename: str, copyright_text: str) -> str:
    """Build a C++ block-comment copyright header with *dynamic* column width.

    The total line width is derived from the display width of the longest
    content line (CJK-aware) so that every line — including the ``/**…*/``
    separator borders — has the same visual width and the closing ``*/``
    is always aligned.

    Layout (every line is ``total_width`` display-columns)::

        /*                              ← border: /* + asterisks + */
        /*  filename                     ← /*  + content + */
        /*                              ← border
        /*  content…                     ← /*  + content + */
        /*                              ← border
    """
    lines = copyright_text.split("\n")

    # Every candidate for the widest line — use display width, not len().
    candidates = [filename] + lines
    max_dw = max(display_width(c) for c in candidates)

    # content_width = max_dw + 2  (1 space + content + 1 space)
    # Each full line = /* + 2sp + content_width + 2 + */ = content_width + 6 chars
    # Border asterisks = content_width + 2  (so /*+asterisks+*/ == content_width+6)
    content_width = max_dw + 2
    border = "/*" + "*" * (content_width + 2) + "*/"

    result = [
        border,
        "/*  " + _pad_to_width(filename, content_width) + "*/",
        border,
    ]
    for line in lines:
        result.append("/*  " + _pad_to_width(line, content_width) + "*/")
    result.append(border)

    return "\n".join(result) + "\n"
