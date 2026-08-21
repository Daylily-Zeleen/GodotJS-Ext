/************************************************************************/
/*  jsb_jsc_function_interop.h                                          */
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
#include "jsb_jsc_isolate.h"
#include "jsb_jsc_object.h"
#include "jsb_jsc_typedef.h"

namespace v8 {
template <typename T>
class ReturnValue {
public:
	ReturnValue(const Data &data) : data_(data) {}

	explicit operator JSValueRef() const {
		return data_.isolate_->stack_val(data_.stack_pos_);
	}

	template <typename S>
	void Set(const Local<S> &value) const {
		if (value.IsEmpty()) {
			data_.isolate_->set_stack_copy(data_.stack_pos_, jsb::impl::StackPos::Undefined);
			return;
		}
		data_.isolate_->set_stack_copy(data_.stack_pos_, value->stack_pos_);
	}

	template <typename S>
	void Set(const Global<S> &value) const {
		if (value.IsEmpty()) {
			data_.isolate_->set_stack_copy(data_.stack_pos_, jsb::impl::StackPos::Undefined);
			return;
		}
		Set(value.Get(data_.isolate_));
	}

	void Set(int32_t value) const {
		data_.isolate_->set_stack_copy(data_.stack_pos_, JSValueMakeNumber(data_.isolate_->ctx(), value));
	}

	void SetUndefined() const {
		data_.isolate_->set_stack_copy(data_.stack_pos_, jsb::impl::StackPos::Undefined);
	}

private:
	Data data_;
};

template <typename T>
class FunctionCallbackInfo {
public:
	FunctionCallbackInfo(Isolate *isolate, int len, bool is_constructor)
			: isolate_(isolate), len_(len), stack_pos_(isolate->stack_pos_), is_constructor_(is_constructor) {
	}

	ReturnValue<T> GetReturnValue() const {
		return ReturnValue<T>(v8::Data(isolate_, stack_pos_ + jsb::impl::FunctionStackBase::ReturnValue));
	}

	Local<Object> This() const {
		return Local<Object>(v8::Data(isolate_, stack_pos_ + jsb::impl::FunctionStackBase::This));
	}

	Local<Value> Data() const {
		return Local<Object>(v8::Data(isolate_, stack_pos_ + jsb::impl::FunctionStackBase::Data));
	}

	Local<Value> NewTarget() const {
		return Local<Object>(v8::Data(isolate_, stack_pos_ + jsb::impl::FunctionStackBase::NewTarget));
	}

	Isolate *GetIsolate() const { return isolate_; }

	Local<Value> operator[](int index) const {
		jsb_check(index >= 0);
		if (index < len_) {
			return Local<Value>(v8::Data(isolate_, stack_pos_ + jsb::impl::FunctionStackBase::Num + index));
		}
		return Local<Value>(v8::Data(isolate_, jsb::impl::StackPos::Undefined));
	}

	int Length() const { return len_; }

	bool IsConstructCall() const { return is_constructor_; }

private:
	Isolate *isolate_;
	int len_;
	uint16_t stack_pos_;
	bool is_constructor_;
};

template <typename T>
class PropertyCallbackInfo {
public:
	PropertyCallbackInfo(Isolate *isolate, uint16_t stack_pos) : isolate_(isolate), stack_pos_(stack_pos) {}
	Isolate *GetIsolate() const { return isolate_; }
	ReturnValue<T> GetReturnValue() const {
		return ReturnValue<T>(v8::Data(isolate_, stack_pos_));
	}

private:
	Isolate *isolate_;
	uint16_t stack_pos_;
};
} //namespace v8
