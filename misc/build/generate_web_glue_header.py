"""生成 `src/runtime/impl/web/jsb_web_glue.gen.h`。

背景：web 腿的 `jsbi_*` 原先由 emscripten JS library 提供（`library_godotjs_jsbi.js`），
那要求 glue 落在**主模块**（引擎模板）里——本仓是 GDExtension，主模块里没有这些符号，
所以官方模板下 side module 会 `Aborted(undefined symbol 'jsbi_init')`。

改为 EM_JS 后，JS 体随**扩展自己的 wasm**（`__em_js__*` 数据段）分发；emscripten 在加载
side module 时由主模块 `libdylink.js` 的 `addEmJs` 物化这些函数，并把它们注册进主模块
JS 作用域（因此可访问 `HEAPU8` / `_malloc` / `wasmMemory` / `PThread` 等）。

生成内容：
1. `kWebBridgeSource` / `kWebGlueLibrarySource`：两个 js 源文件的字节数组（内嵌二进制，不手写字符串）；
2. `jsb_web_glue_setup`：把桥与库在运行时求值一次（幂等）；
3. 每个 `jsbi_*` 一个 EM_JS 包装，转发到运行时求值出来的库对象上（因此 JS 侧的形参名
   与 C 侧无关，仅按位置传参）。

输入：`jsb_web_interop.h`（C 原型）、`js/library_godotjs_jsbi.js`（JS 实现）、`js/jsbb.impl.js`（桥）。
输出：`jsb_web_glue.gen.h`（AUTO-GENERATED，禁止手改）。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HEADER_NAME = "jsb_web_glue.gen.h"
LIBRARY_MERGE_LINE = "mergeInto(LibraryManager.library, GodotJSBrowserInterface);"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    # drop preprocessor lines (the JSBROWSER_API #define block would otherwise be parsed as a declaration)
    return "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))


def parse_prototypes(interop_header: str) -> dict[str, tuple[str, list[str], list[str]]]:
    """从 interop 头里取 `JSBROWSER_API <ret> <name>(<params>);`，返回 name -> (ret, param_types, param_names)。"""
    prototypes: dict[str, tuple[str, list[str], list[str]]] = {}
    text = strip_c_comments(interop_header)
    for match in re.finditer(r"JSBROWSER_API\s+(.*?);", text, flags=re.S):
        decl = " ".join(match.group(1).split())
        call = re.match(r"^(.*?)\b(jsbi_[A-Za-z0-9_]+)\s*\((.*)\)$", decl, flags=re.S)
        if call is None:
            # other browser-API declarations (e.g. jsb_web_glue_ensure) are skipped on purpose
            continue
        ret = call.group(1).strip()
        name = call.group(2)
        params_raw = call.group(3).strip()
        param_types: list[str] = []
        param_names: list[str] = []
        if params_raw and params_raw != "void":
            for index, param in enumerate(params_raw.split(",")):
                param = param.strip()
                parts = param.split()
                if len(parts) < 2:
                    raise SystemExit(f"cannot parse parameter {param!r} of {name}")
                param_names.append(parts[-1].replace("*", "").strip())
                param_types.append(" ".join(parts[:-1]) + ("*" if param.endswith("*") or "*" in parts[-1] else ""))
        prototypes[name] = (ret, param_types, param_names)
    return prototypes


def parse_library_names(library_source: str) -> list[str]:
    names = re.findall(r'^\s*"(jsbi_[A-Za-z0-9_]+)"\s*:', library_source, flags=re.M)
    return names


def emit_byte_array(name: str, content: str) -> str:
    data = content.encode("utf-8") + b"\0"
    lines = [f"constexpr unsigned char {name}[] = {{"]
    for offset in range(0, len(data), 20):
        chunk = data[offset : offset + 20]
        lines.append("\t" + " ".join(f"0x{byte:02x}," for byte in chunk))
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} <src_dir>", file=sys.stderr)
        return 2

    src_dir = Path(sys.argv[1])
    web_dir = src_dir / "runtime" / "impl" / "web"
    interop_header = read_text(web_dir / "jsb_web_interop.h")
    library_source = read_text(web_dir / "js" / "library_godotjs_jsbi.js")
    bridge_source = read_text(web_dir / "js" / "jsbb.impl.js")

    if LIBRARY_MERGE_LINE not in library_source:
        raise SystemExit(f"{LIBRARY_MERGE_LINE!r} not found in library_godotjs_jsbi.js")
    library_source = library_source.replace(LIBRARY_MERGE_LINE, "").replace(
        '// autoAddDeps(GodotJSBrowserInterface, "$GodotJSBrowserInterface")', ""
    )

    prototypes = parse_prototypes(interop_header)
    library_names = parse_library_names(library_source)

    missing_in_library = sorted(set(prototypes) - set(library_names))
    missing_in_header = sorted(set(library_names) - set(prototypes))
    if missing_in_library or missing_in_header:
        raise SystemExit(
            "jsbi symbol mismatch between jsb_web_interop.h and library_godotjs_jsbi.js:\n"
            f"  declared in header but not implemented in JS: {missing_in_library}\n"
            f"  implemented in JS but not declared in header: {missing_in_header}"
        )

    wrappers: list[str] = []
    for name in library_names:
        ret, param_types, param_names = prototypes[name]
        if param_types:
            params = ", ".join(
                f"{param_type} {param_name}" for param_type, param_name in zip(param_types, param_names)
            )
            args = ", ".join(f"a{index}" for index in range(len(param_names)))
            param_decl = ", ".join(
                f"{param_type} a{index}" for index, param_type in enumerate(param_types)
            )
        else:
            params = "void"
            args = ""
            param_decl = ""
        del params
        wrappers.append(
            f"// {name}: forward to the runtime-evaluated JS library entry.\n"
            f"EM_JS({ret if ret else 'void'}, {name}, ({param_decl or 'void'}), {{\n"
            f'\treturn globalThis.__jsb_web_lib["{name}"]({args});\n'
            f"}});"
        )

    output = "\n".join(
        [
            "// AUTO-GENERATED by misc/build/generate_web_glue_header.py",
            "#pragma once",
            "",
            "// The GodotJS web bridge and jsbi_* library, embedded as bytes so that the",
            "// extension side module carries its own JS glue (emscripten __em_js__ data).",
            emit_byte_array("kWebBridgeSource", bridge_source),
            "",
            emit_byte_array("kWebGlueLibrarySource", library_source),
            "",
            "// Evaluate the bridge + jsbi library once, in the main module's JS scope.",
            "EM_JS(void, jsb_web_glue_setup, (const char *bridge_source, const char *library_source), {",
            "\tif (globalThis.__jsb_web_glue_ready) {",
            "\t\treturn;",
            "\t}",
            "\tglobalThis.__jsb_web_glue_ready = true;",
            "\teval(UTF8ToString(bridge_source));",
            '\tglobalThis.__jsb_web_lib = eval(UTF8ToString(library_source) + "\\n;GodotJSBrowserInterface");',
            "});",
            "",
            *wrappers,
            "",
        ]
    )

    (web_dir / HEADER_NAME).write_text(output, encoding="utf-8")
    print(f"generated {web_dir / HEADER_NAME}: {len(library_names)} jsbi wrappers, "
          f"bridge {len(bridge_source)} bytes, library {len(library_source)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
