/************************************************************************/
/*  builtin_methods.h                                                   */
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

#	include "thunks_common.h"
#	include <godot_cpp/variant/variant_internal.hpp>

namespace jsb::static_binding::thunks {

_FORCE_INLINE_ GDExtensionPtrBuiltInMethod resolve_builtin_method(godot::Variant::Type p_type, const godot::StringName &p_method_name, uint32_t p_hash) {
	return ::godot::gdextension_interface::variant_get_ptr_builtin_method(
			(GDExtensionVariantType)p_type,
			p_method_name._native_ptr(),
			(GDExtensionInt)p_hash);
}

// ---------------------------------------------------------------------------
// Fixed-arity builtin method (§4.0-A). Parameters marshaled into ptrcall slots
// via a std::tuple so every slot outlives the fn() call.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method(VTC, godot::StringName(NameLit.value), HashC);
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load builtin method %s::%s",
				godot::Variant::get_type_name(VTC), NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing builtin method: %s::%s", godot::Variant::get_type_name(VTC), NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d", godot::Variant::get_type_name(VTC), NameLit.value, M, N, provided));
		return;
	}

	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		base_ptr = get_opaque_typed<VTC>(self);
	}

	// marshal into ptrcall slots (tuple outlives fn call)
	std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(slots), provided)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return;
	}

	void *arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<N>{});

	ReturnEncodeType<RetT> ret_val{};
	fn(base_ptr, arg_ptrs, &ret_val, N);
	translate_return<RetT>(isolate, context, ret_val, info);
}

// ---------------------------------------------------------------------------
// Vararg builtin method (§4.0-B).
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method(VTC, godot::StringName(NameLit.value), HashC);
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load builtin method %s::%s",
				godot::Variant::get_type_name(VTC), NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing builtin method: %s::%s", godot::Variant::get_type_name(VTC), NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", godot::Variant::get_type_name(VTC), NameLit.value, M, provided));
		return;
	}

	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		base_ptr = get_opaque_typed<VTC>(self);
	}

	// fixed prefix slots
	std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> prefix_slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(prefix_slots), provided)) && ...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

	// vararg tail
	const int argc = provided > F ? provided : F;
	godot::Variant *tail_args =
			(godot::Variant *)jsb_stackalloc(godot::Variant, argc > F ? argc - F : 1);
	for (int i = F; i < argc; ++i) {
		memnew_placement(&tail_args[i - F], godot::Variant);
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], tail_args[i - F])) {
			jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			for (int j = F; j <= i; ++j) {
				tail_args[j - F].~Variant();
			}
			return;
		}
	}

	void **arg_ptrs = (void **)jsb_stackalloc(void *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(prefix_slots)), ...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		arg_ptrs[i] = &tail_args[i - F];
	}

	ReturnEncodeType<RetT> ret_val{};
	for (int _i = 0; _i < argc && _i < 64; ++_i) {
		arg_ptrs[_i] = nullptr;
	}
	fn(base_ptr, arg_ptrs, &ret_val, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	translate_return<RetT>(isolate, context, ret_val, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS