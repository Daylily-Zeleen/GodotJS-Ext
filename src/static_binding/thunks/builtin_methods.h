/************************************************************************/
/*  builtin_methods.h                                                   */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Thunk templates for builtin-type methods (design doc §4.1).         */
/*  All method identity (variant type, hash, name) is baked in as       */
/*  template arguments at generation time; the native pointer is        */
/*  resolved lazily once per instantiation.                             */
/*                                                                      */
/*  Marshaling follows §4.0:                                            */
/*    - fixed arity: unrolled per-parameter conversion, missing args    */
/*      filled from compile-time default singletons, no loops           */
/*    - vararg: fixed prefix unrolled, only the tail iterates           */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Fixed-arity builtin method.
template <uint16_t VTC, uint64_t HashC, FixedString NameLit, bool IsStaticC,
		class RetT, class... ArgsT>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE("static binding: failed to load builtin method "
				+ godot::Variant::get_type_name((godot::Variant::Type)VTC)
				+ "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing builtin method: "
				+ godot::Variant::get_type_name((godot::Variant::Type)VTC)
				+ "::" + godot::String(NameLit.value));
		return;
	}

	// argc gate (§4.0-A): M <= provided <= N
	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d",
				godot::Variant::get_type_name((godot::Variant::Type)VTC),
				godot::String(NameLit.value).utf8().get_data(), M, N, provided));
		return;
	}

	// base
	alignas(8) unsigned char base_buf[sizeof(godot::Variant)];
	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		api_tool::internal::var_to_arg_ptr(*self, base_buf, (godot::Variant::Type)VTC);
		base_ptr = base_buf;
	}

	// marshal args (unrolled; missing slots filled with defaults)
	godot::Variant args[N > 0 ? N : 1];
	if (!marshal_fixed_args<ArgsT...>(isolate, context, info, args, provided,
			std::make_index_sequence<N>{})) {
		return; // js exception already thrown
	}

	// encode into engine buffers
	alignas(8) unsigned char arg_buf[sizeof(godot::Variant) * (N > 0 ? N : 1)] = {};
	GDExtensionConstTypePtr arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(api_tool::internal::var_to_arg_ptr(args[I],
				arg_buf + I * sizeof(godot::Variant), pack_one_type<ArgsT>()), ...);
	}(std::make_index_sequence<N>{});

	// call
	godot::Variant ret;
	init_return<RetT>(ret);
	fn(base_ptr, arg_ptrs, &ret, N);
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg builtin method (6 instances: Callable.call/rpc/..., §4.0-B).
// ArgsT describes the fixed prefix; the tail is collected as untyped
// Variants and the actual argc is forwarded to the engine.
template <uint16_t VTC, uint64_t HashC, FixedString NameLit, bool IsStaticC,
		class RetT, class... ArgsT>
void builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE("static binding: failed to load builtin method "
				+ godot::Variant::get_type_name((godot::Variant::Type)VTC)
				+ "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing builtin method: "
				+ godot::Variant::get_type_name((godot::Variant::Type)VTC)
				+ "::" + godot::String(NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d",
				godot::Variant::get_type_name((godot::Variant::Type)VTC),
				godot::String(NameLit.value).utf8().get_data(), M, provided));
		return;
	}

	alignas(8) unsigned char base_buf[sizeof(godot::Variant)];
	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		api_tool::internal::var_to_arg_ptr(*self, base_buf, (godot::Variant::Type)VTC);
		base_ptr = base_buf;
	}

	// fixed prefix: unrolled with type checks & defaults
	godot::Variant args[F > 0 ? F : 1];
	if (!marshal_fixed_args<ArgsT...>(isolate, context, info, args, provided,
			std::make_index_sequence<F>{})) {
		return;
	}
	// vararg tail: typed as NIL variants, converted without hint
	const int argc = provided > F ? provided : F;
	godot::Variant *tail_args = (godot::Variant *)jsb_stackalloc(godot::Variant, argc > F ? argc - F : 1);
	for (int i = F; i < argc; ++i) {
		memnew_placement(&tail_args[i - F], godot::Variant);
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], tail_args[i - F])) {
			jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			for (int j = F; j < i; ++j) {
				tail_args[j - F].~Variant();
			}
			return;
		}
	}

	// assemble full pointer array: prefix + tail.
	// NOTE: sources stay untouched; encodings land in a separate aligned
	// scratch buffer (same pattern as api_tool's validated_call), never
	// back into the source Variants themselves.
	godot::Variant *all_args = (godot::Variant *)jsb_stackalloc(godot::Variant, argc);
	for (int i = 0; i < F && i < argc; ++i) {
		all_args[i] = args[i];
	}
	for (int i = F; i < argc; ++i) {
		all_args[i] = tail_args[i - F];
	}
	alignas(8) unsigned char *all_buf =
			(unsigned char *)jsb_stackalloc(unsigned char, sizeof(godot::Variant) * (argc > 0 ? argc : 1));
	GDExtensionConstTypePtr *arg_ptrs = (GDExtensionConstTypePtr *)jsb_stackalloc(void *, argc);
	for (int i = 0; i < argc; ++i) {
		// prefix keeps its declared type; tail is untyped (NIL)
		const godot::Variant::Type t = i < F
				? pack_type_at<ArgsT...>(i)
				: godot::Variant::NIL;
		api_tool::internal::var_to_arg_ptr(all_args[i], all_buf + i * sizeof(godot::Variant), t);
		arg_ptrs[i] = all_buf + i * sizeof(godot::Variant);
	}

	godot::Variant ret;
	init_return<RetT>(ret);
	fn(base_ptr, arg_ptrs, &ret, argc);
	translate_return<RetT>(isolate, context, ret, info);
	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
