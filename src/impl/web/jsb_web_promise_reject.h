/************************************************************************/
/*  jsb_web_promise_reject.h                                            */
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

#include "jsb_web_typedef.h"

namespace v8 {
template <typename T>
class Local;

class Promise;
class Value;

class PromiseRejectMessage {
public:
	PromiseRejectMessage(Isolate *isolate, PromiseRejectEvent event, jsb::impl::StackPosition promise_sp, jsb::impl::StackPosition reason_sp)
			: isolate_(isolate), event_(event), promise_sp_(promise_sp), reason_sp_(reason_sp) {}

	PromiseRejectEvent GetEvent() const { return event_; }

	Local<Promise> GetPromise() const;
	Local<Value> GetValue() const;

private:
	Isolate *isolate_;
	PromiseRejectEvent event_;
	jsb::impl::StackPosition promise_sp_;
	jsb::impl::StackPosition reason_sp_;
};

using PromiseRejectCallback = void (*)(PromiseRejectMessage);
} //namespace v8

