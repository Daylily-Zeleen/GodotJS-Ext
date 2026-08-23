#!/usr/bin/env python3
# /************************************************************************/
# /*  static_binding_codegen.py                                           */
# /************************************************************************/
# /*  This file is part of:                                               */
# /*                                GodotJS-Ext                           */
# /*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
# /*                                                                      */
# /*  Generates C++ registration tables for compile-time static bindings  */
# /*  from extension_api.json. See docs/design/static-bindings.md.        */
# /*                                                                      */
# /*  Deterministic output: iteration order follows the input JSON order; */
# /*  the same inputs always produce byte-equal output (required by the   */
# /*  --check freshness gate in CI).                                      */
#
# P0 scope: string table + metadata registry tables + manifest reconciliation.
# Thunk dispatch emission lands in later phases (design doc §11).
#
# NOTE: Variant-type values are resolved BY NAME from godot-cpp's interface
# description (third/godot-cpp/gdextension/gdextension_interface.json) --
# never from the builtin_classes array position: extension_api.json omits the
# "Object" entry, so positions >= OBJECT would be off-by-one.

import argparse
import collections
import hashlib
import json
import os
import re
import sys

HEADER_GUARD_PREFIX = "GODOTJS_EXT_STATIC_BINDING_GEN_"
GENERATED_NOTE = (
    "// GENERATED FILE - DO NOT EDIT.\n"
    "// Regenerate with: scons static_binding_gen\n"
    "//   (or: python tools/static_binding_codegen.py"
    " --input <extension_api.json> --out src/static_binding/gen)\n"
)

DEFAULT_INTERFACE_JSON = os.path.join("third", "godot-cpp", "gdextension",
                                      "gdextension_interface.json")

_ENUM_TOKEN_FIXUPS = {
    "BOOL": "bool",
    "INT": "int",
    "FLOAT": "float",
    "AABB": "AABB",
    "RID": "RID",
    "TRANSFORM2D": "Transform2D",
    "TRANSFORM3D": "Transform3D",
}


def _enum_token_to_json_name(token):
    """GDEXTENSION_VARIANT_TYPE_<TOKENS> tail -> json builtin class name.

    e.g. STRING_NAME -> StringName, PACKED_BYTE_ARRAY -> PackedByteArray,
         VECTOR2 -> Vector2, TRANSFORM2D -> Transform2D (fixup), RID -> RID.
    """
    parts = token.split("_")
    out = []
    for p in parts:
        if p in _ENUM_TOKEN_FIXUPS:
            out.append(_ENUM_TOKEN_FIXUPS[p])
        else:
            out.append(p.capitalize())
    return "".join(out)


def load_variant_type_map(interface_json_path):
    """Extract {json_builtin_name: int value} from GDExtensionVariantType."""
    with open(interface_json_path, encoding="utf-8") as f:
        iface = json.load(f)
    enum_def = next(t for t in iface["types"]
                    if t.get("name") == "GDExtensionVariantType")
    prefix = "GDEXTENSION_VARIANT_TYPE_"
    mapping = {}
    for v in enum_def["values"]:
        name = v["name"]
        if not name.startswith(prefix):
            continue
        tail = name[len(prefix):]
        mapping[_enum_token_to_json_name(tail)] = int(v["value"])

    # sanity: contiguous from NIL=0
    values = sorted(mapping.values())
    assert values[0] == 0 and values == list(range(len(values))), \
        f"non-contiguous GDExtensionVariantType values: {values[:10]}..."
    assert mapping.get("Nil") == 0 and mapping.get("String") == 4 \
        and mapping.get("Vector2") == 5 and mapping.get("Callable") > mapping.get("RID"), \
        "variant type anchor check failed"
    return mapping


class StringPool:
    """Insertion-ordered unique string pool. id == index."""

    def __init__(self):
        self._index = {}
        self.strings = []

    def get(self, s):
        idx = self._index.get(s)
        if idx is None:
            idx = len(self.strings)
            self._index[s] = idx
            self.strings.append(s)
        return idx

    def __len__(self):
        return len(self.strings)


def cxx_bool(b):
    return "true" if b else "false"


def cxx_str(s):
    """Escape a python string as a C++ string literal (byte-stable)."""
    out = ['"']
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


class Model:
    def __init__(self):
        self.pool = StringPool()
        self.builtin_methods = []
        self.utility_funcs = []
        self.class_methods = []
        self.constructors = []
        self.operators = []
        self.members = []
        self.indexed_props = []
        self.exemptions = []
        self.classes_meta = []


