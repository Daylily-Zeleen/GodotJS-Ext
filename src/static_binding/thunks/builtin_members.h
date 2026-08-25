#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"
#include <godot_cpp/variant/variant_internal.hpp>
#include "runtime/internal/jsb_variant_util.h"

namespace jsb::static_binding::thunks {

// Compile-time opaque-pointer fetch: VTC is a template parameter here, so
// dispatching through VariantInternal::get_opaque_pointer's runtime switch
// would be pure overhead. Mirrors that switch exactly.
template <godot::Variant::Type VTC>
_FORCE_INLINE_ static void *get_member_opaque_typed(godot::Variant *self) {
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
// Builtin member accessors (§4.1): fixed Variant-type members such as
// Vector2::x. The GDExtensionPtrGetter/Setter function pointers are resolved
// once per instantiation via variant_get_ptr_getter/setter (magic static).
//
// Semantics mirror the dynamic _getter/_setter in the primitive reflect
// binder: the base Variant is read from `this`, the value is converted with
// the strict JS->T / GD->JS converters.
// ---------------------------------------------------------------------------
template <godot::Variant::Type VTC, FixedString NameLit>
GDExtensionPtrGetter resolve_member_getter() {
	static GDExtensionPtrGetter getter =
			::godot::gdextension_interface::variant_get_ptr_getter(
					(GDExtensionVariantType)VTC,
					godot::StringName(NameLit.value)._native_ptr());
	return getter;
}

template <godot::Variant::Type VTC, FixedString NameLit>
GDExtensionPtrSetter resolve_member_setter() {
	static GDExtensionPtrSetter setter =
			::godot::gdextension_interface::variant_get_ptr_setter(
					(GDExtensionVariantType)VTC,
					godot::StringName(NameLit.value)._native_ptr());
	return setter;
}

template <godot::Variant::Type VTC, godot::Variant::Type MemberVT,
		FixedString NameLit>
void member_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrGetter getter = resolve_member_getter<VTC, NameLit>();
	if (!getter) {
		ERR_PRINT_ONCE("static binding: failed to load member getter "
				+ godot::Variant::get_type_name(VTC) + "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing member getter");
		return;
	}

	const Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);

	// Both base and result are NATIVE typed slots (mirrors
	// ApiMemberInfo::getter_validated_call: var_to_arg_ptr -> getter ->
	// arg_ptr_to_var). Passing the Variant itself as either slot corrupts it:
	// reference-type members deref garbage, value types read shifted data.
	using Native = member_value_t<(int)MemberVT>;
	VtSlotType<(int)MemberVT> base_slot{};
	godot::PtrToArg<Native>::encode(*p_self, &base_slot);
	VtSlotType<(int)MemberVT> ret_slot{};
	getter((GDExtensionConstTypePtr)&base_slot, (GDExtensionTypePtr)&ret_slot);
	{
		const double *dbg = reinterpret_cast<const double *>(&ret_slot);
		JSB_LOG(Warning, "DXR ret=[0]=%f", dbg[0]);
	}

	v8::Local<v8::Value> rval;
	const godot::Variant result = godot::PtrToArg<Native>::convert(&ret_slot);
	if (!TypeConvert::gd_var_to_js(isolate, context, result, rval)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

template <godot::Variant::Type VTC, godot::Variant::Type MemberVT,
		FixedString NameLit>
void member_setter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrSetter setter = resolve_member_setter<VTC, NameLit>();
	if (!setter) {
		ERR_PRINT_ONCE("static binding: failed to load member setter "
				+ godot::Variant::get_type_name(VTC) + "::" + godot::String(NameLit.value));
		jsb_throw(isolate, "missing member setter");
		return;
	}

	Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
	jsb_check(p_self->get_type() == VTC);

	Variant value;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], (godot::Variant::Type)MemberVT, value)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	// Native slots for both sides (see the getter comment).
	using Native = member_value_t<(int)MemberVT>;
	VtSlotType<(int)VTC> base_slot{};
	godot::PtrToArg<Native>::encode(*p_self, &base_slot);
	VtSlotType<(int)MemberVT> val_slot{};
	godot::PtrToArg<Native>::encode(value, &val_slot);
	setter((GDExtensionTypePtr)&base_slot, (GDExtensionConstTypePtr)&val_slot);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
