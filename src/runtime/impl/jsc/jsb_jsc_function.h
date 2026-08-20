/************************************************************************/
/*  jsb_jsc_function.h                                                  */
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

#pragma once
#include "jsb_jsc_function_interop.h"
#include "jsb_jsc_object.h"

namespace jsb::impl {
class Helper;
}

namespace v8 {
class Function : public Object {
	friend class jsb::impl::Helper;
	friend class FunctionTemplate;

public:
	MaybeLocal<Value> Call(
			Local<Context> context,
			Local<Value> recv,
			int argc,
			Local<Value> argv[]);

	static MaybeLocal<Function> New(
			Local<Context> context, FunctionCallback callback, Local<Value> data = Local<Value>(), int length = 0);

	Local<Context> GetCreationContextChecked() const;

private:
	static JSValueRef _function_call(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef *exception);
};

} //namespace v8
