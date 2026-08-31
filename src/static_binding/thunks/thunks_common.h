#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include <cstddef>
#	include <cstdint>
#	include <tuple>
#	include <type_traits>
#	include <utility>

#	include <godot_cpp/variant/string_name.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>
#	include <godot_cpp/variant/variant.hpp>

#	include "runtime/bridge/jsb_type_convert_direct.h"
#	include "runtime/internal/jsb_macros.h"

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
// Return descriptor: numeric hint is the GDExtensionVariantType value
// (0 == NIL == void); RetAny converts without a type hint (json "Variant").
struct RetAny {
	static constexpr bool has_return = true;
};

template <godot::Variant::Type VT_ = godot::Variant::NIL>
struct Ret {
	static constexpr godot::Variant::Type vt = VT_;
	static constexpr bool has_return = VT_ != godot::Variant::NIL;
};

// ---------------------------------------------------------------------------
// Variant::Type -> semantic C++ type (Vector2, double, String, int64_t, ...)
template <godot::Variant::Type VT>
struct VariantNativeType;

// Specializations mirror godot-cpp's PtrToArg<CppT> conversions
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
	using type = double;
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

// Variant::Type -> ptrcall ABI encode type (what engine actually reads/writes)
// This is godot::PtrToArg<CppT>::EncodeT for the corresponding CppT.
template <godot::Variant::Type VT>
using VariantEncodeType = typename godot::PtrToArg<VariantNativeType_t<VT>>::EncodeT;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Return slot type trait: RetAny -> Variant, Ret<VT> -> VariantEncodeType<VT>
template <class RetT>
struct ReturnSlotType;

template <>
struct ReturnSlotType<RetAny> {
	using type = godot::Variant;
};

template <godot::Variant::Type VT>
struct ReturnSlotType<Ret<VT>> {
	using type = VariantEncodeType<VT>;
};

template <class RetT>
using ReturnSlotType_t = typename ReturnSlotType<RetT>::type;

// ---------------------------------------------------------------------------
// Marshaling helpers.

// Produce one strongly-typed value from the JS arguments (conversion,
// default-fill, or error). This is the single conversion entry point shared
// by every thunk shape.
template <class ArgT>
inline bool produce_value(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, int i, typename ArgT::gd_type &out, int provided) {
	if (i < provided) {
		if (!try_js_to_gd(p_isolate, p_context, info[i], out)) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d: got %s", i, TypeConvert::js_debug_typeof(p_isolate, info[i]).utf8().get_data()));
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
inline bool marshal_one(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, int i, typename godot::PtrToArg<typename ArgT::gd_type>::EncodeT &slot, int provided) {
	typename ArgT::gd_type value{};
	if (!produce_value<ArgT>(p_isolate, p_context, info, i, value, provided)) {
		return false;
	}
	godot::PtrToArg<typename ArgT::gd_type>::encode(value, &slot);
	return true;
}

// Untyped tail loop for vararg methods -- the only allowed loop (§4.0-B).
inline bool marshal_tail_args(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, godot::Variant *tail, int from, int provided) {
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
// Ret<VT>   : native encode type of variant type VT (ptrcall ABI)
// RetAny    : full Variant (json "Variant" returns)
// Ret<NIL>  : no return at all

// The slot is either the native return representation (utility/builtin
// ptrcall) or a full Variant (object_method_bind_call in class_methods).
template <class RetT, class SlotT>
inline bool translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, SlotT &ret_slot, const v8::FunctionCallbackInfo<v8::Value> &info) {
	if constexpr (!RetT::has_return) {
		return true;
	} else if constexpr (std::is_same_v<SlotT, godot::Variant>) {
		// Variant ABI: the engine wrote a complete Variant.
		v8::Local<v8::Value> jrval;
		if (!TypeConvert::gd_var_to_js(p_isolate, p_context, ret_slot, jrval)) {
			jsb_throw(p_isolate, "failed to translate godot variant to v8 value");
			return false;
		}
		info.GetReturnValue().Set(jrval);
		return true;
	} else if constexpr (RetT::vt != godot::Variant::NIL) {
		// Native slot: decode through the godot-cpp ptrcall contract, then
		// wrap for the JS translation.
		using SlotCppT = VariantNativeType_t<RetT::vt>;
		godot::Variant ret;
		if constexpr (std::is_same_v<SlotCppT, uint8_t>) {
			ret = (bool)godot::PtrToArg<SlotCppT>::convert(&ret_slot);
		} else {
			ret = godot::PtrToArg<SlotCppT>::convert(&ret_slot);
		}
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