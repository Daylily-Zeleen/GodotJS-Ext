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

# Name -> GDExtensionVariantType value, populated by load_variant_type_map.
VARIANT_TYPE_VALUES = {}

# json type name -> C++ parameter type used by the direct-conversion layer.
PARAM_TYPE_MAP = {
    "bool": "bool",
    "int": "int64_t",
    "float": "double",
    "String": "godot::String",
    "StringName": "godot::StringName",
    "NodePath": "godot::NodePath",
    "RID": "godot::RID",
    "Callable": "godot::Callable",
    "Signal": "godot::Signal",
    "Object": "godot::Object*",
    "Dictionary": "godot::Dictionary",
    "Array": "godot::Array",
    "Variant": "godot::Variant",
}
for _t in ("Vector2", "Vector2i", "Rect2", "Rect2i", "Vector3", "Vector3i",
           "Transform2D", "Vector4", "Vector4i", "Plane", "Quaternion", "AABB",
           "Basis", "Transform3D", "Projection", "Color",
           "PackedByteArray", "PackedInt32Array", "PackedInt64Array",
           "PackedFloat32Array", "PackedFloat64Array", "PackedStringArray",
           "PackedVector2Array", "PackedVector3Array", "PackedColorArray",
           "PackedVector4Array"):
    PARAM_TYPE_MAP[_t] = "godot::" + _t

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
        if tail == "VARIANT_MAX":
            continue  # sentinel entry, not a real type
        mapping[_enum_token_to_json_name(tail)] = int(v["value"])

    # sanity: contiguous from NIL=0
    values = sorted(mapping.values())
    assert values[0] == 0 and values == list(range(len(values))), \
        f"non-contiguous GDExtensionVariantType values: {values[:10]}..."
    VARIANT_TYPE_VALUES.clear()
    VARIANT_TYPE_VALUES.update(mapping)
    # reverse mapping: value -> JSON name (for Ret<godot::Variant::NAME> emission)
    global VARIANT_TYPE_NAMES
    VARIANT_TYPE_NAMES = {v: k for k, v in mapping.items()}
    # JSON name -> enum name mapping (for Ret<godot::Variant::ENUM> emission)
    global JSON_TO_ENUM_NAME
    JSON_TO_ENUM_NAME = {
        "nil": "NIL",
        "bool": "BOOL",
        "int": "INT",
        "float": "FLOAT",
        "String": "STRING",
        "StringName": "STRING_NAME",
        "NodePath": "NODE_PATH",
        "RID": "RID",
        "Object": "OBJECT",
        "Callable": "CALLABLE",
        "Signal": "SIGNAL",
        "Dictionary": "DICTIONARY",
        "Array": "ARRAY",
        "Vector2": "VECTOR2",
        "Vector2i": "VECTOR2I",
        "Rect2": "RECT2",
        "Rect2i": "RECT2I",
        "Vector3": "VECTOR3",
        "Vector3i": "VECTOR3I",
        "Transform2D": "TRANSFORM2D",
        "Vector4": "VECTOR4",
        "Vector4i": "VECTOR4I",
        "Plane": "PLANE",
        "Quaternion": "QUATERNION",
        "AABB": "AABB",
        "Basis": "BASIS",
        "Transform3D": "TRANSFORM3D",
        "Projection": "PROJECTION",
        "Color": "COLOR",
        "PackedByteArray": "PACKED_BYTE_ARRAY",
        "PackedInt32Array": "PACKED_INT32_ARRAY",
        "PackedInt64Array": "PACKED_INT64_ARRAY",
        "PackedFloat32Array": "PACKED_FLOAT32_ARRAY",
        "PackedFloat64Array": "PACKED_FLOAT64_ARRAY",
        "PackedStringArray": "PACKED_STRING_ARRAY",
        "PackedVector2Array": "PACKED_VECTOR2_ARRAY",
        "PackedVector3Array": "PACKED_VECTOR3_ARRAY",
        "PackedColorArray": "PACKED_COLOR_ARRAY",
        "PackedVector4Array": "PACKED_VECTOR4_ARRAY",
    }
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
        self.indexed_method_names = {}
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
    m.vt_names = {}

    # --- builtin classes ----------------------------------------------------
    seen_vts = []
    for bc in data.get("builtin_classes", []):
        name = bc["name"]
        if name not in vt_map:
            raise SystemExit(
                f"FATAL: builtin class '{name}' not found in GDExtensionVariantType "
                f"mapping (check {_ENUM_TOKEN_FIXUPS} / interface json)")
        vt = vt_map[name]
        m.vt_names[vt] = name
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
                # raw name: builtin member groups are keyed per VT in
                # emit_dispatch_cpp; the literal is emitted per entry
                "name_str": mem["name"],
                # the member's OWN declared type drives its ptrcall slot
                "member_type": VARIANT_TYPE_VALUES.get(mem["type"], -1),
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
                # method defs actually carrying this index (None = side
                # unresolved); emit_class_dispatch_cpp threads the index
                # into those thunk instantiations via these hashes
                # declared value type (json property "type") -- drives the
                # strict JS->Variant conversion in the static setter
                "prop_type": prop.get("type", "Variant"),
                "_gdef": gdef,
                "_sdef": sdef,
                "getter_name_id": m.pool.get(gname),
                "setter_name_id": m.pool.get(sname),
                "getter_hash": int(gdef["hash"]) if gdef and "hash" in gdef else 0,
                "setter_hash": int(sdef["hash"]) if sdef and "hash" in sdef else 0,
            })

    # (class_name, method_hash) -> {method_name} backing an indexed property;
    # used to thread the property index into those thunk instantiations

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


