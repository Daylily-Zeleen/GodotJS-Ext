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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
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
 * @brief 类函数的默认值
 *
 * @tparam Ctor 默认值构造器（如 make<godot::Vector2, 0, 1> / make_str<Lit>）；只在首次 get()/get_encoded_ptr() 时调用（magic static 懒初始化），因此扩展 dll 静态初始化期不要求 godot 侧接口就绪
 * @tparam **ExtraIdentifier** 额外的形参用于显示实例化，用于防止引用类型默认值出现潜在的互相干扰。
 */
template <auto Ctor, typename ExtraIdentifierT = void, typename T = std::invoke_result_t<decltype(Ctor)>, std::enable_if_t<!std::is_same_v<T, void>> *_dummy = nullptr>
struct Def {
	using EncodedT = typename godot::PtrToArg<T>::EncodeT;
	using type = T;

	template <typename IdT>
	using rebind = Def<Ctor, IdT>;

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
// Parameter information packs emitted by the code generator. A thunk's
// explicit template argument list cannot disambiguate two trailing parameter
// packs, so each is wrapped in ONE class template argument:
//   Args<Variant, String, int>          the FULL parameter type list
//                                       (required first, optional tail
//                                       contiguous)
//   Defs<Def<make<int64_t, 0>>,         default-value descriptors for the
//         Def<make<godot::String>>,     LAST sizeof...(Ds) parameters
//         Def<make<godot::Color, 1, 1, 1, 1>>>
//                                       (M = N - sizeof...(Ds)); each
//                                       descriptor carries one nullary Ctor
//                                       (compile-time argument list),
//                                       lazily invoked once per slot -- no
//                                       str_to_var anywhere
template <typename... Ts>
struct Args {
	using tuple = std::tuple<Ts...>;
	using encode_slots = std::tuple<typename godot::PtrToArg<Ts>::EncodeT...>;
};

template <typename... Ds>
struct Defs {
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

} //namespace internal

// Return descriptor (the codegen emits Ret<void> / Ret<godot::Variant> /
// Ret<CppT>).
//   type         : the semantic C++ return type resolved from the api json
//   encoded_type : the ptrcall return-slot layout, PtrToArg<T>::EncodeT.
//                  Builtin/utility thunks declare their return buffer as
//                  this type (the engine writes the EncodeT layout into
//                  it); the class family is the exception --
//                  object_method_bind_call always writes a complete
//                  Variant, so class thunks use a plain godot::Variant
//                  slot and RetT is compile-time metadata only.
template <typename T>
struct Ret {
	static constexpr bool has_return = !std::is_same_v<T, void>;
	using type = T;
	using encoded_type = VariantEncodeType<T>;

	// ReturnBufT is the caller's actual slot type (deduced at the call
	// site): godot::Variant for class thunks and Ret<godot::Variant> (the
	// engine wrote a complete Variant there), typename RetT::encoded_type
	// for builtin/utility ptrcall thunks (raw EncodeT buffer, decoded
	// through the ptrcall contract).
	template <class ReturnBufT>
	static _FORCE_INLINE_ void translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
			ReturnBufT &p_ret_val, const v8::FunctionCallbackInfo<v8::Value> &p_info) {
		if constexpr (has_return) {
			if constexpr (std::is_same_v<ReturnBufT, godot::Variant>) {
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
// Probe the runtime Variant type of a JS argument/value. Shared by every
// thunk shape (operator dispatch, builtin ctor overload selection, error
// reporting): a JS value maps to a godot Variant type via the engine's
// INT/FLOAT split (IsInt32 and IsNumber are mutually exclusive), JS-native
// scalars (int/float/bool/String), null/undefined -> NIL, and a godot wrapper
// Object exposed through the internal-field pointer. VARIANT_MAX means "not a
// godot type" (no thunk can match). The `L` template param is legacy and
// unused -- callers pass probe_vt<godot::Variant>(val).
constexpr bool probe_prefer_primitive_types = true;
constexpr bool probe_prefer_object_types = false;

template <bool probe_prefer = probe_prefer_primitive_types>
Variant::Type probe_vt(const v8::Local<v8::Value> &val) {
	if constexpr (probe_prefer == probe_prefer_object_types) {
		if (val->IsObject()) {
			const v8::Local<v8::Object> obj = val.As<v8::Object>();
			if (obj->InternalFieldCount() == IF_VariantFieldCount) {
				return ((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type();
			}
			if (obj->InternalFieldCount() == IF_ObjectFieldCount) return Variant::OBJECT;
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
			if (obj->InternalFieldCount() == IF_VariantFieldCount) {
				return ((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type();
			}
			if (obj->InternalFieldCount() == IF_ObjectFieldCount) return Variant::OBJECT;
		}
	}

	return Variant::VARIANT_MAX;
}

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
// Marshaling helpers.

// Produce one strongly-typed value from the JS arguments (conversion or
// error). This is the single conversion entry point shared by every thunk
// shape. Callers marshal only caller-provided positions (i < provided);
// reaching here with i >= provided is a missing REQUIRED argument.
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

// Variant-slot flavor: typed conversion into a local typed value first, then
// convert-assign into the Variant slot. godot-cpp's Variant(T) constructors
// always copy on the engine side (from_type_constructor takes the native
// pointer), so move semantics change nothing here; the point of this flavor
// is that the slot itself is a plain RAII Variant and needs no hand-rolled
// destruction on failure paths.
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
// Pre-encoded default argument slot for ptrcall thunks (§4.0-A): one EncodeT
// per (method, position), magic-static initialized on first use through the
// Def descriptor's nullary Ctor (make<>/make_str<>) + PtrToArg::encode --
// the same single conversion the provided positions go through, paid once
// instead of per call. The Ctor only runs on first use, so engine-side hooks
// are guaranteed ready (no static-init-order dependency).
//
// Referential defaults (Array/Dictionary) additionally key the slot with an
// ExtraIdentifier (position + the two signature packs) so every occurrence
// is isolated (OQ3): a callee mutating the default value in place never
// leaks into another method sharing the same (type, literal) pair. Value
// defaults (IdentifierT == void) keep the slot in the emitted Defs member
// instantiation itself.
//
// Only instantiated for optional positions (I >= M) by the callers' split
// wiring folds; a required position reaching here is a codegen bug and
// trips the static_assert.
template <std::size_t I, int M, class AllArgsT, class DefsT>
_FORCE_INLINE_ void *default_arg_slot() {
	static_assert(I >= (std::size_t)M, "default_arg_slot instantiated for a required position");
	using DefT = std::tuple_element_t<I - (std::size_t)M, typename DefsT::tuple>;
	using IdentifierT = std::conditional_t<
			GDReferentialBuiltinType<typename DefT::type>,
			ExtraIdentifier<std::integral_constant<std::size_t, I>, AllArgsT, DefsT>,
			void>;
	using ReboundDefT = typename DefT::template rebind<IdentifierT>;
	return ReboundDefT::get_encoded_ptr();
}
} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS