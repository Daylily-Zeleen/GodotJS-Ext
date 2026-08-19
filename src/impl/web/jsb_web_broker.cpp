/************************************************************************/
/*  jsb_web_broker.cpp                                                  */
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

#include "jsb_web_broker.h"
#include "jsb_web_isolate.h"

namespace jsb::impl {
void Broker::SetWeakCallback(v8::Isolate *isolate, HandleID value, void *parameter, void *callback) {
	const v8::HandleScope handle_scope(isolate);
	const jsb::impl::StackPosition sp = jsbi_handle_PushStack(isolate->rt(), value);
	const jsb::impl::InternalDataID index = (jsb::impl::InternalDataID)(uintptr_t)jsbi_GetOpaque(isolate->rt(), sp);
	const jsb::impl::InternalDataPtr data = isolate->get_internal_data(index);
	JSB_WEB_LOG(VeryVerbose, "update internal data JSObject:%d id:%d pc:%d,%d (last:%d,%d)", sp, index, (uintptr_t)parameter, (uintptr_t)callback, (uintptr_t)data->weak.parameter, (uintptr_t)data->weak.callback);
	jsb_checkf(!callback || !data->weak.callback, "overriding an existing value is not allowed");
	data->weak.parameter = (void *)parameter;
	data->weak.callback = (void *)callback;
}

JSRuntime Broker::get_engine(v8::Isolate *isolate) {
	return isolate->rt();
}
} //namespace jsb::impl
