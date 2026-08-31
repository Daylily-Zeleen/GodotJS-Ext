#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"

#	include <godot_cpp/classes/object.hpp>

namespace jsb::static_binding::thunks {

// ---------------------------------------------------------------------------
// Indexed property accessors (§4.0-C). One template instance per property
// side -- the constant index CANNOT live on the shared backing method (a
// single engine method typically serves many indexes), so these are separate
// from class_method_thunk and bind to the JS property accessor itself.
//
// Semantics mirror ObjectReflectBindingUtil::_godot_object_get2/_set2:
// the index is prepended as the first Variant argument of the backing
// method's Variant array.
// ---------------------------------------------------------------------------
template <uint32_t HashC, FixedString ClassLit, FixedString MethodLit, int IndexC>
void indexed_property_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if ((int)info.Length() != 0) {
		jsb_throw(isolate, jsb_errorf("Failed to get property: %s::%s. Arguments unexpectedly provided", ClassLit.value, MethodLit.value));
		return;
	}

	GDExtensionMethodBindPtr method_bind =
			resolve_class_method<HashC, ClassLit, MethodLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s", ClassLit.value, MethodLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, MethodLit.value));
		return;
	}

	godot::Object *instance = nullptr;
	if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
		jsb_throw(isolate, jsb_errorf("Failed to get property: %s::%s. Bad this", ClassLit.value, MethodLit.value));
		return;
	}

	godot::Variant argv[] = { godot::Variant((int64_t)IndexC) };
	const godot::Variant *arg_ptrs[] = { &argv[0] };

	godot::Variant ret;
	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, instance->_owner, (const GDExtensionConstVariantPtr *)arg_ptrs, 1, &ret, &call_error);
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to get property: %s::%s. Execution failed", ClassLit.value, MethodLit.value));
		return;
	}
	v8::Local<v8::Value> jrval;
	if (TypeConvert::gd_var_to_js(isolate, context, ret, jrval)) {
		info.GetReturnValue().Set(jrval);
		return;
	}
	jsb_throw(isolate, jsb_errorf("Failed to get property: %s::%s. Failed to translate returned Godot %s", ClassLit.value, MethodLit.value, godot::Variant::get_type_name(ret.get_type()).utf8().get_data()));
}

template <uint32_t HashC, FixedString ClassLit, FixedString MethodLit, int IndexC, int ArgVTC>
void indexed_property_setter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	constexpr auto arg_vt = (godot::Variant::Type)ArgVTC;

	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if ((int)info.Length() != 1) {
		jsb_throw(isolate, jsb_errorf("Failed to set property: %s::%s. 1 argument is required", ClassLit.value, MethodLit.value));
		return;
	}

	GDExtensionMethodBindPtr method_bind =
			resolve_class_method<HashC, ClassLit, MethodLit>();
	if (!method_bind) {
		ERR_PRINT_ONCE(jsb_errorf("static binding: failed to load method bind %s::%s", ClassLit.value, MethodLit.value));
		jsb_throw(isolate, jsb_errorf("missing method bind: %s::%s", ClassLit.value, MethodLit.value));
		return;
	}

	godot::Object *instance = nullptr;
	if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), instance) || !instance) {
		jsb_throw(isolate, jsb_errorf("Failed to set property: %s::%s. Bad this", ClassLit.value, MethodLit.value));
		return;
	}

	godot::Variant value;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], arg_vt, value)) {
		jsb_throw(isolate, jsb_errorf("Failed to set property: %s::%s. Unable to convert provided JS value to Godot %s", ClassLit.value, MethodLit.value, godot::Variant::get_type_name(arg_vt).utf8().get_data()));
		return;
	}

	godot::Variant argv[] = { godot::Variant((int64_t)IndexC), std::move(value) };
	const godot::Variant *arg_ptrs[] = { &argv[0], &argv[1] };

	GDExtensionCallError call_error{};
	::godot::gdextension_interface::object_method_bind_call(
			method_bind, instance->_owner, (const GDExtensionConstVariantPtr *)arg_ptrs, 2, nullptr, &call_error);
	if (call_error.error != GDEXTENSION_CALL_OK) {
		jsb_throw(isolate, jsb_errorf("Failed to set property: %s::%s. Execution failed", ClassLit.value, MethodLit.value));
	}
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
