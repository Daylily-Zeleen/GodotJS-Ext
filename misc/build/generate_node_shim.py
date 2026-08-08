#!/usr/bin/env python
"""Generate a Windows node.def forwarding library for the embedded libnode.

When libnode is statically linked (--whole-archive) into the main godotjs-ext DLL,
native `.node` addons still import `napi_*` symbols from a module named `node.dll`
(the PE import table references `node.dll`). This script scans the libnode headers
for N-API symbols and generates a `.def` file that forwards every symbol to the
main DLL, e.g.:

    LIBRARY node
    EXPORTS
        napi_get_cb_info=godotjs-ext.windows.editor.x86_64.napi_get_cb_info

Then `link.exe /dll /noentry /def:node.def` produces a tiny `node.dll` whose exports
forward to the real implementations living in the main DLL.

Usage:
    python generate_node_shim.py <node_include_dir> <output.def> <target_dll_name>

The target_dll_name is the *file name* of the main DLL (e.g. godotjs-ext.windows.editor.x86_64.dll).
"""

import os
import re
import sys


def extract_symbols(include_dir):
    """Collect all N-API symbols (napi_*, node_api_*, node_module_register) from headers."""
    symbols = set()
    # Order matters: node.h includes many declarations beyond N-API, and the regexes
    # below only match the N-API prefix, so scanning node.h is safe.
    for header in ("js_native_api.h", "node_api.h", "node.h"):
        path = os.path.join(include_dir, header)
        if not os.path.isfile(path):
            print(f"[node-shim] WARNING: header not found: {path}", file=sys.stderr)
            continue
        with open(path, "rt", encoding="utf-8", errors="ignore") as f:
            text = f.read()
        # matches `napi_xxx(` and `node_api_xxx(` declarations
        for match in re.finditer(r"(napi|node_api)_[A-Za-z0-9_]+(?=\s*\()", text):
            symbols.add(match.group(0))
        # the module registration entry point used by NODE_MODULE_INIT
        for match in re.finditer(r"node_module_register\s*\(", text):
            symbols.add("node_module_register")
    return sorted(symbols)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1

    include_dir, def_path, target_dll = sys.argv[1], sys.argv[2], sys.argv[3]
    target_dll = target_dll.replace(".dll", "").replace(".DLL", "")

    symbols = extract_symbols(include_dir)
    if not symbols:
        print(f"[node-shim] ERROR: no N-API symbols found under {include_dir}", file=sys.stderr)
        return 1

    lines = ["LIBRARY node", "EXPORTS"]
    for symbol in symbols:
        lines.append(f"\t{symbol}={target_dll}.{symbol}")
    content = "\n".join(lines) + "\n"

    os.makedirs(os.path.dirname(def_path) or ".", exist_ok=True)
    with open(def_path, "wt", encoding="utf-8") as f:
        f.write(content)
    print(f"[node-shim] wrote {def_path} with {len(symbols)} forwarded symbols")
    return 0


if __name__ == "__main__":
    sys.exit(main())
