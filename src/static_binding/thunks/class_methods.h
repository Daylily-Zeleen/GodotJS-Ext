#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"

#include <godot_cpp/classes/object.hpp>

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Shared prologue: resolve the MethodBind once per instantiation via
// classdb_get_method_bind(ClassName, MethodName, hash). The StringNames are
// one-shot temporaries -- nothing retains them.
inline GDExtensionMethodBindPtr resolve_class_method_bind(
        const char *p_class_name, const char *p_method_name, uint32_t p_hash) {
	const godot::StringName cls(p_class_name);
	const godot::StringName name(p_method_name);
	return ::godot::gdextension_interface::classdb_get_method_bind(
			cls._native_ptr(), name._native_ptr(), (GDExtensionInt)p_hash);
}

template <uint32_t HashC, FixedString ClassLit, FixedString NameLit>
GDExtensionMethodBindPtr resolve_class_method() {
	static GDExtensionMethodBindPtr mb =
			resolve_class_method_bind(ClassLit.value, NameLit.value, HashC);
	return mb;
}

// ---------------------------------------------------------------------------
// Fixed-arity class method (§4.0-A: unrolled per-parameter, no loops).
// Values are produced by the direct JS->T converters into a tuple, then moved
// into the Variant array that object_method_bind_call's ABI requires.
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit,
		bool IsStaticC, class RetT, class... ArgsT>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionMethodBindPtr method_bind = resolve_class_method<HashC, ClassLit, NameLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE("static binding: failed to load method bind "
				+ godot::String(ClassLit.value) + "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing method bind: "
				+ godot::String(ClassLit.value) + "::" + godot::String(NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d",
				godot::String(ClassLit.value).utf8().get_data(),
				godot::String(NameLit.value).utf8().get_data(), M, N, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, "Failed to call: " + godot::String(ClassLit.value)
					+ "::" + godot::String(NameLit.value) + ". Bad this");
			return;
		}
	}

	// strongly-typed storage (unrolled marshaling)
	std::tuple<typename ArgsT::gd_type...> storage;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && produce_value<ArgsT>(isolate, context, info, (int)I,
						std::get<I>(storage), provided)) &&
				...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // JS exception already thrown by marshal_one
	}

	// move into the Variant array required by the method-bind ABI
	godot::Variant argv[N > 0 ? N : 1];
	const godot::Variant *arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)((argv[I] = std::get<I>(storage), arg_ptrs[I] = &argv[I])), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	init_return<RetT>(ret);
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, instance, (const GDExtensionConstVariantPtr *)arg_ptrs, N, &ret, &call_error);
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, "Failed to call: " + godot::String(ClassLit.value)
				+ "::" + godot::String(NameLit.value));
		return;
	}
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg class method (§4.0-B): fixed prefix unrolled, only the tail loops.
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit,
		bool IsStaticC, class RetT, class... ArgsT>
void class_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionMethodBindPtr method_bind = resolve_class_method<HashC, ClassLit, NameLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE("static binding: failed to load method bind "
				+ godot::String(ClassLit.value) + "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing method bind: "
				+ godot::String(ClassLit.value) + "::" + godot::String(NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d",
				godot::String(ClassLit.value).utf8().get_data(),
				godot::String(NameLit.value).utf8().get_data(), M, provided));
		return;
	}

	godot::Object *instance = nullptr;
	if constexpr (!IsStaticC) {
		if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
			jsb_throw(isolate, "Failed to call: " + godot::String(ClassLit.value)
					+ "::" + godot::String(NameLit.value) + ". Bad this");
			return;
		}
	}

	// fixed prefix: unrolled with type checks & defaults
	std::tuple<typename ArgsT::gd_type...> prefix;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && produce_value<ArgsT>(isolate, context, info, (int)I,
						std::get<I>(prefix), provided)) &&
				...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

	// vararg tail: untyped Variants (the only loop, §4.0-B)
	const int argc = provided > F ? provided : F;
	godot::Variant *argv = (godot::Variant *)jsb_stackalloc(godot::Variant, argc > 0 ? argc : 1);
	const godot::Variant **arg_ptrs =
			(const godot::Variant **)jsb_stackalloc(const godot::Variant *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(argv[I] = std::get<I>(prefix)), ...);
	}(std::make_index_sequence<F>{});
	for (int i = F; i < argc; ++i) {
		memnew_placement(&argv[i], godot::Variant);
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], argv[i])) {
			jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			for (int j = F; j <= i; ++j) {
				argv[j].~Variant();
			}
			return;
		}
		arg_ptrs[i] = &argv[i];
	}
	for (int i = 0; i < F && i < argc; ++i) {
		arg_ptrs[i] = &argv[i];
	}

	godot::Variant ret;
	init_return<RetT>(ret);
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, instance, (const GDExtensionConstVariantPtr *)arg_ptrs, argc, &ret, &call_error);

	for (int i = F; i < argc; ++i) {
		argv[i].~Variant();
	}
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, "Failed to call: " + godot::String(ClassLit.value)
				+ "::" + godot::String(NameLit.value));
		return;
	}
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
