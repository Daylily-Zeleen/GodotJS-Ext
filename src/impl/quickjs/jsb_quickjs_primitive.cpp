/************************************************************************/
/*  jsb_quickjs_primitive.cpp                                           */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
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

#include "jsb_quickjs_primitive.h"
#include "jsb_quickjs_isolate.h"
#include "jsb_quickjs_maybe.h"

namespace v8 {
Local<Primitive> Undefined(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackPos::Undefined));
}

Local<Primitive> Null(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackPos::Null));
}

Maybe<bool> Name::Equals(Local<Context> context, Local<Name> other) const {
	const JSValue v1 = (JSValue) * this;
	const JSValue v2 = (JSValue)other;
	return Maybe<bool>(jsb::impl::QuickJS::Equals(v1, v2));
}

MaybeLocal<Value> Value::ToPrimitive(Local<Context> context) const {
	JSContext *ctx = isolate_->ctx();
	const JSValue self = (JSValue) * this;
	// quickjs 未公开 JS_ToPrimitive API，这里对对象返回空，由调用方按非 primitive 处理。
	if (JS_IsObject(self)) {
		return MaybeLocal<Value>();
	}
	const uint16_t stack_pos = isolate_->push_steal(JS_DupValue(ctx, self));
	return MaybeLocal<Value>(Data(isolate_, stack_pos));
}

MaybeLocal<String> Value::ToDetailString(Local<Context> context) const {
	const uint16_t stack_pos = isolate_->push_steal(JS_ToString(isolate_->ctx(), (JSValue) * this));
	return MaybeLocal<String>(Data(isolate_, stack_pos));
}

Maybe<int32_t> Value::Int32Value(Local<Context> context) const {
	const JSValue val = (JSValue) * this;
	int32_t result;
	if (JS_ToInt32(isolate_->ctx(), &result, val) != 0) {
		return Maybe<int32_t>();
	}
	return Maybe<int32_t>(result);
}

bool Value::BooleanValue(Isolate *isolate) const {
	const JSValue val = (JSValue) * this;
	const int res = JS_ToBool(isolate_->ctx(), val);
	jsb_ensure(res >= 0);
	return !!res;
}

Maybe<double> Value::NumberValue(Local<Context> context) const {
	const JSValue val = (JSValue) * this;
	if (JS_VALUE_GET_TAG(val) == JS_TAG_INT) return Maybe<double>(JS_VALUE_GET_INT(val));
	if (JS_VALUE_GET_TAG(val) == JS_TAG_FLOAT64) return Maybe<double>(JS_VALUE_GET_FLOAT64(val));
	return Maybe<double>();
}

MaybeLocal<String> Value::ToString(Local<Context> context) const {
	return MaybeLocal<String>(Data(isolate_, isolate_->push_steal(JS_ToString(isolate_->ctx(), (JSValue) * this))));
}

void *External::Value() const {
	const JSValue val = (JSValue) * this;
	return JS_VALUE_GET_PTR(val);
}

Local<External> External::New(Isolate *isolate, void *value) {
	const uint16_t stack_pos = isolate->push_steal(JS_MKPTR(jsb::impl::JS_TAG_EXTERNAL, value));
	return Local<External>(Data(isolate, stack_pos));
}

Local<Symbol> Symbol::New(Isolate *isolate) {
	return Local<Symbol>(Data(isolate, isolate->push_symbol()));
}

Local<Symbol> Symbol::New(Isolate *isolate, Local<String> description) {
	return Local<Symbol>(Data(isolate, isolate->push_symbol(description.operator JSValue())));
}

Local<Symbol> Symbol::_get_well_known(Isolate *isolate, const char *name) {
	JSContext *ctx = isolate->ctx();
	HandleScope func_scope(isolate);
	const JSValue &symbol_obj = isolate->stack_val(jsb::impl::StackPos::SymbolClass);
	JSValue val = JS_GetPropertyStr(ctx, symbol_obj, name);
	JS_FreeValue(ctx, symbol_obj);
	return Local<Symbol>(Data(isolate, isolate->push_steal(val)));
}

Local<Symbol> Symbol::GetAsyncIterator(Isolate *isolate) { return _get_well_known(isolate, "asyncIterator"); }
Local<Symbol> Symbol::GetHasInstance(Isolate *isolate) { return _get_well_known(isolate, "hasInstance"); }
Local<Symbol> Symbol::GetIsConcatSpreadable(Isolate *isolate) { return _get_well_known(isolate, "isConcatSpreadable"); }
Local<Symbol> Symbol::GetIterator(Isolate *isolate) { return _get_well_known(isolate, "iterator"); }
Local<Symbol> Symbol::GetMatch(Isolate *isolate) { return _get_well_known(isolate, "match"); }
Local<Symbol> Symbol::GetReplace(Isolate *isolate) { return _get_well_known(isolate, "replace"); }
Local<Symbol> Symbol::GetSearch(Isolate *isolate) { return _get_well_known(isolate, "search"); }
Local<Symbol> Symbol::GetSplit(Isolate *isolate) { return _get_well_known(isolate, "split"); }
Local<Symbol> Symbol::GetToPrimitive(Isolate *isolate) { return _get_well_known(isolate, "toPrimitive"); }
Local<Symbol> Symbol::GetToStringTag(Isolate *isolate) { return _get_well_known(isolate, "toStringTag"); }
Local<Symbol> Symbol::GetUnscopables(Isolate *isolate) { return _get_well_known(isolate, "unscopables"); }

int String::Length() const {
	const JSValue val = JS_GetProperty(isolate_->ctx(), (JSValue) * this, jsb::impl::JS_ATOM_length);
	jsb_check(JS_VALUE_GET_TAG(val) == JS_TAG_INT);
	return JS_VALUE_GET_INT(val);
}

Local<String> String::Empty(Isolate *isolate) {
	return Local<String>(Data(isolate, jsb::impl::StackPos::EmptyString));
}

MaybeLocal<String> String::NewFromUtf8(Isolate *isolate, const char *data, NewStringType /* type */, int length) {
	JSContext *ctx = isolate->ctx();
	JSValue val = JS_NewStringLen(ctx, data, length < 0 ? (int)strlen(data) : length);
	if (JS_IsException(val)) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(ctx);
		return MaybeLocal<String>();
	}
	return MaybeLocal<String>(Data(isolate, isolate->push_steal(val)));
}

int String::WriteUtf8(Isolate *isolate, char *buffer, int length, int *nchars_ref) const {
	JSContext *ctx = isolate->ctx();
	const JSValue self = (JSValue) * this;
	size_t len;
	const char *chars = JS_ToCStringLen(ctx, &len, self);
	if (!chars) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(ctx);
		return 0;
	}

	const int available = (int)len;
	const int to_write = (length < 0 || length > available) ? available : length;
	if (to_write > 0) {
		memcpy(buffer, chars, to_write);
	}

	JS_FreeCString(ctx, chars);

	if (nchars_ref) {
		// QuickJS returns byte length; for pure ASCII they're the same.
		// For non-ASCII we'd need to count codepoints, but this is good enough for our use cases.
		*nchars_ref = to_write;
	}
	return to_write;
}

Local<String> String::NewFromUtf8Literal(Isolate *isolate, const char *literal, NewStringType type, int length) {
	JSValue val = JS_NewStringLen(isolate->ctx(), literal, length);
	jsb_check(!JS_IsException(val));
	return Local<String>(Data(isolate, isolate->push_steal(val)));
}

Local<Symbol> Symbol::For(Isolate *isolate, Local<String> key) {
	JSContext *ctx = isolate->ctx();

	HandleScope func_scope(isolate);
	const JSValue &symbol_obj = isolate->stack_val(jsb::impl::StackPos::SymbolClass);

	// Use Symbol.for(key) from JavaScript
	const JSValue for_fn = JS_GetPropertyStr(ctx, symbol_obj, "for");
	JSValue key_val = JS_DupValue(ctx, (JSValue)key);
	JSValue result = JS_Call(ctx, for_fn, symbol_obj, 1, &key_val);
	JS_FreeValue(ctx, key_val);
	JS_FreeValue(ctx, for_fn);
	if (JS_IsException(result)) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(ctx);
		return Local<Symbol>();
	}
	const uint16_t stack_pos = isolate->push_steal(result);
	return Local<Symbol>(Data(isolate, stack_pos));
}

