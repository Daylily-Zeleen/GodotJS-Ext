#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "runtime/internal/jsb_variant_util.h"
#	include "thunks_common.h"
#	include <godot_cpp/variant/variant_internal.hpp>


namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Builtin member accessors (§4.1): fixed Variant-type members such as
// Vector2::x. The GDExtensionPtrGetter/Setter function pointers are resolved
// once per instantiation via variant_get_ptr_getter/setter (magic static).
//
// CRITICAL: Member getters/setters use the OPAQUE PTRCALL ABI (not full Variant).
// The engine's variant_get_ptr_getter returns ptr_get which expects:
//   p_base = opaque data pointer (e.g., &variant.data.vector2 for Vector2)
//   r_value = member type's native slot (PtrToArg<MemberType>::EncodeT)
// This mirrors VariantSetGet_*::ptr_get in variant_setget.h:
//   PtrToArg<MemberType>::encode(PtrToArg<BaseType>::convert(p_base).member, r_value)
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

template <godot::Variant::Type VTC, godot::Variant::Type MemberVT, FixedString NameLit>
void member_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrGetter getter = resolve_member_getter<VTC, NameLit>();
	if (!getter) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load member getter %s::%s",
				godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
		jsb_throw(isolate, "missing member getter");
		return;
	}

	const Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);

	// Get opaque pointer to the base Variant's internal data
	void *base_opaque = get_opaque_typed<VTC>(const_cast<Variant *>(p_self));

	// Member getter uses OPAQUE PTRCALL ABI: base is opaque pointer, value is native encode type
	VariantEncodeType<VariantNativeType_t<MemberVT>> ret_val{};
	getter((GDExtensionConstTypePtr)base_opaque, (GDExtensionTypePtr)&ret_val);

	// Convert native slot back to Variant for JS
	const godot::Variant result = godot::PtrToArg<VariantNativeType_t<MemberVT>>::convert(&ret_val);

	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, result, rval)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

template <godot::Variant::Type VTC, godot::Variant::Type MemberVT, FixedString NameLit>
void member_setter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const GDExtensionPtrSetter setter = resolve_member_setter<VTC, NameLit>();
	if (!setter) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load member setter %s::%s",
				godot::Variant::get_type_name(VTC).utf8().get_data(), NameLit.value));
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

	// Get opaque pointer to the base Variant's internal data
	void *base_opaque = get_opaque_typed<VTC>(p_self);

	// Member setter uses OPAQUE PTRCALL ABI
	VariantEncodeType<VariantNativeType_t<MemberVT>> encoded_value{};
	godot::PtrToArg<VariantNativeType_t<MemberVT>>::encode(value, &encoded_value);
	setter((GDExtensionTypePtr)base_opaque, (GDExtensionConstTypePtr)&encoded_value);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
