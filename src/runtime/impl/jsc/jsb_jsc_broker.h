/************************************************************************/
/*  jsb_jsc_broker.h                                                    */
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
#include "jsb_jsc_pch.h"

namespace v8 {
class Isolate;
}

namespace jsb::impl {
// a helper class to break header cyclic dependencies
class Broker {
public:
	static void SetWeak(v8::Isolate *isolate, JSObjectRef value, void *parameter, void *callback);

	static JSContextGroupRef rt(v8::Isolate *isolate);
	static JSContextRef ctx(v8::Isolate *isolate);

	// peek JSValue on stack (without duplicating)
	static JSValueRef stack_val(v8::Isolate *isolate, uint16_t index);

	// copy JSValue on stack (with duplicating)
	static JSValueRef stack_dup(v8::Isolate *isolate, uint16_t index);

	static uint16_t push_copy(v8::Isolate *isolate, JSValueRef value);

	static void _add_reference(v8::Isolate *isolate);
	static void _remove_reference(v8::Isolate *isolate);

	// strict eq check
	static bool IsStrictEqual(v8::Isolate *isolate, JSValueRef a, JSValueRef b);
};
} //namespace jsb::impl
