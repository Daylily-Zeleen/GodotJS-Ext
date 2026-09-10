// uid://dtsfbiuf60r16 This line is generated, don't modify or remove it.
/**
 * Builtin-type benchmark cases -- hand-maintained. The former generator
 * misc/build/generate_benchmark_cases.py was removed; extend this file
 * directly when adding cases.
 *
 * One case = (target object factory, call lambda). The harness probes
 * each case first; cases that throw against the default instance are
 * reported invalid and excluded from the timed run.
 *
 * Operator cases call the JS-side static methods `OP_XXX` (naming unified
 * with the editor typings; see JSB_OPERATOR_NAME in src/internal/jsb_macros.h).
 */
import { CaseGroup } from "./benchmark";
import {
    Vector2,
    Vector2i,
    Rect2,
    Rect2i,
    Vector3,
    Vector3i,
    Transform2D,
    Vector4,
    Vector4i,
    Plane,
    Quaternion,
    AABB,
    Basis,
    Transform3D,
    Projection,
    Color,
    NodePath,
    RID,
    Callable,
    Signal,
    GDictionary,
    GArray,
    PackedByteArray,
    PackedInt32Array,
    PackedInt64Array,
    PackedFloat32Array,
    PackedFloat64Array,
    PackedStringArray,
    PackedVector2Array,
    PackedVector3Array,
    PackedVector4Array,
    PackedColorArray,
    Variant,
} from "godot";

