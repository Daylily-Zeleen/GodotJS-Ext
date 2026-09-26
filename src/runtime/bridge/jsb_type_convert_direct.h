/************************************************************************/
/*  jsb_type_convert_direct.h                                           */
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

#include <limits>
#include <type_traits>

// Direct JS value -> strongly-typed Godot value converters (design doc §3,
// Level-1). Static-binding thunks call these directly, so parameters never
// materialize as Variant; TypeConvert::js_to_gd_var delegates here as well,
// keeping one conversion source of truth for both binding paths.
//
// Conversion semantics mirror the corresponding cases inside
// TypeConvert::js_to_gd_var (jsb_type_convert.cpp) so both paths behave
// identically.

#include "jsb_bridge_pch.h"
#include "jsb_class_info.h"
#include "jsb_environment.h"
#include "jsb_object_handle.h"
#include "jsb_type_convert.h"

namespace jsb {

// unwrap JS Proxy before any conversion (same as js_to_gd_var's prologue)
inline bool js_unwrap_proxy(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_val, v8::Local<v8::Value> &r_unwrapped) {
#if JSB_WITH_V8
	if (p_val->IsProxy())
#else
	if (p_val->IsObject())
#endif
	{
		v8::Local<v8::Object> object = p_val.As<v8::Object>();
		v8::MaybeLocal<v8::Value> maybe_target =
				object->Get(p_context, Environment::wrap(p_isolate)->get_symbol(Symbols::ProxyTarget));
		v8::Local<v8::Value> target;
		if (maybe_target.ToLocal(&target) && !target->IsUndefined()) {
			r_unwrapped = target;
			return true;
		}
	}
	return false;
}

template <typename T>
struct JSToGD;

// untyped: delegate to the dynamic converter (used for vararg tails and
// json-typed "Variant" parameters)
template <>
struct JSToGD<godot::Variant> {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, godot::Variant &r_out) {
		return TypeConvert::js_to_gd_var(p_isolate, p_context, p_jval, r_out);
	}
};

// A JS boolean belongs to the engine's INT and FLOAT surfaces
// (`Variant::can_convert_strict`: INT = {BOOL, FLOAT, NIL}, FLOAT = {BOOL, INT,
// NIL}) and `can_be_converted_from<INT/FLOAT>` in type_compatible.h mirrors that
// table. So the marshallers have to take it as well -- otherwise the overload
// filter picks a numeric constructor for `true` and the marshaller then rejects
// it, which is the "selected, then bad argument N" failure that the
// predicate/marshaller contract exists to prevent (cf. the historical
// `new Color("abc")` incident in type_compatible.h).
inline bool js_bool_as_number(const v8::Local<v8::Value> &p_val, double &r_out) {
	if (!p_val->IsBoolean()) {
		return false;
	}
	// No isolate: the metadata-less `StaticBindingUtil` overloads have none to
	// pass, and `Boolean::Value()` needs none either.
	r_out = p_val.As<v8::Boolean>()->Value() ? 1.0 : 0.0;
	return true;
}

// Scalars (plus `String` / `StringName`, below, which have their own caching
// path). One primary template with `if constexpr`, mirroring `GDToJS<T>`: the
// fixed-width ladder is the same on both sides, and a new width only has to be
// added here if the engine exposes one.
//
// Each family carries a different acceptance surface, which is why they are
// arms rather than separate structs:
//
//   - `bool` follows `Helper::to_bool` (boolean / number / bigint / null /
//     undefined, and NOT a string), matching the engine's BOOL strict table.
//   - the float slots take a boolean (`Variant::can_convert_strict` FLOAT =
//     {BOOL, INT, NIL}), hence `js_bool_as_number` -- without it the overload
//     predicate in `type_compatible.h` would select a numeric overload that the
//     marshaller then rejects.
//   - the integer slots take a boolean too (INT = {BOOL, FLOAT, NIL}).
//   - the exact-width integers read through int64 and then TRUNCATE, exactly as
//     the engine does; `js_to_fixed_width_int` below carries the measurements.
//
// `uint64_t` deliberately does NOT go through the int64 read: its domain does
// not fit, and the unsigned read is what keeps a RefCounted ObjectID (bit 63
// set) from being rejected for looking negative.
template <typename T>
struct JSToGD {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, T &r_out) {
		if constexpr (std::is_same_v<T, godot::Variant>) {
			// untyped: the dynamic converter (vararg tails, json-typed
			// "Variant" parameters)
			return TypeConvert::js_to_gd_var(p_isolate, p_context, p_jval, r_out);
		} else if constexpr (std::is_same_v<T, bool>) {
			jsb_unused(p_context);
			return impl::Helper::to_bool(p_isolate, p_jval, r_out);
		} else if constexpr (std::is_floating_point_v<T>) {
			jsb_unused(p_isolate);
			jsb_unused(p_context);
			double wide = 0;
			if (!js_bool_as_number(p_jval, wide) && !impl::Helper::to_double(p_jval, wide)) {
				return false;
			}
			// real_t = float by default (math_defs.hpp); builtin/utility float
			// parameters follow godot-cpp's real_t convention. The engine widens
			// the ptrcall slot to double, so the narrowing here mirrors
			// PtrToArg<float>::convert exactly.
			r_out = static_cast<T>(wide);
			return true;
		} else if constexpr (std::is_same_v<T, int64_t>) {
			jsb_unused(p_isolate);
			jsb_unused(p_context);
			// See `js_bool_as_number`: the engine's INT surface includes BOOL,
			// and `can_be_converted_from<INT>` says so, so `true` is 1.
			double as_number = 0;
			if (js_bool_as_number(p_jval, as_number)) {
				r_out = (int64_t)as_number;
				return true;
			}
			return impl::Helper::to_int64(p_jval, r_out);
		} else if constexpr (std::is_same_v<T, uint64_t>) {
			jsb_unused(p_isolate);
			jsb_unused(p_context);
			return impl::Helper::to_uint64(p_jval, r_out);
		} else if constexpr (std::is_integral_v<T>) {
			// Every remaining exact width (int8/16/32, uint8/16/32, char32_t),
			// signed and unsigned alike: read through int64, then truncate the
			// way the engine does, with the same debug diagnostic.
			return js_to_fixed_width_int<T>(p_isolate, p_context, p_jval, r_out);
		} else {
			static_assert(std::is_same_v<T, void>, "no JSToGD arm for this type");
			return false;
		}
	}
};

// Narrow exact-width integer targets: read through int64, then narrow exactly
// the way the engine does.
//
// The engine never range-checks a narrow parameter. `MethodBind::call` reaches
// `call_with_variant_args_helper`, which (in a debug build) only asks
// `Variant::can_convert_strict` -- a Variant *type-category* check that cannot
// see the declared width -- and then converts through `Variant::operator
// int8_t()`, i.e. `static_cast`. It never looks at the value's magnitude:
//
//     put_8(300)  -> writes 44     (engine, measured)
//     put_8(-129) -> writes 127
//     put_u16(70000) -> writes 4464
//     Vector2i(3000000000, -3000000000) -> (-1294967296, 1294967296)
//
// So the project follows it and truncates too. Rejecting instead would make the
// static leg disagree with the dynamic leg (whose class-method path is the
// engine's own conversion) and with plain GDScript -- the very split this work
// is meant to remove.
//
// A debug build warns about the loss, because a silent truncation is easy to
// stare past. The `#if JSB_DEBUG` block compiles to nothing otherwise, so the
// release build pays neither the comparison nor the branch.
template <typename CppT>
inline bool js_to_fixed_width_int(v8::Isolate *p_isolate,
		const v8::Local<v8::Context> &p_context,
		const v8::Local<v8::Value> &p_jval,
		CppT &r_out) {
	int64_t wide = 0;
	if (!JSToGD<int64_t>::convert(p_isolate, p_context, p_jval, wide)) {
		return false;
	}
#if JSB_DEBUG
	// Report what the narrowing is about to discard, before discarding it.
	// `wide != (int64_t)(CppT)wide` is exact for every width here and adds no
	// branch in release (the macro is empty there).
	if (wide != (int64_t)(CppT)wide) {
		// Godot's `String::sprintf` has no `%lld`; int64 goes through `%d` with a
		// cast (the project-wide convention, e.g. jsb_environment.cpp).
		JSB_LOG(Warning, "narrow slot: %d does not fit, truncating to %d", (int)wide, (int)(int64_t)(CppT)wide);
	}
#endif
	r_out = static_cast<CppT>(wide);
	return true;
}

