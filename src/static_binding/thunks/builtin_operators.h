/************************************************************************/
/*  builtin_operators.h                                                 */
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

#	include "../dispatch.h"
#	include "thunks_common.h"
#	include <godot_cpp/variant/variant_internal.hpp>

// ---------------------------------------------------------------------------
// Static operator thunks use ptrcall evaluators cached per template instance.
// The left operand aliases its wrapper's backing data; the right operand is
// copied into an EncodeT slot (a NIL Variant for R = godot::Variant).
// Binary dispatch uses a generated switch for each (left type, operator)
// pair; unrecognized types and missing overloads fall back to Variant::evaluate.
// ---------------------------------------------------------------------------

namespace jsb::static_binding {

// the variant-backed wrapper of exactly L, or null
template <typename L>
const Variant *left_backing_of(const v8::Local<v8::Value> &val) {
	if (!val->IsObject()) return nullptr;
	const v8::Local<v8::Object> obj = val.As<v8::Object>();
	if (!TypeConvert::is_variant(obj)) return nullptr;
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

// binary operator thunk
template <Variant::Operator OpC, typename L, typename R, typename Ret>
void operator_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant *left_var = left_backing_of<L>(info.This());
	if (!left_var) {
		jsb_throw(isolate, "operator: bad left operand");
		return;
	}
	void *left_opaque = left_opaque_of<L>((Variant *)left_var);

	// Engine rule for equality against nil/undefined: `X == nil` is always
	// false and `X != nil` is always true (OperatorEvaluatorAlwaysFalse /
	// AlwaysTrue, registered for every concrete left type). Short-circuit for a
	// missing or null/undefined right operand, which the NIL case label of the
	// dispatch table otherwise routes to the same-type evaluator thunk.
	if constexpr (OpC == Variant::OP_EQUAL || OpC == Variant::OP_NOT_EQUAL) {
		if (info.Length() < 1 || info[0]->IsNullOrUndefined()) {
			info.GetReturnValue().Set(v8::Boolean::New(isolate, OpC == Variant::OP_NOT_EQUAL));
			return;
		}
	}

	static const GDExtensionPtrOperatorEvaluator eval = ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(
			(GDExtensionVariantOperator)OpC,
			(GDExtensionVariantType)GetTypeInfo<L>::VARIANT_TYPE,
			(GDExtensionVariantType)GetTypeInfo<R>::VARIANT_TYPE);

	if (!eval) {
		jsb_throw(isolate, "operator: evaluator missing");
		return;
	}

	typename godot::PtrToArg<R>::EncodeT right_slot{};
	if constexpr (std::is_same_v<R, int64_t>) {
		right_slot = (int64_t)info[0].As<v8::Int32>()->Value();
	} else if constexpr (std::is_same_v<R, double>) {
		right_slot = info[0].As<v8::Number>()->Value();
	} else if constexpr (std::is_same_v<R, bool>) {
		right_slot = info[0].As<v8::Boolean>()->Value();
	} else if constexpr (std::is_same_v<R, godot::String>) {
		right_slot = impl::Helper::to_string(isolate, info[0]);
	} else if constexpr (std::is_same_v<R, godot::Variant>) {
		// NIL dispatch rows (e.g. String % null) pass a default-constructed
		// Variant, not a wrapper's native payload.
		(void)info;
	} else {
		// Copy the matched wrapper's native value into the right operand slot.
		const v8::Local<v8::Object> obj = info[0].As<v8::Object>();
		const Variant *bv = (const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer);
		if (bv->get_type() != (Variant::Type)GetTypeInfo<R>::VARIANT_TYPE) {
			jsb_throw(isolate, "operator: right operand type changed");
			return;
		}
		right_slot = *godot::VariantInternal::get_internal_value<R>((Variant *)bv);
	}

	typename godot::PtrToArg<Ret>::EncodeT ret_slot{};
	eval(left_opaque, &right_slot, &ret_slot);

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

	const Variant *left_var = left_backing_of<L>(info.This());
	if (!left_var) {
		jsb_throw(isolate, "operator: bad left operand");
		return;
	}
	void *left_opaque = left_opaque_of<L>((Variant *)left_var);

	static const GDExtensionPtrOperatorEvaluator eval = ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(
			(GDExtensionVariantOperator)OpC,
			(GDExtensionVariantType)GetTypeInfo<L>::VARIANT_TYPE,
			GDEXTENSION_VARIANT_TYPE_NIL);

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

// Dynamic fallback: convert both operands to Variants and use generic evaluation.
static void evaluate_dynamic_binary(const v8::FunctionCallbackInfo<v8::Value> &info,
		v8::Isolate *isolate,
		const v8::Local<v8::Context> &context,
		Variant::Operator op) {
	Variant left, right;
	if (!TypeConvert::js_to_gd_var(isolate, context, info.This(), left) || !TypeConvert::js_to_gd_var(isolate, context, info[0], right)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	Variant ret;
	bool r_valid = false;
	Variant::evaluate(op, left, right, ret, r_valid);
	if (!r_valid) {
		jsb_throw(isolate, jsb_format("bad operation between %s and %s.", Variant::get_type_name(left.get_type()), Variant::get_type_name(right.get_type())));
		return;
	}
	v8::Local<v8::Value> rval;
	if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	info.GetReturnValue().Set(rval);
}

// Probe operand types and select a thunk from the pair-local switch emitted
// by static_binding_codegen.py. Type mismatches or table misses use the
// dynamic fallback.
template <Variant::Operator OpC, typename LeftT, ThunkFn (*FindTable)(godot::Variant::Type)>
void operator_dispatch_binary(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant::Type left_vt = probe_vt<probe_prefer_object_types>(info.This());
	const Variant::Type right_vt = probe_vt<probe_prefer_primitive_types>(info[0]);
	if (left_vt == Variant::VARIANT_MAX || right_vt == Variant::VARIANT_MAX) {
		evaluate_dynamic_binary(info, isolate, context, OpC);
		return;
	}
	if (left_vt == (Variant::Type)GetTypeInfo<LeftT>::VARIANT_TYPE) {
		if (const ThunkFn thunk = FindTable(right_vt)) {
			thunk(info);
			return;
		}
	}
	// no thunk for this (left, op, right): dynamic evaluation
	evaluate_dynamic_binary(info, isolate, context, OpC);
}

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
