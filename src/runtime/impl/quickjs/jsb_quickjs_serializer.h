/************************************************************************/
/*  jsb_quickjs_serializer.h                                            */
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
#include "jsb_quickjs_pch.h"
#include <vector>

namespace v8 {
class Isolate;

template <typename T>
class Local;

template <typename T>
class Maybe;

template <typename T>
class MaybeLocal;

class Context;
class String;
class Object;
class SharedArrayBuffer;
class WasmModuleObject;
class Value;

class ValueSerializer {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;
		virtual void ThrowDataCloneError(Local<String> message);
		virtual Maybe<bool> WriteHostObject(Isolate *isolate, Local<Object> object);
		virtual Maybe<uint32_t> GetSharedArrayBufferId(Isolate *isolate, Local<SharedArrayBuffer> shared_array_buffer);
		virtual Maybe<uint32_t> GetWasmModuleTransferId(Isolate *isolate, Local<WasmModuleObject> module);
	};

private:
	uint8_t *buffer_ = nullptr;
	size_t size_ = 0;
	Delegate *delegate_ = nullptr;
	std::vector<uint8_t> stream_buffer_;

public:
	explicit ValueSerializer(Isolate *isolate, Delegate *delegate = nullptr);

	void WriteHeader();
	Maybe<bool> WriteValue(Local<Context> context, Local<Value> value);
	void WriteUint32(uint32_t value);
	void WriteRawBytes(const void *source, size_t length);
	std::pair<uint8_t *, size_t> Release();
};

class ValueDeserializer {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;
		virtual MaybeLocal<Object> ReadHostObject(Isolate *isolate);
	};

private:
	uint8_t *buffer_ = nullptr;
	size_t size_ = 0;
	Delegate *delegate_ = nullptr;
	size_t read_offset_ = 0;

public:
	ValueDeserializer(Isolate *isolate, const uint8_t *data, size_t size, Delegate *delegate = nullptr);
	Maybe<bool> ReadHeader(Local<Context> context);
	bool ReadUint32(uint32_t *value);
	bool ReadRawBytes(size_t length, const void **data);
	size_t GetReadOffset() const;
	bool SetReadOffset(size_t offset);
	MaybeLocal<Value> ReadValue(Local<Context> context);
};
} //namespace v8
