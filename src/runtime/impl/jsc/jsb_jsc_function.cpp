/************************************************************************/
/*  jsb_jsc_function.cpp                                                */
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

#include "jsb_jsc_function.h"

#include "jsb_jsc_context.h"

namespace v8 {
MaybeLocal<Value> Function::Call(Local<Context> context, Local<Value> recv, int argc, Local<Value> argv[]) {
	const JSObjectRef func = jsb::impl::JavaScriptCore::AsObject(isolate_->ctx(), (JSValueRef) * this);
	const JSObjectRef self = jsb::impl::JavaScriptCore::AsObject(isolate_->ctx(), (JSValueRef)recv);
	JSValueRef *vargv = jsb_stackalloc(JSValueRef, argc);
	for (int i = 0; i < argc; i++) {
		vargv[i] = (JSValueRef)argv[i];
	}
	JSValueRef error = nullptr;
	const JSValueRef rval = JSObjectCallAsFunction(isolate_->ctx(), func, self, argc, vargv, &error);
	if (!rval) {
		// intentionally keep the exception
		isolate_->_ThrowError(error);
		return MaybeLocal<Value>();
	}
	return MaybeLocal<Value>(Data(isolate_, isolate_->push_copy(rval)));
}

JSValueRef Function::_function_call(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef *exception) {
	Isolate *isolate = (Isolate *)jsb::impl::JavaScriptCore::GetContextOpaque(ctx);
	const jsb::impl::CFunctionPayload &payload = *(jsb::impl::CFunctionPayload *)JSObjectGetPrivate(function);

	HandleScope func_scope(isolate);
	FunctionCallbackInfo<Value> info(isolate, argumentCount, false);

	// init function stack base
	const JSValueRef undefined = isolate->stack_val(jsb::impl::StackPos::Undefined);

	static_assert(jsb::impl::FunctionStackBase::ReturnValue == 0);
	const uint16_t stack_check_1 = isolate->push_copy(undefined);

	static_assert(jsb::impl::FunctionStackBase::This == 1);
	isolate->push_copy(thisObject);

	static_assert(jsb::impl::FunctionStackBase::Data == 2);
	isolate->push_copy(isolate->_get_captured_value(payload.captured_value_id));

	static_assert(jsb::impl::FunctionStackBase::NewTarget == 3);
	const uint16_t stack_check_2 = isolate->push_copy(undefined);

	jsb_check(stack_check_2 - stack_check_1 == jsb::impl::FunctionStackBase::Num - 1);
	static_assert(jsb::impl::FunctionStackBase::Num == 4);

	// push arguments
	for (int i = 0; i < argumentCount; ++i) {
		isolate->push_copy(arguments[i]);
	}

	((v8::FunctionCallback)payload.callback)(info);
	if (isolate->_HasError()) {
		*exception = isolate->_GetError();
		return nullptr;
	}
	return (JSValueRef)info.GetReturnValue();
}

MaybeLocal<Function> Function::New(Local<Context> context, FunctionCallback callback, Local<Value> data, int length) {
	Isolate *isolate = context->isolate_;
	static_assert(sizeof(callback) == sizeof(void *));
	const JSObjectRef func_obj = isolate->_NewFunction(_function_call, nullptr, (void *)callback, (JSValueRef)data);
	return MaybeLocal<Function>(Data(isolate, isolate->push_copy(func_obj)));
}

Local<Context> Function::GetCreationContextChecked() const {
	return Local<Context>(Data(isolate_, 0));
}
} //namespace v8
