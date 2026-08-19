/************************************************************************/
/*  jsb_web_class.h                                                     */
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
#include "jsb_web_handle.h"
#include "jsb_web_pch.h"

namespace jsb::impl {
class Class {
private:
	friend class ClassBuilder;

	// strong reference.
	// the counterpart of exposed C++ class.
	// in web, it's the prototype object.
	v8::Global<v8::Object> prototype_;

	//TODO may unnecessary, should be identical with prototype.constructor?
	v8::Global<v8::Function> constructor_;

public:
	Class() = default;
	~Class() = default;

	Class(Class &&) noexcept = default;
	Class &operator=(Class &&) = default;

	Class(const Class &) = delete;
	Class &operator=(const Class &) = delete;

	_FORCE_INLINE_ bool IsEmpty() const {
		return prototype_.IsEmpty() || constructor_.IsEmpty();
	}

	// the returned value is the constructor function (the class)
	_FORCE_INLINE_ v8::Local<v8::Object> Get(v8::Isolate *isolate) const {
		return v8::Local<v8::Object>(v8::Data(isolate, constructor_.Get(isolate)->stack_pos_));
	}

	//NOTE NewInstance should not trigger the underlying native constructor of this class
	_FORCE_INLINE_ v8::Local<v8::Object> NewInstance(const v8::Local<v8::Context> context) const {
		v8::Isolate *isolate = context->GetIsolate();
		const jsb::impl::StackPosition sp = jsbi_NewInstance(isolate->rt(), prototype_.Get(isolate)->stack_pos_);
		return v8::Local<v8::Object>(v8::Data(isolate, sp));
	}

private:
	Class(v8::Isolate *isolate, const v8::Local<v8::Object> proto, const v8::Local<v8::Function> constructor) {
		prototype_.Reset(isolate, proto);
		constructor_.Reset(isolate, constructor);
	}
};
} //namespace jsb::impl
