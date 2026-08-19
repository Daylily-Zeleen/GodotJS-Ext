/************************************************************************/
/*  jsb_jsc_data.h                                                      */
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

class Data {
public:
	Data() = default;
	Data(Isolate *isolate, uint16_t stack_pos) : isolate_(isolate), stack_pos_(stack_pos) {}

	Isolate *isolate_ = nullptr;
	uint16_t stack_pos_ = 0;

	explicit operator JSValueRef() const;

	bool operator==(const Data &other) const {
		return isolate_ == other.isolate_ && (stack_pos_ == other.stack_pos_ || strict_eq(other));
	}

	// should only be called on Name & Object
	int GetIdentityHash() const;

	bool IsNullOrUndefined() const;
	bool IsNull() const;
	bool IsUndefined() const;
	bool IsBoolean() const;
	bool IsObject() const;
	bool IsFunction() const;
	bool IsPromise() const;
	bool IsArray() const;
	bool IsMap() const;
	bool IsSet() const;
	bool IsString() const;
	bool IsSymbol() const;
	bool IsInt32() const;
	bool IsUint32() const;
	bool IsNumber() const;
	bool IsBigInt() const;
	bool IsExternal() const;
	bool IsArrayBuffer() const;

private:
	bool strict_eq(const Data &other) const;
};
} //namespace v8
