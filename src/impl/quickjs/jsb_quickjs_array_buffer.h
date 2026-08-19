/************************************************************************/
/*  jsb_quickjs_array_buffer.h                                          */
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
#include "jsb_quickjs_object.h"

namespace v8 {
//NOTE Avoid using ArrayBuffer in 'bridge' layer.
//     It has no direct alternative implementation in web.impl (for simplicity).
class ArrayBuffer : public Object {
public:
	class Allocator {
	public:
		virtual ~Allocator() = default;

		virtual void *Allocate(size_t length) = 0;
		virtual void *AllocateUninitialized(size_t length) = 0;
		virtual void Free(void *data, size_t length) = 0;
	};

	void *Data() const;
	size_t ByteLength() const;

	static Local<ArrayBuffer> New(Isolate *isolate, size_t length);

private:
	static void _free(JSRuntime *rt, void *opaque, void *ptr);
};
} //namespace v8
