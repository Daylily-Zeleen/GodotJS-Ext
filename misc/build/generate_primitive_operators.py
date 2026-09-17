#!/usr/bin/env python3
"""Generate jsb_primitive_operators.def.gen.h from the godot-cpp
extension_api json (builtin_classes[].operators), replacing the handwritten
jsb_primitive_operators.def.h.

The generated file is consumed by TWO translation units with different macro
definitions (the data file stays macro-driven so both keep working):
- runtime (jsb_primitive_bindings_reflect.cpp): registers the operator
  callbacks onto the JS class objects;
- editor (jsb_editor_utility_funcs.cpp): builds the operator metadata tables
  handed to the JS toolchain (consumes the TReturn/TLeft/TRight triplets).

Macro-classification follows the handwritten file's convention (option B,
minimal change): comparison operators via JSB_DEFINE_COMPARATOR, unary
operators via JSB_DEFINE_UNARY, everything else via
JSB_DEFINE_OVERLOADED_BINARY_BEGIN/END blocks.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from copyright import read_copyright_text, generate_copyright_header_cpp

GENERATED_NOTE = (
    "// GENERATED FILE - DO NOT EDIT.\n"
    "// SCons regenerates this file on every build; manual run:\n"
    "//   python misc/build/generate_primitive_operators.py \\\n"
    "//       --input third/godot-cpp/gdextension/extension_api-4-7.json \\\n"
    "//       --interface third/godot-cpp/gdextension/gdextension_interface.json\n"
)

# json operator name -> (Variant::OP_ tail token, grouping kind).
# Comparisons and binary operators share BEGIN/OVERLOAD/END emission;
# unary operators use JSB_DEFINE_UNARY.
OP_MAP = {
    "==": ("EQUAL", "cmp"),
    "!=": ("NOT_EQUAL", "cmp"),
    "<": ("LESS", "cmp"),
    "<=": ("LESS_EQUAL", "cmp"),
    ">": ("GREATER", "cmp"),
    ">=": ("GREATER_EQUAL", "cmp"),
    "unary-": ("NEGATE", "unary"),
    "unary+": ("POSITIVE", "unary"),
    "not": ("NOT", "unary"),
    "~": ("BIT_NEGATE", "unary"),
    "+": ("ADD", "bin"),
    "-": ("SUBTRACT", "bin"),
    "*": ("MULTIPLY", "bin"),
    "/": ("DIVIDE", "bin"),
    "%": ("MODULE", "bin"),
    "**": ("POWER", "bin"),
    "<<": ("SHIFT_LEFT", "bin"),
    ">>": ("SHIFT_RIGHT", "bin"),
    "&": ("BIT_AND", "bin"),
    "|": ("BIT_OR", "bin"),
    "^": ("BIT_XOR", "bin"),
    "and": ("AND", "bin"),
    "or": ("OR", "bin"),
    "xor": ("XOR", "bin"),
    "in": ("IN", "bin"),
}

# json type name -> C++ token in emitted triplets; other names pass through
# unqualified for the macro consumers.
CPP_TYPE_MAP = {
    "float": "Number",   # runtime consumer defines Number as double around the include
    "int": "int64_t",
    "Nil": "Variant",
    # godot-cpp only provides GetTypeInfo for Wrapped types through the
    # T* specialization (type_info.hpp) -- bare Object has none.
    "Object": "Object *",
}

# Preserve the former handwritten table's order; append other classes in JSON order.
PREFERRED_TYPE_ORDER = [
    "Vector2", "Vector2i", "Vector3", "Vector3i", "Vector4", "Vector4i",
    "Quaternion", "AABB", "Basis", "Plane", "Color", "Transform2D",
    "Transform3D", "Projection", "NodePath", "RID", "Callable", "Signal",
    "Dictionary", "Array", "PackedByteArray", "PackedInt32Array",
    "PackedInt64Array", "PackedFloat32Array", "PackedFloat64Array",
    "PackedStringArray", "PackedVector2Array", "PackedVector3Array",
    "PackedVector4Array", "PackedColorArray",
]


def load_variant_op_enum(interface_path):
    """GDExtensionVariantOperator tail tokens from the interface description."""
    with open(interface_path, encoding="utf-8") as f:
        iface = json.load(f)
    enum_def = next(t for t in iface["types"] if t.get("name") == "GDExtensionVariantOperator")
    return {v["name"].replace("GDEXTENSION_VARIANT_OP_", "") for v in enum_def["values"]}


def cpp_type(json_name):
    return CPP_TYPE_MAP.get(json_name, json_name)


def emit_type_block(class_name, operators, variant_ops):
    L = []
    bins, unaries, cmps = [], [], []
    for op in operators:
        name = op["name"]
        if name not in OP_MAP:
            raise SystemExit(f"FATAL: unmapped operator name '{name}' in {class_name}")
        token, kind = OP_MAP[name]
        if token not in variant_ops:
            raise SystemExit(f"FATAL: operator token {token} is not in GDExtensionVariantOperator")
        if kind == "bin":
            bins.append((token, op))
        elif kind == "unary":
            unaries.append((token, op))
        else:
            cmps.append((token, op))

    left_cpp = cpp_type(class_name)

    # Group comparisons and binary overloads by operator for one JS method each.
    bin_groups = {}
    for token, op in bins:
        bin_groups.setdefault(token, []).append(op)
    for token, op in cmps:
        bin_groups.setdefault(token, []).append(op)

    # Static pair resolvers referenced by the BEGIN macro's token paste live in
    # dispatch_builtin.gen.cpp; this file emits the shared operator surface.
    L.append(f"JSB_TYPE_BEGIN({left_cpp})")
    # One BEGIN/END block registers one method, even with multiple right types.
    for token, entries in bin_groups.items():
        L.append(f"    JSB_DEFINE_OVERLOADED_BINARY_BEGIN({class_name}, {token})")
        for op in entries:
            ret = cpp_type(op["return_type"])
            right = cpp_type(op.get("right_type", "Variant"))
            L.append(f"        JSB_DEFINE_BINARY_OVERLOAD({ret}, {left_cpp}, {right})")
        L.append("    JSB_DEFINE_OVERLOADED_BINARY_END()")
    seen_unary = set()
    for token, op in unaries:
        if token in seen_unary:
            continue
        seen_unary.add(token)
        ret = cpp_type(op["return_type"])
        L.append(f"    JSB_DEFINE_UNARY({token}, {ret})")

    L.append("JSB_TYPE_END()")
    return L
def generate(api_path, interface_path, preferred_only=False):
    with open(api_path, encoding="utf-8") as f:
        data = json.load(f)

    variant_ops = load_variant_op_enum(interface_path)

    by_class = {}
    for b in data["builtin_classes"]:
        ops = b.get("operators")
        if ops:
            by_class[b["name"]] = ops

    # stable order: preferred (handwritten) order first, then the rest in json order
    ordered = [t for t in PREFERRED_TYPE_ORDER if t in by_class]
    if not preferred_only:
        ordered += [t for t in by_class if t not in PREFERRED_TYPE_ORDER]

    L = [GENERATED_NOTE.rstrip("\n"),
         "",
         generate_copyright_header_cpp("jsb_primitive_operators.def.gen.h", read_copyright_text()).rstrip("\n"),
         "",
         "// Operators for ALL builtin classes that define them in the api json.",
         "// NOTE: classes outside the primitive-binding registration list (bool,",
         "// float, int, Nil, StringName...) still get their OperatorRegister<>",
         "",
         "// specializations emitted so the table stays complete; their generate()",
         "// is simply never invoked today.",
         "",
         "// Per-(left, op) thunk tables: one switch per (left type, operator) pair;",
         "// operator_dispatch_binary calls the matching one with the probed right",
         "// operand type -- no global binary search at call time.",
         "",
    ]

    for class_name in ordered:
        if class_name == "Nil":
            # Nil has no JS class object or VariantInternalType<Variant>;
            # emitting its block would instantiate unsupported internal access.
            continue
        if class_name in ("bool", "int", "float", "StringName"):
            # These JS-native types have no operator static-method surface.
            # String is not in PREFERRED_TYPE_ORDER, but is emitted unless
            # --preferred-only is selected.
            continue
        L.extend(emit_type_block(class_name, by_class[class_name], variant_ops))
        L.append("")

    return "\n".join(L) + "\n", ordered, by_class


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="extension_api json")
    parser.add_argument("--interface", required=True, help="gdextension_interface json")
    parser.add_argument("--out", required=True, help="output .def.gen.h path")
    parser.add_argument("--summary", action="store_true", help="print per-class operator counts")
    parser.add_argument("--preferred-only", action="store_true",
                        help="debug: emit only the classes present in the handwritten table")
    ns = parser.parse_args()

    content, ordered, by_class = generate(ns.input, ns.interface, ns.preferred_only)

    if ns.summary:
        for class_name in ordered:
            ops = by_class[class_name]
            print(f"{class_name}: {len(ops)} operators")

    out_path = Path(ns.out)
    new = content.encode("utf-8")
    old = out_path.read_bytes() if out_path.exists() else None
    if old != new:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(new)
        print(f"wrote {out_path} ({len(new)} bytes)")
    else:
        print(f"no diff: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
