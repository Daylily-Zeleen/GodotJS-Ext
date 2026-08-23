/************************************************************************/
/*  utility_functions.h                                                 */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Thunk template for global utility functions (design doc §4.2).      */
/*  Utility functions have no default arguments today (engine-side      */
/*  convention), so argc is validated strictly.                         */
/************************************************************************/

#pragma once

#ifdef JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

namespace jsb::static_binding::thunks {

template <uint64_t HashC, FixedString NameLit, class RetT, class... ArgsT>
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

	godot::Variant args[N > 0 ? N : 1];
	if (!marshal_fixed_args<ArgsT...>(isolate, context, info, args, (int)info.Length(),
			std::make_index_sequence<N>{})) {
		return;
	}

	alignas(8) unsigned char arg_buf[sizeof(godot::Variant) * (N > 0 ? N : 1)] = {};
	GDExtensionConstTypePtr arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(api_tool::internal::var_to_arg_ptr(args[I],
				arg_buf + I * sizeof(godot::Variant), ArgsT::vt), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	init_return<RetT>(ret);
	fn(&ret, arg_ptrs, N);
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
