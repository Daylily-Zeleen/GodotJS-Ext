/************************************************************************/
/*  builtin_constructors.h                                              */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        */
/*  GNU Lesser General Public License for more details.                 */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"
// api_tool::internal::arg_ptr_to_var / api_tool::internal::MaxSizeEncodeArgType live here
#	include "api_tool/api_tool_types.h"
// GDExtensionPtrConstructor + variant_get_ptr_constructor declaration
#	include <gdextension_interface.h>
#	include <godot_cpp/core/builtin_ptrcall.hpp>
#	include <godot_cpp/variant/variant_internal.hpp>

namespace jsb::static_binding::thunks {

// Per-overload constructor thunk, the ctor counterpart of
// builtin_method_thunk. Key differences from the method flavor:
//   - no method bind: the engine constructor pointer is resolved by
//     (VTC, CtorIndex) via variant_get_ptr_constructor (magic-static).
//   - the engine ABI is `void ctor(GDExtensionTypePtr base, const
//     GDExtensionTypePtr *args)` -- the constructed value is written IN
//     PLACE into base. We construct into a stack buffer, then copy the
//     result into a fresh environment-owned Variant bound to `info.This()`
//     (same sequence as the reflection fallback's bind_valuetype; V8 takes
//     `info.This()` as the value of the `new` expression).
//   - strict arity: constructors have no default arguments in the api json,
//     so info.Length() must equal sizeof...(ArgsT) exactly.
template <godot::Variant::Type VTC, int CtorIndex, typename... ArgsT>
void builtin_ctor_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();
	fprintf(stderr, "CTOR: enter VTC=%d N=%d argc=%d\n", (int)VTC, (int)sizeof...(ArgsT), info.Length()); fflush(stderr);

	// engine constructor pointer, resolved once per (type, index) pair
	static GDExtensionPtrConstructor ctor = [] {
		return ::godot::gdextension_interface::variant_get_ptr_constructor(
				(GDExtensionVariantType)VTC, (int32_t)CtorIndex);
	}();
	if (!ctor) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load builtin constructor %s (index %d)",
				godot::Variant::get_type_name(VTC).utf8().get_data(), CtorIndex));
		jsb_throw(isolate, jsb_errorf("missing builtin constructor: %s (index %d)",
				godot::Variant::get_type_name(VTC).utf8().get_data(), CtorIndex));
		return;
	}

	constexpr int N = (int)sizeof...(ArgsT);
	if (info.Length() != N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s constructor expects %d, got %d",
				godot::Variant::get_type_name(VTC).utf8().get_data(), N, (int)info.Length()));
		return;
	}

	// strict per-type conversion of every argument into a Variant (same
	// conversion rules as the reflection fallback's can_convert_strict +
	// js_to_gd_var pair)
	godot::Variant arg_values[N > 0 ? N : 1];
	api_tool::internal::MaxSizeEncodeArgType arg_enc[N > 0 ? N : 1];
	GDExtensionConstTypePtr arg_ptrs[N > 0 ? N : 1] = {};
	for (int i = 0; i < N; ++i) {
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], arg_values[i])) {
			jsb_throw(isolate, jsb_errorf("bad argument: %d", i));
			return;
		}
		api_tool::internal::var_to_arg_ptr(arg_values[i], arg_enc + i, arg_values[i].get_type());
		arg_ptrs[i] = arg_enc + i;
	}

	// engine constructor ABI: ctor(base, args). base is
	// GDExtensionUninitializedTypePtr -- raw storage the ctor writes into.
	// The constructed value is converted back into a full Variant (type tag
	// + data) via arg_ptr_to_var -- constructing directly into a Variant
	// object does NOT update its type tag, leaving a NIL-tagged Variant with
	// struct data (SEGV on first use).
	Environment *env = Environment::wrap(isolate);
	api_tool::internal::MaxSizeEncodeArgType base_enc;
	ctor(&base_enc, arg_ptrs);
	Variant *instance = env->alloc_variant();
	api_tool::internal::arg_ptr_to_var(&base_enc, VTC, *instance);
	env->bind_valuetype(instance, info.This());
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