def parse_args_list(args_json):
    out = []
    for a in args_json or []:
        entry = {"type": a.get("type", "Variant")}
        if "default_value" in a:
            entry["default"] = a["default_value"]
        out.append(entry)
    return out


def default_count(ent):
    return sum(1 for a in ent["args"] if "default" in a)


def collect(data, vt_map):
    m = Model()

    # --- builtin classes ----------------------------------------------------
    seen_vts = []
    for bc in data.get("builtin_classes", []):
        name = bc["name"]
        if name not in vt_map:
            raise SystemExit(
                f"FATAL: builtin class '{name}' not found in GDExtensionVariantType "
                f"mapping (check {_ENUM_TOKEN_FIXUPS} / interface json)")
        vt = vt_map[name]
        seen_vts.append(vt)
        for meth in bc.get("methods", []):
            if "hash" not in meth:
                raise SystemExit(
                    f"FATAL: builtin method without hash: {name}.{meth['name']}")
            m.builtin_methods.append({
                "vt": vt,
                "name_id": m.pool.get(meth["name"]),
                "hash": int(meth["hash"]),
                "args": parse_args_list(meth.get("arguments")),
                "ret_id": m.pool.get(meth.get("return_type", "void")),
                "is_vararg": bool(meth.get("is_vararg", False)),
                "is_static": bool(meth.get("is_static", False)),
            })
        for mem in bc.get("members", []):
            m.members.append({
                "vt": vt,
                "name_id": m.pool.get(mem["name"]),
                "type_id": m.pool.get(mem["type"]),
            })
        for ctor in bc.get("constructors", []):
            m.constructors.append({
                "vt": vt,
                "ctor_index": int(ctor["index"]),
                "args": parse_args_list(ctor.get("arguments")),
            })
        for op in bc.get("operators", []):
            m.operators.append({
                "left_vt": vt,
                "op_name_id": m.pool.get(op["name"]),
                "right_type_id": m.pool.get(op.get("right_type", "Variant")),
                "ret_type_id": m.pool.get(op.get("return_type", "Variant")),
            })
    # structural sanity: json order must still track ascending VT values
    # ("Object" is legitimately absent from the json; everything else ascends)
    if seen_vts != sorted(seen_vts):
        raise SystemExit(
            f"FATAL: builtin_classes are not in ascending VT order: {seen_vts}")

    # --- classes --------------------------------------------------------------
    classes_by_name = {c["name"]: c for c in data.get("classes", [])}

    def resolve_method(cls_name, mname, _max_depth=24):
        """Walk the inherits chain upward to find a method definition."""
        cur = cls_name
        for _ in range(_max_depth):
            cdef = classes_by_name.get(cur)
            if cdef is None:
                return None
            meths = cdef.get("methods") or []
            for meth in meths:
                if (meth["name"] == mname and not meth.get("is_virtual", False)
                        and "hash" in meth):
                    return meth
            cur = cdef.get("inherits", "")
            if not cur:
                return None
        return None

    for cls in data.get("classes", []):
        cls_name = cls["name"]
        cls_name_id = m.pool.get(cls_name)
        begin = len(m.class_methods)

        for meth in cls.get("methods", []):
            if meth.get("is_virtual", False) or "hash" not in meth:
                m.exemptions.append(
                    f"class method (virtual/no-hash): {cls_name}.{meth['name']}")
                continue
            m.class_methods.append({
                "class_name_id": cls_name_id,
                "name_id": m.pool.get(meth["name"]),
                "hash": int(meth["hash"]),
                "args": parse_args_list(meth.get("arguments")),
                "ret_id": m.pool.get(meth.get("return_type", "void")),
                "is_vararg": bool(meth.get("is_vararg", False)),
                "is_static": bool(meth.get("is_static", False)),
                "is_const": bool(meth.get("is_const", False)),
            })
        end = len(m.class_methods)
        m.classes_meta.append({
            "name_id": cls_name_id,
            "api_type": cls.get("api_type", ""),
            "method_begin": begin,
            "method_end": end,
        })

        for prop in cls.get("properties", []):
            if "index" not in prop:
                continue
            gname, sname = prop.get("getter", ""), prop.get("setter", "")
            gdef, sdef = resolve_method(cls_name, gname), resolve_method(cls_name, sname)
            if gdef is None and sdef is None:
                m.exemptions.append(
                    f"indexed property (getter/setter unresolved): "
                    f"{cls_name}.{prop['name']}[{prop['index']}]")
                continue
            m.indexed_props.append({
                "class_name_id": cls_name_id,
                "prop_name_id": m.pool.get(prop["name"]),
                "index": int(prop["index"]),
                "getter_name_id": m.pool.get(gname),
                "setter_name_id": m.pool.get(sname),
                "getter_hash": int(gdef["hash"]) if gdef and "hash" in gdef else 0,
                "setter_hash": int(sdef["hash"]) if sdef and "hash" in sdef else 0,
            })

    # --- utility functions ------------------------------------------------------
    for uf in data.get("utility_functions", []):
        if "hash" not in uf:
            m.exemptions.append(f"utility function (no-hash): {uf['name']}")
            continue
        m.utility_funcs.append({
            "name_id": m.pool.get(uf["name"]),
            "hash": int(uf["hash"]),
            "args": parse_args_list(uf.get("arguments")),
            "ret_id": m.pool.get(uf.get("return_type", "void")),
            "is_vararg": bool(uf.get("is_vararg", False)),
        })

    return m


