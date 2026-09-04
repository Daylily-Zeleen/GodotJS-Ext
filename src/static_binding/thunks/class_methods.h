#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"

#	include <array>
#	include <godot_cpp/classes/object.hpp>

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
// Fixed-arity class method (§4.0-A).
//
// MISSING DEFAULTS ARE *NOT* FILLED HERE: json default_value strings are
// lossy for String-typed parameters (type_hint's "" arrives as two quote
// chars), while the engine MethodBind carries the authoritative defaults.
// The dynamic path does the same -- it passes the caller-supplied argc and
// lets object_method_bind_call apply engine-side defaults. Only the
// caller-provided arguments are marshaled; argc == provided.
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionMethodBindPtr method_bind = resolve_class_method<HashC, ClassLit, NameLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s",
				ClassLit.value, NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, NameLit.value));
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
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", ClassLit.value, NameLit.value));
			return;
		}
	}

	// marshal ONLY the caller-provided arguments; the engine fills defaults
	std::tuple<typename ArgsT::gd_type...> storage;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && (sizeof...(ArgsT) <= (size_t)provided
				|| (int)I < provided
				? produce_value<ArgsT>(isolate, context, info, (int)I, std::get<I>(storage), provided)
				: true)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // JS exception already thrown by produce_value
	}

	// move the provided arguments into the Variant array required by the ABI
	godot::Variant argv[N > 0 ? N : 1];
	const godot::Variant *arg_ptrs[N > 0 ? N : 1];
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)((argv[I] = std::get<I>(storage), arg_ptrs[I] = &argv[I])), ...);
	}(std::make_index_sequence<N>{});

	godot::Variant ret;
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, IsStaticC ? nullptr : instance->_owner, (const GDExtensionConstVariantPtr *)arg_ptrs, provided, &ret, &call_error);
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", ClassLit.value, NameLit.value, (int)call_error.error));
		return;
	}
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg class method (§4.0-B): fixed prefix unrolled, only the tail loops.
// Defaults are engine-side (see the fixed-arity comment above): the fixed
// prefix is marshaled only up to min(provided, F).
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void class_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionMethodBindPtr method_bind = resolve_class_method<HashC, ClassLit, NameLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s",
				ClassLit.value, NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, NameLit.value));
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
			jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. Bad this", ClassLit.value, NameLit.value));
			return;
		}
	}

	// fixed prefix: Variant slots (the MethodBind ABI takes const Variant*),
	// held in an RAII tuple so failure paths need no hand-rolled cleanup.
	// Typed conversion goes to a local gd_type inside produce_variant, then
	// converts into the slot.
	std::array<godot::Variant, F> prefix;
	const int fixed_count = provided < F ? provided : F;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = ok && ((int)I < fixed_count
				? produce_variant<ArgsT>(isolate, context, info, (int)I, prefix[I], provided)
				: true)) && ...);
	}(std::make_index_sequence<F>{});
	if (!ok) {
		return;
	}

	// vararg tail: untyped Variants beyond the fixed prefix -- the only part
	// living in raw stack memory (count is runtime-bounded), hence the only
	// part needing placement-new / hand destruction
	const int argc = provided;
	godot::Variant *tail_args = (godot::Variant *)jsb_stackalloc(godot::Variant, argc > F ? argc - F : 1);
	const godot::Variant **arg_ptrs =
			(const godot::Variant **)jsb_stackalloc(const godot::Variant *, argc > 0 ? argc : 1);
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)((int)I < fixed_count
				? (void)(arg_ptrs[I] = &prefix[I])
				: (void)0), ...);
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

	// prefix slots are RAII tuple members (destroyed at scope exit); only the
	// raw-memory tail slots need hand destruction -- their constructed range
	// is the contiguous [0, argc - F)
	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to call: %s::%s. engine error %d", ClassLit.value, NameLit.value, (int)call_error.error));
		return;
	}
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS