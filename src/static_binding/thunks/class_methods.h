/************************************************************************/
/*  class_methods.h                                                     */
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

#	include "thunks_common.h"

#	include <array>
#	include <godot_cpp/classes/object.hpp>

namespace jsb::static_binding::thunks {

template <FixedString ClassLit, FixedString NameLit, uint32_t HashC>
_FORCE_INLINE_ GDExtensionMethodBindPtr resolve_class_method() {
	static GDExtensionMethodBindPtr ret = ::godot::gdextension_interface::classdb_get_method_bind(
			godot::StringName(ClassLit.value)._native_ptr(),
			godot::StringName(NameLit.value)._native_ptr(),
			(GDExtensionInt)HashC);
	return ret;
}

_FORCE_INLINE_ GDExtensionMethodBindPtr resolve_class_method(const godot::StringName &p_class_name, const godot::StringName &p_method_name, uint32_t p_hash) {
	return ::godot::gdextension_interface::classdb_get_method_bind(
			p_class_name._native_ptr(),
			p_method_name._native_ptr(),
			(GDExtensionInt)p_hash);
}

// ---------------------------------------------------------------------------
// Fixed-arity class method. Marshal only provided arguments: MethodBind owns
// and applies missing defaults. M is the minimum arity, not a default-value
// descriptor. Keep the lower-bound check: engine missing-argument checks may
// be disabled in release builds.
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, int M, class RetT, class AllArgsT>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionMethodBindPtr method_bind = resolve_class_method<ClassLit, NameLit, HashC>();
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s", ClassLit.value, NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d", ClassLit.value, NameLit.value, M, N, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", ClassLit.value, NameLit.value));
			return;
		}
	}

	// RAII Variant slots remain NIL if conversion fails; the engine fills defaults.
	godot::Variant argv[N > 0 ? N : 1];
	const godot::Variant *arg_ptrs[N > 0 ? N : 1];
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && (N <= (int)provided || (int)I < provided ? produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, argv[I], provided) : true)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // JS exception already thrown by produce_variant
	}
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = &argv[I]), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, IsStaticC ? nullptr : instance->_owner, (const GDExtensionConstVariantPtr *)arg_ptrs, provided, &ret, &call_error);
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", ClassLit.value, NameLit.value, (int)call_error.error));
		return;
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret, info);
	}
}

// ---------------------------------------------------------------------------
// Vararg class method: unroll the fixed prefix, loop over the tail.
// The generated fixed prefix has no defaults, so M == F.
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, int M, class RetT, class AllArgsT>
void class_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int F = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static GDExtensionMethodBindPtr method_bind = resolve_class_method(godot::StringName(ClassLit.value), godot::StringName(NameLit.value), HashC);
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s", ClassLit.value, NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", ClassLit.value, NameLit.value, M, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", ClassLit.value, NameLit.value));
			return;
		}
	}

	// MethodBind takes Variant pointers; array-owned prefix slots clean up on exit.
	std::array<godot::Variant, F> prefix;
	const int fixed_count = provided < F ? provided : F;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && ((int)I < fixed_count ? produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, prefix[I], provided) : true)) && ...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

	// Runtime-sized tail slots need placement construction and manual destruction.
	const int argc = provided;
	godot::Variant *tail_args = (godot::Variant *)jsb_stackalloc(godot::Variant, argc > F ? argc - F : 1);
	const godot::Variant **arg_ptrs =
			(const godot::Variant **)jsb_stackalloc(const godot::Variant *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)((int)I < fixed_count
						 ? (void)(arg_ptrs[I] = &prefix[I])
						 : (void)0),
				...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		memnew_placement(&tail_args[i - F], godot::Variant);
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], tail_args[i - F])) {
			jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			// constructed tail slots form the contiguous [0, i - F]
			for (int j = 0; j <= i - F; ++j) {
				tail_args[j].~Variant();
			}
			return;
		}
		arg_ptrs[i] = &tail_args[i - F];
	}

	godot::Variant ret;
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, IsStaticC ? nullptr : instance->_owner, (const GDExtensionConstVariantPtr *)arg_ptrs, argc, &ret, &call_error);

	// Only the constructed tail needs manual cleanup; prefix is a RAII array.
	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", ClassLit.value, NameLit.value, (int)call_error.error));
		return;
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret, info);
	}
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS