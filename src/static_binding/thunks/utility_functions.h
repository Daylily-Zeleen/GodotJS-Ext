/************************************************************************/
/*  utility_functions.h                                                 */
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

namespace jsb::static_binding::thunks {

_FORCE_INLINE_ GDExtensionPtrUtilityFunction resolve_utility_function(const godot::StringName &p_function_name, uint32_t p_hash) {
	return ::godot::gdextension_interface::variant_get_ptr_utility_function(
			p_function_name._native_ptr(),
			(GDExtensionInt)p_hash);
}

// ---------------------------------------------------------------------------
// Fixed-arity utility function. Missing optional arguments use the same
// pre-encoded default slots as builtin methods.
template <uint32_t HashC, FixedString NameLit, class RetT, class AllArgsT, class DefsT>
void utility_function_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;
	constexpr int M = N - (int)DefsT::count;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrUtilityFunction fn = resolve_utility_function(godot::StringName(NameLit.value), HashC);
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load utility function %s", NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing utility function: %s", NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s expects %d..%d, got %d", NameLit.value, M, N, provided));
		return;
	}

	typename AllArgsT::encode_slots slots;
	bool ok = true;
	void *arg_ptrs[N > 0 ? N : 1];

	static_assert(N <= 32, "the default-substitution mask assumes at most 32 parameters");
	// Same optional-position handling as builtin_method_thunk, and deliberately the
	// same shape: an explicit `undefined` over [M, N) means "use THAT position's
	// default" (JS default-parameter semantics) and must not shift the positions
	// around it. No Godot utility function has a default value today, so every
	// instantiation takes the branch below, but the shape is kept identical so the
	// two thunk families cannot drift apart.
	//
	// The mask only ever covers [M, N); required positions [0, M) keep converting
	// `undefined` normally -- there it is a value, not an omission.
	if constexpr (M < N) {
		// The mask stays inside this branch (see builtin_method_thunk): hoisting it
		// out would make every no-default instance pay for a stack slot.
		uint32_t use_default_mask = 0;
		for (int i = M; i < N; ++i) {
			// `info[i]` is only valid below the passed arity; the short circuit
			// keeps that dereference off the out-of-range positions.
			if (i >= provided || info[i]->IsUndefined()) {
				use_default_mask |= 1u << i;
			}
		}

		// Marshal only the positions that carry a caller-supplied value.
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(void)((ok = ok && ((int)I < provided && !((use_default_mask >> I) & 1u) ? marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(slots), provided) : true)) && ...);
		}(std::make_index_sequence<N>{});
		if (!ok) {
			return;
		}

		// Instantiate default_arg_slot only for optional positions [M, N).
		[&]<std::size_t... J>(std::index_sequence<J...>) {
			((void)(arg_ptrs[M + J] = ((use_default_mask >> (M + J)) & 1u)
							 ? default_arg_slot<std::tuple_element_t<J, typename DefsT::tuple>,
									   std::conditional_t<GDReferentialBuiltinType<std::tuple_element_t<J, typename DefsT::tuple>>,
											   decltype(utility_function_thunk<HashC, NameLit, RetT, AllArgsT, DefsT>),
											   void>>()
							 : (void *)&std::get<M + J>(slots)),
					...);
		}(std::make_index_sequence<N - M>{});
	} else {
		// No optional position: plain marshal.
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(void)((ok = ok && ((int)I < provided ? marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(slots), provided) : true)) && ...);
		}(std::make_index_sequence<N>{});
		if (!ok) {
			return;
		}
	}

	// required prefix [0, M): the arity check guarantees these are provided.
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<M>{});

	typename RetT::encoded_type ret_val{};
	fn(&ret_val, arg_ptrs, N);

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}

// ---------------------------------------------------------------------------
// Vararg utility function.
template <uint32_t HashC, FixedString NameLit, class RetT, class AllArgsT>
void utility_vararg_function_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int F = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrUtilityFunction fn = resolve_utility_function(godot::StringName(NameLit.value), HashC);
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load utility function %s", NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing utility function: %s", NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < F) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s expects >= %d, got %d", NameLit.value, F, provided));
		return;
	}

	typename AllArgsT::encode_slots prefix_slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(prefix_slots), provided)) && ...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

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

	typename RetT::encoded_type ret_val{};
	fn(&ret_val, arg_ptrs, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS