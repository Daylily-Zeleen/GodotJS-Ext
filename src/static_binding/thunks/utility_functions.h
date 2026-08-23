#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Global utility function (§4.2). Utility functions carry no default
// arguments today, so argc is validated strictly.
template <uint32_t HashC, FixedString NameLit, class RetT, class... ArgsT>
void utility_function_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrUtilityFunction fn = resolve_utility_function<HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE("static binding: failed to load utility function " + godot::String(NameLit.value));
		jsb_throw(isolate, "missing utility function: " + godot::String(NameLit.value));
		return;
	}

	// strict argc gate (utility functions carry no defaults)
	if ((int)info.Length() != N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s expects %d, got %d",
				godot::String(NameLit.value).utf8().get_data(), N, (int)info.Length()));
		return;
	}

	const int provided = (int)info.Length();
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
	fn(&ret, arg_ptrs, N);
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