def build_arg_pool(m):
    """Flat uint16 table of type-name ids; annotates each entity with its span."""
    pool = StringPool()
    flat = []
    for entity_list in (m.builtin_methods, m.class_methods,
                        m.utility_funcs, m.constructors):
        for ent in entity_list:
            off = len(flat)
            for a in ent["args"]:
                flat.append(pool.get(a["type"]))
            ent["arg_offset"] = off
            ent["arg_count"] = len(ent["args"])
    return pool, flat


# ---------------------------------------------------------------------------
# Emitters

def emit_string_names_h():
    guard = HEADER_GUARD_PREFIX + "STRING_NAMES_H"
    return (GENERATED_NOTE
            + f"#ifndef {guard}\n#define {guard}\n\n"
            + "#include <cstdint>\n#include <cstddef>\n\n"
            + "namespace jsb::static_binding::gen {\n\n"
            + "// Insertion-ordered unique string table (classes/methods/members/types).\n"
            + "// Resolved lazily into StringName at runtime (see src/static_binding/string_names.h).\n"
            + "extern const char *const k_strings[];\n"
            + "extern const uint32_t k_string_count;\n\n"
            + "} // namespace jsb::static_binding::gen\n\n"
            + f"#endif // {guard}\n")


def emit_string_names_cpp(m):
    parts = [GENERATED_NOTE,
             '#include "string_names.gen.h"\n',
             "namespace jsb::static_binding::gen {\n",
             "\nconst uint32_t k_string_count = %d;\n" % len(m.pool.strings),
             "\nconst char *const k_strings[] = {"]
    for s in m.pool.strings:
        parts.append("\n    " + cxx_str(s) + ",")
    parts.append("\n};\n\n} // namespace jsb::static_binding::gen\n")
    return "".join(parts)


def emit_registry_h():
    guard = HEADER_GUARD_PREFIX + "REGISTRY_H"
    body = GENERATED_NOTE
    body += f"#ifndef {guard}\n#define {guard}\n\n"
    body += "#include <cstdint>\n#include <cstddef>\n\n"
    body += "namespace jsb::static_binding::gen {\n\n"
    body += ("// POD metadata tables. Thunk dispatch (switch by hash) lands in later\n"
             "// phases; these tables are the reconciliation source of truth\n"
             "// (cross-checked by manifest.gen.json).\n\n")

    body += ("struct BuiltinMethodDef {\n"
             "    uint32_t name_id;\n"
             "    uint64_t hash;\n"
             "    uint16_t vt;          // GDExtensionVariantType value (resolved by NAME at generation time)\n"
             "    uint16_t arg_offset;  // into k_arg_types\n"
             "    uint16_t arg_count;\n"
             "    uint16_t default_count;\n"
             "    bool is_vararg;\n"
             "    bool is_static;\n"
             "};\n\n")

    body += ("struct UtilityFuncDef {\n"
             "    uint32_t name_id;\n"
             "    uint64_t hash;\n"
             "    uint16_t arg_offset;\n"
             "    uint16_t arg_count;\n"
             "    uint16_t default_count;\n"
             "    bool is_vararg;\n"
             "};\n\n")

    body += ("struct ClassMethodDef {\n"
             "    uint32_t class_name_id;\n"
             "    uint32_t name_id;\n"
             "    uint64_t hash;\n"
             "    uint16_t arg_offset;\n"
             "    uint16_t arg_count;\n"
             "    uint16_t default_count;\n"
             "    bool is_vararg;\n"
             "    bool is_static;\n"
             "    bool is_const;\n"
             "};\n\n")

    body += ("struct ConstructorDef {\n"
             "    uint16_t vt;\n"
             "    uint8_t ctor_index;\n"
             "    uint16_t arg_offset;\n"
             "    uint16_t arg_count;\n"
             "};\n\n")

    body += ("struct OperatorDef {\n"
             "    uint16_t left_vt;\n"
             "    uint32_t op_name_id;    // symbol as in json (\"==\", \"unary-\", ...)\n"
             "    uint32_t right_type_id; // type-name id; may be \"Variant\"\n"
             "    uint32_t ret_type_id;\n"
             "};\n\n")

    body += ("struct MemberDef {\n"
             "    uint16_t vt;\n"
             "    uint32_t name_id;\n"
             "    uint32_t type_id;\n"
             "};\n\n")

    body += ("struct IndexedPropDef {\n"
             "    uint32_t class_name_id;\n"
             "    uint32_t prop_name_id;\n"
             "    int32_t index;\n"
             "    uint32_t getter_name_id;\n"
             "    uint32_t setter_name_id;\n"
             "    uint64_t getter_hash;\n"
             "    uint64_t setter_hash;\n"
             "};\n\n")

    body += ("struct ClassInfoDef {\n"
             "    uint32_t name_id;\n"
             "    uint32_t method_begin;\n"
             "    uint32_t method_end;\n"
             "};\n\n")

    for ty, var in (
        ("BuiltinMethodDef", "k_builtin_methods"),
        ("UtilityFuncDef", "k_utility_funcs"),
        ("ClassMethodDef", "k_class_methods"),
        ("ConstructorDef", "k_constructors"),
        ("OperatorDef", "k_operators"),
        ("MemberDef", "k_members"),
        ("IndexedPropDef", "k_indexed_props"),
        ("ClassInfoDef", "k_class_infos"),
    ):
        body += f"extern const {ty} {var}[];\nextern const size_t {var}_count;\n\n"

    body += "extern const uint16_t k_arg_types[];\nextern const size_t k_arg_types_count;\n\n"
    body += "} // namespace jsb::static_binding::gen\n\n"
    body += f"#endif // {guard}\n"
    return body


def _dump_table(L, name, ty, rows, fmt):
    L.append("")
    if not rows:
        L.append(f"const {ty} {name}[] = {{}};")
        L.append(f"const size_t {name}_count = 0;")
        return
    L.append(f"const {ty} {name}[] = {{")
    for r in rows:
        L.append("    {" + fmt(r) + "},")
    L.append("};")
    L.append(f"const size_t {name}_count = {len(rows)};")


def emit_registry_cpp(m):
    _, flat_args = build_arg_pool(m)  # annotate entities before emitting
    L = [GENERATED_NOTE,
         '#include "registry.gen.h"\n',
         '#include "string_names.gen.h"\n',
         "\nnamespace jsb::static_binding::gen {"]

    _dump_table(L, "k_builtin_methods", "BuiltinMethodDef", m.builtin_methods,
                lambda r: (f"{r['name_id']}, {r['hash']}ULL, {r['vt']}, "
                           f"{r['arg_offset']}, {r['arg_count']}, {default_count(r)}, "
                           f"{cxx_bool(r['is_vararg'])}, {cxx_bool(r['is_static'])}"))
    _dump_table(L, "k_utility_funcs", "UtilityFuncDef", m.utility_funcs,
                lambda r: (f"{r['name_id']}, {r['hash']}ULL, {r['arg_offset']}, "
                           f"{r['arg_count']}, {default_count(r)}, "
                           f"{cxx_bool(r['is_vararg'])}"))
    _dump_table(L, "k_class_methods", "ClassMethodDef", m.class_methods,
                lambda r: (f"{r['class_name_id']}, {r['name_id']}, {r['hash']}ULL, "
                           f"{r['arg_offset']}, {r['arg_count']}, {default_count(r)}, "
                           f"{cxx_bool(r['is_vararg'])}, {cxx_bool(r['is_static'])}, "
                           f"{cxx_bool(r['is_const'])}"))
    _dump_table(L, "k_constructors", "ConstructorDef", m.constructors,
                lambda r: (f"{r['vt']}, {min(r['ctor_index'], 255)}, "
                           f"{r['arg_offset']}, {r['arg_count']}"))
    _dump_table(L, "k_operators", "OperatorDef", m.operators,
                lambda r: (f"{r['left_vt']}, {r['op_name_id']}, "
                           f"{r['right_type_id']}, {r['ret_type_id']}"))
    _dump_table(L, "k_members", "MemberDef", m.members,
                lambda r: f"{r['vt']}, {r['name_id']}, {r['type_id']}")
    _dump_table(L, "k_indexed_props", "IndexedPropDef", m.indexed_props,
                lambda r: (f"{r['class_name_id']}, {r['prop_name_id']}, {r['index']}, "
                           f"{r['getter_name_id']}, {r['setter_name_id']}, "
                           f"{r['getter_hash']}ULL, {r['setter_hash']}ULL"))
    _dump_table(L, "k_class_infos", "ClassInfoDef", m.classes_meta,
                lambda r: f"{r['name_id']}, {r['method_begin']}, {r['method_end']}")

    L.append("")
    if flat_args:
        L.append("const uint16_t k_arg_types[] = {")
        line = "   "
        for v in flat_args:
            piece = f" {v},"
            if len(line) + len(piece) > 100:
                L.append(line)
                line = "   "
            line += piece
        if line.strip():
            L.append(line)
        L.append("};")
    else:
        L.append("const uint16_t k_arg_types[] = {};")
    L.append(f"const size_t k_arg_types_count = {len(flat_args)};")

    L.append("\n} // namespace jsb::static_binding::gen\n")
    return "\n".join(L)


