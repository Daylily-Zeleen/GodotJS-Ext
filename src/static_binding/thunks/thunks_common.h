/************************************************************************/
/*  thunks_common.h                                                     */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include <cstddef>
#	include <cstdint>
#	include <tuple>
#	include <type_traits>
#	include <utility>

#	include <godot_cpp/classes/global_constants.hpp>
#	include <godot_cpp/variant/string_name.hpp>
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

template <typename... Arg>
struct ExtraIdentifier {};

/**
 * @brief 懒初始化默认值描述符
 *
 * @tparam Ctor 无参默认值构造器；get() 与不同 EncodeT 的 get_encoded_ptr() 各自懒初始化，避免 DLL 静态初始化期调用引擎接口。
 * @tparam ExtraIdentifierT 区分共享默认值存储的实例身份。
 */
template <auto Ctor, typename ExtraIdentifierT = void, typename T = std::invoke_result_t<decltype(Ctor)>, std::enable_if_t<!std::is_same_v<T, void>> *_dummy = nullptr>
struct DefV {
	using EncodedT = typename godot::PtrToArg<T>::EncodeT;
	using type = T;

	template <typename IdT>
	using rebind = DefV<Ctor, IdT>;

	static T &get() {
		static type value = Ctor();
		return value;
	}

	static void *get_encoded_ptr() {
		if constexpr (std::is_same_v<T, EncodedT>) {
			return &get();
		} else {
			static EncodedT value = [] {
				EncodedT ret;
				godot::PtrToArg<T>::encode(Ctor(), &ret);
				return ret;
			}();
			return &value;
		}
	}
};

template <typename T, auto... Args>
T make() { return T(Args...); }

template <typename T, FixedString Lit, std::enable_if_t<std::is_same_v<T, godot::String> || std::is_same_v<T, godot::StringName>> *_dummy = nullptr>
T make_str() { return T(Lit.value); }

// ---------------------------------------------------------------------------
// Wrap parameter and default-descriptor packs separately so the thunk's
// explicit template arguments can distinguish them. Args lists all parameter
// types; DefVs describes the trailing optional parameters.
template <typename... Ts>
struct Args {
	using tuple = std::tuple<Ts...>;
	using encode_slots = std::tuple<typename godot::PtrToArg<Ts>::EncodeT...>;
};

template <typename... Ds>
struct DefVs {
	static constexpr std::size_t count = sizeof...(Ds);
	using tuple = std::tuple<Ds...>;
};

template <typename T>
using VariantEncodeType = typename godot::PtrToArg<T>::EncodeT;

namespace internal {
template <typename VarT>
static _FORCE_INLINE_ bool translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, VarT &&p_ret_val, const v8::FunctionCallbackInfo<v8::Value> &p_info) {
	v8::Local<v8::Value> jrval;
	if (!TypeConvert::gd_var_to_js(p_isolate, p_context, std::forward<VarT>(p_ret_val), jrval)) {
		jsb_throw(p_isolate, "failed to translate godot variant to v8 value");
		return false;
	}
	p_info.GetReturnValue().Set(jrval);
	return true;
}

// Unsigned 64-bit returns. The signedness lives in the C++ type rather than in
// the value -- `Ret<uint64_t>` and `Ret<int64_t>` are separate instantiations
// (the codegen already emits them separately), so no runtime metadata is
// needed to pick this path. It matters because an ObjectID carries
// `is_ref_counted` in bit 63: written signed it comes back as a negative
// BigInt, and `instance_from_id()` then receives a different id.
static _FORCE_INLINE_ void translate_uint64_return(v8::Isolate *p_isolate, const uint64_t p_val, const v8::FunctionCallbackInfo<v8::Value> &p_info) {
	p_info.GetReturnValue().Set(impl::Helper::new_unsigned_integer(p_isolate, p_val));
}

} //namespace internal

// Return metadata: builtin/utility ptrcalls use PtrToArg<T>::EncodeT slots;
// class MethodBind calls always return a complete Variant.
template <typename T>
struct Ret {
	static constexpr bool has_return = !std::is_same_v<T, void>;
	using type = T;
	using encoded_type = VariantEncodeType<T>;

	// Complete Variant slots need no decoding; other slots use PtrToArg<T>.
	template <class ReturnBufT>
	static _FORCE_INLINE_ void translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, ReturnBufT &p_ret_val, const v8::FunctionCallbackInfo<v8::Value> &p_info) {
		if constexpr (has_return) {
			// A uint64_t slot is unsigned in the type itself, so the value has to
			// leave through the unsigned writer. Routing it through `Variant`
			// would re-read the same bits as int64 and emit a negative BigInt.
			if constexpr (std::is_same_v<type, uint64_t>) {
				if constexpr (std::is_same_v<ReturnBufT, godot::Variant>) {
					internal::translate_uint64_return(p_isolate, (uint64_t)p_ret_val, p_info);
				} else {
					internal::translate_uint64_return(p_isolate, godot::PtrToArg<uint64_t>::convert(&p_ret_val), p_info);
				}
			} else if constexpr (std::is_same_v<ReturnBufT, godot::Variant>) {
				internal::translate_return(p_isolate, p_context, p_ret_val, p_info);
			} else {
				internal::translate_return(p_isolate, p_context, Variant(godot::PtrToArg<type>::convert(&p_ret_val)), p_info);
			}
		}
	}
};

