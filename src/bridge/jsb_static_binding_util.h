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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once
#include "jsb_bridge_pch.h"

#include "jsb_type_convert.h"

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

template <>
struct StaticBindingUtil<float> {
	static bool get(const v8::Local<v8::Value> &p_input, float &r_value) {
		if (p_input->IsNumber()) {
			r_value = (float)p_input.As<v8::Number>()->Value();
			return true;
		}
		return false;
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, float &r_value) {
		if (p_input->IsNumber()) {
			r_value = (float)p_input.As<v8::Number>()->Value();
			return true;
		}
		return false;
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const float &p_input, v8::Local<v8::Value> &r_value) {
		r_value = v8::Number::New(isolate, (float)p_input);
		return true;
	}
};

template <>
struct StaticBindingUtil<double> {
	static bool get(const v8::Local<v8::Value> &p_input, double &r_value) {
		if (p_input->IsNumber()) {
			r_value = (double)p_input.As<v8::Number>()->Value();
			return true;
		}
		return false;
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, double &r_value) {
		if (p_input->IsNumber()) {
			r_value = (double)p_input.As<v8::Number>()->Value();
			return true;
		}
		return false;
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const double &p_input, v8::Local<v8::Value> &r_value) {
		r_value = v8::Number::New(isolate, (double)p_input);
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
struct StaticBindingUtil<int32_t> {
	static bool get(const v8::Local<v8::Value> &p_input, int32_t &r_value) {
		if (p_input->IsNumber()) {
			r_value = (int32_t)p_input.As<v8::Int32>()->Value();
			return true;
		}
		return false;
	}

	static bool get(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_input, int32_t &r_value) {
		if (p_input->IsNumber()) {
			r_value = (int32_t)p_input.As<v8::Int32>()->Value();
			return true;
		}
		return false;
	}

	static bool set(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const int32_t &p_input, v8::Local<v8::Value> &r_value) {
		r_value = impl::Helper::new_integer(isolate, p_input);
		return true;
	}
};
} //namespace jsb

