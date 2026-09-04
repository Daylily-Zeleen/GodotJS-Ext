#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "thunks_common.h"
#include "../dispatch.h"
#include <godot_cpp/variant/variant_internal.hpp>

// ---------------------------------------------------------------------------
// Operator thunks (static path).
//
// One thunk instance per (operator, left type, right type, return type)
// overload from the generated operator table. Operands go straight from the
// v8 values into ptrcall slots -- zero Variant materialization on the operand
// path:
//   - left:  the variant-backed wrapper of exactly L; its backing Variant's
//            internal value doubles as the opaque ptrcall slot
//   - right: filled per R's compile-time type from the v8 value
//            (IsInt32 -> INT slot, IsNumber -> FLOAT slot, wrapper -> backing)
// The engine's registered operator evaluator is resolved once per
// instantiation (magic static) and called through the opaque ptrcall ABI.
//
// The generated dispatch table (dispatch_operators.gen.cpp) selects the thunk
// by (left type, operator, right type); combinations without an engine
// evaluator never enter the table, so no per-call fallback logic is needed
// here beyond the defensive type checks.
// ---------------------------------------------------------------------------

namespace jsb::static_binding {

// the variant-backed wrapper of exactly L, or null
template <typename L>
const Variant *left_backing_of(const v8::Local<v8::Value> &val) {
	if (!val->IsObject()) return nullptr;
	const v8::Local<v8::Object> obj = val.As<v8::Object>();
	if (obj->InternalFieldCount() != IF_VariantFieldCount) return nullptr;
	const Variant *v = (const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer);
	if (v->get_type() != (Variant::Type)GetTypeInfo<L>::VARIANT_TYPE) return nullptr;
	return v;
}

template <typename L>
void *left_opaque_of(Variant *v) {
	if constexpr (std::is_same_v<L, godot::Variant>) {
		return v;
	}
	return godot::VariantInternal::get_internal_value<L>(v);
}

// expected Variant::Type from the v8 value's shape; JS semantics map 1:1 to
// the engine's INT/FLOAT split (IsInt32 and IsNumber are mutually exclusive,
// so the generated overload order is irrelevant to the selection)
template <typename L>
Variant::Type probe_vt(const v8::Local<v8::Value> &val) {
	if (val->IsInt32()) return Variant::INT;
	if (val->IsNumber()) return Variant::FLOAT;
	if (val->IsBoolean()) return Variant::BOOL;
	if (val->IsString()) return Variant::STRING;
	if (val->IsNullOrUndefined()) return Variant::NIL;
	if (val->IsObject()) {
		const v8::Local<v8::Object> obj = val.As<v8::Object>();
		if (obj->InternalFieldCount() == IF_VariantFieldCount) {
			return ((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type();
		}
		if (obj->InternalFieldCount() == IF_ObjectFieldCount) return Variant::OBJECT;
	}
	return Variant::VARIANT_MAX;
}

// binary operator thunk
template <Variant::Operator OpC, typename L, typename R, typename Ret>
void operator_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant *left_var = left_backing_of<L>(info[0]);
	if (!left_var) {
		jsb_throw(isolate, "operator: bad left operand");
		return;
	}
	void *left_opaque = left_opaque_of<L>((Variant *)left_var);

	static GDExtensionPtrOperatorEvaluator eval = [] {
		return ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(
				(GDExtensionVariantOperator)OpC,
				(GDExtensionVariantType)GetTypeInfo<L>::VARIANT_TYPE,
				(GDExtensionVariantType)GetTypeInfo<R>::VARIANT_TYPE);
	}();
	if (!eval) {
		jsb_throw(isolate, "operator: evaluator missing");
		return;
	}

	typename godot::PtrToArg<R>::EncodeT r_slot{};
	if constexpr (std::is_same_v<R, int64_t>) {
		r_slot = (int64_t)info[1].As<v8::Int32>()->Value();
	} else if constexpr (std::is_same_v<R, double>) {
		r_slot = info[1].As<v8::Number>()->Value();
	} else if constexpr (std::is_same_v<R, bool>) {
		r_slot = info[1].As<v8::Boolean>()->Value();
	} else if constexpr (std::is_same_v<R, godot::String>) {
		r_slot = impl::Helper::to_string(isolate, info[1]);
	} else {
		// builtin struct / container / StringName / NodePath wrapper: copy R
		// out of its backing Variant (the dispatch probe already matched the
		// wrapper's type against R)
		const v8::Local<v8::Object> obj = info[1].As<v8::Object>();
		const Variant *bv = (const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer);
		if (bv->get_type() != (Variant::Type)GetTypeInfo<R>::VARIANT_TYPE) {
			jsb_throw(isolate, "operator: right operand type changed");
			return;
		}
		r_slot = *godot::VariantInternal::get_internal_value<R>((Variant *)bv);
	}

	typename godot::PtrToArg<Ret>::EncodeT ret_slot{};
	eval(left_opaque, &r_slot, &ret_slot);

	Variant ret_val = godot::PtrToArg<Ret>::convert(&ret_slot);
	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, ret_val, rval)) {
		jsb_throw(isolate, "operator: bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

// unary operator thunk (json unary rows: right type is NIL)
template <Variant::Operator OpC, typename L, typename Ret>
void operator_unary_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant *left_var = left_backing_of<L>(info[0]);
	if (!left_var) {
		jsb_throw(isolate, "operator: bad left operand");
		return;
	}
	void *left_opaque = left_opaque_of<L>((Variant *)left_var);

	static GDExtensionPtrOperatorEvaluator eval = [] {
		return ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(
				(GDExtensionVariantOperator)OpC,
				(GDExtensionVariantType)GetTypeInfo<L>::VARIANT_TYPE,
				GDEXTENSION_VARIANT_TYPE_NIL);
	}();
	if (!eval) {
		jsb_throw(isolate, "operator: evaluator missing");
		return;
	}

	typename godot::PtrToArg<Ret>::EncodeT ret_slot{};
	eval(left_opaque, nullptr, &ret_slot);

	Variant ret_val = godot::PtrToArg<Ret>::convert(&ret_slot);
	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, ret_val, rval)) {
		jsb_throw(isolate, "operator: bad translate");
		return;
	}
	info.GetReturnValue().Set(rval);
}

