/************************************************************************/
/*  jsb_static_binding_util.h                                           */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
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
#include "jsb_bridge_pch.h"
#include "jsb_type_convert.h"
#include "jsb_type_convert_direct.h"

namespace jsb {
// fallback to Variant transpiler
template <typename T>
struct StaticBindingUtil {
	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, T &r_value) {
		Variant cv;
		if (TypeConvert::js_to_gd_var(isolate, context, p_input, (Variant::Type)GetTypeInfo<T>::VARIANT_TYPE, cv)) {
			jsb_check(cv.get_type() == (Variant::Type)GetTypeInfo<T>::VARIANT_TYPE);
			r_value = cv;
			return true;
		}
		return false;
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, T const &p_input, v8::Local<v8::Value> &r_value) {
		return TypeConvert::gd_var_to_js(isolate, context, (Variant)p_input, r_value);
	}
};

template <>
struct StaticBindingUtil<Object *> {
	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, Object *&r_value) {
		return TypeConvert::js_to_gd_obj(isolate, context, p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, Object *const &p_input, v8::Local<v8::Value> &r_value) {
		v8::Local<v8::Object> obj;
		if (TypeConvert::gd_obj_to_js(isolate, context, p_input, obj)) {
			r_value = obj;
			return true;
		}
		return false;
	}
};

// float / double / bool all delegate to `JSToGD<T>`. Those direct converters
// already carry the engine's numeric acceptance surface (BigInt for the float
// slots, number/bigint/null/undefined for bool), so the reflect path used by
// constructor calls cannot drift away from the static thunk path.
template <>
struct StaticBindingUtil<float> {
	static bool get(const v8::Local<v8::Value> &p_input, float &r_value) {
		return JSToGD<float>::convert(nullptr, v8::Local<v8::Context>(), p_input, r_value);
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, float &r_value) {
		return JSToGD<float>::convert(isolate, context, p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const float &p_input, v8::Local<v8::Value> &r_value) {
		r_value = v8::Number::New(isolate, (float)p_input);
		return true;
	}
};

template <>
struct StaticBindingUtil<double> {
	static bool get(const v8::Local<v8::Value> &p_input, double &r_value) {
		return JSToGD<double>::convert(nullptr, v8::Local<v8::Context>(), p_input, r_value);
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, double &r_value) {
		return JSToGD<double>::convert(isolate, context, p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const double &p_input, v8::Local<v8::Value> &r_value) {
		r_value = v8::Number::New(isolate, (double)p_input);
		return true;
	}
};

// No metadata-less `get` overload: `to_bool` needs the isolate for
// `BooleanValue`. The reflect constructor path always passes one, and nothing
// else instantiates this specialization.
template <>
struct StaticBindingUtil<bool> {
	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, bool &r_value) {
		return JSToGD<bool>::convert(isolate, context, p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const bool &p_input, v8::Local<v8::Value> &r_value) {
		r_value = v8::Boolean::New(isolate, p_input);
		return true;
	}
};

template <>
struct StaticBindingUtil<int64_t> {
	// for hardcoded call
	static bool get(const v8::Local<v8::Value> &p_input, int64_t &r_value) {
		return impl::Helper::to_int64(p_input, r_value);
	}

	// for template-based call
	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, int64_t &r_value) {
		return impl::Helper::to_int64(p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const int64_t &p_input, v8::Local<v8::Value> &r_value) {
		r_value = impl::Helper::new_integer(isolate, p_input);
		return true;
	}
};

template <>
struct StaticBindingUtil<uint64_t> {
	// for hardcoded call
	static bool get(const v8::Local<v8::Value> &p_input, uint64_t &r_value) {
		return impl::Helper::to_uint64(p_input, r_value);
	}

	// for template-based call
	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, uint64_t &r_value) {
		return impl::Helper::to_uint64(p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const uint64_t &p_input, v8::Local<v8::Value> &r_value) {
		// Unsigned writer: a value with bit 63 set must not come back negative.
		r_value = impl::Helper::new_unsigned_integer(isolate, p_input);
		return true;
	}
};

// int32 delegates to `JSToGD<int32_t>` for the same reason float / double do:
// the constructor reflect path uses this specialization, and the declared
// `int64` components of `Vector2i` / `Vector3i` accept a BigInt (and a boolean,
// and any number). The previous body tested `IsNumber()` and then did a bare
// `As<v8::Int32>()` -- which is a pure handle reinterpretation, so a BigInt or a
// double was either rejected or read as garbage.
template <>
struct StaticBindingUtil<int32_t> {
	static bool get(const v8::Local<v8::Value> &p_input, int32_t &r_value) {
		return JSToGD<int32_t>::convert(nullptr, v8::Local<v8::Context>(), p_input, r_value);
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, int32_t &r_value) {
		return JSToGD<int32_t>::convert(isolate, context, p_input, r_value);
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const int32_t &p_input, v8::Local<v8::Value> &r_value) {
		r_value = impl::Helper::new_integer(isolate, p_input);
		return true;
	}
};
// The remaining exact-width integers delegate to `JSToGD<T>`, like int32 above.
//
// Without these they fell through to the primary template, which routes through
// `TypeConvert::js_to_gd_var` and a `Variant`: the narrowing then happened in
// `Variant::operator int8_t()` (etc.), which is the engine's own truncation but
// a different code path from the static thunks -- so the two legs could disagree
// about which values are accepted, and the debug truncation warning in
// `js_to_gd_var` could not see the declared width.
//
// Delegating keeps one conversion and one diagnostic for both legs. The
// metadata-less overload passes no isolate: `JSToGD<CppT>` for these types only
// calls `to_int64`, which needs none.
#define JSB_STATIC_BINDING_FIXED_INT(CppType)                                                                           \
	template <>                                                                                                          \
	struct StaticBindingUtil<CppType> {                                                                                  \
		static bool get(const v8::Local<v8::Value> &p_input, CppType &r_value) {                                          \
			return JSToGD<CppType>::convert(nullptr, v8::Local<v8::Context>(), p_input, r_value);                         \
		}                                                                                                                 \
                                                                                                                      \
		static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, \
				CppType &r_value) {                                                                                      \
			return JSToGD<CppType>::convert(isolate, context, p_input, r_value);                                          \
		}                                                                                                                 \
                                                                                                                      \
		static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const CppType &p_input,              \
				v8::Local<v8::Value> &r_value) {                                                                         \
			r_value = impl::Helper::new_integer(isolate, (int64_t)p_input);                                                \
			return true;                                                                                                  \
		}                                                                                                                 \
	};

JSB_STATIC_BINDING_FIXED_INT(int8_t)
JSB_STATIC_BINDING_FIXED_INT(int16_t)
JSB_STATIC_BINDING_FIXED_INT(uint8_t)
JSB_STATIC_BINDING_FIXED_INT(uint16_t)
JSB_STATIC_BINDING_FIXED_INT(uint32_t)
JSB_STATIC_BINDING_FIXED_INT(char32_t)
#undef JSB_STATIC_BINDING_FIXED_INT

} //namespace jsb
