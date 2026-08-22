/************************************************************************/
/*  jsb_v8_class.h                                                      */
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
#include "jsb_v8_pch.h"

namespace jsb::impl {
class Class {
private:
	friend class ClassBuilder;

	// strong reference.
	// the counterpart of exposed C++ class.
	//NOTE template_.GetFunction() returns the `constructor`,
	//NOTE `constructor == info.NewTarget()` only if directly creating a class instance
	v8::Global<v8::FunctionTemplate> template_;

public:
	Class() = default;
	~Class() = default;

	Class(Class &&) noexcept = default;
	Class &operator=(Class &&) = default;

	Class(const Class &) = delete;
	Class &operator=(const Class &) = delete;

	_FORCE_INLINE_ bool IsEmpty() const {
		return template_.IsEmpty();
	}

	_FORCE_INLINE_ v8::Local<v8::Function> Get(v8::Isolate *isolate) const {
		return template_.Get(isolate)->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();
	}

	//NOTE NewInstance should not trigger the underlying native constructor of this class
	_FORCE_INLINE_ v8::Local<v8::Object> NewInstance(const v8::Local<v8::Context> context) const {
		return template_.Get(context->GetIsolate())->InstanceTemplate()->NewInstance(context).ToLocalChecked();
	}

private:
	Class(v8::Isolate *isolate, const v8::Local<v8::FunctionTemplate> p_template) {
		template_.Reset(isolate, p_template);
	}
};
} //namespace jsb::impl
