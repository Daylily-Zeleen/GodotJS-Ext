#!/usr/bin/env python3
"""
Merge multiple natvis files into a single <AutoVisualizer> document.

Usage:
    merge_natvis.py <output> <input1> [<input2> ...]

Parses each input file (a standard Natvis XML document with an
<AutoVisualizer> root), collects all child elements (<Type>, <UIVisualizer>,
etc.) in argument order, and writes them into one <AutoVisualizer> root.

The output is idempotent: if the merged content is unchanged, the file is not
rewritten (mirrors the "no diff" behavior in SConstruct's write_file helper).
The merged document is re-parsed before writing to guarantee valid XML.

Use cases:
  * Non-MSVC toolchains (which have no /NATVIS: linker option to embed natvis
    into the PDB) can generate a single root-level godotjs-ext.natvis that
    debuggers can load via visualizerFile / _NT_NATVIS_FILE.
"""

import os
import sys
import xml.etree.ElementTree as ET

# Keep the default (empty) namespace prefix so serialized elements do not get
# an unwanted "ns0:" prefix, which the Visual Studio natvis engine would not
# recognize.
ET.register_namespace('', 'http://schemas.microsoft.com/vstudio/debugger/natvis/2010')

NATVIS_NS = 'http://schemas.microsoft.com/vstudio/debugger/natvis/2010'
ROOT_TAG = f'{{{NATVIS_NS}}}AutoVisualizer'


def read_children(path: str) -> list:
    """Parse a natvis file and return its root element's child elements."""
    if not os.path.exists(path):
        print(f"Error: natvis source not found: {path}", file=sys.stderr)
        sys.exit(1)

    tree = ET.parse(path)
    root = tree.getroot()

    if root.tag != ROOT_TAG:
        print(f"Error: '{path}' is not a natvis file (root is '{root.tag}')", file=sys.stderr)
        sys.exit(1)

    # Return a deep copy so later mutation does not affect the source tree.
    return [ET.fromstring(ET.tostring(child)) for child in root]


def merge(input_files: list) -> ET.Element:
    """Build a merged <AutoVisualizer> element from the input files."""
    merged = ET.Element(ROOT_TAG)

    for path in input_files:
        for child in read_children(path):
            merged.append(child)

    return merged


def write_if_changed(output_file: str, content: str) -> bool:
    """Write content only when it differs from the current file. Returns True when written."""
    if os.path.exists(output_file):
        with open(output_file, 'rt', encoding='utf-8') as f:
            if f.read() == content:
                print(f"generate {output_file}: no diff")
                return False

    with open(output_file, 'wt', encoding='utf-8') as f:
        f.write(content)

    print(f"generating {output_file}")
    return True


def main():
    if len(sys.argv) < 3:
        print("Usage: merge_natvis.py <output> <input1> [<input2> ...]", file=sys.stderr)
        sys.exit(1)

    output_file = os.path.abspath(sys.argv[1])
    input_files = [os.path.abspath(p) for p in sys.argv[2:]]

    if len(input_files) == 1:
        print(f"Warning: only one natvis source given; merge is a no-op for '{input_files[0]}'")

    merged = merge(input_files)

    # Re-parse the serialized output to guarantee it is valid XML before writing.
    xml_bytes = ET.tostring(merged, encoding='utf-8', xml_declaration=True)
    ET.fromstring(xml_bytes)

    # Pretty-print: XML declaration + indented tree.
    ET.indent(merged, space='    ')
    xml_bytes = ET.tostring(merged, encoding='utf-8', xml_declaration=True)
    content = xml_bytes.decode('utf-8') + '\n'

    write_if_changed(output_file, content)


if __name__ == "__main__":
    main()