template <>
struct Ret<void> {
	static constexpr bool has_return = false;
	using type = void;
	// unused (has_return == false): builtin/utility thunks still declare a
	// slot of this type, which the engine never writes for void returns
	using encoded_type = godot::Variant;
};

// ---------------------------------------------------------------------------
// C++ types used for builtin storage. FLOAT maps to real_t (double with
// REAL_T_IS_DOUBLE, otherwise float); its ptrcall EncodeT is double.
// INT maps to int64_t.
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
// Probe JS scalars, null/undefined (NIL), and internal-field Godot wrappers.
// IsInt32 is checked before IsNumber, so matching integers map to INT and
// remaining numbers to FLOAT. VARIANT_MAX denotes an unrecognized value.
// The template parameter selects whether wrapper/null checks precede or
// follow scalar checks; binary dispatch uses object-first for its left operand.
constexpr bool probe_prefer_primitive_types = true;
constexpr bool probe_prefer_object_types = false;

template <bool probe_prefer = probe_prefer_primitive_types>
Variant::Type probe_vt(const v8::Local<v8::Value> &val) {
	if constexpr (probe_prefer == probe_prefer_object_types) {
		if (val->IsObject()) {
			const v8::Local<v8::Object> obj = val.As<v8::Object>();
			if (TypeConvert::is_variant(obj)) {
				return ((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type();
			}
			if (TypeConvert::is_object(obj)) return Variant::OBJECT;
		}
		if (val->IsNullOrUndefined()) return Variant::NIL;
	}

	if (val->IsInt32()) return Variant::INT;
	if (val->IsNumber()) return Variant::FLOAT;
	if (val->IsBoolean()) return Variant::BOOL;
	if (val->IsString()) return Variant::STRING;

	if constexpr (probe_prefer == probe_prefer_primitive_types) {
		if (val->IsNullOrUndefined()) return Variant::NIL;
		if (val->IsObject()) {
			const v8::Local<v8::Object> obj = val.As<v8::Object>();
			if (TypeConvert::is_variant(obj)) {
				return ((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type();
			}
			if (TypeConvert::is_object(obj)) return Variant::OBJECT;
		}
	}

	return Variant::VARIANT_MAX;
}

// ---------------------------------------------------------------------------
// Select the VariantInternal accessor at compile time instead of switching
// on the Variant's runtime type. The caller must ensure its type is VTC.
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
// Marshaling helpers.

// Typed conversion shared by produce_variant and marshal_one. Callers supply
// only provided positions; a missing position throws instead of filling a default.
template <class T>
inline bool produce_value(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, int i, T &out, int provided) {
	if (i < provided) {
		if (!try_js_to_gd(p_isolate, p_context, info[i], out)) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d: got %s", i, TypeConvert::js_debug_typeof(p_isolate, info[i])));
			return false;
		}
		return true;
	}
	jsb_throw(p_isolate, jsb_errorf("missing argument %d", i));
	return false;
}

// Convert through a typed local into a RAII Variant slot; the slot is assigned
// only after successful conversion and needs no manual cleanup on failure.
template <class T>
inline bool produce_variant(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, int i, godot::Variant &out, int provided) {
	T value{};
	if (!produce_value<T>(p_isolate, p_context, info, i, value, provided)) {
		return false;
	}
	out = std::move(value);
	return true;
}

// ptrcall flavor: produce the value and encode it into a raw argument slot
// through godot-cpp's ptrcall contract.
template <class T>
inline bool marshal_one(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::FunctionCallbackInfo<v8::Value> &info, int i, typename godot::PtrToArg<T>::EncodeT &slot, int provided) {
	T value{};
	if (!produce_value<T>(p_isolate, p_context, info, i, value, provided)) {
		return false;
	}
	godot::PtrToArg<T>::encode(value, &slot);
	return true;
}

template <typename T>
concept GDReferentialBuiltinType = std::is_base_of_v<godot::Array, T> || std::is_base_of_v<godot::Dictionary, T>;

// ---------------------------------------------------------------------------
// Lazily initialized default storage for optional ptrcall arguments. Engine
// interfaces must be ready at first use, not during DLL static initialization.
// Is for instantiate a independent instance of the default argument.
template <class DefT, typename IdentifierT = void>
_FORCE_INLINE_ void *default_arg_slot() {
	using ReboundDefT = typename DefT::template rebind<IdentifierT>;
	return ReboundDefT::get_encoded_ptr();
}
} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS