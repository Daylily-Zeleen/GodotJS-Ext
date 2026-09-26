// uid://du1soqwk5jkko This line is generated, don't modify or remove it.
// Operator coverage test: every bound builtin operator, in member form.
//
// OP_METHODS / OP_CALLS are machine-extracted from the two code generators
// (jsb_primitive_operators.def.gen.h and dispatch_builtin.gen.cpp), so the
// expectations cannot drift from the actually bound surface.
//
// Runs on both binding legs (static / dynamic): assertions only use
// behaviour both legs share.
import {
    AABB,
    Basis,
    Callable,
    Color,
    GArray,
    GDictionary,
    Node,
    NodePath,
    PackedByteArray,
    PackedColorArray,
    PackedFloat32Array,
    PackedFloat64Array,
    PackedInt32Array,
    PackedInt64Array,
    PackedStringArray,
    PackedVector2Array,
    PackedVector3Array,
    PackedVector4Array,
    Plane,
    Projection,
    Quaternion,
    RID,
    Rect2,
    Rect2i,
    Signal,
    Transform2D,
    Transform3D,
    Vector2,
    Vector2i,
    Vector3,
    Vector3i,
    Vector4,
    Vector4i,
} from "godot";
import { reportTestFailure } from "../test-status";

interface OpCall { c: string; o: string; r: string; k: string }

