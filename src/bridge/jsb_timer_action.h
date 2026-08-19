/************************************************************************/
/*  jsb_timer_action.h                                                  */
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

#include "jsb_bridge_pch.h"

namespace jsb {
/**
 * This struct is *not* POD, but aims to be compatible with SArray's memory relocation logic.
 */
struct JavaScriptTimerAction {
	_FORCE_INLINE_ JavaScriptTimerAction() : function_(nullptr), argc_(0), argv_(nullptr) {
	}

	_FORCE_INLINE_ JavaScriptTimerAction(v8::Global<v8::Function> &&p_func, int p_argc) : argc_(p_argc) {
		function_ = new v8::Global<v8::Function>(std::move(p_func));

		if (p_argc > 0) {
			argv_ = new v8::Global<v8::Value>[p_argc];
		} else {
			argv_ = nullptr;
		}
	}

	_FORCE_INLINE_ ~JavaScriptTimerAction() {
		delete function_;
		delete[] argv_;
		function_ = nullptr;
		argv_ = nullptr;
	}

	JavaScriptTimerAction(JavaScriptTimerAction &p_other) = delete;

	_FORCE_INLINE_ JavaScriptTimerAction(JavaScriptTimerAction &&p_other) noexcept
			: function_(p_other.function_), argc_(p_other.argc_), argv_(p_other.argv_) {
		p_other.function_ = nullptr;
		p_other.argc_ = 0;
		p_other.argv_ = nullptr;
	}

	JavaScriptTimerAction &operator=(JavaScriptTimerAction &p_other) = delete;

	_FORCE_INLINE_ JavaScriptTimerAction &operator=(JavaScriptTimerAction &&p_other) noexcept {
		if (this != &p_other) {
			delete function_;
			delete[] argv_;

			function_ = p_other.function_;
			argc_ = p_other.argc_;
			argv_ = p_other.argv_;

			p_other.function_ = nullptr;
			p_other.argc_ = 0;
			p_other.argv_ = nullptr;
		}
		return *this;
	}

	_FORCE_INLINE_ explicit operator bool() const { return function_ && !function_->IsEmpty(); }

	_FORCE_INLINE_ void store(int index, v8::Global<v8::Value> &&value) {
		jsb_check(index >= 0 && index < argc_);
		argv_[index] = std::move(value);
	}

	void operator()(v8::Isolate *isolate);

private:
	v8::Global<v8::Function> *function_;
	int argc_;
	v8::Global<v8::Value> *argv_;
};
} //namespace jsb
