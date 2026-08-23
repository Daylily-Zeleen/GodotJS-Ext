#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Fixed-arity builtin method (§4.0-A: unrolled per-parameter, no loops).
// Parameters live as strongly-typed values in a tuple; each element's address
// is handed to the engine directly -- godot-cpp types are engine-layout
// mirrors and PtrToArg<T>::EncodeT == T for every parameter type we emit,
// so no separate encoding buffer exists.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC,
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

	// argc gate: M <= provided <= N
	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d",
				godot::Variant::get_type_name((godot::Variant::Type)VTC),
				godot::String(NameLit.value).utf8().get_data(), M, N, provided));
		return;
	}

	// base: the wrapper's Variant IS the engine layout, hand its address over
	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		base_ptr = self;
	}

	// marshal into strongly-typed storage (unrolled, §4.0-A)
	std::tuple<SlotOf<typename ArgsT::gd_type>...> storage;
		[&]<std::size_t... I>(std::index_sequence<I...>) {
		bool ok = true;
		(void)((ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I,
				std::get<I>(storage), provided)) && ...);
		return ok;
	}(std::make_index_sequence<N>{});


	void *arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(storage)), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	init_return<RetT>(ret);
	fn(base_ptr, arg_ptrs, &ret, N);
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg builtin method (§4.0-B): fixed prefix unrolled, only the tail loops.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC,
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

	void *base_ptr = nullptr;
	if constexpr (!IsStaticC) {
		godot::Variant *self = TypeConvert::is_variant(info.This())
				? (godot::Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
				: nullptr;
		if (!self) {
			jsb_throw(isolate, "no bound this");
			return;
		}
		base_ptr = self;
	}

	// fixed prefix: unrolled with type checks & defaults
	std::tuple<typename ArgsT::gd_type...> prefix;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		bool ok = true;
		(void)((ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I,
				std::get<I>(prefix), provided)) && ...);
		return ok;
	}(std::make_index_sequence<F>{});

	// vararg tail: untyped Variants (the only loop, §4.0-B)
	const int argc = provided > F ? provided : F;
	godot::Variant *tail_args =
			(godot::Variant *)jsb_stackalloc(godot::Variant, argc > F ? argc - F : 1);
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

	// argument pointers must cover ALL argc slots: prefix elements are already
	// engine-layout values; tail elements are full Variants (NIL slots).
	void **arg_ptrs = (void **)jsb_stackalloc(void *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(prefix)), ...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		arg_ptrs[i] = &tail_args[i - F];
	}

	godot::Variant ret;
	init_return<RetT>(ret);
	fn(base_ptr, arg_ptrs, &ret, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
