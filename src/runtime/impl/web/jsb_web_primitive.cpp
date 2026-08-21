/************************************************************************/
/*  jsb_web_primitive.cpp                                               */
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

#include "jsb_web_primitive.h"
#include "jsb_web_interop.h"
#include "jsb_web_isolate.h"
#include "jsb_web_maybe.h"
#include "jsb_web_typedef.h"

namespace v8 {
Local<Primitive> Undefined(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackBase::Undefined));
}

Local<Primitive> Null(Isolate *isolate) {
	return Local<Primitive>(Data(isolate, jsb::impl::StackBase::Null));
}

MaybeLocal<String> Value::ToDetailString(Local<Context> context) const {
	return MaybeLocal<String>(Data(isolate_, jsbi_ToString(isolate_->rt(), stack_pos_)));
}

Maybe<int32_t> Value::Int32Value(Local<Context> context) const {
	return Maybe<int32_t>(jsbi_Int32Value(isolate_->rt(), stack_pos_));
}

bool Value::BooleanValue(Isolate *isolate) const {
	return jsbi_BooleanValue(isolate_->rt(), stack_pos_);
}

Maybe<double> Value::NumberValue(Local<Context> context) const {
	return Maybe<double>(jsbi_NumberValue(isolate_->rt(), stack_pos_));
}

MaybeLocal<String> Value::ToString(Local<Context> context) const {
	return MaybeLocal<String>(Data(isolate_, jsbi_ToString(isolate_->rt(), stack_pos_)));
}

void *External::Value() const {
	return jsbi_GetExternal(isolate_->rt(), stack_pos_);
}

Local<External> External::New(Isolate *isolate, void *value) {
	return Local<External>(Data(isolate, jsbi_NewExternal(isolate->rt(), value)));
}

Local<Symbol> Symbol::New(Isolate *isolate) {
	return Local<Symbol>(Data(isolate, jsbi_NewSymbol(isolate->rt())));
}

int String::Length() const {
	return jsbi_GetStringLength(isolate_->rt(), stack_pos_);
}

Local<String> String::Empty(Isolate *isolate) {
	return Local<String>(Data(isolate, jsb::impl::StackBase::EmptyString));
}

MaybeLocal<String> String::NewFromUtf8(Isolate *isolate, const char *data, NewStringType /* type */, int length) {
	return MaybeLocal<String>(Data(isolate, jsbi_NewString(isolate->rt(), data, length < 0 ? (int)strlen(data) : length)));
}

int String::WriteUtf8(Isolate *isolate, char *buffer, int length, int *nchars_ref) const {
	jsb::impl::JSRuntime rt = isolate->rt();
	int32_t len;
	char *chars = jsbi_ToCStringLen(rt, &len, this->stack_pos_);
	if (!chars) {
		return 0;
	}

	const int available = (int)len;
	const int to_write = (length < 0 || length > available) ? available : length;
	if (to_write > 0) {
		memcpy(buffer, chars, to_write);
	}

	jsbi_free(chars);

	if (nchars_ref) {
		// QuickJS returns byte length; for pure ASCII they're the same.
		// For non-ASCII we'd need to count codepoints, but this is good enough for our use cases.
		*nchars_ref = to_write;
	}
	return to_write;
}

Local<String> String::NewFromUtf8Literal(Isolate *isolate, const char *literal, NewStringType type, int length) {
	return Local<String>(Data(isolate, jsbi_NewString(isolate->rt(), literal, length)));
}

Local<Integer> Integer::New(Isolate *isolate, int32_t value) {
	return Local<String>(Data(isolate, jsbi_NewInt32(isolate->rt(), value)));
}

Local<Integer> Integer::NewFromUnsigned(Isolate *isolate, uint32_t value) {
	return Local<String>(Data(isolate, jsbi_NewUint32(isolate->rt(), value)));
}

double Number::Value() const {
	return jsbi_NumberValue(isolate_->rt(), stack_pos_);
}

Local<Number> Number::New(Isolate *isolate, double value) {
	return Local<String>(Data(isolate, jsbi_NewNumber(isolate->rt(), value)));
}

int32_t Int32::Value() const {
	return jsbi_Int32Value(isolate_->rt(), stack_pos_);
}

uint32_t Uint32::Value() const {
	return jsbi_Uint32Value(isolate_->rt(), stack_pos_);
}

bool Boolean::Value() const {
	if (stack_pos_ == jsb::impl::StackBase::True) return true;
	if (stack_pos_ == jsb::impl::StackBase::False) return false;
	return jsbi_BooleanValue(isolate_->rt(), stack_pos_);
}

Local<Boolean> Boolean::New(Isolate *isolate, bool value) {
	return Local<Boolean>(Data(isolate, value ? jsb::impl::StackBase::True : jsb::impl::StackBase::False));
}

int64_t BigInt::Int64Value(bool *lossless) const {
	int64_t rval;
	const bool res = jsbi_Int64Value(isolate_->rt(), stack_pos_, &rval);
	jsb_unused(res);
	jsb_check(res);
	return rval;
}

Local<BigInt> BigInt::New(Isolate *isolate, int64_t value) {
	return Local<String>(Data(isolate, jsbi_NewBigInt64(isolate->rt(), &value)));
}

Local<BigInt> BigInt::NewFromUnsigned(Isolate *isolate, uint64_t value) {
	return Local<String>(Data(isolate, jsbi_NewBigUint64(isolate->rt(), &value)));
}

} //namespace v8
