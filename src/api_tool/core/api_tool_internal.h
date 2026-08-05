#pragma once

#include "godot_cpp/core/method_ptrcall.hpp"
#include <godot_cpp/variant/variant.hpp>

namespace api_tool {
namespace internal {

static bool double_precision{ false };

// 不需要 PtrToArg<Ref<RefCounted>>，UtilityFunctions 与 内建类的函数都不涉及RefCounted参数与返回值，非内建类的调用全都转换成 Variant 了。
_FORCE_INLINE_ void var_to_arg_ptr(const godot::Variant &p_val, void *r_arg_ptr, godot::Variant::Type p_type = godot::Variant::VARIANT_MAX, const GDExtensionClassMethodArgumentMetadata p_meta = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE) {
	using namespace godot;
	if (p_type >= Variant::VARIANT_MAX) {
		p_type = p_val.get_type();
	}
	switch (p_type) {
		case Variant::Type::NIL: {
			using T = PtrToArg<Variant>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Variant>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::Type::BOOL: {
			using T = PtrToArg<bool>::EncodeT;
			*(T *)r_arg_ptr = false;
			PtrToArg<bool>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::Type::INT: {
			*(int64_t *)r_arg_ptr = 0;
			switch (p_meta) {
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT8:
					PtrToArg<int8_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT16:
					PtrToArg<int16_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT32:
					PtrToArg<int32_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64:
					PtrToArg<int64_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT8:
					PtrToArg<uint8_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT16:
					PtrToArg<uint16_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT32:
					PtrToArg<uint32_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64:
					PtrToArg<uint64_t>::encode(p_val, r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR16:
					PtrToArg<char16_t>::encode(static_cast<int64_t>(p_val), r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR32:
					PtrToArg<char32_t>::encode(static_cast<int64_t>(p_val), r_arg_ptr);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE:
					PtrToArg<int64_t>::encode(p_val, r_arg_ptr);
					break;
				default:
					CRASH_NOW_MSG("Unsupported metadata type: " + itos(p_meta));
					break;
			}
		} break;
		case Variant::Type::FLOAT: {
			*(double *)r_arg_ptr = 0.0;
			if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_DOUBLE) {
				PtrToArg<double>::encode(p_val, r_arg_ptr);
			} else if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_FLOAT) {
				PtrToArg<float>::encode(p_val, r_arg_ptr);
			} else {
				if (double_precision) {
					PtrToArg<double>::encode(p_val, r_arg_ptr);
				} else {
					PtrToArg<float>::encode(p_val, r_arg_ptr);
				}
			}
		} break;
		case Variant::Type::STRING: {
			using T = PtrToArg<String>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<String>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR2: {
			using T = PtrToArg<Vector2>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector2>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR2I: {
			using T = PtrToArg<Vector2i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector2i>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::RECT2: {
			using T = PtrToArg<Rect2>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Rect2>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::RECT2I: {
			using T = PtrToArg<Rect2i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Rect2i>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR3: {
			using T = PtrToArg<Vector3>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector3>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR3I: {
			using T = PtrToArg<Vector3i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector3i>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::TRANSFORM2D: {
			using T = PtrToArg<Transform2D>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Transform2D>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR4: {
			using T = PtrToArg<Vector4>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector4>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::VECTOR4I: {
			using T = PtrToArg<Vector4i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Vector4i>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PLANE: {
			using T = PtrToArg<Plane>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Plane>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::QUATERNION: {
			using T = PtrToArg<Quaternion>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Quaternion>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::AABB: {
			using T = PtrToArg<AABB>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<AABB>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::BASIS: {
			using T = PtrToArg<Basis>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Basis>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::TRANSFORM3D: {
			using T = PtrToArg<Transform3D>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Transform3D>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PROJECTION: {
			using T = PtrToArg<Projection>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Projection>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::COLOR: {
			using T = PtrToArg<Color>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Color>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::STRING_NAME: {
			using T = PtrToArg<StringName>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<StringName>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::NODE_PATH: {
			using T = PtrToArg<NodePath>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<NodePath>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::RID: {
			using T = PtrToArg<RID>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<RID>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::OBJECT: {
			using T = PtrToArg<Object *>::EncodeT;
			*(T *)r_arg_ptr = nullptr;
			PtrToArg<Object *>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::CALLABLE: {
			using T = PtrToArg<Callable>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Callable>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::SIGNAL: {
			using T = PtrToArg<Signal>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Signal>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::DICTIONARY: {
			using T = PtrToArg<Dictionary>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Dictionary>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::ARRAY: {
			using T = PtrToArg<Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_BYTE_ARRAY: {
			using T = PtrToArg<PackedByteArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedByteArray>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_INT32_ARRAY: {
			using T = PtrToArg<PackedInt32Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedInt32Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_INT64_ARRAY: {
			using T = PtrToArg<PackedInt64Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedInt64Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			using T = PtrToArg<PackedFloat32Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedFloat32Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			using T = PtrToArg<PackedFloat64Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedFloat64Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			using T = PtrToArg<PackedStringArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedStringArray>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_VECTOR2_ARRAY: {
			using T = PtrToArg<PackedVector2Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedVector2Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_VECTOR3_ARRAY: {
			using T = PtrToArg<PackedVector3Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedVector3Array>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_COLOR_ARRAY: {
			using T = PtrToArg<PackedColorArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedColorArray>::encode(p_val, r_arg_ptr);
		} break;
		case Variant::PACKED_VECTOR4_ARRAY: {
			using T = PtrToArg<PackedVector4Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
			PtrToArg<PackedVector4Array>::encode(p_val, r_arg_ptr);
		} break;
		default: {
			CRASH_NOW_MSG("Invalid variant type: " + Variant::get_type_name(p_val.get_type()));
		} break;
	};
}

_FORCE_INLINE_ void arg_ptr_to_var(const void *p_val, const godot::Variant::Type p_type, godot::Variant &r_val, const GDExtensionClassMethodArgumentMetadata p_meta = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE) {
	using namespace godot;
	switch (p_type) {
		case Variant::Type::NIL: {
			r_val = PtrToArg<Variant>::convert(p_val);
		} break;
		case Variant::Type::BOOL: {
			r_val = PtrToArg<bool>::convert(p_val);
		} break;
		case Variant::Type::INT: {
			switch (p_meta) {
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT8:
					r_val = PtrToArg<int8_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT16:
					r_val = PtrToArg<int16_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT32:
					r_val = PtrToArg<int32_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64:
					r_val = PtrToArg<int64_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT8:
					r_val = PtrToArg<uint8_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT16:
					r_val = PtrToArg<uint16_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT32:
					r_val = PtrToArg<uint32_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64:
					r_val = PtrToArg<uint64_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR16:
					r_val = PtrToArg<char16_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR32:
					r_val = PtrToArg<char32_t>::convert(p_val);
					break;
				case GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE:
					r_val = PtrToArg<int64_t>::convert(p_val);
					break;
				default:
					CRASH_NOW_MSG("Unsupported metadata type: " + itos(p_meta));
					break;
			}
		} break;
		case Variant::Type::FLOAT: {
			if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_DOUBLE) {
				r_val = PtrToArg<double>::convert(p_val);
			} else if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_FLOAT) {
				r_val = PtrToArg<float>::convert(p_val);
			} else {
				if (double_precision) {
					r_val = PtrToArg<double>::convert(p_val);
				} else {
					r_val = PtrToArg<float>::convert(p_val);
				}
			}
		} break;
		case Variant::Type::STRING: {
			r_val = PtrToArg<String>::convert(p_val);
		} break;
		case Variant::VECTOR2: {
			r_val = PtrToArg<Vector2>::convert(p_val);
		} break;
		case Variant::VECTOR2I: {
			r_val = PtrToArg<Vector2i>::convert(p_val);
		} break;
		case Variant::RECT2: {
			r_val = PtrToArg<Rect2>::convert(p_val);
		} break;
		case Variant::RECT2I: {
			r_val = PtrToArg<Rect2i>::convert(p_val);
		} break;
		case Variant::VECTOR3: {
			r_val = PtrToArg<Vector3>::convert(p_val);
		} break;
		case Variant::VECTOR3I: {
			r_val = PtrToArg<Vector3i>::convert(p_val);
		} break;
		case Variant::TRANSFORM2D: {
			r_val = PtrToArg<Transform2D>::convert(p_val);
		} break;
		case Variant::VECTOR4: {
			r_val = PtrToArg<Vector4>::convert(p_val);
		} break;
		case Variant::VECTOR4I: {
			r_val = PtrToArg<Vector4i>::convert(p_val);
		} break;
		case Variant::PLANE: {
			r_val = PtrToArg<Plane>::convert(p_val);
		} break;
		case Variant::QUATERNION: {
			r_val = PtrToArg<Quaternion>::convert(p_val);
		} break;
		case Variant::AABB: {
			r_val = PtrToArg<AABB>::convert(p_val);
		} break;
		case Variant::BASIS: {
			r_val = PtrToArg<Basis>::convert(p_val);
		} break;
		case Variant::TRANSFORM3D: {
			r_val = PtrToArg<Transform3D>::convert(p_val);
		} break;
		case Variant::PROJECTION: {
			r_val = PtrToArg<Projection>::convert(p_val);
		} break;
		case Variant::COLOR: {
			r_val = PtrToArg<Color>::convert(p_val);
		} break;
		case Variant::STRING_NAME: {
			r_val = PtrToArg<StringName>::convert(p_val);
		} break;
		case Variant::NODE_PATH: {
			r_val = PtrToArg<NodePath>::convert(p_val);
		} break;
		case Variant::RID: {
			r_val = PtrToArg<RID>::convert(p_val);
		} break;
		case Variant::OBJECT: {
			r_val = PtrToArg<Object *>::convert(p_val);
		} break;
		case Variant::CALLABLE: {
			r_val = PtrToArg<Callable>::convert(p_val);
		} break;
		case Variant::SIGNAL: {
			r_val = PtrToArg<Signal>::convert(p_val);
		} break;
		case Variant::DICTIONARY: {
			r_val = PtrToArg<Dictionary>::convert(p_val);
		} break;
		case Variant::ARRAY: {
			r_val = PtrToArg<Array>::convert(p_val);
		} break;
		case Variant::PACKED_BYTE_ARRAY: {
			r_val = PtrToArg<PackedByteArray>::convert(p_val);
		} break;
		case Variant::PACKED_INT32_ARRAY: {
			r_val = PtrToArg<PackedInt32Array>::convert(p_val);
		} break;
		case Variant::PACKED_INT64_ARRAY: {
			r_val = PtrToArg<PackedInt64Array>::convert(p_val);
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			r_val = PtrToArg<PackedFloat32Array>::convert(p_val);
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			r_val = PtrToArg<PackedFloat64Array>::convert(p_val);
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			r_val = PtrToArg<PackedStringArray>::convert(p_val);
		} break;
		case Variant::PACKED_VECTOR2_ARRAY: {
			r_val = PtrToArg<PackedVector2Array>::convert(p_val);
		} break;
		case Variant::PACKED_VECTOR3_ARRAY: {
			r_val = PtrToArg<PackedVector3Array>::convert(p_val);
		} break;
		case Variant::PACKED_COLOR_ARRAY: {
			r_val = PtrToArg<PackedColorArray>::convert(p_val);
		} break;
		case Variant::PACKED_VECTOR4_ARRAY: {
			r_val = PtrToArg<PackedVector4Array>::convert(p_val);
		} break;
		default: {
			CRASH_NOW_MSG("Invalid variant type: " + Variant::get_type_name(p_type));
		} break;
	};
}

_FORCE_INLINE_ void ctor_arg_ptr(void *r_arg_ptr, const godot::Variant::Type p_type) {
	using namespace godot;
	switch (p_type) {
		case Variant::Type::NIL: {
			using T = PtrToArg<Variant>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::Type::BOOL: {
			using T = PtrToArg<bool>::EncodeT;
			*(T *)r_arg_ptr = false;
		} break;
		case Variant::Type::INT: {
			using T = PtrToArg<int64_t>::EncodeT;
			*(T *)r_arg_ptr = 0;
		} break;
		case Variant::Type::FLOAT: {
			using T = PtrToArg<double>::EncodeT;
			*(T *)r_arg_ptr = 0.0;
		} break;
		case Variant::Type::STRING: {
			using T = PtrToArg<String>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR2: {
			using T = PtrToArg<Vector2>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR2I: {
			using T = PtrToArg<Vector2i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::RECT2: {
			using T = PtrToArg<Rect2>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::RECT2I: {
			using T = PtrToArg<Rect2i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR3: {
			using T = PtrToArg<Vector3>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR3I: {
			using T = PtrToArg<Vector3i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::TRANSFORM2D: {
			using T = PtrToArg<Transform2D>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR4: {
			using T = PtrToArg<Vector4>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::VECTOR4I: {
			using T = PtrToArg<Vector4i>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PLANE: {
			using T = PtrToArg<Plane>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::QUATERNION: {
			using T = PtrToArg<Quaternion>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::AABB: {
			using T = PtrToArg<AABB>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::BASIS: {
			using T = PtrToArg<Basis>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::TRANSFORM3D: {
			using T = PtrToArg<Transform3D>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PROJECTION: {
			using T = PtrToArg<Projection>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::COLOR: {
			using T = PtrToArg<Color>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::STRING_NAME: {
			using T = PtrToArg<StringName>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::NODE_PATH: {
			using T = PtrToArg<NodePath>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::RID: {
			using T = PtrToArg<RID>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::OBJECT: {
			using T = PtrToArg<Object *>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::CALLABLE: {
			using T = PtrToArg<Callable>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::SIGNAL: {
			using T = PtrToArg<Signal>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::DICTIONARY: {
			using T = PtrToArg<Dictionary>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::ARRAY: {
			using T = PtrToArg<Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_BYTE_ARRAY: {
			using T = PtrToArg<PackedByteArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_INT32_ARRAY: {
			using T = PtrToArg<PackedInt32Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_INT64_ARRAY: {
			using T = PtrToArg<PackedInt64Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			using T = PtrToArg<PackedFloat32Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			using T = PtrToArg<PackedFloat64Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			using T = PtrToArg<PackedStringArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_VECTOR2_ARRAY: {
			using T = PtrToArg<PackedVector2Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_VECTOR3_ARRAY: {
			using T = PtrToArg<PackedVector3Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_COLOR_ARRAY: {
			using T = PtrToArg<PackedColorArray>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		case Variant::PACKED_VECTOR4_ARRAY: {
			using T = PtrToArg<PackedVector4Array>::EncodeT;
			memnew_placement((T *)r_arg_ptr, T);
		} break;
		default: {
			CRASH_NOW_MSG("Invalid variant type: " + Variant::get_type_name(p_type));
		} break;
	};
}

_FORCE_INLINE_ void dctor_arg_ptr(void *arg_ptr, const godot::Variant::Type p_type) {
	using namespace godot;
	switch (p_type) {
		case godot::Variant::NIL: {
			using T = PtrToArg<Variant>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::STRING: {
			using T = PtrToArg<String>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::STRING_NAME: {
			using T = PtrToArg<StringName>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::NODE_PATH: {
			using T = PtrToArg<NodePath>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::CALLABLE: {
			using T = PtrToArg<Callable>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::SIGNAL: {
			using T = PtrToArg<Signal>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::DICTIONARY: {
			using T = PtrToArg<Dictionary>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::ARRAY: {
			using T = PtrToArg<Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_BYTE_ARRAY: {
			using T = PtrToArg<PackedByteArray>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_INT32_ARRAY: {
			using T = PtrToArg<PackedInt32Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_INT64_ARRAY: {
			using T = PtrToArg<PackedInt64Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_FLOAT32_ARRAY: {
			using T = PtrToArg<PackedFloat32Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_FLOAT64_ARRAY: {
			using T = PtrToArg<PackedFloat64Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_STRING_ARRAY: {
			using T = PtrToArg<PackedStringArray>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_VECTOR2_ARRAY: {
			using T = PtrToArg<PackedStringArray>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_VECTOR3_ARRAY: {
			using T = PtrToArg<PackedVector3Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_COLOR_ARRAY: {
			using T = PtrToArg<PackedColorArray>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		case godot::Variant::PACKED_VECTOR4_ARRAY: {
			using T = PtrToArg<PackedVector4Array>::EncodeT;
			reinterpret_cast<T *>(arg_ptr)->~T();
		} break;
		default: {
			return;
		}
	}
}

static bool has_returns(const godot::MethodInfo &p_info) {
	using namespace godot;
	return (p_info.return_val.type != Variant::NIL) || (p_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
}

} //namespace internal

} //namespace api_tool
