/************************************************************************/
/*  jsb_jsc_catch.h                                                     */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include <godot_cpp/variant/string.hpp>

namespace v8 {
class Isolate;
class Value;
template <typename T>
class Local;
}

namespace jsb::impl {
class TryCatch {
public:
	v8::Isolate *isolate_;

	TryCatch(v8::Isolate *isolate) : isolate_(isolate) {}
	~TryCatch() = default;

	v8::Isolate *get_isolate() const { return isolate_; }

	bool has_caught() const;
	void get_message(godot::String *r_message, godot::String *r_stacktrace = nullptr) const;

	/** 异常值本身（跨隔离区错误记录用）。必须在 `has_caught()` 之后、`get_message()` 之前取：
	 *  `get_message()` 会消费/清空该槽。 */
	v8::Local<v8::Value> get_exception_value() const;
};
} //namespace jsb::impl
