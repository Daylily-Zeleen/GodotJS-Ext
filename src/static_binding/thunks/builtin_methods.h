#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"
#	include <godot_cpp/variant/variant_internal.hpp>

namespace jsb::static_binding::thunks {

// Compile-time opaque-pointer fetch: VTC is a template parameter here, so
// dispatching through the runtime switch is pure overhead. Mirrors that switch.
template <godot::Variant::Type VTC>
_FORCE_INLINE_ static void *get_opaque_typed(godot::Variant *self) {
	if constexpr (VTC == godot::Variant::NIL) {
		return nullptr;
	} else if constexpr (VTC == godot::Variant::BOOL) {
		return godot::VariantInternal::get_bool(self);
	} else if constexpr (VTC == godot::Variant::INT) {
		return godot::VariantInternal::get_int(self);
	} else if constexpr (VTC == godot::Variant::FLOAT) {
		return godot::VariantInternal::get_float(self);
	} else if constexpr (VTC == godot::Variant::STRING) {
		return godot::VariantInternal::get_string(self);
	} else if constexpr (VTC == godot::Variant::VECTOR2) {
		return godot::VariantInternal::get_vector2(self);
	} else if constexpr (VTC == godot::Variant::VECTOR2I) {
		return godot::VariantInternal::get_vector2i(self);
	} else if constexpr (VTC == godot::Variant::RECT2) {
		return godot::VariantInternal::get_rect2(self);
	} else if constexpr (VTC == godot::Variant::RECT2I) {
		return godot::VariantInternal::get_rect2i(self);
	} else if constexpr (VTC == godot::Variant::VECTOR3) {
		return godot::VariantInternal::get_vector3(self);
	} else if constexpr (VTC == godot::Variant::VECTOR3I) {
		return godot::VariantInternal::get_vector3i(self);
	} else if constexpr (VTC == godot::Variant::TRANSFORM2D) {
		return godot::VariantInternal::get_transform2d(self);
	} else if constexpr (VTC == godot::Variant::VECTOR4) {
		return godot::VariantInternal::get_vector4(self);
	} else if constexpr (VTC == godot::Variant::VECTOR4I) {
		return godot::VariantInternal::get_vector4i(self);
	} else if constexpr (VTC == godot::Variant::PLANE) {
		return godot::VariantInternal::get_plane(self);
	} else if constexpr (VTC == godot::Variant::QUATERNION) {
		return godot::VariantInternal::get_quaternion(self);
	} else if constexpr (VTC == godot::Variant::AABB) {
		return godot::VariantInternal::get_aabb(self);
	} else if constexpr (VTC == godot::Variant::BASIS) {
		return godot::VariantInternal::get_basis(self);
	} else if constexpr (VTC == godot::Variant::TRANSFORM3D) {
		return godot::VariantInternal::get_transform(self);
	} else if constexpr (VTC == godot::Variant::PROJECTION) {
		return godot::VariantInternal::get_projection(self);
	} else if constexpr (VTC == godot::Variant::COLOR) {
		return godot::VariantInternal::get_color(self);
	} else if constexpr (VTC == godot::Variant::STRING_NAME) {
		return godot::VariantInternal::get_string_name(self);
	} else if constexpr (VTC == godot::Variant::NODE_PATH) {
		return godot::VariantInternal::get_node_path(self);
	} else if constexpr (VTC == godot::Variant::RID) {
		return godot::VariantInternal::get_rid(self);
	} else if constexpr (VTC == godot::Variant::OBJECT) {
		return godot::VariantInternal::get_object(self);
	} else if constexpr (VTC == godot::Variant::CALLABLE) {
		return godot::VariantInternal::get_callable(self);
	} else if constexpr (VTC == godot::Variant::SIGNAL) {
		return godot::VariantInternal::get_signal(self);
	} else if constexpr (VTC == godot::Variant::DICTIONARY) {
		return godot::VariantInternal::get_dictionary(self);
	} else if constexpr (VTC == godot::Variant::ARRAY) {
		return godot::VariantInternal::get_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_BYTE_ARRAY) {
		return godot::VariantInternal::get_byte_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_INT32_ARRAY) {
		return godot::VariantInternal::get_int32_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_INT64_ARRAY) {
		return godot::VariantInternal::get_int64_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_FLOAT32_ARRAY) {
		return godot::VariantInternal::get_float32_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_FLOAT64_ARRAY) {
		return godot::VariantInternal::get_float64_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_STRING_ARRAY) {
		return godot::VariantInternal::get_string_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR2_ARRAY) {
		return godot::VariantInternal::get_vector2_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR3_ARRAY) {
		return godot::VariantInternal::get_vector3_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_COLOR_ARRAY) {
		return godot::VariantInternal::get_color_array(self);
	} else if constexpr (VTC == godot::Variant::PACKED_VECTOR4_ARRAY) {
		return godot::VariantInternal::get_vector4_array(self);
	} else {
		static_assert(VTC != godot::Variant::NIL, "unreachable");
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Fixed-arity builtin method (§4.0-A). Parameters marshaled into ptrcall slots
// via a std::tuple so every slot outlives the fn() call.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int N = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = N - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load builtin method %s::%s",
				godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing builtin method: %s::%s", godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M || provided > N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects %d..%d, got %d", godot::Variant::get_type_name(VTC), godot::String(NameLit.value).utf8().get_data(), M, N, provided));
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

	// marshal into ptrcall slots (tuple outlives fn call)
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

	ReturnSlotType_t<RetT> ret{};
	fn(base_ptr, arg_ptrs, &ret, N);
	translate_return<RetT>(isolate, context, ret, info);
}

// ---------------------------------------------------------------------------
// Vararg builtin method (§4.0-B).
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit, bool IsStaticC, class RetT, class... ArgsT>
void builtin_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr int F = (int)sizeof...(ArgsT);
	constexpr int D = (0 + ... + (ArgsT::has_default ? 1 : 0));
	constexpr int M = F - D;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	GDExtensionPtrBuiltInMethod fn = resolve_builtin_method<VTC, HashC, NameLit>();
	if (!fn) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load builtin method %s::%s",
				godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
		jsb_throw(isolate, jsb_errorf("missing builtin method: %s::%s", godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
		return;
	}

	const int provided = (int)info.Length();
	if (provided < M) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s::%s expects >= %d, got %d", godot::Variant::get_type_name(VTC), godot::String(NameLit.value).utf8().get_data(), M, provided));
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

	// fixed prefix slots
	std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> prefix_slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(prefix_slots), provided)) && ...);
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
			for (int j = F; j < i; ++j) {
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

	ReturnSlotType_t<RetT> ret{};
	for (int _i = 0; _i < argc && _i < 64; ++_i) {
		arg_ptrs[_i] = nullptr;
	}
	fn(base_ptr, arg_ptrs, &ret, argc);

	for (int i = F; i < argc; ++i) {
		tail_args[i - F].~Variant();
	}
	translate_return<RetT>(isolate, context, ret, info);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS