/************************************************************************/
/*  jsb_quickjs_context.h                                               */
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

#include "jsb_quickjs_handle.h"
#include "jsb_quickjs_pch.h"

namespace v8 {
class Object;

class Context : public Data {
public:
	class Scope {
	public:
		Scope(Local<Context> context) {}
	};

	Isolate *GetIsolate() const { return isolate_; }

	void *GetAlignedPointerFromEmbedderData(int index) const;
	void SetAlignedPointerInEmbedderData(int index, void *data);

	static Local<Context> New(Isolate *isolate);
	Local<Object> Global() const;
};

template <>
class Global<Context> {
	// clear all fields silently after moved
	void _clear() {
		isolate_ = nullptr;
	}

public:
	Global() = default;
	Global(Isolate *isolate, Local<Context> value) { Reset(isolate, value); }

	Global(const Global &) = delete;
	Global &operator=(const Global &) = delete;

	~Global() { Reset(); }

	Global(Global &&other) noexcept {
		isolate_ = other.isolate_;
		other._clear();
	}

	void Reset() {
		if (!isolate_) return;
		isolate_ = nullptr;
	}

	void Reset(Isolate *isolate, Local<Context> value) {
		Reset();

		jsb_check(isolate);
		isolate_ = isolate;
	}

	void Reset(Isolate *isolate, const Global &value) {
		Reset(isolate, value.Get(isolate));
	}

	// Return true if no value held by this handle
	bool IsEmpty() const { return !isolate_; }

	Local<Context> Get(Isolate *isolate) const {
		jsb_check(isolate_ == isolate && isolate_);
		return Local<Context>(Data(isolate_, 0));
	}

private:
	Isolate *isolate_ = nullptr;
};
} //namespace v8

