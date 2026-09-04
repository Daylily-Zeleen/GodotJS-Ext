#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Global utility function (§4.2). Utility functions carry no default args.
template <uint32_t HashC, FixedString NameLit, class RetT, class... ArgsT>
void utility_function_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrUtilityFunction fn = resolve_utility_function<HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load utility function %s", NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing utility function: %s", NameLit.value));
		return;
	}

	if ((int)info.Length() != N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s expects %d, got %d", godot::String(NameLit.value).utf8().get_data(), N, (int)info.Length()));
		return;
	}

	const int provided = (int)info.Length();
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
	fn(&ret_val, arg_ptrs, N);
	translate_return<RetT>(isolate, context, ret_val, info);
}

// ---------------------------------------------------------------------------
// Vararg utility function (§4.0-B).
template <uint32_t HashC, FixedString NameLit, class RetT, class... ArgsT>
void utility_vararg_function_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrUtilityFunction fn = resolve_utility_function<HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load utility function %s", NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing utility function: %s", NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < F) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s expects >= %d, got %d", godot::String(NameLit.value).utf8().get_data(), F, provided));
		return;
	}

	std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> prefix_slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(prefix_slots), provided)) && ...);
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

	ReturnEncodeType<RetT> ret_val{};
	fn(&ret_val, arg_ptrs, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	translate_return<RetT>(isolate, context, ret_val, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS