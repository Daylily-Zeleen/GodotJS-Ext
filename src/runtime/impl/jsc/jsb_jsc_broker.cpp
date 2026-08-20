/************************************************************************/
/*  jsb_jsc_broker.cpp                                                  */
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

#include "jsb_jsc_broker.h"
#include "jsb_jsc_isolate.h"

namespace jsb::impl {
void Broker::SetWeak(v8::Isolate *isolate, JSObjectRef value, void *parameter, void *callback) {
	if (!value) return;
	if (jsb::impl::InternalData *data = (jsb::impl::InternalData *)JSObjectGetPrivate(value)) {
		JSB_JSC_LOG(VeryVerbose, "update internal data JSObject:%s id:%s pc:%s,%s (last:%s,%s)", (uintptr_t)value, (uintptr_t)data, (uintptr_t)parameter, (uintptr_t)callback, (uintptr_t)data->weak.parameter, (uintptr_t)data->weak.callback);
		if (callback && data->weak.callback) {
			if (data->weak.callback == callback && data->weak.parameter == parameter) {
				return;
			}
		}
		jsb_checkf(!callback || !data->weak.callback, "overriding an existing value is not allowed");
		data->weak.parameter = (void *)parameter;
		data->weak.callback = (void *)callback;
	}
}

JSValueRef Broker::stack_val(v8::Isolate *isolate, uint16_t index) {
	return isolate->stack_val(index);
}

JSValueRef Broker::stack_dup(v8::Isolate *isolate, uint16_t index) {
	return isolate->stack_dup(index);
}

uint16_t Broker::push_copy(v8::Isolate *isolate, JSValueRef value) {
	return isolate->push_copy(value);
}

void Broker::_add_reference(v8::Isolate *isolate) {
	isolate->_add_reference();
	;
}

void Broker::_remove_reference(v8::Isolate *isolate) {
	isolate->_remove_reference();
	;
}

bool Broker::IsStrictEqual(v8::Isolate *isolate, JSValueRef a, JSValueRef b) {
	return JSValueIsStrictEqual(isolate->ctx(), a, b);
}

JSContextRef Broker::ctx(v8::Isolate *isolate) {
	return isolate->ctx();
}

JSContextGroupRef Broker::rt(v8::Isolate *isolate) {
	return isolate->rt();
}

} //namespace jsb::impl
