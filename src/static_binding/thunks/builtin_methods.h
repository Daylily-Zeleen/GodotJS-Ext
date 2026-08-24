#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Fixed-arity builtin method (§4.0-A: unrolled per-parameter, no loops).
// Parameters are marshaled into ptrcall slots (SlotOf<T> = PtrToArg<T>::EncodeT)
// through the official encode() contract.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit,
		bool IsStaticC, class RetT, class... ArgsT>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE("static binding: failed to load builtin method "
				+ godot::Variant::get_type_name(VTC)
				+ "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing builtin method: "
				+ godot::Variant::get_type_name(VTC)
				+ "::" + godot::String(NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d",
				godot::Variant::get_type_name(VTC),
				godot::String(NameLit.value).utf8().get_data(), M, N, provided));
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
		base_ptr = self; // a Variant is the engine layout itself
	}

	// marshal into ptrcall slots (unrolled)
	std::tuple<SlotOf<typename ArgsT::gd_type>...> slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I,
						std::get<I>(slots), provided)) &&
				...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // JS exception already thrown by marshal_one
	}

	void *arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	init_return<RetT>(ret);
	fn(base_ptr, arg_ptrs, &ret, N);
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg builtin method (§4.0-B): fixed prefix unrolled, only the tail loops.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit,
		bool IsStaticC, class RetT, class... ArgsT>
void builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE("static binding: failed to load builtin method "
				+ godot::Variant::get_type_name(VTC)
				+ "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing builtin method: "
				+ godot::Variant::get_type_name(VTC)
				+ "::" + godot::String(NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d",
				godot::Variant::get_type_name(VTC),
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
	std::tuple<SlotOf<typename ArgsT::gd_type>...> prefix_slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I,
						std::get<I>(prefix_slots), provided)) &&
				...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

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

	// argument pointers must cover ALL argc slots
	void **arg_ptrs = (void **)jsb_stackalloc(void *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(prefix_slots)), ...);
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
