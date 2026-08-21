/************************************************************************/
/*  jsb_jsc_primitive.h                                                 */
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
#include "jsb_jsc_data.h"
#include "jsb_jsc_handle.h"
#include "jsb_jsc_pch.h"

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
	static constexpr int kMaxLength = ((1 << 30) - 1);

	int Length() const;

	static Local<String> Empty(Isolate *isolate);

	// V8 string creation APIs
	static MaybeLocal<String> NewFromUtf8(Isolate *isolate, const char *data, NewStringType /* type */, int length);

	// UTF-8 encoded characters.
	int WriteUtf8(Isolate *isolate, char *buffer, int length = -1, int *nchars_ref = nullptr /*, int options = NO_OPTIONS*/) const;

	// V8 compatibility macro for creating string from literal
	template <int N>
	static Local<String> NewFromUtf8Literal(
			Isolate *isolate, const char (&literal)[N], NewStringType type = NewStringType::kNormal) {
		static_assert(N <= kMaxLength, "String is too long");
		if constexpr (N == 1) {
			// Zero-length string specialization (templated string size includes terminator).
			return String::Empty(isolate);
		}
		return NewFromUtf8Literal(isolate, literal, type, N - 1);
	}

private:
	static Local<String> NewFromUtf8Literal(
			Isolate *isolate, const char *literal, NewStringType type, int length);
};

class Symbol : public Name {
public:
	static Local<Symbol> New(Isolate *isolate);
	static Local<Symbol> New(Isolate *isolate, Local<String> description);

	// Well-Known Symbols - retrieved from the global Symbol object
	static Local<Symbol> GetAsyncIterator(Isolate *isolate);
	static Local<Symbol> GetHasInstance(Isolate *isolate);
	static Local<Symbol> GetIsConcatSpreadable(Isolate *isolate);
	static Local<Symbol> GetIterator(Isolate *isolate);
	static Local<Symbol> GetMatch(Isolate *isolate);
	static Local<Symbol> GetReplace(Isolate *isolate);
	static Local<Symbol> GetSearch(Isolate *isolate);
	static Local<Symbol> GetSplit(Isolate *isolate);
	static Local<Symbol> GetToPrimitive(Isolate *isolate);
	static Local<Symbol> GetToStringTag(Isolate *isolate);
	static Local<Symbol> GetUnscopables(Isolate *isolate);

	// Get the description of this symbol
	Local<String> Description(Isolate *isolate) const;

	// Symbol.for(key) - get or create a global symbol
	static Local<Symbol> For(Isolate *isolate, Local<String> key);

private:
	static Local<Symbol> _get_well_known(Isolate *isolate, const char *name);
};

class Boolean : public Primitive {
public:
	bool Value() const;

	static Local<Boolean> New(Isolate *isolate, bool value);
};

class Number : public Primitive {
public:
	// will return NaN if an error is thrown (but ignored)
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
	// will return 0 if an error is thrown (but ignored)
	int32_t Value() const;
};

class Exception {
public:
	static Local<Value> Error(Local<String> message);
};

Local<Primitive> Undefined(Isolate *isolate);
Local<Primitive> Null(Isolate *isolate);
} //namespace v8
