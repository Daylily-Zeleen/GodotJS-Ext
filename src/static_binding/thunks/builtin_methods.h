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

#	include <godot_cpp/variant/variant_internal.hpp>
#	include <array>
#	include <atomic>

namespace jsb::static_binding::thunks {

_FORCE_INLINE_ GDExtensionPtrBuiltInMethod resolve_builtin_method(godot::Variant::Type p_type, const godot::StringName &p_method_name, uint32_t p_hash) {
	return ::godot::gdextension_interface::variant_get_ptr_builtin_method(
			(GDExtensionVariantType)p_type,
			p_method_name._native_ptr(),
			(GDExtensionInt)p_hash);
}

// ---------------------------------------------------------------------------
// Fixed-arity builtin method. Tuple-owned EncodeT slots outlive the call.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class AllArgsT, class DefsT>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;
	constexpr int M = N - (int)DefsT::count;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method(VTC, godot::StringName(NameLit.value), HashC);
	if (!fn) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load builtin method %s::%s: missing builtin method", godot::Variant::get_type_name(VTC), NameLit.value));
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

	// An explicit `undefined` over an optional position [M, N) means "use THAT
	// position's default" (JS default-parameter semantics) and must not shift the
	// arguments around it, so the caller's arity is never rewritten. Required
	// positions [0, M) keep converting `undefined` normally -- there it is a
	// value, not an omission, and it still fails if it does not fit.
	//
	// Diverting before conversion is what keeps a default from being a conversion
	// error: a diverted position never reaches marshal_one, it goes straight to
	// the shared default slot.
	//
	// The decision is taken once for the whole optional range, including the
	// positions the caller left out entirely, so the argument-pointer pass reads a
	// bit instead of re-testing the same JS value (each re-test costs an
	// out-of-line `Value::IsUndefined` call).
	//
	// A builtin with no optional position (`M == N`) discards this whole block and
	// marshals exactly as it did before this feature existed.
	static_assert(N <= 32, "the default-substitution mask assumes at most 32 parameters");
	typename AllArgsT::encode_slots slots;
	bool ok = true;
	void *arg_ptrs[N > 0 ? N : 1];
	if constexpr (M < N) {
		// An explicit `undefined` over an optional position [M, N) means "use THAT
		// position's default" (JS default-parameter semantics) and must not shift
		// the arguments around it, so the caller's arity is never rewritten.
		//
		// The mask is computed once for the whole optional range, so a position is
		// never tested twice (each re-test costs an out-of-line `Value::IsUndefined`
		// call).
		//
		// The mask stays inside this branch: it is only ever read on the optional
		// positions, so hoisting the declaration out would make every no-default
		// builtin thunk pay for a stack slot it never uses (measured: +29 KiB
		// across the 680 no-default instances under /Od). That is why the marshal
		// pass and the `ok` check -- the only statements that read the mask or
		// depend on this branch -- are written twice; the required-prefix pointer
		// pass below is shared.
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
											   decltype(builtin_method_thunk<VTC, HashC, NameLit, IsStaticC, RetT, AllArgsT, DefsT>),
											   void>>()
							 : (void *)&std::get<M + J>(slots)),
					...);
		}(std::make_index_sequence<N - M>{});
	} else {
		// No optional position: plain marshal, exactly as before this feature.
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(void)((ok = ok && ((int)I < provided ? marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(slots), provided) : true)) && ...);
		}(std::make_index_sequence<N>{});
		if (!ok) {
			return;
		}
	}

	// required prefix [0, M): the arity check guarantees these are provided. Fills
	// only the positions [0, M), which neither branch above touches, so it is
	// shared and its order against the optional positions is free.
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<M>{});

	typename RetT::encoded_type ret_val{};
	fn(base_ptr, arg_ptrs, &ret_val, N);

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}

// ---------------------------------------------------------------------------
// Vararg builtin method.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class AllArgsT>
void builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int F = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method(VTC, godot::StringName(NameLit.value), HashC);
	if (!fn) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load builtin method %s::%s: missing builtin method", godot::Variant::get_type_name(VTC), NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < F) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", godot::Variant::get_type_name(VTC), NameLit.value, F, provided));
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

	// Builtin vararg ptrcalls consume Variant slots for the fixed prefix as
	// well as the tail, unlike the typed EncodeT slots of fixed-arity methods.
	std::array<godot::Variant, F> prefix;
	const int fixed_count = provided < F ? provided : F;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && ((int)I < fixed_count ? produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, prefix[I], provided) : true)) && ...);
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
		((void)(arg_ptrs[I] = (void *)&prefix[I]), ...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		arg_ptrs[i] = &tail_args[i - F];
	}

	typename RetT::encoded_type ret_val{};
	fn(base_ptr, arg_ptrs, &ret_val, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}

#	if JSB_WITH_SHARED_THUNKS
// ---------------------------------------------------------------------------
// Signature-shared builtin methods (binding_mode=shared): one thunk instance
// per unique (VTC, IsStaticC, RetT, ArgsT...) signature; per-method identity
// (type/method names, hash) and the eagerly-resolved ptrcall function arrive
// through info.Data() (SharedBuiltinMethodData) instead of template params.
// Default handling is moved out of the thunk into a codegen-emitted table of
// owned default slots (see design.md); each optional parameter position holds
// an accessor function pointer (lazily materializing the PtrToArg<T>::EncodeT
// slot through a function-local magic static). A null entry marks a required
// position, which also drives the arity check.

struct SharedBuiltinMethodData {
	using DefaultAccessor = void *(*)();
	mutable std::atomic<GDExtensionPtrBuiltInMethod> fn; // eagerly resolved at mount
	godot::Variant::Type vt; // error text: Variant::get_type_name(vt)
	const char *method_name; // error text
	// codegen emits k_defs_* as an array of const accessor pointers; the
	// decayed type is DefaultAccessor const* (pointer to const element).
	// Methods with no optional (defaulted) parameters point at a single
	// shared all-nullptr long table (length == max arity, so defaults[provided]
	// never reads out of bounds); only methods with defaults own a distinct
	// table.
	DefaultAccessor const *defaults; // per-position accessor; null = required
};

// Resolve-and-cache the ptrcall function. Eager, called ONCE at mount time from
// the single top-level find_shared_builtin_binding (Variant type / name / hash
// already in hand). Multiple worker Environments share one DLL .data copy, so
// concurrent first writes need well-defined semantics: the CAS keeps that
// (same value written, memory_order_relaxed, single-word aligned -- zero cost).
_FORCE_INLINE_ GDExtensionPtrBuiltInMethod ensure_builtin_method(godot::Variant::Type p_vt, const godot::StringName &p_method_name, uint32_t p_hash, const SharedBuiltinMethodData &md) {
	GDExtensionPtrBuiltInMethod fn = md.fn.load(std::memory_order_relaxed);
	if (fn) {
		return fn; // already resolved by a previous Environment
	}
	fn = resolve_builtin_method(p_vt, p_method_name, p_hash);
	if (fn) {
		GDExtensionPtrBuiltInMethod expected = nullptr;
		if (md.fn.compare_exchange_strong(expected, fn, std::memory_order_relaxed)) {
			return fn;
		}
		return expected; // another worker won the race with the same value
	}
	return nullptr;
}

