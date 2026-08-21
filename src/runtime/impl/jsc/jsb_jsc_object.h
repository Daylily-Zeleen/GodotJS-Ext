/************************************************************************/
/*  jsb_jsc_object.h                                                    */
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
#include "jsb_jsc_handle.h"
#include "jsb_jsc_maybe.h"
#include "jsb_jsc_pch.h"
#include "jsb_jsc_primitive.h"
#include "jsb_jsc_typedef.h"

namespace v8 {
class Isolate;
class FunctionTemplate;

class Object : public Data {
public:
	Isolate *GetIsolate() const { return isolate_; }

	int InternalFieldCount() const;
	void SetAlignedPointerInInternalField(int slot, void *data);
	void SetAlignedPointerInInternalFields(int argc, int indices[], void *values[]);
	void *GetAlignedPointerFromInternalField(int slot) const;

	Local<String> GetConstructorName();

	Maybe<bool> Set(Local<Context> context, Local<Value> key, Local<Value> value);
	Maybe<bool> Set(Local<Context> context, uint32_t index, Local<Value> value);

	MaybeLocal<Value> Get(Local<Context> context, Local<Value> key) const;
	MaybeLocal<Value> Get(Local<Context> context, uint32_t index) const;

	Maybe<bool> Has(Local<Context> context, Local<Value> key) const;

	Maybe<bool> DefineOwnProperty(
			Local<Context> context, Local<Name> key, Local<Value> value, PropertyAttribute attributes = None);

	MaybeLocal<Value> GetOwnPropertyDescriptor(Local<Context> context, Local<Name> key) const;
	Maybe<bool> HasOwnProperty(Local<Context> context, Local<Name> key) const;

	MaybeLocal<Array> GetOwnPropertyNames(
			Local<Context> context, PropertyFilter filter, KeyConversionMode key_conversion = KeyConversionMode::kKeepNumbers);

	Maybe<bool> SetPrototype(Local<Context> context, Local<Value> prototype);
	Local<Value> GetPrototype();
	MaybeLocal<Value> CallAsConstructor(Local<Context> context, int argc, Local<Value> argv[]);
	void SetAccessorProperty(Local<Name> name, Local<FunctionTemplate> getter = Local<FunctionTemplate>(), Local<FunctionTemplate> setter = Local<FunctionTemplate>());

	Maybe<bool> SetLazyDataProperty(
			Local<Context> context, Local<Name> name, AccessorNameGetterCallback getter);

	static Local<Object> New(Isolate *isolate);
};

class Promise : public Object {
public:
	class Resolver : public Object {
		enum : uint32_t { kHolderIndexResolve,
			kHolderIndexReject,
			kHolderIndexPromise,
			kHolderIndexCount };

	public:
		static MaybeLocal<Resolver> New(Local<Context> context);

		Local<Promise> GetPromise();

		Maybe<bool> Resolve(Local<Context> context, Local<Value> value);

		Maybe<bool> Reject(Local<Context> context, Local<Value> value);
	};
};

} //namespace v8