template <>
struct JSToGD<godot::String> {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, godot::String &r_out) {
		jsb_unused(p_context);
		if (!p_jval->IsString()) {
			return false;
		}
		godot::StringName sn;
		if (Environment::wrap(p_isolate)->get_string_name_cache().try_get_string_name(p_isolate, p_jval, sn)) {
			r_out = (godot::String)sn;
			return true;
		}
		r_out = impl::Helper::to_string(p_isolate, p_jval);
		return true;
	}
};

template <>
struct JSToGD<godot::StringName> {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, godot::StringName &r_out) {
		jsb_unused(p_context);
		if (p_jval->IsString()) {
			r_out = Environment::wrap(p_isolate)->get_string_name(p_jval.As<v8::String>());
			return true;
		}
		// same fallback semantics as the dynamic path: accept a variant-backed wrapper
		godot::Variant v;
		if (!JSToGD<godot::Variant>::convert(p_isolate, p_context, p_jval, v)) {
			return false;
		}
		r_out = v;
		return true;
	}
};

template <>
struct JSToGD<godot::NodePath> {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, godot::NodePath &r_out) {
		jsb_unused(p_context);
		if (p_jval->IsString()) {
			godot::StringName sn;
			if (Environment::wrap(p_isolate)->get_string_name_cache().try_get_string_name(p_isolate, p_jval, sn)) {
				r_out = godot::NodePath((godot::String)sn);
				return true;
			}
			r_out = godot::NodePath(impl::Helper::to_string(p_isolate, p_jval));
			return true;
		}
		godot::Variant v;
		if (!JSToGD<godot::Variant>::convert(p_isolate, p_context, p_jval, v)) {
			return false;
		}
		r_out = v;
		return true;
	}
};

template <>
struct JSToGD<godot::Object *> {
	static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, godot::Object *&r_out) {
		jsb_unused(p_context);
		if (!p_jval->IsObject()) {
			// objects are usually nullable
			if (p_jval->IsNullOrUndefined()) {
				r_out = nullptr;
				return true;
			}
			return false;
		}
		const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
		if (!TypeConvert::is_object(self)) {
			return false;
		}
		void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
		r_out = Environment::wrap(p_isolate)->verify_object(pointer) ? (godot::Object *)pointer : nullptr;
		return true;
	}
};

// variant-backed types: the JS wrapper stores a full Variant internally; read
// it and convert to the requested type via Variant's implicit conversion.
template <typename T>
inline bool extract_variant_backed(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, T &r_out) {
	jsb_unused(p_isolate);
	jsb_unused(p_context);
	if (!p_jval->IsObject()) {
		return false;
	}
	const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
	if (!TypeConvert::is_variant(self)) {
		return false;
	}
	void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
	r_out = *(godot::Variant *)pointer;
	return true;
}

#define JSB_DIRECT_VARIANT_BACKED(CppType)                                                                                                         \
	template <>                                                                                                                                    \
	struct JSToGD<CppType> {                                                                                                                       \
		static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, CppType &r_out) { \
			return extract_variant_backed(p_isolate, p_context, p_jval, r_out);                                                                    \
		}                                                                                                                                          \
	};

