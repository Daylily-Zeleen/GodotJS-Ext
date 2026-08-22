/************************************************************************/
/*  jsb_web_isolate.cpp                                                 */
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

#include "jsb_web_isolate.h"

#include "jsb_web_catch.h"
#include "jsb_web_context.h"
#include "jsb_web_handle.h"

namespace v8 {
Isolate *Isolate::New(const CreateParams &params) {
	Isolate *isolate = memnew(Isolate);
	return isolate;
}

Isolate::Isolate() : ref_count_(1), disposed_(false), handle_scope_(nullptr) {
	rt_ = jsbi_NewEngine(this);
}

Isolate::~Isolate() {
	jsb_check(!rt_);
}

void Isolate::_release() {
	JSB_WEB_LOG(VeryVerbose, "release web runtime");

	// cleanup
	jsb_check(!handle_scope_);

	// make it behave like v8, not to trigger gc callback after the isolate disposed
	internal_data_.clear();

	// dispose the runtime
	jsbi_FreeEngine(rt_);
	rt_ = {};

	memdelete(this);
}

void Isolate::Dispose() {
	jsb_check(!disposed_);
	disposed_ = true;
	_remove_reference();
}

void Isolate::SetData(int index, void *data) {
	jsb_check(index == 0);
	embedder_data_ = data;
}

Local<Context> Isolate::GetCurrentContext() {
	return Local<Context>(Data(this, 0));
}

} //namespace v8