export const BUILTIN_CASES: CaseGroup[] = [
    // ---- Vector2 ----
    {
        group: "Vector2",
        makeTarget: () => new Vector2(1.5, 1.5),
        cases: [
            { name: "angle(0)", fn: (t: any) => t["angle"]() },
            { name: "limit_length(1)", fn: (t: any) => t["limit_length"](1.5) },
            { name: "angle_to(1)", fn: (t: any) => t["angle_to"](new Vector2(1.5, 2.5)) },
        ],
    },
    // ---- Vector2i ----
    {
        group: "Vector2i",
        makeTarget: () => new Vector2i(0, 0),
        cases: [
            { name: "aspect(0)", fn: (t: any) => t["aspect"]() },
            { name: "clampi(2)", fn: (t: any) => t["clampi"](0, 0) },
            { name: "distance_to(1)", fn: (t: any) => t["distance_to"](new Vector2i(1, 2)) },
        ],
    },
    // ---- Rect2 ----
    {
        group: "Rect2",
        makeTarget: () => new Rect2(1.5, 1.5, 1.5, 1.5),
        cases: [
            { name: "get_center(0)", fn: (t: any) => t["get_center"]() },
            { name: "grow(1)", fn: (t: any) => t["grow"](1.5) },
            { name: "has_point(1)", fn: (t: any) => t["has_point"](new Vector2(1.5, 2.5)) },
        ],
    },
    // ---- Rect2i ----
    {
        group: "Rect2i",
        makeTarget: () => new Rect2i(0, 0, 0, 0),
        cases: [
            { name: "get_center(0)", fn: (t: any) => t["get_center"]() },
            { name: "grow(1)", fn: (t: any) => t["grow"](0) },
            { name: "has_point(1)", fn: (t: any) => t["has_point"](new Vector2i(1, 2)) },
        ],
    },
    // ---- Vector3 ----
    {
        group: "Vector3",
        makeTarget: () => new Vector3(1.5, 1.5, 1.5),
        cases: [
            { name: "min_axis_index(0)", fn: (t: any) => t["min_axis_index"]() },
            { name: "limit_length(1)", fn: (t: any) => t["limit_length"](1.5) },
            { name: "angle_to(1)", fn: (t: any) => t["angle_to"](new Vector3(0, 0, 1)) },
        ],
    },
    // ---- Vector3i ----
    {
        group: "Vector3i",
        makeTarget: () => new Vector3i(0, 0, 0),
        cases: [
            { name: "min_axis_index(0)", fn: (t: any) => t["min_axis_index"]() },
            { name: "clampi(2)", fn: (t: any) => t["clampi"](0, 0) },
            { name: "distance_to(1)", fn: (t: any) => t["distance_to"](new Vector3i(1, 2, 3)) },
        ],
    },
    // ---- Transform2D ----
    {
        group: "Transform2D",
        makeTarget: () => new Transform2D(),
        cases: [
            { name: "inverse(0)", fn: (t: any) => t["inverse"]() },
            { name: "rotated(1)", fn: (t: any) => t["rotated"](1.5) },
            { name: "scaled(1)", fn: (t: any) => t["scaled"](new Vector2(1.5, 2.5)) },
        ],
    },
    // ---- Vector4 ----
    {
        group: "Vector4",
        makeTarget: () => new Vector4(1.5, 1.5, 1.5, 1.5),
        cases: [
            { name: "min_axis_index(0)", fn: (t: any) => t["min_axis_index"]() },
            { name: "posmod(1)", fn: (t: any) => t["posmod"](1.5) },
            { name: "lerp(2)", fn: (t: any) => t["lerp"](new Vector4(1, 2, 3, 4), 1.5) },
        ],
    },
    // ---- Vector4i ----
    {
        group: "Vector4i",
        makeTarget: () => new Vector4i(0, 0, 0, 0),
        cases: [
            { name: "min_axis_index(0)", fn: (t: any) => t["min_axis_index"]() },
            { name: "clampi(2)", fn: (t: any) => t["clampi"](0, 0) },
            { name: "clamp(2)", fn: (t: any) => t["clamp"](new Vector4i(1, 2, 3, 4), new Vector4i(1, 2, 3, 4)) },
        ],
    },
    // ---- Plane ----
    {
        group: "Plane",
        makeTarget: () => new Plane(1.5, 1.5, 1.5, 1.5),
        cases: [
            { name: "normalized(0)", fn: (t: any) => t["normalized"]() },
            { name: "is_equal_approx(1)", fn: (t: any) => t["is_equal_approx"](new Plane(0, 0, 1, 0)) },
        ],
    },
    // ---- Quaternion ----
    {
        group: "Quaternion",
        makeTarget: () => new Quaternion(0, 0, 0, 1),
        cases: [
            { name: "length(0)", fn: (t: any) => t["length"]() },
            { name: "get_euler(1)", fn: (t: any) => t["get_euler"](0) },
            { name: "is_equal_approx(1)", fn: (t: any) => t["is_equal_approx"](new Quaternion(0, 0, 0, 1)) },
        ],
    },
    // ---- AABB ----
    {
        group: "AABB",
        makeTarget: () => new AABB(new Vector3(0, 0, 1), new Vector3(0, 0, 1)),
        cases: [
            { name: "abs(0)", fn: (t: any) => t["abs"]() },
            { name: "grow(1)", fn: (t: any) => t["grow"](1.5) },
            { name: "has_point(1)", fn: (t: any) => t["has_point"](new Vector3(0, 0, 1)) },
        ],
    },
    // ---- Basis ----
    {
        group: "Basis",
        makeTarget: () => new Basis(),
        cases: [
            { name: "inverse(0)", fn: (t: any) => t["inverse"]() },
            { name: "get_euler(1)", fn: (t: any) => t["get_euler"](0) },
            { name: "rotated(2)", fn: (t: any) => t["rotated"](new Vector3(0, 0, 1), 1.5) },
        ],
    },
    // ---- Transform3D ----
    {
        group: "Transform3D",
        makeTarget: () => new Transform3D(),
        cases: [
            { name: "inverse(0)", fn: (t: any) => t["inverse"]() },
            { name: "rotated(2)", fn: (t: any) => t["rotated"](new Vector3(0, 0, 1), 1.5) },
        ],
    },
    // ---- Projection ----
    {
        group: "Projection",
        makeTarget: () =>
            new Projection(
                new Vector4(1, 2, 3, 4),
                new Vector4(1, 2, 3, 4),
                new Vector4(1, 2, 3, 4),
                new Vector4(1, 2, 3, 4),
            ),
        cases: [
            { name: "determinant(0)", fn: (t: any) => t["determinant"]() },
            { name: "perspective_znear_adjusted(1)", fn: (t: any) => t["perspective_znear_adjusted"](1.5) },
            { name: "jitter_offseted(1)", fn: (t: any) => t["jitter_offseted"](new Vector2(1.5, 2.5)) },
        ],
    },
    // ---- Color ----
    {
        group: "Color",
        makeTarget: () => new Color(1.5, 1.5, 1.5, 1.5),
        cases: [
            { name: "to_argb32(0)", fn: (t: any) => t["to_argb32"]() },
            { name: "to_html(1)", fn: (t: any) => t["to_html"](true) },
            {
                name: "clamp(2)",
                fn: (t: any) => t["clamp"](new Color(0.2, 0.4, 0.6, 0.8), new Color(0.2, 0.4, 0.6, 0.8)),
            },
        ],
    },
    // ---- NodePath ----
    {
        group: "NodePath",
        makeTarget: () => new NodePath(new NodePath("abc")),
        cases: [
            { name: "is_absolute(0)", fn: (t: any) => t["is_absolute"]() },
            { name: "get_name(1)", fn: (t: any) => t["get_name"](0) },
        ],
    },
    // ---- RID ----
    {
        group: "RID",
        makeTarget: () => new RID(new RID()),
        cases: [{ name: "is_valid(0)", fn: (t: any) => t["is_valid"]() }],
    },
    // ---- Callable ----
    {
        group: "Callable",
        makeTarget: () => new Callable(new Callable()),
        cases: [
            { name: "is_null(0)", fn: (t: any) => t["is_null"]() },
            { name: "unbind(1)", fn: (t: any) => t["unbind"](1) },
        ],
    },
    // ---- Signal ----
    {
        group: "Signal",
        makeTarget: () => new Signal(new Signal()),
        cases: [{ name: "is_null(0)", fn: (t: any) => t["is_null"]() }],
    },
    // ---- Dictionary ----
    {
        group: "Dictionary",
        makeTarget: () => new GDictionary(new GDictionary()),
        cases: [
            { name: "size(0)", fn: (t: any) => t["size"]() },
            { name: "duplicate(1)", fn: (t: any) => t["duplicate"](true) },
        ],
    },
    // ---- Array ----
    {
        group: "Array",
        makeTarget: () =>
            (() => {
                const a = new GArray();
                a.append(1);
                return a;
            })(),
        cases: [
            { name: "size(0)", fn: (t: any) => t["size"]() },
            { name: "get(1)", fn: (t: any) => t["get"](0) },
        ],
    },
    // ---- Operators ----
    {
        group: "Operators",
        makeTarget: () => ({
            aabb: new AABB(),
            basis: new Basis(),
            color: new Color(),
            plane: new Plane(),
            projection: new Projection(),
            quaternion: new Quaternion(),
            transform2d: new Transform2D(),
            transform3d: new Transform3D(),
            vector2: new Vector2(),
            vector2i: new Vector2i(),
            vector3: new Vector3(),
        }),
        cases: [
            { name: "Vector2.OP_EQUAL(Vector2)", fn: (t: any) => Vector2.OP_EQUAL(t.vector2, t.vector2) },
            { name: "Vector2.OP_ADD(Vector2)", fn: (t: any) => Vector2.OP_ADD(t.vector2, t.vector2) },
            { name: "Vector2.OP_MULTIPLY(Vector2)", fn: (t: any) => Vector2.OP_MULTIPLY(t.vector2, t.vector2) },
            { name: "Vector2.OP_MULTIPLY(int)", fn: (t: any) => Vector2.OP_MULTIPLY(t.vector2, 0) },
            { name: "Vector2i.OP_LESS(Vector2i)", fn: (t: any) => Vector2i.OP_LESS(t.vector2i, t.vector2i) },
            { name: "Vector3.OP_ADD(Vector3)", fn: (t: any) => Vector3.OP_ADD(t.vector3, t.vector3) },
            { name: "Vector3.OP_MULTIPLY(Vector3)", fn: (t: any) => Vector3.OP_MULTIPLY(t.vector3, t.vector3) },
            {
                name: "Transform2D.OP_MULTIPLY(Transform2D)",
                fn: (t: any) => Transform2D.OP_MULTIPLY(t.transform2d, t.transform2d),
            },
            {
                name: "Transform2D.OP_MULTIPLY(Vector2)",
                fn: (t: any) => Transform2D.OP_MULTIPLY(t.transform2d, t.vector2),
            },
            {
                name: "Quaternion.OP_MULTIPLY(Quaternion)",
                fn: (t: any) => Quaternion.OP_MULTIPLY(t.quaternion, t.quaternion),
            },
            { name: "AABB.OP_MULTIPLY(Transform3D)", fn: (t: any) => AABB.OP_MULTIPLY(t.aabb, t.transform3d) },
            { name: "Plane.OP_MULTIPLY(Transform3D)", fn: (t: any) => Plane.OP_MULTIPLY(t.plane, t.transform3d) },
            { name: "Basis.OP_MULTIPLY(Basis)", fn: (t: any) => Basis.OP_MULTIPLY(t.basis, t.basis) },
            { name: "Basis.OP_MULTIPLY(Vector3)", fn: (t: any) => Basis.OP_MULTIPLY(t.basis, t.vector3) },
            { name: "Basis.OP_MULTIPLY(float)", fn: (t: any) => Basis.OP_MULTIPLY(t.basis, 1.5) },
            { name: "Basis.OP_EQUAL(Basis)", fn: (t: any) => Basis.OP_EQUAL(t.basis, t.basis) },
            {
                name: "Transform3D.OP_MULTIPLY(Transform3D)",
                fn: (t: any) => Transform3D.OP_MULTIPLY(t.transform3d, t.transform3d),
            },
            {
                name: "Transform3D.OP_MULTIPLY(Vector3)",
                fn: (t: any) => Transform3D.OP_MULTIPLY(t.transform3d, t.vector3),
            },
            { name: "Transform3D.OP_MULTIPLY(Plane)", fn: (t: any) => Transform3D.OP_MULTIPLY(t.transform3d, t.plane) },
            {
                name: "Projection.OP_MULTIPLY(Projection)",
                fn: (t: any) => Projection.OP_MULTIPLY(t.projection, t.projection),
            },
            {
                name: "Projection.OP_MULTIPLY(Vector4)",
                fn: (t: any) => Projection.OP_MULTIPLY(t.projection, new Vector4(1, 2, 3, 4)),
            },
            { name: "Color.OP_ADD(Color)", fn: (t: any) => Color.OP_ADD(t.color, t.color) },
            { name: "Color.OP_MULTIPLY(Color)", fn: (t: any) => Color.OP_MULTIPLY(t.color, t.color) },
        ],
    },
    // 23 operator cases (Operators group)
    // ---- FixArity/Rect2 ----
    {
        group: "FixArity",
        makeTarget: () => new Rect2(1.5, 1.5, 1.5, 1.5),
        cases: [{ name: "Rect2.grow_individual(4)", fn: (t: any) => t["grow_individual"](1.5, 1.5, 1.5, 1.5) }],
    },
    // ---- FixArity/Rect2i ----
    {
        group: "FixArity",
        makeTarget: () => new Rect2i(0, 0, 0, 0),
        cases: [{ name: "Rect2i.grow_individual(4)", fn: (t: any) => t["grow_individual"](0, 0, 0, 0) }],
    },
    // ---- FixArity/Vector2 ----
    {
        group: "FixArity",
        makeTarget: () => new Vector2(1.5, 1.5),
        cases: [
            {
                name: "Vector2.bezier_derivative(4)",
                fn: (t: any) =>
                    t["bezier_derivative"](new Vector2(1.5, 2.5), new Vector2(1.5, 2.5), new Vector2(1.5, 2.5), 1.5),
            },
        ],
    },
    // ---- FixArity/Vector3 ----
    {
        group: "FixArity",
        makeTarget: () => new Vector3(1.5, 1.5, 1.5),
        cases: [
            {
                name: "Vector3.bezier_derivative(4)",
                fn: (t: any) =>
                    t["bezier_derivative"](new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1), 1.5),
            },
        ],
    },
    // ---- FixArity/Transform3D ----
    {
        group: "FixArity",
        makeTarget: () =>
            new Transform3D(new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1)),
        cases: [
            {
                name: "Transform3D.looking_at(3)",
                fn: (t: any) => t["looking_at"](new Vector3(0, 0, -3), new Vector3(0, 1, 0), false),
            },
        ],
    },
    // ---- FixArity/Array ----
    {
        group: "FixArity",
        makeTarget: () => new GArray(new GArray()),
        cases: [{ name: "Array.slice(4)", fn: (t: any) => t["slice"](0, 4, 2, false) }],
    },
    // 6 fixed-arity cases (FixArity groups)
    // ---- Constructors/Vector2 ----
    {
        group: "Constructors",
        makeTarget: () => new Vector2(1.5, 2.5),
        cases: [
            { name: "new Vector2(0 args)", fn: () => new Vector2() },
            { name: "new Vector2(Vector2)", fn: () => new Vector2(new Vector2(1.5, 2.5)) },
            { name: "new Vector2(Vector2i)", fn: () => new Vector2(new Vector2i(1, 2)) },
            { name: "new Vector2(float,float)", fn: () => new Vector2(1.5, 1.5) },
        ],
    },
    // ---- Constructors/Vector2i ----
    {
        group: "Constructors",
        makeTarget: () => new Vector2i(1, 2),
        cases: [
            { name: "new Vector2i(0 args)", fn: () => new Vector2i() },
            { name: "new Vector2i(Vector2i)", fn: () => new Vector2i(new Vector2i(1, 2)) },
            { name: "new Vector2i(Vector2)", fn: () => new Vector2i(new Vector2(1.5, 2.5)) },
            { name: "new Vector2i(int,int)", fn: () => new Vector2i(1, 1) },
        ],
    },
    // ---- Constructors/Rect2 ----
    {
        group: "Constructors",
        makeTarget: () => new Rect2(0, 0, 10, 10),
        cases: [
            { name: "new Rect2(0 args)", fn: () => new Rect2() },
            { name: "new Rect2(Rect2)", fn: () => new Rect2(new Rect2(0, 0, 10, 10)) },
            { name: "new Rect2(Rect2i)", fn: () => new Rect2(new Rect2i(0, 0, 10, 10)) },
            { name: "new Rect2(Vector2,Vector2)", fn: () => new Rect2(new Vector2(1.5, 2.5), new Vector2(1.5, 2.5)) },
            { name: "new Rect2(float,float,float,float)", fn: () => new Rect2(1.5, 1.5, 1.5, 1.5) },
        ],
    },
    // ---- Constructors/Rect2i ----
    {
        group: "Constructors",
        makeTarget: () => new Rect2i(0, 0, 10, 10),
        cases: [
            { name: "new Rect2i(0 args)", fn: () => new Rect2i() },
            { name: "new Rect2i(Rect2i)", fn: () => new Rect2i(new Rect2i(0, 0, 10, 10)) },
            { name: "new Rect2i(Rect2)", fn: () => new Rect2i(new Rect2(0, 0, 10, 10)) },
            { name: "new Rect2i(Vector2i,Vector2i)", fn: () => new Rect2i(new Vector2i(1, 2), new Vector2i(1, 2)) },
            { name: "new Rect2i(int,int,int,int)", fn: () => new Rect2i(1, 1, 1, 1) },
        ],
    },
    // ---- Constructors/Vector3 ----
    {
        group: "Constructors",
        makeTarget: () => new Vector3(0, 0, 1),
        cases: [
            { name: "new Vector3(0 args)", fn: () => new Vector3() },
            { name: "new Vector3(Vector3)", fn: () => new Vector3(new Vector3(0, 0, 1)) },
            { name: "new Vector3(Vector3i)", fn: () => new Vector3(new Vector3i(1, 2, 3)) },
            { name: "new Vector3(float,float,float)", fn: () => new Vector3(1.5, 1.5, 1.5) },
        ],
    },
    // ---- Constructors/Vector3i ----
    {
        group: "Constructors",
        makeTarget: () => new Vector3i(1, 2, 3),
        cases: [
            { name: "new Vector3i(0 args)", fn: () => new Vector3i() },
            { name: "new Vector3i(Vector3i)", fn: () => new Vector3i(new Vector3i(1, 2, 3)) },
            { name: "new Vector3i(Vector3)", fn: () => new Vector3i(new Vector3(0, 0, 1)) },
            { name: "new Vector3i(int,int,int)", fn: () => new Vector3i(1, 1, 1) },
        ],
    },
    // ---- Constructors/Transform2D ----
    {
        group: "Constructors",
        makeTarget: () => new Transform2D(),
        cases: [
            { name: "new Transform2D(0 args)", fn: () => new Transform2D() },
            { name: "new Transform2D(Transform2D)", fn: () => new Transform2D() },
            { name: "new Transform2D(float,Vector2)", fn: () => new Transform2D(1.5, new Vector2(1.5, 2.5)) },
            { name: "new Transform2D(float,Vector2,float,Vector2)", fn: () => new Transform2D(1.5, new Vector2(1.5, 2.5), 1.5, new Vector2(1.5, 2.5)) },
            { name: "new Transform2D(Vector2,Vector2,Vector2)", fn: () => new Transform2D(new Vector2(1.5, 2.5), new Vector2(1.5, 2.5), new Vector2(1.5, 2.5)) },
        ],
    },
    // ---- Constructors/Vector4 ----
    {
        group: "Constructors",
        makeTarget: () => new Vector4(1, 2, 3, 4),
        cases: [
            { name: "new Vector4(0 args)", fn: () => new Vector4() },
            { name: "new Vector4(Vector4)", fn: () => new Vector4(new Vector4(1, 2, 3, 4)) },
            { name: "new Vector4(Vector4i)", fn: () => new Vector4(new Vector4i(1, 2, 3, 4)) },
            { name: "new Vector4(float,float,float,float)", fn: () => new Vector4(1.5, 1.5, 1.5, 1.5) },
        ],
    },
    // ---- Constructors/Vector4i ----
    {
        group: "Constructors",
        makeTarget: () => new Vector4i(1, 2, 3, 4),
        cases: [
            { name: "new Vector4i(0 args)", fn: () => new Vector4i() },
            { name: "new Vector4i(Vector4i)", fn: () => new Vector4i(new Vector4i(1, 2, 3, 4)) },
            { name: "new Vector4i(Vector4)", fn: () => new Vector4i(new Vector4(1, 2, 3, 4)) },
            { name: "new Vector4i(int,int,int,int)", fn: () => new Vector4i(1, 1, 1, 1) },
        ],
    },
    // ---- Constructors/Plane ----
    {
        group: "Constructors",
        makeTarget: () => new Plane(0, 0, 1, 0),
        cases: [
            { name: "new Plane(0 args)", fn: () => new Plane() },
            { name: "new Plane(Plane)", fn: () => new Plane(new Plane(0, 0, 1, 0)) },
            { name: "new Plane(Vector3)", fn: () => new Plane(new Vector3(0, 0, 1)) },
            { name: "new Plane(Vector3,float)", fn: () => new Plane(new Vector3(0, 0, 1), 1.5) },
            { name: "new Plane(Vector3,Vector3)", fn: () => new Plane(new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
            { name: "new Plane(Vector3,Vector3,Vector3)", fn: () => new Plane(new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
            { name: "new Plane(float,float,float,float)", fn: () => new Plane(1.5, 1.5, 1.5, 1.5) },
        ],
    },
    // ---- Constructors/Quaternion ----
    {
        group: "Constructors",
        makeTarget: () => new Quaternion(0, 0, 0, 1),
        cases: [
            { name: "new Quaternion(0 args)", fn: () => new Quaternion() },
            { name: "new Quaternion(Quaternion)", fn: () => new Quaternion(new Quaternion(0, 0, 0, 1)) },
            { name: "new Quaternion(Basis)", fn: () => new Quaternion(new Basis()) },
            { name: "new Quaternion(Vector3,float)", fn: () => new Quaternion(new Vector3(0, 0, 1), 1.5) },
            { name: "new Quaternion(Vector3,Vector3)", fn: () => new Quaternion(new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
            { name: "new Quaternion(float,float,float,float)", fn: () => new Quaternion(1.5, 1.5, 1.5, 1.5) },
        ],
    },
    // ---- Constructors/AABB ----
    {
        group: "Constructors",
        makeTarget: () => new AABB(new Vector3(0, 0, 0), new Vector3(1, 1, 1)),
        cases: [
            { name: "new AABB(0 args)", fn: () => new AABB() },
            { name: "new AABB(AABB)", fn: () => new AABB(new AABB(new Vector3(0, 0, 0), new Vector3(1, 1, 1))) },
            { name: "new AABB(Vector3,Vector3)", fn: () => new AABB(new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
        ],
    },
    // ---- Constructors/Basis ----
    {
        group: "Constructors",
        makeTarget: () => new Basis(),
        cases: [
            { name: "new Basis(0 args)", fn: () => new Basis() },
            { name: "new Basis(Basis)", fn: () => new Basis(new Basis()) },
            { name: "new Basis(Quaternion)", fn: () => new Basis(new Quaternion(0, 0, 0, 1)) },
            { name: "new Basis(Vector3,float)", fn: () => new Basis(new Vector3(0, 0, 1), 1.5) },
            { name: "new Basis(Vector3,Vector3,Vector3)", fn: () => new Basis(new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
        ],
    },
    // ---- Constructors/Transform3D ----
    {
        group: "Constructors",
        makeTarget: () => new Transform3D(new Vector3(0, 0, 1), new Vector3(0, 1, 0), new Vector3(1, 0, 0), new Vector3(0, 0, 0)),
        cases: [
            { name: "new Transform3D(0 args)", fn: () => new Transform3D() },
            { name: "new Transform3D(Transform3D)", fn: () => new Transform3D(new Transform3D(new Vector3(0, 0, 1), new Vector3(0, 1, 0), new Vector3(1, 0, 0), new Vector3(0, 0, 0))) },
            { name: "new Transform3D(Basis,Vector3)", fn: () => new Transform3D(new Basis(), new Vector3(0, 0, 1)) },
            { name: "new Transform3D(Vector3,Vector3,Vector3,Vector3)", fn: () => new Transform3D(new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1), new Vector3(0, 0, 1)) },
            { name: "new Transform3D(Projection)", fn: () => new Transform3D(new Projection()) },
        ],
    },
    // ---- Constructors/Projection ----
    {
        group: "Constructors",
        makeTarget: () => new Projection(),
        cases: [
            { name: "new Projection(0 args)", fn: () => new Projection() },
            { name: "new Projection(Projection)", fn: () => new Projection(new Projection()) },
            { name: "new Projection(Transform3D)", fn: () => new Projection(new Transform3D(new Vector3(0, 0, 1), new Vector3(0, 1, 0), new Vector3(1, 0, 0), new Vector3(0, 0, 0))) },
            { name: "new Projection(Vector4,Vector4,Vector4,Vector4)", fn: () => new Projection(new Vector4(1, 2, 3, 4), new Vector4(1, 2, 3, 4), new Vector4(1, 2, 3, 4), new Vector4(1, 2, 3, 4)) },
        ],
    },
    // ---- Constructors/Color ----
    {
        group: "Constructors",
        makeTarget: () => new Color(0.2, 0.4, 0.6, 0.8),
        cases: [
            { name: "new Color(0 args)", fn: () => new Color() },
            { name: "new Color(Color)", fn: () => new Color(new Color(0.2, 0.4, 0.6, 0.8)) },
            { name: "new Color(Color,float)", fn: () => new Color(new Color(0.2, 0.4, 0.6, 0.8), 1.5) },
            { name: "new Color(float,float,float)", fn: () => new Color(1.5, 1.5, 1.5) },
            { name: "new Color(float,float,float,float)", fn: () => new Color(1.5, 1.5, 1.5, 1.5) },
            { name: "new Color(String)", fn: () => new Color("abc") },
            { name: "new Color(String,float)", fn: () => new Color("abc", 1.5) },
        ],
    },
    // ---- Constructors/NodePath ----
    {
        group: "Constructors",
        makeTarget: () => new NodePath("abc"),
        cases: [
            { name: "new NodePath(0 args)", fn: () => new NodePath() },
            { name: "new NodePath(NodePath)", fn: () => new NodePath(new NodePath("abc")) },
            { name: "new NodePath(String)", fn: () => new NodePath("abc") },
        ],
    },
    // ---- Constructors/RID ----
    {
        group: "Constructors",
        makeTarget: () => new RID(),
        cases: [
            { name: "new RID(0 args)", fn: () => new RID() },
            { name: "new RID(RID)", fn: () => new RID(new RID()) },
        ],
    },
    // ---- Constructors/Callable ----
    {
        group: "Constructors",
        makeTarget: () => new Callable(),
        cases: [
            { name: "new Callable(0 args)", fn: () => new Callable() },
            { name: "new Callable(Callable)", fn: () => new Callable(new Callable()) },
        ],
    },
    // ---- Constructors/Signal ----
    {
        group: "Constructors",
        makeTarget: () => new Signal(),
        cases: [
            { name: "new Signal(0 args)", fn: () => new Signal() },
            { name: "new Signal(Signal)", fn: () => new Signal(new Signal()) },
        ],
    },
    // ---- Constructors/Dictionary ----
    {
        group: "Constructors",
        makeTarget: () => new GDictionary(),
        cases: [
            { name: "new Dictionary(0 args)", fn: () => new GDictionary() },
            { name: "new Dictionary(Dictionary)", fn: () => new GDictionary(new GDictionary()) },
            { name: "new Dictionary(Dictionary,int,StringName,Variant,int,StringName,Variant)", fn: () => new GDictionary(new GDictionary(), Variant.Type.TYPE_INT, "", null, Variant.Type.TYPE_STRING, "", null) },
        ],
    },
    // ---- Constructors/Array ----
    {
        group: "Constructors",
        makeTarget: () => new GArray(),
        cases: [
            { name: "new Array(0 args)", fn: () => new GArray() },
            { name: "new Array(Array)", fn: () => new GArray(new GArray()) },
            { name: "new Array(Array,int,StringName,Variant)", fn: () => new GArray(new GArray(), Variant.Type.TYPE_STRING, "", null) },
        ],
    },
    // ---- Constructors/PackedByteArray ----
    {
        group: "Constructors",
        makeTarget: () => new PackedByteArray(),
        cases: [
            { name: "new PackedByteArray(0 args)", fn: () => new PackedByteArray() },
            { name: "new PackedByteArray(PackedByteArray)", fn: () => new PackedByteArray(new PackedByteArray()) },
            { name: "new PackedByteArray(Array)", fn: () => new PackedByteArray(new GArray()) },
        ],
    },
    // ---- Constructors/PackedInt32Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedInt32Array(),
        cases: [
            { name: "new PackedInt32Array(0 args)", fn: () => new PackedInt32Array() },
            { name: "new PackedInt32Array(PackedInt32Array)", fn: () => new PackedInt32Array(new PackedInt32Array()) },
            { name: "new PackedInt32Array(Array)", fn: () => new PackedInt32Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedInt64Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedInt64Array(),
        cases: [
            { name: "new PackedInt64Array(0 args)", fn: () => new PackedInt64Array() },
            { name: "new PackedInt64Array(PackedInt64Array)", fn: () => new PackedInt64Array(new PackedInt64Array()) },
            { name: "new PackedInt64Array(Array)", fn: () => new PackedInt64Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedFloat32Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedFloat32Array(),
        cases: [
            { name: "new PackedFloat32Array(0 args)", fn: () => new PackedFloat32Array() },
            { name: "new PackedFloat32Array(PackedFloat32Array)", fn: () => new PackedFloat32Array(new PackedFloat32Array()) },
            { name: "new PackedFloat32Array(Array)", fn: () => new PackedFloat32Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedFloat64Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedFloat64Array(),
        cases: [
            { name: "new PackedFloat64Array(0 args)", fn: () => new PackedFloat64Array() },
            { name: "new PackedFloat64Array(PackedFloat64Array)", fn: () => new PackedFloat64Array(new PackedFloat64Array()) },
            { name: "new PackedFloat64Array(Array)", fn: () => new PackedFloat64Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedStringArray ----
    {
        group: "Constructors",
        makeTarget: () => new PackedStringArray(),
        cases: [
            { name: "new PackedStringArray(0 args)", fn: () => new PackedStringArray() },
            { name: "new PackedStringArray(PackedStringArray)", fn: () => new PackedStringArray(new PackedStringArray()) },
            { name: "new PackedStringArray(Array)", fn: () => new PackedStringArray(new GArray()) },
        ],
    },
    // ---- Constructors/PackedVector2Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedVector2Array(),
        cases: [
            { name: "new PackedVector2Array(0 args)", fn: () => new PackedVector2Array() },
            { name: "new PackedVector2Array(PackedVector2Array)", fn: () => new PackedVector2Array(new PackedVector2Array()) },
            { name: "new PackedVector2Array(Array)", fn: () => new PackedVector2Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedVector3Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedVector3Array(),
        cases: [
            { name: "new PackedVector3Array(0 args)", fn: () => new PackedVector3Array() },
            { name: "new PackedVector3Array(PackedVector3Array)", fn: () => new PackedVector3Array(new PackedVector3Array()) },
            { name: "new PackedVector3Array(Array)", fn: () => new PackedVector3Array(new GArray()) },
        ],
    },
    // ---- Constructors/PackedColorArray ----
    {
        group: "Constructors",
        makeTarget: () => new PackedColorArray(),
        cases: [
            { name: "new PackedColorArray(0 args)", fn: () => new PackedColorArray() },
            { name: "new PackedColorArray(PackedColorArray)", fn: () => new PackedColorArray(new PackedColorArray()) },
            { name: "new PackedColorArray(Array)", fn: () => new PackedColorArray(new GArray()) },
        ],
    },
    // ---- Constructors/PackedVector4Array ----
    {
        group: "Constructors",
        makeTarget: () => new PackedVector4Array(),
        cases: [
            { name: "new PackedVector4Array(0 args)", fn: () => new PackedVector4Array() },
            { name: "new PackedVector4Array(PackedVector4Array)", fn: () => new PackedVector4Array(new PackedVector4Array()) },
            { name: "new PackedVector4Array(Array)", fn: () => new PackedVector4Array(new GArray()) },
        ],
    },
];
// 85 method/member cases over 32 classes
