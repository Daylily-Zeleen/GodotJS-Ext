/************************************************************************/
/*  jsb_quickjs_array_buffer.cpp                                        */
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

#include "jsb_quickjs_array_buffer.h"
#include "jsb_quickjs_isolate.h"

namespace v8 {
void *ArrayBuffer::Data() const {
	size_t size;
	const uint8_t *ptr = JS_GetArrayBuffer(isolate_->ctx(), &size, (JSValue) * this);
	jsb_check(ptr);
	return (void *)ptr;
}

size_t ArrayBuffer::ByteLength() const {
	size_t size;
	const uint8_t *ptr = JS_GetArrayBuffer(isolate_->ctx(), &size, (JSValue) * this);
	jsb_check(ptr);
	return size;
}

Local<ArrayBuffer> ArrayBuffer::New(Isolate *isolate, size_t length) {
	uint8_t *buf = (uint8_t *)memalloc(length);
	return Local<ArrayBuffer>(v8::Data(isolate, isolate->push_steal(JS_NewArrayBuffer(isolate->ctx(), buf, length, _free, nullptr, 0))));
}

void ArrayBuffer::_free(JSRuntime *rt, void *opaque, void *ptr) {
	memfree(ptr);
}

} //namespace v8
