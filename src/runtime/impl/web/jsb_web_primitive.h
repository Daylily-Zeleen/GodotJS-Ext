/************************************************************************/
/*  jsb_web_primitive.h                                                 */
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
#include "jsb_web_data.h"
#include "jsb_web_handle.h"
#include "jsb_web_pch.h"

namespace v8 {
class Isolate;
class String;
class Context;

template <typename T>
class Maybe;

class Value : public Data {
public:
	MaybeLocal<String> ToDetailString(Local<Context> context) const;
	Maybe<double> NumberValue(Local<Context> context) const;
	Maybe<int32_t> Int32Value(Local<Context> context) const;
	bool BooleanValue(Isolate *isolate) const;

	MaybeLocal<String> ToString(Local<Context> context) const;
};

class External : public Value {
public:
	void *Value() const;

	static Local<External> New(Isolate *isolate, void *value);
};

class Primitive : public Value {};
class Name : public Primitive {};

class String : public Name {
public:
	int Length() const;

	static Local<String> Empty(Isolate *isolate);
};

class Symbol : public Name {
public:
	static Local<Symbol> New(Isolate *isolate);
};

class Boolean : public Primitive {
public:
	bool Value() const;

	static Local<Boolean> New(Isolate *isolate, bool value);
};

class Number : public Primitive {
public:
	double Value() const;

	static Local<Number> New(Isolate *isolate, double value);
};

class BigInt : public Primitive {
public:
	int64_t Int64Value(bool *lossless = nullptr) const;

	static Local<BigInt> New(Isolate *isolate, int64_t value);
	static Local<BigInt> NewFromUnsigned(Isolate *isolate, uint64_t value);
};

class Integer : public Number {
public:
	// int64_t Value() const;

	static Local<Integer> New(Isolate *isolate, int32_t value);
	static Local<Integer> NewFromUnsigned(Isolate *isolate, uint32_t value);
};

class Uint32 : public Integer {
public:
	uint32_t Value() const;
};

class Int32 : public Integer {
public:
	int32_t Value() const;
};

Local<Primitive> Undefined(Isolate *isolate);
Local<Primitive> Null(Isolate *isolate);
} //namespace v8