JSB_DIRECT_VARIANT_BACKED(godot::Vector2)
JSB_DIRECT_VARIANT_BACKED(godot::Vector2i)
JSB_DIRECT_VARIANT_BACKED(godot::Rect2)
JSB_DIRECT_VARIANT_BACKED(godot::Rect2i)
JSB_DIRECT_VARIANT_BACKED(godot::Vector3)
JSB_DIRECT_VARIANT_BACKED(godot::Vector3i)
JSB_DIRECT_VARIANT_BACKED(godot::Transform2D)
JSB_DIRECT_VARIANT_BACKED(godot::Vector4)
JSB_DIRECT_VARIANT_BACKED(godot::Vector4i)
JSB_DIRECT_VARIANT_BACKED(godot::Plane)
JSB_DIRECT_VARIANT_BACKED(godot::Quaternion)
JSB_DIRECT_VARIANT_BACKED(godot::AABB)
JSB_DIRECT_VARIANT_BACKED(godot::Basis)
JSB_DIRECT_VARIANT_BACKED(godot::Transform3D)
JSB_DIRECT_VARIANT_BACKED(godot::Projection)
JSB_DIRECT_VARIANT_BACKED(godot::Color)
JSB_DIRECT_VARIANT_BACKED(godot::RID)
JSB_DIRECT_VARIANT_BACKED(godot::Callable)
JSB_DIRECT_VARIANT_BACKED(godot::Signal)

#undef JSB_DIRECT_VARIANT_BACKED

// Container-family targets: accept BOTH a variant-backed wrapper (Godot
// Array/Dictionary/Packed*) AND raw JS values (native Array, object literal,
// null) by falling back to the full typed dynamic converter. The strict
// variant-backed path above would reject native JS values that the dynamic
// path happily converts (e.g. OS.execute("sh", ["-v"], output)).
#define JSB_DIRECT_CONTAINER(CppType, GDType)                                                                                                      \
	template <>                                                                                                                                    \
	struct JSToGD<CppType> {                                                                                                                       \
		static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, CppType &r_out) { \
			if (extract_variant_backed(p_isolate, p_context, p_jval, r_out)) {                                                                     \
				return true;                                                                                                                       \
			}                                                                                                                                      \
			godot::Variant v;                                                                                                                      \
			if (!TypeConvert::js_to_gd_var(p_isolate, p_context, p_jval, GDType, v)) {                                                             \
				return false;                                                                                                                      \
			}                                                                                                                                      \
			r_out = v;                                                                                                                             \
			return true;                                                                                                                           \
		}                                                                                                                                          \
	};

JSB_DIRECT_CONTAINER(godot::Array, godot::Variant::ARRAY)
JSB_DIRECT_CONTAINER(godot::Dictionary, godot::Variant::DICTIONARY)
JSB_DIRECT_CONTAINER(godot::PackedByteArray, godot::Variant::PACKED_BYTE_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedInt32Array, godot::Variant::PACKED_INT32_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedInt64Array, godot::Variant::PACKED_INT64_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedFloat32Array, godot::Variant::PACKED_FLOAT32_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedFloat64Array, godot::Variant::PACKED_FLOAT64_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedStringArray, godot::Variant::PACKED_STRING_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector2Array, godot::Variant::PACKED_VECTOR2_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector3Array, godot::Variant::PACKED_VECTOR3_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedColorArray, godot::Variant::PACKED_COLOR_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector4Array, godot::Variant::PACKED_VECTOR4_ARRAY)

#undef JSB_DIRECT_CONTAINER

// ---------------------------------------------------------------------------
// Godot -> JS, the mirror of `JSToGD<T>` above. Same shape, same namespace, one
// entry point:
//
//     JSToGD<T>::convert(isolate, context, const Local<Value> &, T &)        -> bool
//     GDToJS<T>::convert(isolate, context, const T &, Local<Value> &)        -> bool
//
// Callers must hand `convert` a value of the DECLARED type. A class MethodBind
// return arrives as a complete `Variant`; `Ret<T>::translate_return` is where
// that difference is normalized away (see thunks_common.h), so no specialization
// here has a second, Variant-taking overload.
//
// The scalar arms read the value directly instead of round-tripping through a
// `Variant`, which is the whole point of the mirror:
//
//   - `Object *` -- `Variant(const Object *)` calls `init_ref()` on a RefCounted
//     (core/variant/variant.cpp), so every object return used to pay a refcount
//     bump and a Variant construction. `Ret<godot::Object *>` is ~719 of the
//     generated returns, the single most common non-trivial one.
//   - `String` / `StringName` -- the Variant wrapper is pure overhead.
//
// Types whose Variant arm builds a JS *wrapper object* (math types, RID,
// NodePath, Callable/Signal, Array/Dictionary/Packed*) have no cheaper exit:
// `TypeConvert::gd_var_to_js` must create that wrapper and bind the Variant to
// it, so there is nothing for a specialization to skip. They fall through to the
// primary template, which is the same code path, not a degraded one.
template <typename T>
struct GDToJS {
	static _FORCE_INLINE_ bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const T &p_src, v8::Local<v8::Value> &r_out) {
		if constexpr (std::is_same_v<T, bool>) {
			r_out = v8::Boolean::New(p_isolate, p_src);
			return true;
		} else if constexpr (std::is_integral_v<T>) {
			// Signedness picks the writer, magnitude picks `Number` vs `BigInt`.
			// Unsigned has to go through the unsigned writer: a value with bit 63
			// set (an ObjectID) would otherwise come back negative.
			if constexpr (std::is_signed_v<T>) {
				r_out = impl::internal::new_integer(p_isolate, (int64_t)p_src);
			} else {
				r_out = impl::internal::new_unsigned_integer(p_isolate, (uint64_t)p_src);
			}
			return true;
		} else if constexpr (std::is_floating_point_v<T>) {
			r_out = v8::Number::New(p_isolate, (double)p_src);
			return true;
		} else if constexpr (std::is_same_v<T, godot::Object *>) {
			if (p_src == nullptr) {
				r_out = v8::Null(p_isolate);
				return true;
			}
			v8::Local<v8::Object> obj;
			if (TypeConvert::gd_obj_to_js(p_isolate, p_context, p_src, obj)) {
				r_out = obj;
				return true;
			}
			return false;
		} else if constexpr (std::is_same_v<T, godot::String>) {
			r_out = impl::Helper::new_string(p_isolate, p_src);
			return true;
		} else if constexpr (std::is_same_v<T, godot::StringName>) {
			r_out = Environment::wrap(p_isolate)->get_string_value(p_src);
			return true;
		} else {
			// No direct arm: build the Variant and let the dynamic writer decide.
			// Every remaining return type needs its wrapper object either way.
			return TypeConvert::gd_var_to_js(p_isolate, p_context, godot::Variant(p_src), r_out);
		}
	}
};

// Guards the scalar contract below: `GDToJS<T>` writes through the numeric
// primitives, which only accept these widths. A new scalar return type would
// have to be routed explicitly rather than silently read as an integer.
static_assert(sizeof(int64_t) == 8 && sizeof(uint64_t) == 8 && sizeof(char32_t) == 4
				&& sizeof(godot::real_t) <= 8,
		"GDToJS assumes the engine's scalar widths");

// A complete `Variant` needs no decoding.
template <>
struct GDToJS<godot::Variant> {
	static _FORCE_INLINE_ bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const godot::Variant &p_src, v8::Local<v8::Value> &r_out) {
		return TypeConvert::gd_var_to_js(p_isolate, p_context, p_src, r_out);
	}
};

// entry point with Proxy unwrapping
template <typename T>
inline bool try_js_to_gd(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_jval, T &r_out) {
	v8::Local<v8::Value> unwrapped;
	if (js_unwrap_proxy(p_isolate, p_context, p_jval, unwrapped)) {
		return try_js_to_gd(p_isolate, p_context, unwrapped, r_out);
	}
	return JSToGD<T>::convert(p_isolate, p_context, p_jval, r_out);
}

} // namespace jsb
