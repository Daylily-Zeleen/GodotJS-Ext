/************************************************************************/
/*  builtin_members.h                                                   */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "internal/jsb_variant_util.h"
#	include "thunks_common.h"
#	include <godot_cpp/variant/variant_internal.hpp>

#	include <atomic>

namespace jsb::static_binding::thunks {

template <godot::Variant::Type VTC, typename T, FixedString NameLit>
void member_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrGetter getter = ::godot::gdextension_interface::variant_get_ptr_getter(
			(GDExtensionVariantType)VTC,
			godot::StringName(NameLit.value)._native_ptr());

	if (!getter) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load member getter %s::%s: missing member getter", godot::Variant::get_type_name(VTC), NameLit.value));
		return;
	}

	const Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);

	// Get opaque pointer to the base Variant's internal data
	void *base_opaque = get_opaque_typed<VTC>(const_cast<Variant *>(p_self));

	// Ptrcall ABI: base points into the Variant; the result uses EncodeT storage.
	// `T` is the member's concrete C++ type (the same one the engine's named
	// member getter encodes -- the generated instantiation carries it), so the
	// result crosses through `GDToJS<T>` with no Variant in between.
	VariantEncodeType<T> ret_val{};
	getter((GDExtensionConstTypePtr)base_opaque, (GDExtensionTypePtr)&ret_val);

	const T value = godot::PtrToArg<T>::convert(&ret_val);
	v8::Local<v8::Value> rval;
	if (!GDToJS<T>::convert(isolate, context, value, rval)) {
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

	static const GDExtensionPtrSetter setter = ::godot::gdextension_interface::variant_get_ptr_setter(
			(GDExtensionVariantType)VTC,
			godot::StringName(NameLit.value)._native_ptr());

	if (!setter) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load member setter %s::%s: missing member setter", godot::Variant::get_type_name(VTC), NameLit.value));
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

	// The setter reads an EncodeT value, not a boxed Variant.
	VariantEncodeType<VariantNativeType_t<MemberVT>> encoded_value{};
	godot::PtrToArg<VariantNativeType_t<MemberVT>>::encode(value, &encoded_value);
	setter((GDExtensionTypePtr)base_opaque, (GDExtensionConstTypePtr)&encoded_value);
}

#	if JSB_WITH_SHARED_THUNKS
// ---------------------------------------------------------------------------
// Signature-shared builtin member accessors (binding_mode=shared): one getter
// thunk per unique (VTC, concrete C++ member type) signature and one setter
// thunk per unique (VTC, Variant member type) signature (19 each); the member identity
// (name) and the eagerly-resolved getter/setter ptrcall functions arrive
// through info.Data() (SharedMemberAccessorData) instead of template params.

struct SharedMemberAccessorData {
	mutable std::atomic<GDExtensionPtrGetter> getter; // eagerly resolved at mount
	mutable std::atomic<GDExtensionPtrSetter> setter; // eagerly resolved at mount
	const char *name; // error text
};

// Resolve-and-cache both accessor ptrcall functions once at mount time. Same
// worker-environment CAS semantics as ensure_builtin_method.
_FORCE_INLINE_ bool ensure_member_accessor(godot::Variant::Type p_vt, const godot::StringName &p_name, SharedMemberAccessorData &md) {
	GDExtensionPtrGetter getter = md.getter.load(std::memory_order_relaxed);
	GDExtensionPtrSetter setter = md.setter.load(std::memory_order_relaxed);
	if (getter && setter) {
		return true;
	}
	GDExtensionPtrGetter new_getter = ::godot::gdextension_interface::variant_get_ptr_getter(
			(GDExtensionVariantType)p_vt, p_name._native_ptr());
	GDExtensionPtrSetter new_setter = ::godot::gdextension_interface::variant_get_ptr_setter(
			(GDExtensionVariantType)p_vt, p_name._native_ptr());
	if (!new_getter || !new_setter) {
		return false;
	}
	md.getter.compare_exchange_strong(getter, new_getter, std::memory_order_relaxed);
	md.setter.compare_exchange_strong(setter, new_setter, std::memory_order_relaxed);
	return true;
}

template <godot::Variant::Type VTC, typename T>
void shared_member_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedMemberAccessorData &md =
			*static_cast<const SharedMemberAccessorData *>(info.Data().As<v8::External>()->Value());

	const GDExtensionPtrGetter getter = md.getter.load(std::memory_order_relaxed);
	if (!getter) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load member getter %s::%s: missing member getter", godot::Variant::get_type_name(VTC), md.name));
		return;
	}

	const Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
	void *base_opaque = get_opaque_typed<VTC>(const_cast<Variant *>(p_self));

	VariantEncodeType<T> ret_val{};
	getter((GDExtensionConstTypePtr)base_opaque, (GDExtensionTypePtr)&ret_val);

	const T value = godot::PtrToArg<T>::convert(&ret_val);
	v8::Local<v8::Value> rval;
	if (!GDToJS<T>::convert(isolate, context, value, rval)) {
		jsb_throw(isolate, "bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

template <godot::Variant::Type VTC, godot::Variant::Type MemberVT>
void shared_member_setter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const SharedMemberAccessorData &md =
			*static_cast<const SharedMemberAccessorData *>(info.Data().As<v8::External>()->Value());

	const GDExtensionPtrSetter setter = md.setter.load(std::memory_order_relaxed);
	if (!setter) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load member setter %s::%s: missing member setter", godot::Variant::get_type_name(VTC), md.name));
		return;
	}

	Variant *p_self = (Variant *)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
	jsb_check(p_self->get_type() == VTC);

	Variant value;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], (godot::Variant::Type)MemberVT, value)) {
		jsb_throw(isolate, "bad translate");
		return;
	}

	void *base_opaque = get_opaque_typed<VTC>(p_self);

	VariantEncodeType<VariantNativeType_t<MemberVT>> encoded_value{};
	godot::PtrToArg<VariantNativeType_t<MemberVT>>::encode(value, &encoded_value);
	setter((GDExtensionTypePtr)base_opaque, (GDExtensionConstTypePtr)&encoded_value);
}
#	endif // JSB_WITH_SHARED_THUNKS

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
