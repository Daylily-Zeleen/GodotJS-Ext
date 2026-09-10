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
//     PLACE into base. The constructed builtin's C++ type is derived from
//     VTC via VariantNativeType_t<VTC> (see thunks_common.h), so base is
//     aligned raw storage of exactly that size; the result is lifted into a
//     full Variant through the godot-cpp Variant(target) constructor --
//     same self-sufficient pattern as builtin_method_thunk's
//     marshal_one/PtrToArg args -- NOT api_tool (no var_to_arg_ptr /
//     arg_ptr_to_var / MaxSizeEncodeArgType).
//   - strict arity: constructors have no default arguments in the api json,
//     so info.Length() must equal sizeof...(ArgsT) exactly.
template <godot::Variant::Type VTC, int CtorIndex, typename... ArgsT>
void builtin_ctor_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using TargetCppT = VariantNativeType_t<VTC>;
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();
	// TargetCppT (above) resolves the constructed builtin's C++ type from VTC.

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

	// marshal every argument into a typed ptrcall slot (godot-cpp native
	// mechanism, identical to builtin_method_thunk -- no api_tool encode).
	std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(slots), N)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // marshal_one already jsb_threw
	}

	void *arg_ptrs[N > 0 ? N : 1] = {};
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<N>{});

	// engine constructor ABI: ctor(base, args). base is
	// GDExtensionUninitializedTypePtr -- raw storage of sizeof(TargetCppT),
	// which the ctor writes in place. The constructed value is lifted into a
	// full godot::Variant through Variant(const TargetCppT &) -- this sets
	// the type tag AND copies the data (the prior failure mode was passing a
	// `variant_new_nil` Variant as base: the ctor only wrote data and never
	// updated the NIL tag, yielding a NIL-tagged Variant with struct data =
	// SEGV on first use).
	Environment *env = Environment::wrap(isolate);
	std::aligned_storage_t<sizeof(TargetCppT), alignof(TargetCppT)> base_storage;
	ctor(&base_storage, arg_ptrs);
	godot::Variant constructed(*reinterpret_cast<const TargetCppT *>(&base_storage));
	Variant *instance = env->alloc_variant();
	*instance = std::move(constructed);
	env->bind_valuetype(instance, info.This());
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
