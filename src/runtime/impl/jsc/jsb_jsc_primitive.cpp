/************************************************************************/
/*  jsb_jsc_primitive.cpp                                               */
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

#include "jsb_jsc_primitive.h"
#include "jsb_jsc_isolate.h"
#include "jsb_jsc_maybe.h"

namespace v8 {
Local<Primitive> Undefined(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackPos::Undefined));
}

Local<Primitive> Null(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackPos::Null));
}

MaybeLocal<String> Value::ToDetailString(Local<Context> context) const {
	//TODO no equivalent implementation
	return ToString(context);
}

Maybe<int32_t> Value::Int32Value(Local<Context> context) const {
	JSValueRef error = nullptr;
	const int32_t rval = JSValueToInt32(isolate_->ctx(), (JSValueRef) * this, &error);
	if (jsb_unlikely(error)) return Maybe<int32_t>();
	return Maybe<int32_t>(rval);
}

bool Value::BooleanValue(Isolate *isolate) const {
	return JSValueToBoolean(isolate_->ctx(), (JSValueRef) * this);
}

Maybe<double> Value::NumberValue(Local<Context> context) const {
	JSValueRef error = nullptr;
	const double rval = JSValueToNumber(isolate_->ctx(), (JSValueRef) * this, &error);
	if (jsb_unlikely(error)) return Maybe<double>();
	return Maybe<double>(rval);
}

MaybeLocal<String> Value::ToString(Local<Context> context) const {
	if (const JSStringRef str = JSValueToStringCopy(isolate_->ctx(), (JSValueRef) * this, nullptr)) {
		const JSValueRef val = JSValueMakeString(isolate_->ctx(), str);
		return MaybeLocal<String>(Data(isolate_, isolate_->push_copy(val)));
	}
	return MaybeLocal<String>();
}

void *External::Value() const {
	const JSValueRef val = (JSValueRef) * this;
	jsb_check(isolate_->_IsExternal(val));
	//TODO we know val must be an instance of External. uncertain whether it's reasonable not using `JSValueToObject` here?
	return JSObjectGetPrivate((JSObjectRef)val);
}

Local<External> External::New(Isolate *isolate, void *value) {
	const JSObjectRef obj = isolate->_NewExternal(value);
	const uint16_t stack_pos = isolate->push_copy(obj);
	return Local<External>(Data(isolate, stack_pos));
}

Local<Symbol> Symbol::New(Isolate *isolate) {
	const JSValueRef val = JSValueMakeSymbol(isolate->ctx(), nullptr);
	jsb_check(val);
	return Local<Symbol>(Data(isolate, isolate->push_copy(val)));
}

Local<Symbol> Symbol::New(Isolate *isolate, Local<String> description) {
	const JSStringRef desc = JSValueToStringCopy(isolate->ctx(), (JSValueRef)description, nullptr);
	if (!desc) {
		return Local<Symbol>();
	}
	const JSValueRef val = JSValueMakeSymbol(isolate->ctx(), desc);
	JSStringRelease(desc);
	jsb_check(val);
	return Local<Symbol>(Data(isolate, isolate->push_copy(val)));
}

Local<Symbol> Symbol::_get_well_known(Isolate *isolate, const char *name) {
	JSContextRef ctx = isolate->ctx();
	const JSObjectRef symbol_ctor = jsb::impl::JavaScriptCore::AsObject(ctx, isolate->_GetSymbolConstructor());
	const JSStringRef name_str = JSStringCreateWithUTF8CString(name);
	JSValueRef error = nullptr;
	const JSValueRef val = JSObjectGetProperty(ctx, symbol_ctor, name_str, &error);
	JSStringRelease(name_str);
	if (jsb_unlikely(error) || !val || !JSValueIsSymbol(ctx, val)) {
		if (error) {
			jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(ctx, error);
		}
		return Local<Symbol>();
	}
	return Local<Symbol>(Data(isolate, isolate->push_copy(val)));
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

Local<String> Symbol::Description(Isolate *isolate) const {
	JSContextRef ctx = isolate_->ctx();
	// The JSC C API has no direct way to read a symbol's description, so
	// evaluate `Symbol.prototype.description.call(<symbol>)` instead.
	const JSStringRef code = JSStringCreateWithUTF8CString(
			"(function(s){ return s.description; })");
	JSValueRef error = nullptr;
	const JSValueRef getter = JSEvaluateScript(ctx, code, nullptr, nullptr, 1, &error);
	JSStringRelease(code);
	if (jsb_unlikely(error) || !getter) {
		if (error) {
			jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(ctx, error);
		}
		return Local<String>();
	}
	const JSValueRef self = (JSValueRef)*this;
	const JSValueRef desc = JSObjectCallAsFunction(ctx, (JSObjectRef)getter, nullptr, 1, &self, &error);
	if (jsb_unlikely(error) || !desc) {
		if (error) {
			jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(ctx, error);
		}
		return Local<String>();
	}
	if (JSValueIsUndefined(ctx, desc) || JSValueIsNull(ctx, desc)) {
		return Local<String>();
	}
	return Local<String>(Data(isolate_, isolate_->push_copy(desc)));
}

Local<Symbol> Symbol::For(Isolate *isolate, Local<String> key) {
	JSContextRef ctx = isolate->ctx();
	const JSObjectRef symbol_ctor = jsb::impl::JavaScriptCore::AsObject(ctx, isolate->_GetSymbolConstructor());
	const JSStringRef for_key = JSStringCreateWithUTF8CString("for");
	JSValueRef error = nullptr;
	const JSValueRef for_fn_val = JSObjectGetProperty(ctx, symbol_ctor, for_key, &error);
	JSStringRelease(for_key);
	const JSObjectRef for_fn = jsb::impl::JavaScriptCore::AsObject(ctx, for_fn_val);
	if (jsb_unlikely(error) || !for_fn || !JSObjectIsFunction(ctx, for_fn)) {
		if (error) {
			jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(ctx, error);
		}
		return Local<Symbol>();
	}
	// JSObjectCallAsFunction takes a non-const JSValueRef array
	JSValueRef key_val = (JSValueRef)key;
	const JSValueRef result = JSObjectCallAsFunction(ctx, for_fn, symbol_ctor, 1, &key_val, &error);
	if (jsb_unlikely(error) || !result) {
		if (error) {
			jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(ctx, error);
		}
		return Local<Symbol>();
	}
	return Local<Symbol>(Data(isolate, isolate->push_copy(result)));
}

int String::Length() const {
	jsb_check(JSValueIsString(isolate_->ctx(), (JSValueRef) * this));
	if (const JSStringRef str = JSValueToStringCopy(isolate_->ctx(), (JSValueRef) * this, nullptr)) {
		const size_t len = JSStringGetLength(str);
		jsb_check((size_t)(int)len == len);
		return (int)len;
	}
	return 0;
}

Local<String> String::Empty(Isolate *isolate) {
	return Local<String>(Data(isolate, jsb::impl::StackPos::EmptyString));
}

MaybeLocal<String> String::NewFromUtf8(Isolate *isolate, const char *data, NewStringType /* type */, int length) {
	JSStringRef str = JSStringCreateWithUTF8CString(data);
	const JSValueRef val = JSValueMakeString(isolate->ctx(), str);
	JSStringRelease(str);
	jsb_check(val);
	const uint16_t stack_pos = isolate->push_copy(val);
	return MaybeLocal<String>(Data(isolate, stack_pos));
}

int String::WriteUtf8(Isolate *isolate, char *buffer, int length, int *nchars_ref) const {
	jsb_check(JSValueIsString(isolate_->ctx(), (JSValueRef) * this));
	if (const JSStringRef str = JSValueToStringCopy(isolate_->ctx(), (JSValueRef) * this, nullptr)) {
		const size_t len = JSStringGetLength(str);
		jsb_check((size_t)(int)len == len);
		if (nchars_ref) {
			*nchars_ref = (int)len;
		}
		const int to_write = (length < 0 || length > (int)len) ? (int)len : length;
		if (to_write > 0) {
			JSStringGetUTF8CString(str, buffer, to_write + 1);
		}
		JSStringRelease(str);
		return to_write;
	}
	return 0;
}

Local<String> String::NewFromUtf8Literal(Isolate *isolate, const char *literal, NewStringType type, int length) {
	JSStringRef str = JSStringCreateWithUTF8CString(literal);
	const JSValueRef val = JSValueMakeString(isolate->ctx(), str);
	JSStringRelease(str);
	jsb_check(val);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<Integer> Integer::New(Isolate *isolate, int32_t value) {
	const JSValueRef val = JSValueMakeNumber(isolate->ctx(), (int32_t)value);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<Integer> Integer::NewFromUnsigned(Isolate *isolate, uint32_t value) {
	const JSValueRef val = JSValueMakeNumber(isolate->ctx(), (uint32_t)value);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

double Number::Value() const {
	return JSValueToNumber(isolate_->ctx(), (JSValueRef) * this, nullptr);
}

Local<Number> Number::New(Isolate *isolate, double value) {
	const JSValueRef val = JSValueMakeNumber(isolate->ctx(), value);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

int32_t Int32::Value() const {
	return JSValueToInt32(isolate_->ctx(), (JSValueRef) * this, nullptr);
}

uint32_t Uint32::Value() const {
	return JSValueToUInt32(isolate_->ctx(), (JSValueRef) * this, nullptr);
}

bool Boolean::Value() const {
	return JSValueToBoolean(isolate_->ctx(), (JSValueRef) * this);
}

Local<Boolean> Boolean::New(Isolate *isolate, bool value) {
	return Local<Boolean>(Data(isolate, value ? jsb::impl::StackPos::True : jsb::impl::StackPos::False));
}

int64_t BigInt::Int64Value(bool *lossless) const {
	return JSValueToInt64(isolate_->ctx(), (JSValueRef) * this, nullptr);
}

Local<BigInt> BigInt::New(Isolate *isolate, int64_t value) {
	const JSValueRef val = JSBigIntCreateWithInt64(isolate->ctx(), value, nullptr);
	jsb_check(val);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<BigInt> BigInt::NewFromUnsigned(Isolate *isolate, uint64_t value) {
	const JSValueRef val = JSBigIntCreateWithUInt64(isolate->ctx(), value, nullptr);
	jsb_check(val);
	const uint16_t stack_pos = isolate->push_copy(val);
	return Local<String>(Data(isolate, stack_pos));
}

Local<Value> Exception::Error(Local<String> message) {
	Isolate *isolate = message->isolate_;
	jsb_check(isolate);
	JSValueRef trivial_error = nullptr;
	const JSValueRef msg = (JSValueRef)message;
	const JSValueRef error = JSObjectMakeError(isolate->ctx(), 1, &msg, &trivial_error);
	if (trivial_error) {
		jsb::impl::JavaScriptCore::MarkExceptionAsTrivial(isolate->ctx(), trivial_error);
		return Local<Value>(Data(isolate, jsb::impl::StackPos::Undefined));
	}
	return Local<Value>(Data(isolate, isolate->push_copy(error)));
}

} //namespace v8
