/************************************************************************/
/*  jsb_v8_catch.h                                                      */
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

#include "jsb_v8_helper.h"
#include "jsb_v8_pch.h"
#include <godot_cpp/variant/string.hpp>

namespace jsb::impl {
struct TryCatch {
private:
	v8::Isolate *isolate_;
	v8::TryCatch try_catch_;

public:
	TryCatch(v8::Isolate *isolate) : isolate_(isolate), try_catch_(isolate) {}

	TryCatch(const TryCatch &) = delete;
	TryCatch &operator=(const TryCatch &) = delete;
	TryCatch(TryCatch &&) = delete;
	TryCatch &operator=(TryCatch &&) = delete;

	v8::Isolate *get_isolate() const { return isolate_; }

	bool has_caught() const { return try_catch_.HasCaught(); }

	void get_message(godot::String *r_message, godot::String *r_stacktrace = nullptr) const {
		const v8::Local<v8::Message> message = try_catch_.Message();
		if (message.IsEmpty()) {
			if (r_message) *r_message = "";
			if (r_stacktrace) *r_stacktrace = "";
			return;
		}

		v8::Isolate *isolate = isolate_;
		const v8::Local<v8::Context> context = isolate->GetCurrentContext();
		if (r_message) {
			*r_message = Helper::to_string(isolate, message->Get());
		}

		if (r_stacktrace) {
			if (v8::Local<v8::Value> stack_trace; try_catch_.StackTrace(context).ToLocal(&stack_trace)) {
				*r_stacktrace = Helper::to_string(isolate, stack_trace);
			}
		}
	}
};
} //namespace jsb::impl