def arg_template_expr(a):
    t = a["type"]
    ct = PARAM_TYPE_MAP.get(t)
    if ct is None:
        # enums/bitfields are integers at the ABI level; bare engine class
        # names (Button, Sprite2D, ...) behave as plain Object* in the
        # dynamic path as well -- same conversion semantics.
        ct = ("int64_t" if t.startswith(("enum::", "bitfield::"))
              else "godot::Object*")
    if "default" in a:
        return 'Arg<%s, %s>' % (ct, cxx_str(a["default"]))
    return 'Arg<%s>' % ct


def ret_template_expr(t):
    if t in ('void', 'null'):
        return 'Ret<godot::Variant::NIL>'
    if t in VARIANT_TYPE_VALUES:
        # Emit enum name for C++20 NTTP (godot::Variant::VECTOR2 etc.)
        enum_name = JSON_TO_ENUM_NAME.get(t, t.upper())
        return 'Ret<godot::Variant::%s>' % enum_name
    if t.startswith("enum::") or t.startswith("bitfield::") or t == "Variant":
        if t == "Variant":
            return 'RetAny'
        return 'Ret<godot::Variant::INT>'  # enums/bitfields come back as integers
    raise AssertionError(f"unhandled return type: {t}")


def emit_dispatch_cpp(m):
    """Dispatch tables: outer switch on variant type / flat for utilities,
    inner resolution by (hash -> name if-chain), because signature-derived
    hashes collide across same-signature methods."""
    L = [GENERATED_NOTE,
         '#include "static_binding/dispatch.h"',
         '#include "static_binding/thunks/builtin_methods.h"',
         '#include "static_binding/thunks/builtin_members.h"',
         '#include "static_binding/thunks/utility_functions.h"',
         "",
         "namespace jsb::static_binding {",
         "namespace {",
         "using thunks::builtin_method_thunk;",
         "using thunks::builtin_vararg_method_thunk;",
         "using thunks::utility_function_thunk;",
         ""]

    def entry_expr(e, is_utility=False):
        name_lit = cxx_str(m.pool.strings[e["name_id"]])
        args_exprs = "".join(", " + arg_template_expr(a) for a in e["args"])
        tmpl_name = ("thunks::utility_vararg_function_thunk" if is_utility and e.get("is_vararg") else
                     "thunks::builtin_vararg_method_thunk" if e.get("is_vararg") else
                     "thunks::utility_function_thunk" if is_utility else
                     "thunks::builtin_method_thunk")
        vt_part = ("(godot::Variant::Type)%d, " % e["vt"]) if not is_utility else ""
        static_part = ("%s, " % cxx_bool(e["is_static"])) if not is_utility else ""
        return "%s%s<%s%du, %s, %s%s%s>" % (
            "(ThunkFn)&", tmpl_name, vt_part, e["hash"], name_lit,
            static_part, ret_template_expr(m.pool.strings[e["ret_id"]]), args_exprs)

    by_vt = collections.OrderedDict()
    for e in m.builtin_methods:
        by_vt.setdefault(e["vt"], []).append(e)

    for vt in sorted(by_vt):
        entries = by_vt[vt]
        by_hash = collections.OrderedDict()
        for e in entries:
            by_hash.setdefault(e["hash"], []).append(e)
        L.append("ThunkFn find_%s(const godot::StringName &p_name, uint32_t p_hash) {" % m.vt_names[vt])
        L.append("\tswitch (p_hash) {")
        for h, group in by_hash.items():
            if len(group) == 1:
                e = group[0]
                L.append("\tcase %du: return %s;" % (h, entry_expr(e)))
            else:
                L.append("\tcase %du: {" % h)
                for e in group:
                    nlit = cxx_str(m.pool.strings[e["name_id"]])
                    L.append('\t\tif (p_name == godot::StringName(%s)) return %s;' % (nlit, entry_expr(e)))
                L.append("\t\treturn nullptr;")
                L.append("\t}")
        L.append("\tdefault: return nullptr;")
        L.append("\t}")
        L.append("}")
        L.append("")

    L.append("} // namespace")
    L.append("")
    L.append("const ThunkFn find_builtin_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name, uint32_t p_hash) {")
    L.append("\tstatic_assert((int)godot::Variant::VARIANT_MAX <= 64, \"slot table sized for 64 variant types\");")
    L.append("\tusing PerVtResolver = ThunkFn (*)(const godot::StringName &, uint32_t);")
    L.append("\tstatic const PerVtResolver k_by_type[(int)godot::Variant::VARIANT_MAX] = {")
    max_vt = max(VARIANT_TYPE_VALUES.values())
    for vt in range(max_vt + 1):
        fn = ("find_" + m.vt_names[vt]) if vt in by_vt else "nullptr"
        L.append("\t\t%s," % fn)
    L.append("\t};")
    L.append("\treturn unsigned(p_vt) < std::size(k_by_type) ? k_by_type[unsigned(p_vt)](p_name, p_hash) : nullptr;")
    L.append("}")
    L.append("")

    util_by_hash = collections.OrderedDict()
    for u in m.utility_funcs:
        util_by_hash.setdefault(u["hash"], []).append(u)

    L.append("const ThunkFn find_utility_thunk(const godot::StringName &p_name, uint32_t p_hash) {")
    L.append("\tswitch (p_hash) {")
    for h, group in util_by_hash.items():
        if len(group) == 1:
            L.append("\tcase %du: return %s;" % (h, entry_expr(group[0], True)))
        else:
            L.append("\tcase %du: {" % h)
            for u in group:
                nlit = cxx_str(m.pool.strings[u["name_id"]])
                L.append('\t\tif (p_name == godot::StringName(%s)) return %s;' % (nlit, entry_expr(u, True)))
            L.append("\t\treturn nullptr;")
            L.append("\t}")
    L.append("\tdefault: return nullptr;")
    L.append("\t}")
    L.append("}")
    L.append("")
    # ---- builtin member accessors (P3) -------------------------------------
    L.append("// ---- builtin member accessors (P3) ----")
    by_vt = collections.OrderedDict()
    for e in m.members:
        by_vt.setdefault(e["vt"], []).append(e)

    def emit_member_lookup(fn_name, side):
        tmpl = ("thunks::member_getter_thunk" if side == "g"
                else "thunks::member_setter_thunk")
        L.append("const ThunkFn %s(godot::Variant::Type p_vt, const godot::StringName &p_name) {" % fn_name)
        L.append("\tswitch ((int)p_vt) {")
        for vt in sorted(by_vt):
            L.append("\tcase %d: {" % vt)
            for e in by_vt[vt]:
                nlit = cxx_str(e["name_str"])
                L.append('\t\tif (p_name == godot::StringName(%s))'
                         ' return (ThunkFn)&%s<(godot::Variant::Type)%d, (godot::Variant::Type)%d, %s>;'
                         % (nlit, tmpl, vt, e["member_type"], nlit))
            L.append("\t\treturn nullptr;")
            L.append("\t}")
        L.append("\tdefault: return nullptr;")
        L.append("\t}")
        L.append("}")
        L.append("")

    emit_member_lookup("find_builtin_member_getter_thunk", "g")
    emit_member_lookup("find_builtin_member_setter_thunk", "s")

    L.append("} // namespace jsb::static_binding")
    L.append("")
    return "\n".join(L)


def class_ident(name):
    """Class names in Godot are identifier-safe; keep a conservative sanitizer."""
    out = []
    for ch in name:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        else:
            out.append("_%02X" % ord(ch))
    s = "".join(out)
    assert s.isidentifier(), f"class name is not a valid identifier: {name!r} -> {s}"
    return s


def class_entry_expr(e, cname, extra=""):
    name_lit = cxx_str(m.pool.strings[e["name_id"]])
    cls_lit = cxx_str(cname)
    args_exprs = "".join(", " + arg_template_expr(a) for a in e["args"])
    tmpl_name = ("thunks::class_vararg_method_thunk" if e.get("is_vararg")
                 else "thunks::class_method_thunk")
    return "%s<%du, %s, %s, %s, %s%s%s>" % (
        tmpl_name, e["hash"], cls_lit, name_lit,
        cxx_bool(e["is_static"]),
        ret_template_expr(m.pool.strings[e["ret_id"]]), args_exprs, extra)


def emit_class_dispatch_cpp(m):
    """Per-class hash switches for Object-derived methods. The top-level entry
    resolves the class via binary search over sorted entries (registration-time
    only), then delegates -- the per-class switch disambiguates same-hash
    overloads by method name."""
    L = [GENERATED_NOTE,
         '#include "static_binding/dispatch.h"',
         '#include "static_binding/thunks/class_methods.h"',
         '#include "static_binding/thunks/indexed_properties.h"',
         "",
         "#include <cstring>",
         "",
         "namespace jsb::static_binding {",
         "namespace {",
         "using thunks::class_method_thunk;",
         "using thunks::class_vararg_method_thunk;",
         ""]

    def class_entry_expr(e, cname, extra=""):
        name_lit = cxx_str(m.pool.strings[e["name_id"]])
        cls_lit = cxx_str(cname)
        args_exprs = "".join(", " + arg_template_expr(a) for a in e["args"])
        tmpl_name = ("thunks::class_vararg_method_thunk" if e.get("is_vararg")
                     else "thunks::class_method_thunk")
        return "%s<%du, %s, %s, %s, %s%s%s>" % (
            tmpl_name, e["hash"], cls_lit, name_lit,
            cxx_bool(e["is_static"]),
            ret_template_expr(m.pool.strings[e["ret_id"]]), args_exprs, extra)

    by_cls = collections.OrderedDict()
    for e in m.class_methods:
        by_cls.setdefault(e["class_name_id"], []).append(e)


    cls_entries = []
    for cid in sorted(by_cls, key=lambda cid: m.pool.strings[cid]):
        entries = by_cls[cid]
        cname = m.pool.strings[cid]
        ident = "find_cls_" + class_ident(cname)
        cls_entries.append((cxx_str(cname), ident))

        by_hash = collections.OrderedDict()
        for e in entries:
            by_hash.setdefault(e["hash"], []).append(e)

        L.append("ThunkFn %s(const godot::StringName &p_name, uint32_t p_hash) {" % ident)
        L.append("\tswitch (p_hash) {")
        for h, group in by_hash.items():
            if len(group) == 1:
                e = group[0]
                L.append("\tcase %du: return (ThunkFn)&%s;" % (h, class_entry_expr(e, cname)))
            else:
                L.append("\tcase %du: {" % h)
                for e in group:
                    nlit = cxx_str(m.pool.strings[e["name_id"]])
                    L.append('\t\tif (p_name == godot::StringName(%s)) return (ThunkFn)&%s;'
                             % (nlit, class_entry_expr(e, cname)))
                L.append("\t\treturn nullptr;")
                L.append("\t}")
        L.append("\tdefault: return nullptr;")
        L.append("\t}")
        L.append("}")
        L.append("")

    L.append("} // namespace")
    L.append("")
    L.append("const ThunkFn find_class_method_thunk(const godot::StringName &p_class,")
    L.append("        const godot::StringName &p_name, uint32_t p_hash) {")
    L.append("\tstruct Entry { const char *name; ThunkFn (*resolve)(const godot::StringName &, uint32_t); };")
    L.append("\tstatic const Entry k_entries[] = {")
    for lit, ident in cls_entries:
        L.append('\t\t{%s, &%s},' % (lit, ident))
    L.append("\t};")
    L.append("\t// binary search by class name (registration-time only, ~10 compares)")
    L.append("\tint lo = 0, hi = (int)std::size(k_entries) - 1;")
    L.append("\twhile (lo <= hi) {")
    L.append("\t\tconst int mid = lo + (hi - lo) / 2;")
    L.append("\t\tconst int cmp = strcmp(godot::String(p_class).utf8().get_data(), k_entries[mid].name);")
    L.append("\t\tif (cmp == 0) return k_entries[mid].resolve(p_name, p_hash);")
    L.append("\t\tif (cmp < 0) hi = mid - 1; else lo = mid + 1;")
    L.append("\t}")
    L.append("\treturn nullptr;")
    L.append("}")
    L.append("")
    # ---- indexed property accessors (P3) ------------------------------------
    # One template instance per (property side): the constant index cannot live
    # on the shared backing method (one method typically serves many indexes).
    L.append("// ---- indexed property accessors (P3) ----")
    ip_by_cls = collections.OrderedDict()
    for p in m.indexed_props:
        ip_by_cls.setdefault(p["class_name_id"], []).append(p)

    def prop_vt_value(t):
        """json property type -> GDExtensionVariantType value."""
        if t in VARIANT_TYPE_VALUES:
            return VARIANT_TYPE_VALUES[t]
        if t.startswith(("enum::", "bitfield::")):
            return VARIANT_TYPE_VALUES["int"]
        # engine class names (AudioStream...) and compound hints
        # ("Texture2D,-AtlasTexture") behave as Object* everywhere else
        return VARIANT_TYPE_VALUES["Object"]

    def ip_side_expr(p, setter):
        d = p["_sdef"] if setter else p["_gdef"]
        nm = m.pool.strings[p["setter_name_id" if setter else "getter_name_id"]]
        if setter:
            return "%s<%du, %s, %s, %d, %d>" % (
                "thunks::indexed_property_setter_thunk", int(d["hash"]),
                cxx_str(cname), cxx_str(nm),
                p["index"], prop_vt_value(p["prop_type"]))
        return "%s<%du, %s, %s, %d>" % (
            "thunks::indexed_property_getter_thunk", int(d["hash"]),
            cxx_str(cname), cxx_str(nm), p["index"])

    ip_entries = []
    for cid in sorted(ip_by_cls, key=lambda cid: m.pool.strings[cid]):
        cname = m.pool.strings[cid]
        ident = "find_ip_" + class_ident(cname)
        ip_entries.append((cxx_str(cname), ident))
        L.append("namespace {")
        L.append("ThunkFn %s(const godot::StringName &p_name, bool p_setter) {" % ident)
        for p in ip_by_cls[cname if False else cid]:
            nlit = cxx_str(m.pool.strings[p["prop_name_id"]])
            gdef, sdef = p["_gdef"], p["_sdef"]
            branches = []
            if gdef is not None and "hash" in gdef:
                branches.append("\t\tif (!p_setter) return (ThunkFn)&%s;" % ip_side_expr(p, False))
            if sdef is not None and "hash" in sdef:
                branches.append("\t\tif (p_setter) return (ThunkFn)&%s;" % ip_side_expr(p, True))
            if branches:
                L.append('\t\tif (p_name == godot::StringName(%s)) {' % nlit)
                L.extend(branches)
                L.append("\t\t}")
        L.append("\t\treturn nullptr;")
        L.append("\t}")
        L.append("} // namespace")
        L.append("")

    L.append("const ThunkFn find_indexed_property_getter_thunk(const godot::StringName &p_class,")
    L.append("        const godot::StringName &p_name) {")
    L.append("\tstruct Entry { const char *name; ThunkFn (*resolve)(const godot::StringName &, bool); };")
    L.append("\tstatic const Entry k_entries[] = {")
    for lit, ident in ip_entries:
        L.append('\t\t{%s, &%s},' % (lit, ident))
    L.append("\t};")
    L.append("\tint lo = 0, hi = (int)std::size(k_entries) - 1;")
    L.append("\twhile (lo <= hi) {")
    L.append("\t\tconst int mid = lo + (hi - lo) / 2;")
    L.append("\t\tconst int cmp = strcmp(godot::String(p_class).utf8().get_data(), k_entries[mid].name);")
    L.append("\t\tif (cmp == 0) return k_entries[mid].resolve(p_name, false);")
    L.append("\t\tif (cmp < 0) hi = mid - 1; else lo = mid + 1;")
    L.append("\t}")
    L.append("\treturn nullptr;")
    L.append("}")
    L.append("")
    L.append("const ThunkFn find_indexed_property_setter_thunk(const godot::StringName &p_class,")
    L.append("        const godot::StringName &p_name) {")
    L.append("\tstruct Entry { const char *name; ThunkFn (*resolve)(const godot::StringName &, bool); };")
    L.append("\tstatic const Entry k_entries[] = {")
    for lit, ident in ip_entries:
        L.append('\t\t{%s, &%s},' % (lit, ident))
    L.append("\t};")
    L.append("\tint lo = 0, hi = (int)std::size(k_entries) - 1;")
    L.append("\twhile (lo <= hi) {")
    L.append("\t\tconst int mid = lo + (hi - lo) / 2;")
    L.append("\t\tconst int cmp = strcmp(godot::String(p_class).utf8().get_data(), k_entries[mid].name);")
    L.append("\t\tif (cmp == 0) return k_entries[mid].resolve(p_name, true);")
    L.append("\t\tif (cmp < 0) hi = mid - 1; else lo = mid + 1;")
    L.append("\t}")
    L.append("\treturn nullptr;")
    L.append("}")
    L.append("")
    L.append("} // namespace jsb::static_binding")
    L.append("")
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
        "dispatch.gen.cpp": emit_dispatch_cpp(m),
        "dispatch_class.gen.cpp": emit_class_dispatch_cpp(m),
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
