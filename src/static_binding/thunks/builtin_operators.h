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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

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

// binary operator thunk
template <Variant::Operator OpC, typename L, typename R, typename Ret>
void operator_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant *left_var = left_backing_of<L>(info[0]);
	if (!left_var) {
		jsb_throw(isolate, "operator: bad left operand");
		return;
	}
	void *left_opaque = left_opaque_of<L>((Variant *)left_var);

	// equality against null/undefined: the engine's api json emits a
	// right=NIL row for ==/!= whose evaluator is ALWAYS-FALSE/ALWAYS-TRUE
	// ("comparing against an uninitialized Variant", variant_op.cpp:487/571).
	// Short-circuit here so `vec == null` returns false without hitting the
	// evaluator at all. Applies to every concrete R -- the engine rules are:
	// X == nil is always false, X != nil is always true.
	if constexpr (OpC == Variant::OP_EQUAL || OpC == Variant::OP_NOT_EQUAL) {
		if (info.Length() < 2 || info[1]->IsNullOrUndefined()) {
			const bool equal = OpC == Variant::OP_EQUAL;
			info.GetReturnValue().Set(v8::Boolean::New(isolate, equal));
			return;
		}
	}

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

	typename godot::PtrToArg<R>::EncodeT right_slot{};
	if constexpr (std::is_same_v<R, int64_t>) {
		right_slot = (int64_t)info[1].As<v8::Int32>()->Value();
	} else if constexpr (std::is_same_v<R, double>) {
		right_slot = info[1].As<v8::Number>()->Value();
	} else if constexpr (std::is_same_v<R, bool>) {
		right_slot = info[1].As<v8::Boolean>()->Value();
	} else if constexpr (std::is_same_v<R, godot::String>) {
		right_slot = impl::Helper::to_string(isolate, info[1]);
	} else if constexpr (std::is_same_v<R, godot::Variant>) {
		// nil-payload rows (e.g. String % null): the right operand IS the
		// uninitialized Variant itself -- nothing to unbox, right_slot stays
		// value-initialized and the evaluator reads it as NIL. The dispatch
		// probe keyed this case on Variant::NIL.
		(void)info;
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

// shared dynamic fallback: marshal both operands into Variants and evaluate
// through the engine's generic operator table -- identical to the dynamic
// binding path's behavior.
static void evaluate_dynamic_binary(const v8::FunctionCallbackInfo<v8::Value> &info,
		v8::Isolate *isolate, const v8::Local<v8::Context> &context, Variant::Operator op) {
	Variant left, right;
	if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left) || !TypeConvert::js_to_gd_var(isolate, context, info[1], right)) {
		jsb_throw(isolate, "bad translation");
		return;
	}
	Variant ret;
	bool r_valid = false;
	Variant::evaluate(op, left, right, ret, r_valid);
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

// mounted callback for binary operators: probes both operand types (JS
// argument types are only known at runtime), then takes the thunk from the
// pair-local table emitted by generate_primitive_operators.py -- a switch
// over the right operand's Variant type covering every overload of
// (OpC, LeftT). A miss falls back to the dynamic evaluation
// (Variant::evaluate), matching the dynamic path exactly.
template <Variant::Operator OpC, typename LeftT, ThunkFn (*FindTable)(godot::Variant::Type)>
void operator_dispatch_binary(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	const Variant::Type left_vt = probe_vt<probe_prefer_object_types>(info[0]);
	const Variant::Type right_vt = probe_vt<probe_prefer_primitive_types>(info[1]);
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
