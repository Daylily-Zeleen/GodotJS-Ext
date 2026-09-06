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

# api json operator symbol -> the runtime JS method name (JSB_OPERATOR_NAME
# stringifies the bare def token in jsb_primitive_operators.def.gen.h, e.g.
# ADD -- no OP_ prefix)
OP_TOKEN = {
    "==": "EQUAL", "!=": "NOT_EQUAL", "<": "LESS", "<=": "LESS_EQUAL",
    ">": "GREATER", ">=": "GREATER_EQUAL", "+": "ADD", "-": "SUBTRACT",
    "*": "MULTIPLY", "/": "DIVIDE", "%": "MODULE", "**": "POWER",
    "not": "NOT", "and": "AND", "or": "OR", "xor": "XOR", "in": "IN",
    "~": "BIT_NEGATE", "<<": "SHIFT_LEFT", ">>": "SHIFT_RIGHT",
    "&": "BIT_AND", "|": "BIT_OR", "^": "BIT_XOR",
}

# Operator case sampling per class: which (operator symbol, right type)
# overloads to emit. Covers the struct-arg dispatch paths whose ptrcall
# stack-slot overflow (a5f0db9) was first observed on these types, plus a
# scalar-arg counterpart (int/float have no bound operator surface of their
# own; scalar * struct exercises the same OP dispatch with a different
# argument encoding).
OP_CASE_PICKS = {
    "Vector2": [("==", None), ("+", None), ("*", "Vector2"), ("*", "int")],
    "Vector2i": [("<", "Vector2i")],
    "Vector3": [("+", None), ("*", "Vector3")],
    "Transform2D": [("*", "Transform2D"), ("*", "Vector2")],
    "Quaternion": [("*", "Quaternion")],
    "AABB": [("*", "Transform3D")],
    "Plane": [("*", "Transform3D")],
    "Basis": [("*", "Basis"), ("*", "Vector3"), ("*", "float"), ("==", "Basis")],
    "Transform3D": [("*", "Transform3D"), ("*", "Vector3"), ("*", "Plane")],
    "Projection": [("*", "Projection"), ("*", "Vector4")],
    "Color": [("+", None), ("*", "Color")],
}


def exposed(json_name):
    return EXPOSED_NAME.get(json_name, json_name)


# json argument type -> TS expression producing that value.
FACTORY = {
    "bool": "true",
    "int": "0",   # 0 keeps index-ish arguments in range
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

# Explicit makeTarget overrides. pick_ctor() prefers the constructor with the
# most arguments and arg_factory fills scalars with 1.5 / Vector3(1,2,3),
# which is NOT a valid instance for a few types:
#   Quaternion(x,y,z,w) with all-1.5 is unnormalized -> get_euler() prints
#     an engine ERROR per call (and poisons the measurement with log I/O);
#   Basis(Vector3(1,2,3), Vector3(1,2,3), Vector3(1,2,3)) has linearly
#     dependent columns -> det == 0 -> inverse() prints errors.
# The bench probe only filters cases that THROW; engine ERR_FAIL prints are
# not throws, so these must be overridden at the source.
FACTORY.update({
    "Vector2": "new Vector2(1.5, 2.5)",
    "Vector2i": "new Vector2i(1, 2)",
    # unit-length +0z: Vector3-typed parameters include axis arguments
    # (Basis.rotated / Transform3D.rotated) that the engine requires to be
    # normalized; (1,2,3) would print an engine ERROR per timed call and
    # poison the measurement with log I/O
    "Vector3": "new Vector3(0, 0, 1)",
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

# makeTarget overrides (see rationale above)
CTOR_OVERRIDE = {
    "Quaternion": "new Quaternion(0, 0, 0, 1)",
    "Basis": "new Basis()",
    "Transform3D": "new Transform3D()",
    "Transform2D": "new Transform2D()",
    # pick_ctor() prefers Array's copy constructor (from: Array) and
    # arg_factory fills it with an empty array, so the Array.get(0) case ran
    # out-of-bounds and printed an engine ERROR on every call (the probe only
    # filters cases that throw; ERR_FAIL prints are not throws). Give the
    # target one element instead.
    "Array": "(() => { const a = new GArray(); a.append(1); return a; })()",
}

# Per-case argument overrides. The generic int factory emits 0, which the
# engine rejects for Callable.unbind (argcount must be >= 1, ERR_FAIL_COND at
# core/variant/callable.cpp) -- same probe-blindness as above; override here.
METHOD_ARG_OVERRIDE = {
    ("Callable", "unbind"): ["1"],
}


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
        if name in CTOR_OVERRIDE:
            ctor_expr = CTOR_OVERRIDE[name]
        else:
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
            override = METHOD_ARG_OVERRIDE.get((name, m["name"]))
            if override is not None:
                args = ", ".join(override)
            else:
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

    # Operators group: per-class op overloads drawn from the api json.
    # Operators are class-level statics (t.OP-name(left, right)), so they
    # share one synthetic target carrying one instance of every operand
    # type; `--only=Operators` filters via the group name like any other.
    op_total = 0
    op_picks = []
    for name, picks in OP_CASE_PICKS.items():
        cls = classes.get(name)
        if not cls:
            continue
        ops = cls.get("operators", [])
        for sym, right_filter in picks:
            token = OP_TOKEN.get(sym)
            if not token:
                continue
            for op in ops:
                if op["name"] != sym:
                    continue
                rt = op.get("right_type")
                if right_filter is None:
                    # first overload of a symbol that has exactly one
                    # interesting form (equality etc.); "Variant" right type
                    # is the untyped fallback (left-operand-only dispatch)
                    # -- skip it when a concrete sibling exists
                    if rt == "Variant" and any(o["name"] == sym and o.get("right_type") != "Variant" for o in ops):
                        continue
                elif rt != right_filter:
                    continue
                # struct right operand -> reference the shared target field
                # (bare class names have no arg_factory entry; null would
                # print an engine ERROR on every call and poison the timing)
                if rt in OP_CASE_PICKS:
                    right_expr = f"t.{rt.lower()}"
                else:
                    right_expr = arg_factory({"type": rt}) if rt else ""
                label = f"{name}.{token}" + (f"({rt})" if rt else "(unary)")
                if rt:
                    fn = f'(t: any) => {exposed(name)}.{token}(t.lhs, {right_expr})'
                else:
                    fn = f'(t: any) => {exposed(name)}.{token}(t.lhs)'
                op_picks.append((label, fn))
                break
    if op_picks:
        op_target_types = sorted({n for n, _ in OP_CASE_PICKS.items()})
        # one lhs instance per participating class, keyed by json name
        target_parts = ", ".join(f"{n.lower()}: new {exposed(n)}()" for n in op_target_types)
        L.append("    // ---- Operators ----")
        L.append("    {")
        L.append('        group: "Operators",')
        L.append(f"        makeTarget: () => ({{ {target_parts} }}),")
        L.append("        cases: [")
        for label, fn in op_picks:
            op_total += 1
            fn2 = fn.replace("t.lhs", "t." + label.split(".")[0].lower())
            L.append(f'            {{ name: "{label}", fn: {fn2} }},')
        L.append("        ],")
        L.append("    },")
    total += op_total
    L.append(f"// {op_total} operator cases (Operators group)")
    L.append("];")
    L.append(f"// {total} method/member cases over {len(BOUND_CLASSES)} classes")
    L.append("")

    Path(out_path).write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({total} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