/** Expected operator methods per class (generated). */
const OP_METHODS: Record<string, { bin: string[]; un: string[] }> = {
    AABB: { bin: ["OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Basis: { bin: ["OP_DIVIDE", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Callable: { bin: ["OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Color: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Dictionary: { bin: ["OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    NodePath: { bin: ["OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedByteArray: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedColorArray: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedFloat32Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedFloat64Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedInt32Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedInt64Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedStringArray: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedVector2Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedVector3Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    PackedVector4Array: { bin: ["OP_ADD", "OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Plane: { bin: ["OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Projection: { bin: ["OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Quaternion: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    RID: { bin: ["OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Rect2: { bin: ["OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Rect2i: { bin: ["OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Signal: { bin: ["OP_EQUAL", "OP_IN", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Transform2D: { bin: ["OP_DIVIDE", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Transform3D: { bin: ["OP_DIVIDE", "OP_EQUAL", "OP_IN", "OP_MULTIPLY", "OP_NOT_EQUAL"], un: ["OP_NOT"] },
    Vector2: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Vector2i: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MODULE", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Vector3: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Vector3i: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MODULE", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Vector4: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
    Vector4i: { bin: ["OP_ADD", "OP_DIVIDE", "OP_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL", "OP_IN", "OP_LESS", "OP_LESS_EQUAL", "OP_MODULE", "OP_MULTIPLY", "OP_NOT_EQUAL", "OP_SUBTRACT"], un: ["OP_NEGATE", "OP_NOT", "OP_POSITIVE"] },
};

/** Every selectable (left, op, right) combination (generated). */
const OP_CALLS: OpCall[] = [
    { c: "Vector2", o: "OP_EQUAL", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector2", o: "OP_NOT_EQUAL", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector2", o: "OP_LESS", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_LESS_EQUAL", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_GREATER", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_GREATER_EQUAL", r: "VECTOR2", k: "bool" },
    { c: "Vector2", o: "OP_ADD", r: "VECTOR2", k: "class:Vector2" },
    { c: "Vector2", o: "OP_SUBTRACT", r: "VECTOR2", k: "class:Vector2" },
    { c: "Vector2", o: "OP_MULTIPLY", r: "INT", k: "class:Vector2" },
    { c: "Vector2", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector2" },
    { c: "Vector2", o: "OP_MULTIPLY", r: "VECTOR2", k: "class:Vector2" },
    { c: "Vector2", o: "OP_MULTIPLY", r: "TRANSFORM2D", k: "class:Vector2" },
    { c: "Vector2", o: "OP_DIVIDE", r: "INT", k: "class:Vector2" },
    { c: "Vector2", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector2" },
    { c: "Vector2", o: "OP_DIVIDE", r: "VECTOR2", k: "class:Vector2" },
    { c: "Vector2", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector2", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Vector2", o: "OP_IN", r: "PACKED_VECTOR2_ARRAY", k: "bool" },
    { c: "Vector2i", o: "OP_EQUAL", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector2i", o: "OP_NOT_EQUAL", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector2i", o: "OP_LESS", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_LESS_EQUAL", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_GREATER", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_GREATER_EQUAL", r: "VECTOR2I", k: "bool" },
    { c: "Vector2i", o: "OP_ADD", r: "VECTOR2I", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_SUBTRACT", r: "VECTOR2I", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_MULTIPLY", r: "INT", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector2" },
    { c: "Vector2i", o: "OP_MULTIPLY", r: "VECTOR2I", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_DIVIDE", r: "INT", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector2" },
    { c: "Vector2i", o: "OP_DIVIDE", r: "VECTOR2I", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_MODULE", r: "INT", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_MODULE", r: "VECTOR2I", k: "class:Vector2i" },
    { c: "Vector2i", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector2i", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Rect2", o: "OP_EQUAL", r: "RECT2", k: "bool" },
    { c: "Rect2", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Rect2", o: "OP_NOT_EQUAL", r: "RECT2", k: "bool" },
    { c: "Rect2", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Rect2", o: "OP_MULTIPLY", r: "TRANSFORM2D", k: "class:Rect2" },
    { c: "Rect2", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Rect2", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Rect2i", o: "OP_EQUAL", r: "RECT2I", k: "bool" },
    { c: "Rect2i", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Rect2i", o: "OP_NOT_EQUAL", r: "RECT2I", k: "bool" },
    { c: "Rect2i", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Rect2i", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Rect2i", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Vector3", o: "OP_EQUAL", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector3", o: "OP_NOT_EQUAL", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector3", o: "OP_LESS", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_LESS_EQUAL", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_GREATER", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_GREATER_EQUAL", r: "VECTOR3", k: "bool" },
    { c: "Vector3", o: "OP_ADD", r: "VECTOR3", k: "class:Vector3" },
    { c: "Vector3", o: "OP_SUBTRACT", r: "VECTOR3", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "INT", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "VECTOR3", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "QUATERNION", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "BASIS", k: "class:Vector3" },
    { c: "Vector3", o: "OP_MULTIPLY", r: "TRANSFORM3D", k: "class:Vector3" },
    { c: "Vector3", o: "OP_DIVIDE", r: "INT", k: "class:Vector3" },
    { c: "Vector3", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector3" },
    { c: "Vector3", o: "OP_DIVIDE", r: "VECTOR3", k: "class:Vector3" },
    { c: "Vector3", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector3", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Vector3", o: "OP_IN", r: "PACKED_VECTOR3_ARRAY", k: "bool" },
    { c: "Vector3i", o: "OP_EQUAL", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector3i", o: "OP_NOT_EQUAL", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector3i", o: "OP_LESS", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_LESS_EQUAL", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_GREATER", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_GREATER_EQUAL", r: "VECTOR3I", k: "bool" },
    { c: "Vector3i", o: "OP_ADD", r: "VECTOR3I", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_SUBTRACT", r: "VECTOR3I", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_MULTIPLY", r: "INT", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector3" },
    { c: "Vector3i", o: "OP_MULTIPLY", r: "VECTOR3I", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_DIVIDE", r: "INT", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector3" },
    { c: "Vector3i", o: "OP_DIVIDE", r: "VECTOR3I", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_MODULE", r: "INT", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_MODULE", r: "VECTOR3I", k: "class:Vector3i" },
    { c: "Vector3i", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector3i", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Transform2D", o: "OP_EQUAL", r: "TRANSFORM2D", k: "bool" },
    { c: "Transform2D", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Transform2D", o: "OP_NOT_EQUAL", r: "TRANSFORM2D", k: "bool" },
    { c: "Transform2D", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "INT", k: "class:Transform2D" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Transform2D" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "VECTOR2", k: "class:Vector2" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "RECT2", k: "class:Rect2" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "TRANSFORM2D", k: "class:Transform2D" },
    { c: "Transform2D", o: "OP_MULTIPLY", r: "PACKED_VECTOR2_ARRAY", k: "class:PackedVector2Array" },
    { c: "Transform2D", o: "OP_DIVIDE", r: "INT", k: "class:Transform2D" },
    { c: "Transform2D", o: "OP_DIVIDE", r: "FLOAT", k: "class:Transform2D" },
    { c: "Transform2D", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Transform2D", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Vector4", o: "OP_EQUAL", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector4", o: "OP_NOT_EQUAL", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector4", o: "OP_LESS", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_LESS_EQUAL", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_GREATER", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_GREATER_EQUAL", r: "VECTOR4", k: "bool" },
    { c: "Vector4", o: "OP_ADD", r: "VECTOR4", k: "class:Vector4" },
    { c: "Vector4", o: "OP_SUBTRACT", r: "VECTOR4", k: "class:Vector4" },
    { c: "Vector4", o: "OP_MULTIPLY", r: "INT", k: "class:Vector4" },
    { c: "Vector4", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector4" },
    { c: "Vector4", o: "OP_MULTIPLY", r: "VECTOR4", k: "class:Vector4" },
    { c: "Vector4", o: "OP_MULTIPLY", r: "PROJECTION", k: "class:Vector4" },
    { c: "Vector4", o: "OP_DIVIDE", r: "INT", k: "class:Vector4" },
    { c: "Vector4", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector4" },
    { c: "Vector4", o: "OP_DIVIDE", r: "VECTOR4", k: "class:Vector4" },
    { c: "Vector4", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector4", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Vector4", o: "OP_IN", r: "PACKED_VECTOR4_ARRAY", k: "bool" },
    { c: "Vector4i", o: "OP_EQUAL", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector4i", o: "OP_NOT_EQUAL", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Vector4i", o: "OP_LESS", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_LESS_EQUAL", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_GREATER", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_GREATER_EQUAL", r: "VECTOR4I", k: "bool" },
    { c: "Vector4i", o: "OP_ADD", r: "VECTOR4I", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_SUBTRACT", r: "VECTOR4I", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_MULTIPLY", r: "INT", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Vector4" },
    { c: "Vector4i", o: "OP_MULTIPLY", r: "VECTOR4I", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_DIVIDE", r: "INT", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_DIVIDE", r: "FLOAT", k: "class:Vector4" },
    { c: "Vector4i", o: "OP_DIVIDE", r: "VECTOR4I", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_MODULE", r: "INT", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_MODULE", r: "VECTOR4I", k: "class:Vector4i" },
    { c: "Vector4i", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Vector4i", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Plane", o: "OP_EQUAL", r: "PLANE", k: "bool" },
    { c: "Plane", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Plane", o: "OP_NOT_EQUAL", r: "PLANE", k: "bool" },
    { c: "Plane", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Plane", o: "OP_MULTIPLY", r: "TRANSFORM3D", k: "class:Plane" },
    { c: "Plane", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Plane", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Quaternion", o: "OP_EQUAL", r: "QUATERNION", k: "bool" },
    { c: "Quaternion", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Quaternion", o: "OP_NOT_EQUAL", r: "QUATERNION", k: "bool" },
    { c: "Quaternion", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Quaternion", o: "OP_ADD", r: "QUATERNION", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_SUBTRACT", r: "QUATERNION", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_MULTIPLY", r: "INT", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_MULTIPLY", r: "VECTOR3", k: "class:Vector3" },
    { c: "Quaternion", o: "OP_MULTIPLY", r: "QUATERNION", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_DIVIDE", r: "INT", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_DIVIDE", r: "FLOAT", k: "class:Quaternion" },
    { c: "Quaternion", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Quaternion", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "AABB", o: "OP_EQUAL", r: "AABB", k: "bool" },
    { c: "AABB", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "AABB", o: "OP_NOT_EQUAL", r: "AABB", k: "bool" },
    { c: "AABB", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "AABB", o: "OP_MULTIPLY", r: "TRANSFORM3D", k: "class:AABB" },
    { c: "AABB", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "AABB", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Basis", o: "OP_EQUAL", r: "BASIS", k: "bool" },
    { c: "Basis", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Basis", o: "OP_NOT_EQUAL", r: "BASIS", k: "bool" },
    { c: "Basis", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Basis", o: "OP_MULTIPLY", r: "INT", k: "class:Basis" },
    { c: "Basis", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Basis" },
    { c: "Basis", o: "OP_MULTIPLY", r: "VECTOR3", k: "class:Vector3" },
    { c: "Basis", o: "OP_MULTIPLY", r: "BASIS", k: "class:Basis" },
    { c: "Basis", o: "OP_DIVIDE", r: "INT", k: "class:Basis" },
    { c: "Basis", o: "OP_DIVIDE", r: "FLOAT", k: "class:Basis" },
    { c: "Basis", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Basis", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Transform3D", o: "OP_EQUAL", r: "TRANSFORM3D", k: "bool" },
    { c: "Transform3D", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Transform3D", o: "OP_NOT_EQUAL", r: "TRANSFORM3D", k: "bool" },
    { c: "Transform3D", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "INT", k: "class:Transform3D" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Transform3D" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "VECTOR3", k: "class:Vector3" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "PLANE", k: "class:Plane" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "AABB", k: "class:AABB" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "TRANSFORM3D", k: "class:Transform3D" },
    { c: "Transform3D", o: "OP_MULTIPLY", r: "PACKED_VECTOR3_ARRAY", k: "class:PackedVector3Array" },
    { c: "Transform3D", o: "OP_DIVIDE", r: "INT", k: "class:Transform3D" },
    { c: "Transform3D", o: "OP_DIVIDE", r: "FLOAT", k: "class:Transform3D" },
    { c: "Transform3D", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Transform3D", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Projection", o: "OP_EQUAL", r: "PROJECTION", k: "bool" },
    { c: "Projection", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Projection", o: "OP_NOT_EQUAL", r: "PROJECTION", k: "bool" },
    { c: "Projection", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Projection", o: "OP_MULTIPLY", r: "VECTOR4", k: "class:Vector4" },
    { c: "Projection", o: "OP_MULTIPLY", r: "PROJECTION", k: "class:Projection" },
    { c: "Projection", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Projection", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Color", o: "OP_EQUAL", r: "COLOR", k: "bool" },
    { c: "Color", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Color", o: "OP_NOT_EQUAL", r: "COLOR", k: "bool" },
    { c: "Color", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Color", o: "OP_ADD", r: "COLOR", k: "class:Color" },
    { c: "Color", o: "OP_SUBTRACT", r: "COLOR", k: "class:Color" },
    { c: "Color", o: "OP_MULTIPLY", r: "INT", k: "class:Color" },
    { c: "Color", o: "OP_MULTIPLY", r: "FLOAT", k: "class:Color" },
    { c: "Color", o: "OP_MULTIPLY", r: "COLOR", k: "class:Color" },
    { c: "Color", o: "OP_DIVIDE", r: "INT", k: "class:Color" },
    { c: "Color", o: "OP_DIVIDE", r: "FLOAT", k: "class:Color" },
    { c: "Color", o: "OP_DIVIDE", r: "COLOR", k: "class:Color" },
    { c: "Color", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Color", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Color", o: "OP_IN", r: "PACKED_COLOR_ARRAY", k: "bool" },
    { c: "NodePath", o: "OP_EQUAL", r: "NODE_PATH", k: "bool" },
    { c: "NodePath", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "NodePath", o: "OP_NOT_EQUAL", r: "NODE_PATH", k: "bool" },
    { c: "NodePath", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "NodePath", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "NodePath", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "RID", o: "OP_EQUAL", r: "RID", k: "bool" },
    { c: "RID", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "RID", o: "OP_NOT_EQUAL", r: "RID", k: "bool" },
    { c: "RID", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "RID", o: "OP_LESS", r: "RID", k: "bool" },
    { c: "RID", o: "OP_LESS_EQUAL", r: "RID", k: "bool" },
    { c: "RID", o: "OP_GREATER", r: "RID", k: "bool" },
    { c: "RID", o: "OP_GREATER_EQUAL", r: "RID", k: "bool" },
    { c: "RID", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "RID", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Callable", o: "OP_EQUAL", r: "CALLABLE", k: "bool" },
    { c: "Callable", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Callable", o: "OP_NOT_EQUAL", r: "CALLABLE", k: "bool" },
    { c: "Callable", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Callable", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Callable", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Signal", o: "OP_EQUAL", r: "SIGNAL", k: "bool" },
    { c: "Signal", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Signal", o: "OP_NOT_EQUAL", r: "SIGNAL", k: "bool" },
    { c: "Signal", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Signal", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Signal", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Dictionary", o: "OP_EQUAL", r: "DICTIONARY", k: "bool" },
    { c: "Dictionary", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Dictionary", o: "OP_NOT_EQUAL", r: "DICTIONARY", k: "bool" },
    { c: "Dictionary", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Dictionary", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Dictionary", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_EQUAL", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "Array", o: "OP_NOT_EQUAL", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "Array", o: "OP_LESS", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_LESS_EQUAL", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_GREATER", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_GREATER_EQUAL", r: "ARRAY", k: "bool" },
    { c: "Array", o: "OP_ADD", r: "ARRAY", k: "class:Array" },
    { c: "Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedByteArray", o: "OP_EQUAL", r: "PACKED_BYTE_ARRAY", k: "bool" },
    { c: "PackedByteArray", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedByteArray", o: "OP_NOT_EQUAL", r: "PACKED_BYTE_ARRAY", k: "bool" },
    { c: "PackedByteArray", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedByteArray", o: "OP_ADD", r: "PACKED_BYTE_ARRAY", k: "class:PackedByteArray" },
    { c: "PackedByteArray", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedByteArray", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedInt32Array", o: "OP_EQUAL", r: "PACKED_INT32_ARRAY", k: "bool" },
    { c: "PackedInt32Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedInt32Array", o: "OP_NOT_EQUAL", r: "PACKED_INT32_ARRAY", k: "bool" },
    { c: "PackedInt32Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedInt32Array", o: "OP_ADD", r: "PACKED_INT32_ARRAY", k: "class:PackedInt32Array" },
    { c: "PackedInt32Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedInt32Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedInt64Array", o: "OP_EQUAL", r: "PACKED_INT64_ARRAY", k: "bool" },
    { c: "PackedInt64Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedInt64Array", o: "OP_NOT_EQUAL", r: "PACKED_INT64_ARRAY", k: "bool" },
    { c: "PackedInt64Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedInt64Array", o: "OP_ADD", r: "PACKED_INT64_ARRAY", k: "class:PackedInt64Array" },
    { c: "PackedInt64Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedInt64Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_EQUAL", r: "PACKED_FLOAT32_ARRAY", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_NOT_EQUAL", r: "PACKED_FLOAT32_ARRAY", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_ADD", r: "PACKED_FLOAT32_ARRAY", k: "class:PackedFloat32Array" },
    { c: "PackedFloat32Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedFloat32Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_EQUAL", r: "PACKED_FLOAT64_ARRAY", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_NOT_EQUAL", r: "PACKED_FLOAT64_ARRAY", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_ADD", r: "PACKED_FLOAT64_ARRAY", k: "class:PackedFloat64Array" },
    { c: "PackedFloat64Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedFloat64Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedStringArray", o: "OP_EQUAL", r: "PACKED_STRING_ARRAY", k: "bool" },
    { c: "PackedStringArray", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedStringArray", o: "OP_NOT_EQUAL", r: "PACKED_STRING_ARRAY", k: "bool" },
    { c: "PackedStringArray", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedStringArray", o: "OP_ADD", r: "PACKED_STRING_ARRAY", k: "class:PackedStringArray" },
    { c: "PackedStringArray", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedStringArray", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedVector2Array", o: "OP_EQUAL", r: "PACKED_VECTOR2_ARRAY", k: "bool" },
    { c: "PackedVector2Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector2Array", o: "OP_NOT_EQUAL", r: "PACKED_VECTOR2_ARRAY", k: "bool" },
    { c: "PackedVector2Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector2Array", o: "OP_ADD", r: "PACKED_VECTOR2_ARRAY", k: "class:PackedVector2Array" },
    { c: "PackedVector2Array", o: "OP_MULTIPLY", r: "TRANSFORM2D", k: "class:PackedVector2Array" },
    { c: "PackedVector2Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedVector2Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedVector3Array", o: "OP_EQUAL", r: "PACKED_VECTOR3_ARRAY", k: "bool" },
    { c: "PackedVector3Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector3Array", o: "OP_NOT_EQUAL", r: "PACKED_VECTOR3_ARRAY", k: "bool" },
    { c: "PackedVector3Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector3Array", o: "OP_ADD", r: "PACKED_VECTOR3_ARRAY", k: "class:PackedVector3Array" },
    { c: "PackedVector3Array", o: "OP_MULTIPLY", r: "TRANSFORM3D", k: "class:PackedVector3Array" },
    { c: "PackedVector3Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedVector3Array", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedColorArray", o: "OP_EQUAL", r: "PACKED_COLOR_ARRAY", k: "bool" },
    { c: "PackedColorArray", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedColorArray", o: "OP_NOT_EQUAL", r: "PACKED_COLOR_ARRAY", k: "bool" },
    { c: "PackedColorArray", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedColorArray", o: "OP_ADD", r: "PACKED_COLOR_ARRAY", k: "class:PackedColorArray" },
    { c: "PackedColorArray", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedColorArray", o: "OP_IN", r: "ARRAY", k: "bool" },
    { c: "PackedVector4Array", o: "OP_EQUAL", r: "PACKED_VECTOR4_ARRAY", k: "bool" },
    { c: "PackedVector4Array", o: "OP_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector4Array", o: "OP_NOT_EQUAL", r: "PACKED_VECTOR4_ARRAY", k: "bool" },
    { c: "PackedVector4Array", o: "OP_NOT_EQUAL", r: "NIL", k: "bool" },
    { c: "PackedVector4Array", o: "OP_ADD", r: "PACKED_VECTOR4_ARRAY", k: "class:PackedVector4Array" },
    { c: "PackedVector4Array", o: "OP_IN", r: "DICTIONARY", k: "bool" },
    { c: "PackedVector4Array", o: "OP_IN", r: "ARRAY", k: "bool" },
];

const EXPECTED_METHODS = 236;
const EXPECTED_UNARY = 50;
const EXPECTED_BINARY = 186;
const EXPECTED_CALLS = 344;

/** Class constructor, for return-type checks. */
const CTOR: Record<string, Function> = {
    AABB: AABB,
    Array: GArray,
    Basis: Basis,
    Callable: Callable,
    Color: Color,
    Dictionary: GDictionary,
    NodePath: NodePath,
    PackedByteArray: PackedByteArray,
    PackedColorArray: PackedColorArray,
    PackedFloat32Array: PackedFloat32Array,
    PackedFloat64Array: PackedFloat64Array,
    PackedInt32Array: PackedInt32Array,
    PackedInt64Array: PackedInt64Array,
    PackedStringArray: PackedStringArray,
    PackedVector2Array: PackedVector2Array,
    PackedVector3Array: PackedVector3Array,
    PackedVector4Array: PackedVector4Array,
    Plane: Plane,
    Projection: Projection,
    Quaternion: Quaternion,
    RID: RID,
    Rect2: Rect2,
    Rect2i: Rect2i,
    Signal: Signal,
    Transform2D: Transform2D,
    Transform3D: Transform3D,
    Vector2: Vector2,
    Vector2i: Vector2i,
    Vector3: Vector3,
    Vector3i: Vector3i,
    Vector4: Vector4,
    Vector4i: Vector4i,
};

/**
 * Fresh receiver (the left operand) per class.
 *
 * Array-backed types use the no-arg constructor: it is the only construction
 * form both binding legs accept. A raw JS array argument to those constructors
 * is not supported by the static leg (the generated ctor table finds no
 * overload and returns an instance with no bound Variant), and element-wise
 * push_back crashes the dynamic leg; neither is a property of the operators
 * under test, so the suite stays on the shared form. Operators on empty
 * containers still exercise the full call path and return type.
 */
const RECEIVER: Record<string, () => object> = {
    AABB: () => new AABB(),
    Array: () => new GArray(),
    Basis: () => new Basis(),
    Callable: () => new Callable(),
    Color: () => new Color(0.5, 0.5, 0.5, 1),
    Dictionary: () => new GDictionary(),
    NodePath: () => new NodePath("x"),
    PackedByteArray: () => new PackedByteArray(),
    PackedColorArray: () => new PackedColorArray(),
    PackedFloat32Array: () => new PackedFloat32Array(),
    PackedFloat64Array: () => new PackedFloat64Array(),
    PackedInt32Array: () => new PackedInt32Array(),
    PackedInt64Array: () => new PackedInt64Array(),
    PackedStringArray: () => new PackedStringArray(),
    PackedVector2Array: () => new PackedVector2Array(),
    PackedVector3Array: () => new PackedVector3Array(),
    PackedVector4Array: () => new PackedVector4Array(),
    Plane: () => new Plane(),
    Projection: () => new Projection(),
    Quaternion: () => new Quaternion(),
    RID: () => new RID(),
    Rect2: () => new Rect2(1.5, 2.5, 3.5, 4.5),
    Rect2i: () => new Rect2i(1, 2, 3, 4),
    Signal: () => new Signal(),
    Transform2D: () => new Transform2D(),
    Transform3D: () => new Transform3D(),
    Vector2: () => new Vector2(1.5, 2.5),
    Vector2i: () => new Vector2i(1, 2),
    Vector3: () => new Vector3(1.5, 2.5, 3.5),
    Vector3i: () => new Vector3i(1, 2, 3),
    Vector4: () => new Vector4(1.5, 2.5, 3.5, 4.5),
    Vector4i: () => new Vector4i(1, 2, 3, 4),
};

/** Fresh right operand per Variant type name (dispatch-table case label). */
const RIGHT: Record<string, () => unknown> = {
    AABB: () => new AABB(),
    ARRAY: () => new GArray(),
    BASIS: () => new Basis(),
    BOOL: () => true,
    CALLABLE: () => new Callable(),
    COLOR: () => new Color(0.5, 0.5, 0.5, 1),
    DICTIONARY: () => new GDictionary(),
    FLOAT: () => 2.5,
    INT: () => 2,
    NIL: () => null,
    NODE_PATH: () => new NodePath("x"),
    OBJECT: () => new Node(),
    PACKED_BYTE_ARRAY: () => new PackedByteArray(),
    PACKED_COLOR_ARRAY: () => new PackedColorArray(),
    PACKED_FLOAT32_ARRAY: () => new PackedFloat32Array(),
    PACKED_FLOAT64_ARRAY: () => new PackedFloat64Array(),
    PACKED_INT32_ARRAY: () => new PackedInt32Array(),
    PACKED_INT64_ARRAY: () => new PackedInt64Array(),
    PACKED_STRING_ARRAY: () => new PackedStringArray(),
    PACKED_VECTOR2_ARRAY: () => new PackedVector2Array(),
    PACKED_VECTOR3_ARRAY: () => new PackedVector3Array(),
    PACKED_VECTOR4_ARRAY: () => new PackedVector4Array(),
    PLANE: () => new Plane(),
    PROJECTION: () => new Projection(),
    QUATERNION: () => new Quaternion(),
    RECT2: () => new Rect2(1.5, 2.5, 3.5, 4.5),
    RECT2I: () => new Rect2i(1, 2, 3, 4),
    RID: () => new RID(),
    SIGNAL: () => new Signal(),
    STRING: () => "2",
    STRING_NAME: () => "x",
    TRANSFORM2D: () => new Transform2D(),
    TRANSFORM3D: () => new Transform3D(),
    VECTOR2: () => new Vector2(1.5, 2.5),
    VECTOR2I: () => new Vector2i(1, 2),
    VECTOR3: () => new Vector3(1.5, 2.5, 3.5),
    VECTOR3I: () => new Vector3i(1, 2, 3),
    VECTOR4: () => new Vector4(1.5, 2.5, 3.5, 4.5),
    VECTOR4I: () => new Vector4i(1, 2, 3, 4),
};


function fail(context: string, detail: string): void {
    reportTestFailure(context, detail);
}

/**
 * Dynamic member invocation `receiver[op](right)`.
 *
 * The bound classes have no index signature, so a lookup table of them cannot
 * be keyed by string without a cast. `Reflect.get`/`Reflect.apply` are the
 * type-safe standard-library route: no `as any` / `as unknown` suppression.
 */
function callOp(receiver: object, op: string, right: unknown): unknown {
    const fn: unknown = Reflect.get(receiver, op);
    if (typeof fn !== "function") {
        throw new Error(`member '${op}' is not a function`);
    }
    return Reflect.apply(fn, receiver, [right]);
}

/** Runs one phase; a throw must surface as a failure, never escape _ready. */
function section(name: string, fn: () => void): void {
    try {
        fn();
    } catch (e) {
        reportTestFailure(name, e);
    }
}

/** Component-wise exact comparison (no dependency on bound helper methods). */
function vecEq(v: object, comps: number[]): boolean {
    const keys = ["x", "y", "z", "w"];
    for (let i = 0; i < comps.length; ++i) {
        if (Reflect.get(v, keys[i]) !== comps[i]) {
            return false;
        }
    }
    return true;
}

/** Strict for scalars, instanceof for wrapper returns. */
function resultMatches(kind: string, value: unknown): boolean {
    switch (kind) {
        case "bool":
            return typeof value === "boolean";
        case "number":
            return typeof value === "number";
        case "string":
            return typeof value === "string";
        case "variant":
            return true;
        case "object":
            return value !== null && typeof value === "object";
        default:
            if (kind.startsWith("class:")) {
                const ctor = CTOR[kind.slice(6)];
                return ctor !== undefined && value !== null && typeof value === "object" && value instanceof ctor;
            }
            return false;
    }
}

/** Shape: member form exists, static form is gone. */
function checkShape(): { methods: number; unary: number } {
    let methods = 0;
    let unary = 0;
    for (const cls of Object.keys(OP_METHODS)) {
        const ctor: Function = CTOR[cls];
        const receiver = RECEIVER[cls]();
        const proto = Object.getPrototypeOf(receiver);
        const spec = OP_METHODS[cls];

        for (const op of spec.bin) {
            methods += 1;
            if (typeof Reflect.get(proto, op) !== "function") {
                fail(`shape ${cls}.${op}`, "member method missing on the prototype");
            }
            if (Reflect.get(ctor, op) !== undefined) {
                fail(`shape ${cls}.${op}`, "static form still present (must be instance-only)");
            }
        }
        for (const op of spec.un) {
            methods += 1;
            unary += 1;
            if (typeof Reflect.get(proto, op) !== "function") {
                fail(`shape ${cls}.${op}`, "member method missing on the prototype");
            }
            if (Reflect.get(ctor, op) !== undefined) {
                fail(`shape ${cls}.${op}`, "static form still present (must be instance-only)");
            }
        }
    }
    return { methods, unary };
}

/** Completeness guard: the declared surface must match what is found at runtime. */
function checkCompleteness(seenMethods: number, seenUnary: number): void {
    if (seenMethods !== EXPECTED_METHODS) {
        fail("completeness methods", `expected ${EXPECTED_METHODS}, saw ${seenMethods}`);
    }
    if (seenUnary !== EXPECTED_UNARY) {
        fail("completeness unary", `expected ${EXPECTED_UNARY}, saw ${seenUnary}`);
    }
    if (EXPECTED_METHODS - EXPECTED_UNARY !== EXPECTED_BINARY) {
        fail("completeness binary", "generated table is internally inconsistent");
    }
    // The combination count is the coverage contract; assert it rather than
    // only printing it, so a dropped row cannot pass silently.
    if (OP_CALLS.length !== EXPECTED_CALLS) {
        fail("completeness calls", `expected ${EXPECTED_CALLS}, saw ${OP_CALLS.length}`);
    }
    // Compare the runtime prototype surface against the declared set, both
    // directions: a missing member or an undeclared extra both fail. This is
    // the drift guard -- editing the generator without regenerating the test
    // (or vice versa) is caught here.
    for (const cls of Object.keys(OP_METHODS)) {
        const receiver = RECEIVER[cls]();
        const found = new Set<string>();
        for (const name of Object.getOwnPropertyNames(Object.getPrototypeOf(receiver))) {
            if (name.startsWith("OP_")) {
                found.add(name);
            }
        }
        const declared = new Set<string>([...OP_METHODS[cls].bin, ...OP_METHODS[cls].un]);
        for (const name of declared) {
            if (!found.has(name)) {
                fail(`completeness ${cls}.${name}`, "declared but not bound");
            }
        }
        for (const name of found) {
            if (!declared.has(name)) {
                fail(`completeness ${cls}.${name}`, "bound but not declared in the extracted table");
            }
        }
    }
    // Every binary method must be exercised by at least one call row.
    const exercised = new Set<string>();
    for (const call of OP_CALLS) {
        exercised.add(`${call.c}.${call.o}`);
    }
    for (const cls of Object.keys(OP_METHODS)) {
        for (const op of OP_METHODS[cls].bin) {
            if (!exercised.has(`${cls}.${op}`)) {
                fail(`completeness ${cls}.${op}`, "binary method has no call row");
            }
        }
    }
}

/** Contract: every (left, op, right) combination invokes and returns the declared kind. */
function checkCalls(): void {
    for (const call of OP_CALLS) {
        const context = `call ${call.c}.${call.o}(${call.r})`;
        const receiver = RECEIVER[call.c]();
        const right: unknown = RIGHT[call.r]();
        let result: unknown;
        try {
            result = callOp(receiver, call.o, right);
        } catch (e) {
            fail(context, `threw: ${String(e)}`);
            continue;
        }
        if (!resultMatches(call.k, result)) {
            fail(context, `return kind mismatch: expected ${call.k}, got ${typeof result}`);
        }
    }
}

/** Invariants that hold for every operand type, independent of the evaluator. */
function checkInvariants(): void {
    for (const cls of Object.keys(OP_METHODS)) {
        const spec = OP_METHODS[cls];
        if (!spec.bin.includes("OP_EQUAL")) {
            continue;
        }
        const a = RECEIVER[cls]();
        const b = RECEIVER[cls]();
        if (callOp(a, "OP_EQUAL", a) !== true) {
            fail(`invariant ${cls}.OP_EQUAL(self)`, "not true");
        }
        if (callOp(a, "OP_NOT_EQUAL", a) !== false) {
            fail(`invariant ${cls}.OP_NOT_EQUAL(self)`, "not false");
        }
        if (typeof callOp(a, "OP_EQUAL", b) !== "boolean") {
            fail(`invariant ${cls}.OP_EQUAL(other)`, "not boolean");
        }
        // Comparison antisymmetry, where both directions exist.
        if (spec.bin.includes("OP_LESS") && spec.bin.includes("OP_GREATER")) {
            const ab = callOp(a, "OP_LESS", b);
            const ba = callOp(b, "OP_GREATER", a);
            if (ab !== ba) {
                fail(`invariant ${cls} antisymmetry`, `LESS=${ab} GREATER=${ba}`);
            }
        }
    }
}

/** Boundary: the null/undefined short-circuit on ==/!= must not reach the evaluator. */
function checkBoundaries(): void {
    for (const cls of Object.keys(OP_METHODS)) {
        const spec = OP_METHODS[cls];
        if (!spec.bin.includes("OP_EQUAL")) {
            continue;
        }
        const a = RECEIVER[cls]();
        if (callOp(a, "OP_EQUAL", null) !== false) {
            fail(`boundary ${cls}.OP_EQUAL(null)`, "expected false");
        }
        if (callOp(a, "OP_NOT_EQUAL", undefined) !== true) {
            fail(`boundary ${cls}.OP_NOT_EQUAL(undefined)`, "expected true");
        }
    }
    // Exact values for a representative, well-understood subset. Component
    // comparison avoids depending on any other bound helper method.
    if (!vecEq(new Vector2(1, 2).OP_ADD(new Vector2(3, 4)), [4, 6])) {
        fail("exact Vector2.OP_ADD", "expected (4, 6)");
    }
    if (!vecEq(new Vector2(1, -2).OP_NEGATE(), [-1, 2])) {
        fail("exact Vector2.OP_NEGATE", "expected (-1, 2)");
    }
    if (!vecEq(new Vector2(2, 3).OP_MULTIPLY(new Vector2(4, 5)), [8, 15])) {
        fail("exact Vector2.OP_MULTIPLY", "expected (8, 15)");
    }
    if (!vecEq(new Vector2(2, 3).OP_MULTIPLY(2), [4, 6])) {
        fail("exact Vector2.OP_MULTIPLY(int)", "expected (4, 6)");
    }
    if (!vecEq(new Vector3(1, 2, 3).OP_ADD(new Vector3(4, 5, 6)), [5, 7, 9])) {
        fail("exact Vector3.OP_ADD", "expected (5, 7, 9)");
    }
    if (new Vector2(1, 2).OP_EQUAL(new Vector2(1, 2)) !== true) {
        fail("exact Vector2.OP_EQUAL", "expected true");
    }
    if (new Vector2(1, 2).OP_LESS(new Vector2(2, 3)) !== true) {
        fail("exact Vector2.OP_LESS", "expected true");
    }
    if (new Vector2(1, 2).OP_NOT() !== false) {
        fail("exact Vector2.OP_NOT", "expected false");
    }
    if (new Vector2(0, 0).OP_NOT() !== true) {
        fail("exact Vector2.OP_NOT(zero)", "expected true");
    }
}

export default class TestOperators extends Node {
    _ready(): void {
        // Every phase goes through section(): a throw must surface as a
        // failure. An uncaught throw here would escape _ready and the suite
        // would still print the COMPLETED sentinel (see test/index.md).
        let sawMethods = 0;
        let sawUnary = 0;
        section("shape", () => {
            const counted = checkShape();
            sawMethods = counted.methods;
            sawUnary = counted.unary;
        });
        section("completeness", () => checkCompleteness(sawMethods, sawUnary));
        section("calls", () => checkCalls());
        section("invariants", () => checkInvariants());
        section("boundaries", () => checkBoundaries());
        console.log(
            `OPERATORS-DIAG methods=${sawMethods} unary=${sawUnary} binary=${sawMethods - sawUnary} calls=${EXPECTED_CALLS}`,
        );
    }
}

