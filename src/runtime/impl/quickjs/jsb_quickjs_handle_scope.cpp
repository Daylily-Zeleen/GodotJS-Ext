/************************************************************************/
/*  jsb_quickjs_handle_scope.cpp                                        */
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

#include "jsb_quickjs_handle_scope.h"
#include "jsb_quickjs_isolate.h"
namespace v8 {
HandleScope::HandleScope(Isolate *isolate) {
	isolate_ = isolate;

	last_ = isolate_->handle_scope_;
	stack_ = isolate_->stack_pos_;
	isolate_->handle_scope_ = this;
	JSB_QUICKJS_LOG(VeryVerbose, "enter stack frame %d", stack_);
}

HandleScope::~HandleScope() {
	jsb_check(isolate_->handle_scope_ == this);
	for (uint16_t i = stack_; i < isolate_->stack_pos_; i++) {
		JS_FreeValue(isolate_->ctx_, isolate_->stack_[i]);
	}
	isolate_->handle_scope_ = last_;
	isolate_->stack_pos_ = stack_;
	if (!isolate_->handle_scope_) {
		isolate_->swap_free_queue();
	}
	JSB_QUICKJS_LOG(VeryVerbose, "leave stack frame %d", stack_);
}

} //namespace v8