def emit_manifest(m, input_path, interface_path):
    uniq_defaults = set()
    for lst in (m.builtin_methods, m.class_methods, m.utility_funcs):
        for e in lst:
            for a in e["args"]:
                if "default" in a:
                    uniq_defaults.add((a["type"], a["default"]))

    def sha12(path):
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()[:12]

    manifest = {
        "generator_input_sha256_12": sha12(input_path),
        "interface_json_sha256_12": sha12(interface_path),
        "counts": {
            "builtin_methods": len(m.builtin_methods),
            "utility_funcs": len(m.utility_funcs),
            "class_methods": len(m.class_methods),
            "constructors": len(m.constructors),
            "operators": len(m.operators),
            "members": len(m.members),
            "indexed_props": len(m.indexed_props),
            "classes": len(m.classes_meta),
            "exemptions": len(m.exemptions),
            "unique_default_values": len(uniq_defaults),
            "unique_strings": len(m.pool.strings),
        },
        "reconciliation": {
            "variant_type_resolved_by_name": True,
            "builtin_vt_order_ascending": True,
            "indexed_props_all_resolved": all(
                p["getter_hash"] or p["setter_hash"] for p in m.indexed_props),
        },
        "exemption_kinds": dict(collections.Counter(
            e.split(":")[0] for e in m.exemptions)),
    }
    return json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="GodotJS-Ext static binding codegen (P0)")
    ap.add_argument("--input", required=True, help="path to extension_api.json")
    ap.add_argument("--out", required=True, help="output dir, e.g. src/static_binding/gen")
    ap.add_argument("--interface", default=DEFAULT_INTERFACE_JSON,
                    help="godot-cpp gdextension_interface.json (variant type enum source)")
    ap.add_argument("--check", action="store_true",
                    help="verify existing outputs are fresh (byte-identical); exit 1 otherwise")
    ns = ap.parse_args()

    vt_map = load_variant_type_map(ns.interface)

    with open(ns.input, encoding="utf-8") as f:
        data = json.load(f)

    m = collect(data, vt_map)
    outputs = {
        "string_names.gen.h": emit_string_names_h(),
        "string_names.gen.cpp": emit_string_names_cpp(m),
        "registry.gen.h": emit_registry_h(),
        "registry.gen.cpp": emit_registry_cpp(m),
        "manifest.gen.json": emit_manifest(m, ns.input, ns.interface),
    }

    if ns.check:
        stale = []
        for fname, content in outputs.items():
            path = os.path.join(ns.out, fname)
            if not os.path.exists(path):
                stale.append(fname + " (missing)")
                continue
            with open(path, "rb") as f:
                if f.read() != content.encode("utf-8"):
                    stale.append(fname + " (stale)")
        if stale:
            print("STALE generated files detected:", file=sys.stderr)
            for s in stale:
                print("  " + s, file=sys.stderr)
            print("Run: scons static_binding_gen", file=sys.stderr)
            return 1
        print("generated files are fresh")
        return 0

    os.makedirs(ns.out, exist_ok=True)
    for fname, content in outputs.items():
        path = os.path.join(ns.out, fname)
        new = content.encode("utf-8")
        old = None
        if os.path.exists(path):
            with open(path, "rb") as f:
                old = f.read()
        if old != new:
            with open(path, "wb") as f:
                f.write(new)
            print(f"wrote {path} ({len(new)} bytes)")
    man = json.loads(outputs["manifest.gen.json"])
    print(json.dumps(man["counts"], indent=2))
    print("OK: static binding tables generated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
