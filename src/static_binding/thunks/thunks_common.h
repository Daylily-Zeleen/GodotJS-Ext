#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include <cstddef>
#	include <cstdint>
#	include <tuple>
#	include <type_traits>
#	include <utility>

#	include <godot_cpp/classes/global_constants.hpp>
#	include <godot_cpp/variant/string_name.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>
#	include <godot_cpp/variant/variant.hpp>
#	include <godot_cpp/variant/variant_internal.hpp>

#	include "bridge/jsb_type_convert_direct.h"
#	include "internal/jsb_macros.h"

namespace jsb::static_binding {

// ---------------------------------------------------------------------------
// C++20 structural string constant usable as a non-type template parameter.
template <size_t N>
struct FixedString {
	char value[N]{};
	static constexpr size_t length = N - 1; // excluding terminator

	constexpr FixedString(const char (&s)[N]) {
		for (size_t i = 0; i < N; ++i) {
			value[i] = s[i];
		}
	}
};

// ---------------------------------------------------------------------------
// Default value for optional parameters, keyed by (C++ type, literal).
// Parsed once per instantiation (magic static); STRING literals are used
// verbatim, everything else goes through str_to_var -- the same rule the
// dynamic path follows (api_tool_parser.cpp).
template <typename T, FixedString Lit>
inline const T &default_as() {
	static const T value = []() -> T {
		if constexpr (std::is_same_v<T, godot::String>) {
			return T(Lit.value);
		} else {
			godot::Variant parsed = godot::UtilityFunctions::str_to_var(Lit.value);
			return parsed;
		}
	}();
	return value;
}

// ---------------------------------------------------------------------------
// Parameter descriptor emitted by the code generator:
//   Arg<godot::Vector2>            required parameter, converted directly to
//                                  the strongly-typed value via try_js_to_gd
//   Arg<int64_t, "-1">             optional, filled from a per-(type,literal)
//                                  default singleton when missing
template <typename T_, FixedString Def = FixedString("")>
struct Arg {
	using gd_type = T_;
	static constexpr bool has_default = Def.length > 0;
	static constexpr FixedString def = Def;
};

// ---------------------------------------------------------------------------
// Return descriptor.
//   RetVoid               json void/nil -- no return value
//   RetAny                json "Variant" / typedarray (no static type hint)
//   Ret<CppT>             typed return carrying the C++ semantic type the
//                         codegen resolved from the api json: class methods
//                         follow the argument metadata (float/double,
//                         int8..uint64), builtin/utility follow godot-cpp
//                         conventions (float -> real_t, int -> int64_t).
struct RetVoid {
	static constexpr bool has_return = false;
	// unused (has_return == false); keeps ReturnEncodeType well-formed
	using cpp_type = godot::Variant;
};

struct RetAny {
	static constexpr bool has_return = true;
	using cpp_type = godot::Variant;
};

template <typename CppT_, uint64_t Usage_ = 0>
struct Ret {
	using cpp_type = CppT_;
	static constexpr uint64_t usage = Usage_;
	static constexpr bool has_return = true;
};

// ---------------------------------------------------------------------------
// Semantic C++ type of a Variant-typed value (member slots, storage).
// FLOAT follows the engine: real_t (double by default, float with
// REAL_T_IS_DOUBLE undefined -- see godot_cpp/core/math_defs.hpp).
// INT follows the engine Variant storage: int64_t.
template <godot::Variant::Type VT>
struct VariantNativeType;

template <>
struct VariantNativeType<godot::Variant::NIL> {
	using type = godot::Variant;
};
template <>
struct VariantNativeType<godot::Variant::BOOL> {
	using type = uint8_t;
};
template <>
struct VariantNativeType<godot::Variant::INT> {
	using type = int64_t;
};
template <>
struct VariantNativeType<godot::Variant::FLOAT> {
	using type = godot::real_t;
};
template <>
struct VariantNativeType<godot::Variant::STRING> {
	using type = godot::String;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR2> {
	using type = godot::Vector2;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR2I> {
	using type = godot::Vector2i;
};
template <>
struct VariantNativeType<godot::Variant::RECT2> {
	using type = godot::Rect2;
};
template <>
struct VariantNativeType<godot::Variant::RECT2I> {
	using type = godot::Rect2i;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR3> {
	using type = godot::Vector3;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR3I> {
	using type = godot::Vector3i;
};
template <>
struct VariantNativeType<godot::Variant::TRANSFORM2D> {
	using type = godot::Transform2D;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR4> {
	using type = godot::Vector4;
};
template <>
struct VariantNativeType<godot::Variant::VECTOR4I> {
	using type = godot::Vector4i;
};
template <>
struct VariantNativeType<godot::Variant::PLANE> {
	using type = godot::Plane;
};
template <>
struct VariantNativeType<godot::Variant::QUATERNION> {
	using type = godot::Quaternion;
};
template <>
struct VariantNativeType<godot::Variant::AABB> {
	using type = godot::AABB;
};
template <>
struct VariantNativeType<godot::Variant::BASIS> {
	using type = godot::Basis;
};
template <>
struct VariantNativeType<godot::Variant::TRANSFORM3D> {
	using type = godot::Transform3D;
};
template <>
struct VariantNativeType<godot::Variant::PROJECTION> {
	using type = godot::Projection;
};
template <>
struct VariantNativeType<godot::Variant::COLOR> {
	using type = godot::Color;
};
template <>
struct VariantNativeType<godot::Variant::STRING_NAME> {
	using type = godot::StringName;
};
template <>
struct VariantNativeType<godot::Variant::NODE_PATH> {
	using type = godot::NodePath;
};
template <>
struct VariantNativeType<godot::Variant::RID> {
	using type = godot::RID;
};
template <>
struct VariantNativeType<godot::Variant::OBJECT> {
	using type = godot::Object *;
};
template <>
struct VariantNativeType<godot::Variant::CALLABLE> {
	using type = godot::Callable;
};
template <>
struct VariantNativeType<godot::Variant::SIGNAL> {
	using type = godot::Signal;
};
template <>
struct VariantNativeType<godot::Variant::DICTIONARY> {
	using type = godot::Dictionary;
};
template <>
struct VariantNativeType<godot::Variant::ARRAY> {
	using type = godot::Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_BYTE_ARRAY> {
	using type = godot::PackedByteArray;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_INT32_ARRAY> {
	using type = godot::PackedInt32Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_INT64_ARRAY> {
	using type = godot::PackedInt64Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_FLOAT32_ARRAY> {
	using type = godot::PackedFloat32Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_FLOAT64_ARRAY> {
	using type = godot::PackedFloat64Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_STRING_ARRAY> {
	using type = godot::PackedStringArray;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_VECTOR2_ARRAY> {
	using type = godot::PackedVector2Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_VECTOR3_ARRAY> {
	using type = godot::PackedVector3Array;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_COLOR_ARRAY> {
	using type = godot::PackedColorArray;
};
template <>
struct VariantNativeType<godot::Variant::PACKED_VECTOR4_ARRAY> {
	using type = godot::PackedVector4Array;
};

template <godot::Variant::Type VT>
using VariantNativeType_t = typename VariantNativeType<VT>::type;

// ---------------------------------------------------------------------------
// ptrcall ENCODE type (what the engine actually reads/writes through
// GDExtensionPtrBuiltInMethod / PtrToArg<T>::EncodeT).
//
// The engine's variant ptrcall ABI widens unconditionally (method_ptrcall.h):
//   bool   -> uint8_t       narrow ints -> int64_t (sign/zero extended)
//   float  -> double        Variant::FLOAT stores double internally
// Only POD math structs (Vector2, Color, ...) and ref-counted handles pass
// through directly. This is INDEPENDENT from the semantic member type above:
// e.g. Vector2::x is a real_t member, but its builtin-method arguments and
// every Variant::FLOAT ptrcall slot are still double.
template <typename CppT>
using VariantEncodeType = typename godot::PtrToArg<CppT>::EncodeT;

// ---------------------------------------------------------------------------
// Compile-time opaque-pointer fetch: VTC is a template parameter, so
// dispatching through VariantInternal::get_opaque_pointer's runtime switch
// would be pure overhead. Mirrors that switch exactly.
template <godot::Variant::Type VTC>
_FORCE_INLINE_ static void *get_opaque_typed(godot::Variant *self) {
	if constexpr (VTC == godot::Variant::NIL) {
		return nullptr;
	} else if constexpr (VTC == godot::Variant::BOOL) {
		return godot::VariantInternal::get_bool(self);
	} else if constexpr (VTC == godot::Variant::INT) {
		return godot::VariantInternal::get_int(self);
	} else if constexpr (VTC == godot::Variant::FLOAT) {
		return godot::VariantInternal::get_float(self);
	} else if constexpr (VTC == godot::Variant::STRING) {
		return godot::VariantInternal::get_string(self);
	} else if constexpr (VTC == godot::Variant::VECTOR2) {
		return godot::VariantInternal::get_vector2(self);
	} else if constexpr (VTC == godot::Variant::VECTOR2I) {
		return godot::VariantInternal::get_vector2i(self);
	} else if constexpr (VTC == godot::Variant::RECT2) {
		return godot::VariantInternal::get_rect2(self);
	} else if constexpr (VTC == godot::Variant::RECT2I) {
		return godot::VariantInternal::get_rect2i(self);
	} else if constexpr (VTC == godot::Variant::VECTOR3) {
		return godot::VariantInternal::get_vector3(self);
	} else if constexpr (VTC == godot::Variant::VECTOR3I) {
		return godot::VariantInternal::get_vector3i(self);
	} else if constexpr (VTC == godot::Variant::TRANSFORM2D) {
		return godot::VariantInternal::get_transform2d(self);
	} else if constexpr (VTC == godot::Variant::VECTOR4) {
		return godot::VariantInternal::get_vector4(self);
	} else if constexpr (VTC == godot::Variant::VECTOR4I) {
		return godot::VariantInternal::get_vector4i(self);
	} else if constexpr (VTC == godot::Variant::PLANE) {
		return godot::VariantInternal::get_plane(self);
	} else if constexpr (VTC == godot::Variant::QUATERNION) {
		return godot::VariantInternal::get_quaternion(self);
	} else if constexpr (VTC == godot::Variant::AABB) {
		return godot::VariantInternal::get_aabb(self);
	} else if constexpr (VTC == godot::Variant::BASIS) {
		return godot::VariantInternal::get_basis(self);
	} else if constexpr (VTC == godot::Variant::TRANSFORM3D) {
		return godot::VariantInternal::get_transform(self);
	} else if constexpr (VTC == godot::Variant::PROJECTION) {
		return godot::VariantInternal::get_projection(self);
	} else if constexpr (VTC == godot::Variant::COLOR) {
		return godot::VariantInternal::get_color(self);
	} else if constexpr (VTC == godot::Variant::STRING_NAME) {
		return godot::VariantInternal::get_string_name(self);
	} else if constexpr (VTC == godot::Variant::NODE_PATH) {
		return godot::VariantInternal::get_node_path(self);
	} else if constexpr (VTC == godot::Variant::RID) {
		return godot::VariantInternal::get_rid(self);
	} else if constexpr (VTC == godot::Variant::OBJECT) {
		return godot::VariantInternal::get_object(self);
	} else if constexpr (VTC == godot::Variant::CALLABLE) {
		return godot::VariantInternal::get_callable(self);
	} else if constexpr (VTC == godot::Variant::SIGNAL) {
		return godot::VariantInternal::get_signal(self);
	} else if constexpr (VTC == godot::Variant::DICTIONARY) {
		return godot::VariantInternal::get_dictionary(self);
	} else if constexpr (VTC == godot::Variant::ARRAY) {
		return godot::VariantInternal::get_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_BYTE_ARRAY) {
		return godot::VariantInternal::get_byte_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_INT32_ARRAY) {
		return godot::VariantInternal::get_int32_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_INT64_ARRAY) {
		return godot::VariantInternal::get_int64_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_FLOAT32_ARRAY) {
		return godot::VariantInternal::get_float32_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_FLOAT64_ARRAY) {
		return godot::VariantInternal::get_float64_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_STRING_ARRAY) {
		return godot::VariantInternal::get_string_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR2_ARRAY) {
		return godot::VariantInternal::get_vector2_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR3_ARRAY) {
		return godot::VariantInternal::get_vector3_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_COLOR_ARRAY) {
		return godot::VariantInternal::get_color_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR4_ARRAY) {
		return godot::VariantInternal::get_vector4_array(self);
	} else {
		static_assert(VTC != godot::Variant::NIL, "unreachable");
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// ptrcall return-slot type: the engine reads/writes the return through
// PtrToArg<CppT>::EncodeT (method_ptrcall.hpp widens narrow ints to int64_t,
// float to double, bool to uint8_t). RetAny decodes from a full Variant slot.
template <class RetT>
using ReturnEncodeType = VariantEncodeType<typename RetT::cpp_type>;

// ---------------------------------------------------------------------------
// Marshaling helpers.

// Produce one strongly-typed value from the JS arguments (conversion,
// default-fill, or error). This is the single conversion entry point shared
// by every thunk shape.
template <class ArgT>
inline bool produce_value(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info, int i,
		typename ArgT::gd_type &out, int provided) {
	if (i < provided) {
		if (!try_js_to_gd(p_isolate, p_context, info[i], out)) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d: got %s", i,
					TypeConvert::js_debug_typeof(p_isolate, info[i]).utf8().get_data()));
			return false;
		}
		return true;
	}
	if constexpr (ArgT::has_default) {
		out = default_as<typename ArgT::gd_type, ArgT::def>();
		return true;
	}
	jsb_throw(p_isolate, jsb_errorf("missing argument %d", i));
	return false;
}

// ptrcall flavor: produce the value and encode it into a raw argument slot
// through godot-cpp's ptrcall contract.
template <class ArgT>
inline bool marshal_one(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info, int i,
		typename godot::PtrToArg<typename ArgT::gd_type>::EncodeT &slot, int provided) {
	typename ArgT::gd_type value{};
	if (!produce_value<ArgT>(p_isolate, p_context, info, i, value, provided)) {
		return false;
	}
	godot::PtrToArg<typename ArgT::gd_type>::encode(value, &slot);
	return true;
}

// Untyped tail loop for vararg methods -- the only allowed loop.
inline bool marshal_tail_args(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info,
		godot::Variant *tail, int from, int provided) {
	for (int i = from; i < provided; ++i) {
		if (!TypeConvert::js_to_gd_var(p_isolate, p_context, info[i], tail[i - from])) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d", i));
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Return value translation.
//   RetVoid               : no return at all
//   class-method ABI      : ReturnBufT is godot::Variant and the engine
//                           wrote a complete Variant (RetT is compile-time
//                           metadata)
//   ptrcall ABI           : ReturnBufT is the raw encode buffer; decode
//                           through PtrToArg<RetT::cpp_type>::convert
template <class RetT, class ReturnBufT>
inline bool translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		ReturnBufT &ret_val, const v8::FunctionCallbackInfo<v8::Value> &info) {
	if constexpr (!RetT::has_return) {
		return true;
	} else if constexpr (std::is_same_v<ReturnBufT, godot::Variant>) {
		v8::Local<v8::Value> jrval;
		if (!TypeConvert::gd_var_to_js(p_isolate, p_context, ret_val, jrval)) {
			jsb_throw(p_isolate, "failed to translate godot variant to v8 value");
			return false;
		}
		info.GetReturnValue().Set(jrval);
		return true;
	} else {
		godot::Variant ret = godot::PtrToArg<typename RetT::cpp_type>::convert(&ret_val);
		v8::Local<v8::Value> jrval;
		if (!TypeConvert::gd_var_to_js(p_isolate, p_context, ret, jrval)) {
			jsb_throw(p_isolate, "failed to translate godot variant to v8 value");
			return false;
		}
		info.GetReturnValue().Set(jrval);
		return true;
	}
}

// Per-instantiation lazy native-pointer cache (magic static, thread-safe on
// first call). Returns nullptr on engine/generated-tables version mismatch.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit>
GDExtensionPtrBuiltInMethod resolve_builtin_method() {
	static GDExtensionPtrBuiltInMethod fn = [&] {
		const godot::StringName method_name(NameLit.value);
		return ::godot::gdextension_interface::variant_get_ptr_builtin_method(
				(GDExtensionVariantType)VTC,
				method_name._native_ptr(),
				(GDExtensionInt)HashC);
	}();
	return fn;
}

template <uint32_t HashC, FixedString NameLit>
GDExtensionPtrUtilityFunction resolve_utility_function() {
	static GDExtensionPtrUtilityFunction fn = [&] {
		const godot::StringName function_name(NameLit.value);
		return ::godot::gdextension_interface::variant_get_ptr_utility_function(
				function_name._native_ptr(),
				(GDExtensionInt)HashC);
	}();
	return fn;
}

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS