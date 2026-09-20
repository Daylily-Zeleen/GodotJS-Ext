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
#	include <atomic>
#	include <godot_cpp/classes/object.hpp>

#	include "static_binding/dispatch.h"

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
// Signature-shared class method binding (binding_mode=shared, JSB_WITH_SHARED_THUNKS).
// One per-method data blob in DLL .data (codegen-emitted parallel to the
// signature thunk table). Shared thunks read identity (class/method names,
// hash) for lazy method-bind resolution and min-arity for the lower-bound
// check through info.Data() -- the template parameters carry only the
// signature.
struct SharedClassMethodData {
	mutable std::atomic<GDExtensionMethodBindPtr> method_bind; // lazy, filled on first call
	const char *class_name;
	const char *method_name;
	int32_t min_argc;
};

// Resolve-and-cache the method bind. Called EAGERLY at mount time from the
// single top-level find_shared_class_method_binding (after the binary search
// hits the class and the nested find_cls_* does a pure lookup), ONCE per
// registered method while the class template is being built -- the class name
// and method hash are already in hand there (PRD: eager resolution is the
// parent-task ruling). Multiple worker Environments share the same static
// table (one DLL .data copy), so concurrent first writes to the same slot need
// well-defined semantics: the CAS keeps that (same value written,
// memory_order_relaxed, single-word aligned -- zero cost).
//
// The hot path (shared_class_*_thunk) does NOT call this; it only
// relaxed-loads the result.
_FORCE_INLINE_ GDExtensionMethodBindPtr ensure_class_method_bind(uint32_t p_hash, const SharedClassMethodData &md) {
	GDExtensionMethodBindPtr mb = md.method_bind.load(std::memory_order_relaxed);
	if (mb) {
		return mb; // already resolved by a previous Environment
	}
	mb = ::godot::gdextension_interface::classdb_get_method_bind(
			godot::StringName(md.class_name)._native_ptr(),
			godot::StringName(md.method_name)._native_ptr(),
			(GDExtensionInt)p_hash);
	if (mb) {
		GDExtensionMethodBindPtr expected = nullptr;
		if (md.method_bind.compare_exchange_strong(expected, mb, std::memory_order_relaxed)) {
			return mb;
		}
		return expected; // another worker won the race with the same value
	}
	return nullptr;
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

	// A class thunk carries no default literal of its own -- the engine MethodBind
	// fills the *trailing omitted* arguments -- so an explicit `undefined` the
	// caller passed over a defaulted position [M, N) is resolved from the method
	// record that registration attached as this thunk's data payload. The value
	// lands in this thunk's OWN argument slot and the call stays on the static
	// path: nothing re-dispatches.
	//
	// A method with no defaulted position takes the plain path below unchanged --
	// `M == N` discards the whole substitution block, so those instantiations carry
	// no extra code or state at all.
	//
	// Positions the caller omitted altogether stay below `provided` and keep the
	// engine's trailing fill.
	godot::Variant argv[N > 0 ? N : 1];
	const godot::Variant *arg_ptrs[N > 0 ? N : 1];
	bool ok = true;
	if constexpr (M < N) {
		// The method record is attached once at registration and is stable for the
		// process lifetime, so it is fetched on the first hit and never again; both
		// statics are constant-initialized (no guard variable, no atomic). A method
		// with no defaulted position discards this whole block, state included.
		static const godot::Variant *cached_defaults = nullptr;
		static uint32_t cached_count = 0;
		// Deliberately non-generic: one out-of-line helper serves every optional
		// position. A per-position template would emit a separate copy of the whole
		// body for each one, which for 15370 instantiations dominates the object.
		auto substitute_default = [&](int i) -> bool {
			if (i < M || !info[i]->IsUndefined()) {
				return false;
			}
			if (cached_defaults == nullptr) {
				jsb_check(info.Data()->IsExternal());
				cached_defaults = class_method_defaults(info.Data().As<v8::External>()->Value(), cached_count);
			}
			// A record too short for this position (corrupt store) falls through to
			// the normal conversion -- the per-position range check the dynamic path
			// does, minus the out-of-bounds read.
			if ((uint32_t)(i - M) < cached_count) {
				argv[i] = cached_defaults[i - M];
				return true;
			}
			return false;
		};
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(void)((ok = ok && (N <= (int)provided || (int)I < provided ? (substitute_default((int)I) ? true : produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, argv[I], provided)) : true)) && ...);
		}(std::make_index_sequence<N>{});
	} else {
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(void)((ok = ok && (N <= (int)provided || (int)I < provided ? produce_variant<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, argv[I], provided) : true)) && ...);
		}(std::make_index_sequence<N>{});
	}
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

#	if JSB_WITH_SHARED_THUNKS
// ---------------------------------------------------------------------------
// Signature-shared class methods (binding_mode=shared): one thunk instance
// per unique (IsStaticC, RetT, ArgsT...) signature; per-method identity
// (class/method names, hash, min arity) and the lazily-resolved method bind
// arrive through info.Data() (SharedClassMethodData) instead of template
// parameters. Marshal semantics identical to the per-method thunks above.

// Fixed-arity signature-shared class method.
template <bool IsStaticC, class RetT, class AllArgsT>
void shared_class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedClassMethodData &md =
			*static_cast<const SharedClassMethodData *>(info.Data().As<v8::External>()->Value());

	// Resolved eagerly at mount time: find_shared_class_method_binding fills
	// method_bind (via ensure_class_method_bind) BEFORE returning this thunk,
	// and returns nullptr on failure so the mount point falls back to dynamic.
	// A mounted thunk's slot is therefore always populated. The hot path is a
	// single relaxed load -- identical codegen to reading a raw pointer.
	const GDExtensionMethodBindPtr method_bind = md.method_bind.load(std::memory_order_relaxed);

	const int provided = (int)info.Length();
	if (provided < md.min_argc || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d", md.class_name, md.method_name, md.min_argc, N, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", md.class_name, md.method_name));
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
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", md.class_name, md.method_name, (int)call_error.error));
		return;
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret, info);
	}
}

// Vararg signature-shared class method: unroll the fixed prefix, loop the tail.
// The generated fixed prefix carries no defaults, so min_argc == F.
template <bool IsStaticC, class RetT, class AllArgsT>
void shared_class_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using AllArgsTuple = typename AllArgsT::tuple;
	constexpr int F = (int)std::tuple_size_v<AllArgsTuple>;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedClassMethodData &md =
			*static_cast<const SharedClassMethodData *>(info.Data().As<v8::External>()->Value());

	// Resolved eagerly at mount time: find_shared_class_method_binding fills
	// method_bind (via ensure_class_method_bind) BEFORE returning this thunk,
	// and returns nullptr on failure so the mount point falls back to dynamic.
	// A mounted thunk's slot is therefore always populated. The hot path is a
	// single relaxed load -- identical codegen to reading a raw pointer.
	const GDExtensionMethodBindPtr method_bind = md.method_bind.load(std::memory_order_relaxed);

	const int provided = (int)info.Length();
	if (provided < md.min_argc) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", md.class_name, md.method_name, md.min_argc, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", md.class_name, md.method_name));
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
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", md.class_name, md.method_name, (int)call_error.error));
		return;
	}

	if constexpr (RetT::has_return) {
		RetT::translate_return(isolate, context, ret, info);
	}
}
#	endif // JSB_WITH_SHARED_THUNKS

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS