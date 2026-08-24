#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"
#include "runtime/internal/jsb_variant_util.h"

namespace jsb::static_binding::thunks {

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

template <godot::Variant::Type VTC, FixedString NameLit>
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

	Variant value;
	internal::VariantUtil::construct_variant(value, VTC);
	//NOTE the getter will not touch the Variant type tag, so it must be set properly first
	getter((GDExtensionConstTypePtr)p_self, (GDExtensionTypePtr)&value);

	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, value, rval)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

template <godot::Variant::Type VTC, FixedString NameLit>
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
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], VTC, value)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	setter((GDExtensionTypePtr)p_self, (GDExtensionConstTypePtr)&value);
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