// Fixed-arity shared builtin method. Marshal only provided positions; missing
// optionals take their codegen-owned default slot through the accessor
// pointer (defaults[i]()). Arity: provided > N is an error; provided < N is an
// error iff defaults[provided] == nullptr (missing first optional == the
// minimum arity bound, tail-contiguous defaults guaranteed at generation time).
template <godot::Variant::Type VTC, bool IsStaticC, class RetT, class AllArgsT>
void shared_builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedBuiltinMethodData &md =
			*static_cast<const SharedBuiltinMethodData *>(info.Data().As<v8::External>()->Value());

	const GDExtensionPtrBuiltInMethod fn = md.fn.load(std::memory_order_relaxed);
	if (!fn) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load builtin method %s::%s: missing builtin method", godot::Variant::get_type_name(md.vt), md.method_name));
		return;
	}

	const int provided = (int)info.Length();
	if (provided > N || (provided < N && md.defaults[provided] == nullptr)) {
		// Error path only (cold): derive the minimum arity M by scanning the
		// tail-contiguous defaults, for a clear message.
		int m = N;
		while (m > 0 && md.defaults[m - 1] != nullptr) {
			--m;
		}
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d", godot::Variant::get_type_name(md.vt), md.method_name, m, N, provided));
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

	// An explicit `undefined` over a defaulted position [M, N) means "use THAT
	// position's default". Compute a per-position mask first so a defaulted
	// position is never tested twice (each re-test costs an out-of-line
	// `Value::IsUndefined` call) and the marshal pass can skip those positions
	// -- an explicit Undefined does not convert into the declared type. Required
	// positions [0, M) keep converting `undefined` normally (md.defaults[I] is
	// nullptr there, so the mask never claims them).
	uint32_t use_default_mask = 0;
	if (N > 0) {
		const int probe_n = provided < N ? provided : N;
		for (int i = 0; i < probe_n; ++i) {
			if (info[i]->IsUndefined() && md.defaults[i] != nullptr) {
				use_default_mask |= 1u << i;
			}
		}
	}

	// Marshal only provided non-undefined positions into tuple-owned EncodeT
	// slots; mask-claimed positions keep their slot value (unused).
	typename AllArgsT::encode_slots slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && ((int)I < provided && !((use_default_mask >> I) & 1u) ? marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(slots), provided) : true)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return;
	}

	void *arg_ptrs[N > 0 ? N : 1];
	// Provided positions come from conversion slots unless the mask claims them
	// (then the codegen-owned default slot); missing optionals take the default.
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = ((int)I < provided && !((use_default_mask >> I) & 1u))
						 ? (void *)&std::get<I>(slots)
						 : md.defaults[I]()),
				...);
	}(std::make_index_sequence<N>{});

	typename RetT::encoded_type ret_val{};
	fn(base_ptr, arg_ptrs, &ret_val, N);

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}

// Vararg signature-shared builtin method: unroll the fixed prefix (all
// Variant slots, like the fixed-arity builtin vararg), loop the tail. The
// generated fixed prefix carries no defaults, so the minimum arity is F
// (compile-time). Identity + eagerly-resolved ptrcall function arrive through
// info.Data() (SharedBuiltinMethodData); the hot path is one relaxed load of
// fn.
template <godot::Variant::Type VTC, bool IsStaticC, class RetT, class AllArgsT>
void shared_builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int F = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedBuiltinMethodData &md =
			*static_cast<const SharedBuiltinMethodData *>(info.Data().As<v8::External>()->Value());

	const GDExtensionPtrBuiltInMethod fn = md.fn.load(std::memory_order_relaxed);
	if (!fn) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load builtin method %s::%s: missing builtin method", godot::Variant::get_type_name(md.vt), md.method_name));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < F) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", godot::Variant::get_type_name(md.vt), md.method_name, F, provided));
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

	// Builtin vararg ptrcalls consume Variant slots for the fixed prefix as
	// well as the tail (see builtin_vararg_method_thunk).
	std::array<godot::Variant, F> prefix;
	const int fixed_count = provided < F ? provided : F;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && ((int)I < fixed_count ? produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, prefix[I], provided) : true)) && ...);
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
		((void)(arg_ptrs[I] = (void *)&prefix[I]), ...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		arg_ptrs[i] = &tail_args[i - F];
	}

	typename RetT::encoded_type ret_val{};
	fn(base_ptr, arg_ptrs, &ret_val, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret_val, info);
	}
}
#	endif // JSB_WITH_SHARED_THUNKS

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS