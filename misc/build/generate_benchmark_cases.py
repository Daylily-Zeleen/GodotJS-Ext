#!/usr/bin/env python3
"""Generate project/tests/benchmark/cases.builtin.ts from the godot-cpp
extension_api json.

Sampling strategy (per builtin class in the primitive-binding registration
list -- only those get JS class objects):
  - one constructor with the most arguments (falls back to the default ctor)
  - up to 3 instance methods sampled by parameter shape:
      * a 0-argument method
      * a method taking scalar arguments (int/float/bool/String)
      * a method taking at least one struct argument (Vector2, Color, ...)
  - one read/write member (first writable one), if any

Static methods and vararg methods are skipped (static calls have a different
shape; vararg tails never convert JS arrays -- both are covered by the
handwritten object cases instead). Methods that fail against a default
instance are filtered at runtime by the bench harness probe.
"""

import json
import sys
from pathlib import Path

# Classes that actually receive a JS class object (jsb_primitive_types.def.h
# + String's separate registration). Everything else in the api json has no
# runtime surface here.
BOUND_CLASSES = [
    "Vector2", "Vector2i", "Rect2", "Rect2i", "Vector3", "Vector3i",
    "Transform2D", "Vector4", "Vector4i", "Plane", "Quaternion", "AABB",
    "Basis", "Transform3D", "Projection", "Color", "NodePath", "RID",
    "Callable", "Signal", "Dictionary", "Array", "PackedByteArray",
    "PackedInt32Array", "PackedInt64Array", "PackedFloat32Array",
    "PackedFloat64Array", "PackedStringArray", "PackedVector2Array",
    "PackedVector3Array", "PackedVector4Array", "PackedColorArray",
]

# classes whose exposed JS name differs from the json name (jsb renames
# Array/Dictionary to avoid colliding with the JS built-ins)
EXPOSED_NAME = {"Array": "GArray", "Dictionary": "GDictionary"}


def exposed(json_name):
    return EXPOSED_NAME.get(json_name, json_name)


# json argument type -> TS expression producing that value.
FACTORY = {
    "bool": "true",
    "int": "3",
    "float": "1.5",
    "String": '"abc"',
    "StringName": '"abc"',   # godot.StringName is not exported; typed calls accept JS strings
    "NodePath": 'new NodePath("abc")',
    "RID": "new RID()",
    "Callable": "new Callable()",
    "Signal": "new Signal()",
    "Array": "new GArray()",
    "Dictionary": "new GDictionary()",
    "Variant": "1.5",
    "Object": "null",
}
# NOTE: non-zero values -- zero vectors/planes trip engine assertions
# (e.g. Basis::set_axis_angle requires a normalized axis) and would turn the
# probe/timed calls into thousands of engine ERROR prints.
FACTORY.update({
    "Vector2": "new Vector2(1.5, 2.5)",
    "Vector2i": "new Vector2i(1, 2)",
    "Vector3": "new Vector3(1, 2, 3)",
    "Vector3i": "new Vector3i(1, 2, 3)",
    "Vector4": "new Vector4(1, 2, 3, 4)",
    "Vector4i": "new Vector4i(1, 2, 3, 4)",
    "Quaternion": "new Quaternion(0, 0, 0, 1)",      # identity
    "Color": "new Color(0.2, 0.4, 0.6, 0.8)",
    "Rect2": "new Rect2(0, 0, 10, 10)",
    "Rect2i": "new Rect2i(0, 0, 10, 10)",
    "AABB": "new AABB(new Vector3(0, 0, 0), new Vector3(1, 1, 1))",
    "Plane": "new Plane(0, 0, 1, 0)",                 # normalized normal
    # Basis() / Transform3D() / Projection() are valid identity defaults
})


def arg_factory(a):
    t = a["type"]
    if t.startswith("enum::") or t.startswith("bitfield::"):
        return "0"
    if t.startswith("typedarray::"):
        return "new Array()"
    return FACTORY.get(t, "null")


def is_scalar(t):
    return t in ("bool", "int", "float", "String")


def is_struct(t):
    return (t in FACTORY and t not in
            ("bool", "int", "float", "String", "StringName", "NodePath",
             "RID", "Callable", "Signal", "Array", "Dictionary", "Variant",
             "Object"))


# argument types whose factory value cannot be reliably constructed through
# the binding constructors (Packed* ctor is unavailable, null Object fails
# constructor matching) -- candidates taking them are skipped entirely
UNSAFE_ARG_TYPES = {
    "Object", "Variant", "PackedByteArray", "PackedInt32Array",
    "PackedInt64Array", "PackedFloat32Array", "PackedFloat64Array",
    "PackedStringArray", "PackedVector2Array", "PackedVector3Array",
    "PackedColorArray", "PackedVector4Array",
}

# classes whose constructors are unavailable through the binding layer
# ("no suitable constructor" regardless of json data) -- skipped entirely
UNSAFE_CLASSES = {
    "PackedByteArray", "PackedInt32Array", "PackedInt64Array",
    "PackedFloat32Array", "PackedFloat64Array", "PackedStringArray",
    "PackedVector2Array", "PackedVector3Array", "PackedVector4Array",
    "PackedColorArray",
}


def args_safe(args):
    return all(a["type"] not in UNSAFE_ARG_TYPES for a in args)


def sample_methods(cls):
    methods = [m for m in cls.get("methods", [])
               if not m.get("is_virtual") and "hash" in m
               and not m.get("is_static") and not m.get("is_vararg")
               and not any(a.get("type", "").startswith("typedarray::")
                           for a in m.get("arguments", []))
               and args_safe(m.get("arguments", []))]
    picked, used_names = [], set()

    def pick(pred):
        for m in methods:
            if m["name"] in used_names:
                continue
            if pred(m.get("arguments", [])):
                used_names.add(m["name"])
                picked.append(m)
                return True
        return False

    pick(lambda args: len(args) == 0)
    pick(lambda args: len(args) > 0 and all(is_scalar(a["type"]) for a in args))
    pick(lambda args: any(is_struct(a["type"]) for a in args))
    return picked


def pick_member(cls):
    for mem in cls.get("members", []):
        if "setter" in mem:
            return mem
    return None


def pick_ctor(cls):
    ctors = cls.get("constructors", [])
    best, best_n = None, -1
    for c in ctors:
        args = c.get("arguments", [])
        if any(a["type"].startswith(("typedarray::", "enum::", "bitfield::"))
               for a in args):
            continue
        if not args_safe(args):
            continue
        if len(args) > best_n and len(args) <= 6:
            best, best_n = c, len(args)
    return best


def main():
    if len(sys.argv) != 3:
        print("usage: generate_benchmark_cases.py <extension_api.json> <out.ts>", file=sys.stderr)
        return 1
    api_path, out_path = sys.argv[1], sys.argv[2]
    with open(api_path, encoding="utf-8") as f:
        data = json.load(f)

    classes = {b["name"]: b for b in data["builtin_classes"]}

    L = ["/**",
         " * GENERATED by misc/build/generate_benchmark_cases.py -- do not edit",
         " * by hand; regenerate from the extension_api json when it changes.",
         " *",
         " * One case = (target object factory, call lambda). The harness probes",
         " * each case first; cases that throw against the default instance are",
         " * reported invalid and excluded from the timed run.",
         " */",
         'import { BuiltinCase } from "./bench";',
         "import { " + ", ".join(exposed(t) for t in BOUND_CLASSES) + ' } from "godot";',
         "",
         "export const BUILTIN_CASES: BuiltinCase[] = ["]
    total = 0
    for name in BOUND_CLASSES:
        cls = classes.get(name)
        if not cls:
            continue
        if name in UNSAFE_CLASSES:
            continue
        ctor = pick_ctor(cls)
        ctor_args = [arg_factory(a) for a in (ctor.get("arguments", []) if ctor else [])]
        ctor_expr = f"new {exposed(name)}({', '.join(ctor_args)})" if ctor else f"new {exposed(name)}()"
        L.append(f"    // ---- {name} ----")
        L.append("    {")
        L.append(f'        group: "{name}",')
        L.append(f"        makeTarget: () => {ctor_expr},")
        L.append("        cases: [")
        cases = []
        for m in sample_methods(cls):
            args = ", ".join(arg_factory(a) for a in m.get("arguments", []))
            cases.append((f"{m['name']}({len(m.get('arguments', []))})",
                          f'(t: any) => t["{m["name"]}"]({args})'))
        mem = pick_member(cls)
        if mem:
            mem_val = arg_factory({"type": mem["type"]})
            cases.append((f"get {mem['name']}", '(t: any) => t["%s"]' % mem["name"]))
            cases.append((f"set {mem['name']}", f'(t: any) => {{ t["{mem["name"]}"] = {mem_val}; }}'))
        for case_name, fn in cases:
            total += 1
            L.append(f'            {{ name: "{case_name}", fn: {fn} }},')
        L.append("        ],")
        L.append("    },")
    L.append("];")
    L.append(f"// {total} method/member cases over {len(BOUND_CLASSES)} classes")

    Path(out_path).write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({total} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