Local<String> Symbol::Description(Isolate *isolate) const {
	JSContext *ctx = isolate->ctx();
	// Read the `description` property of the symbol
	const JSValue self = (JSValue) * this;
	const JSValue desc = JS_GetPropertyStr(ctx, self, "description");
	if (JS_IsException(desc) || JS_IsUndefined(desc)) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(ctx);
		return Local<String>();
	}
	if (JS_IsNull(desc)) {
		return Local<String>();
	}
	const uint16_t stack_pos = isolate->push_steal(desc);
	return Local<String>(Data(isolate, stack_pos));
}

Local<Integer> Integer::New(Isolate *isolate, int32_t value) {
	const uint16_t stack_pos = isolate->push_steal(JS_NewInt32(isolate->ctx(), value));
	return Local<String>(Data(isolate, stack_pos));
}

Local<Integer> Integer::NewFromUnsigned(Isolate *isolate, uint32_t value) {
	//TODO avoid using Uint32 because the underlying tag is INT or FLOAT64
	const uint16_t stack_pos = isolate->push_steal(JS_NewUint32(isolate->ctx(), value));
	return Local<Integer>(Data(isolate, stack_pos));
}

double Number::Value() const {
	const JSValue val = (JSValue) * this;
	double rval;
	if (JS_ToFloat64(isolate_->ctx(), &rval, val) == -1) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(isolate_->ctx());
	}
	return rval;
}

Local<Number> Number::New(Isolate *isolate, double value) {
	const uint16_t stack_pos = isolate->push_steal(JS_NewFloat64(isolate->ctx(), value));
	return Local<String>(Data(isolate, stack_pos));
}

// int64_t Integer::Value() const
// {
//     const JSValue val = (JSValue) *this;
//     int32_t rval;
//     if (JS_ToInt32(isolate_->ctx(), &rval, val))
//     {
//         isolate_->remove_exception_anyway();
//     }
//     return rval;
// }

int32_t Int32::Value() const {
	const JSValue val = (JSValue) * this;
	int32_t rval;
	if (JS_ToInt32(isolate_->ctx(), &rval, val) == -1) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(isolate_->ctx());
	}
	return rval;
}

uint32_t Uint32::Value() const {
	const JSValue val = (JSValue) * this;
	uint32_t rval;
	if (JS_ToUint32(isolate_->ctx(), &rval, val) == -1) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(isolate_->ctx());
	}
	return rval;
}

bool Boolean::Value() const {
	const JSValue val = (JSValue) * this;
	return !!JS_VALUE_GET_BOOL(val);
}

Local<Boolean> Boolean::New(Isolate *isolate, bool value) {
	return Local<Boolean>(Data(isolate, value ? jsb::impl::StackPos::True : jsb::impl::StackPos::False));
}

int64_t BigInt::Int64Value(bool *lossless) const {
	const JSValue val = (JSValue) * this;
	int64_t rval;
	if (JS_ToBigInt64(isolate_->ctx(), &rval, val) == -1) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(isolate_->ctx());
		if (lossless) *lossless = false;
		return 0;
	}
	if (lossless) *lossless = true;
	return rval;
}

Local<BigInt> BigInt::New(Isolate *isolate, int64_t value) {
	const JSValue val = JS_NewBigInt64(isolate->ctx(), value);
	jsb_check(!JS_IsException(val));
	const uint16_t stack_pos = isolate->push_steal(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<BigInt> BigInt::NewFromUnsigned(Isolate *isolate, uint64_t value) {
	const JSValue val = JS_NewBigUint64(isolate->ctx(), value);
	jsb_check(!JS_IsException(val));
	const uint16_t stack_pos = isolate->push_steal(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<Value> Exception::Error(Local<String> message) {
	Isolate *isolate = message->isolate_;
	jsb_check(isolate);
	JSContext *ctx = isolate->ctx();
	JSValue error = JS_NewError(ctx);
	if (JS_IsException(error)) {
		jsb::impl::QuickJS::MarkExceptionAsTrivial(ctx);
		return Local<Value>(Data(isolate, jsb::impl::StackPos::Undefined));
	}
	jsb_ensure(JS_SetProperty(ctx, error, jsb::impl::JS_ATOM_message, JS_DupValue(ctx, (JSValue)message)) >= 0);
	return Local<Value>(Data(isolate, isolate->push_steal(error)));
}

} //namespace v8