// mounted callback for binary operators: probes both operands and looks the
// matching thunk up in the generated table; a miss falls back to the dynamic
// evaluation (Variant::evaluate), matching the dynamic path exactly.
template <Variant::Operator OpC, typename LeftT>
void operator_dispatch_binary(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant::Type left_vt = probe_vt<godot::Variant>(info[0]);
	const Variant::Type right_vt = probe_vt<godot::Variant>(info[1]);
	if (left_vt == Variant::VARIANT_MAX || right_vt == Variant::VARIANT_MAX) {
		// dynamic path: same marshal + Variant::evaluate
		Variant left, right;
		if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left) || !TypeConvert::js_to_gd_var(isolate, context, info[1], right)) {
			jsb_throw(isolate, "bad translation");
			return;
		}
		Variant ret;
		bool r_valid = false;
		Variant::evaluate(OpC, left, right, ret, r_valid);
		if (!r_valid) {
			jsb_throw(isolate, jsb_format("bad operation between %s and %s.",
					Variant::get_type_name(left.get_type()),
					Variant::get_type_name(right.get_type())));
			return;
		}
		v8::Local<v8::Value> rval;
		if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval)) {
			jsb_throw(isolate, "bad translation");
			return;
		}
		info.GetReturnValue().Set(rval);
		return;
	}
	if (const ThunkFn thunk = find_operator_thunk(left_vt, OpC, right_vt)) {
		thunk(info);
		return;
	}
	// no thunk for this (left, op, right): dynamic evaluation
	Variant left, right;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left) || !TypeConvert::js_to_gd_var(isolate, context, info[1], right)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	Variant ret;
	bool r_valid = false;
	Variant::evaluate(OpC, left, right, ret, r_valid);
	if (!r_valid) {
		jsb_throw(isolate, jsb_format("bad operation between %s and %s.",
				Variant::get_type_name(left.get_type()),
				Variant::get_type_name(right.get_type())));
		return;
	}
	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	info.GetReturnValue().Set(rval);
}

// mounted callback for unary operators (single argument)
template <Variant::Operator OpC, typename LeftT>
void operator_dispatch_unary(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant::Type left_vt = probe_vt<godot::Variant>(info[0]);
	if (left_vt != Variant::VARIANT_MAX) {
		if (const ThunkFn thunk = find_operator_thunk(left_vt, OpC, Variant::NIL)) {
			thunk(info);
			return;
		}
	}
	// dynamic fallback
	Variant left, right;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	Variant ret;
	bool r_valid = false;
	Variant::evaluate(OpC, left, right, ret, r_valid);
	if (!r_valid) {
		jsb_throw(isolate, "bad operation");
		return;
	}
	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	info.GetReturnValue().Set(rval);
}

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
