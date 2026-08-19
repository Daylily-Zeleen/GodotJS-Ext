/************************************************************************/
/*  jsb_web_function.cpp                                                */
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

#include "jsb_web_function.h"

#include "jsb_web_context.h"

namespace v8 {
MaybeLocal<Value> Function::Call(Local<Context> context, Local<Value> recv, int argc, Local<Value> argv[]) {
	jsb::impl::StackPosition *vargv = jsb_stackalloc(jsb::impl::StackPosition, argc);
	for (int i = 0; i < argc; i++) {
		vargv[i] = argv[i]->stack_pos_;
	}
	const jsb::impl::StackPosition rval_sp = jsbi_Call(isolate_->rt(), recv->stack_pos_, stack_pos_, argc, vargv);
	if (rval_sp == jsb::impl::StackBase::Error) {
		return MaybeLocal<Value>();
	}
	return MaybeLocal<Value>(Data(isolate_, rval_sp));
}

MaybeLocal<Function> Function::New(Local<Context> context, FunctionCallback callback, Local<Value> data, int length) {
	Isolate *isolate = context->isolate_;
	static_assert(sizeof(callback) == sizeof(void *));
	const jsb::impl::StackPosition func_sp = jsbi_NewCFunction(isolate->rt(), (jsb::impl::FunctionPointer)callback, data->stack_pos_, nullptr);
	if (func_sp == jsb::impl::StackBase::Error) {
		return MaybeLocal<Function>();
	}
	return MaybeLocal<Function>(Data(isolate, func_sp));
}

Local<Context> Function::GetCreationContextChecked() const {
	return Local<Context>(Data(isolate_, 0));
}
} //namespace v8
