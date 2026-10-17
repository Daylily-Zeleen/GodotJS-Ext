/************************************************************************/
/* jsb_web_serializer.h                                                 */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_web_handle.h"
#include "jsb_web_maybe.h"
#include "jsb_web_pch.h"
#include "jsb_web_primitive.h"
#include "jsb_web_typedef.h"

namespace v8 {

class Isolate;
class SharedArrayBuffer;
class WasmModuleObject;

// web 上参与序列化的多个 Environment 共享同一个 JS realm（同一线程的同一
// `_jsbb_` 模块作用域），因此「结构化序列化」拆成两类条目：
//  - 普通值：引用直通。WriteValue 把值存进 realm 级 stash（JS 侧数组，同线程
//    所有引擎可见），流里只留一个 0x01 标记；ReadValue 按入队顺序取回。
//  - 宿主对象（绑定 Godot 对象，带内部字段）：内部字段（class_id/对象指针）是
//    每个 Environment 各自一份，引用直通会把发送方的 class_id 带进接收方的
//    注册表导致越界，因此必须走 v8 同款 delegate 协议——WriteValue 调
//    `Delegate::WriteHostObject`（delegate 经 WriteRawBytes/WriteUint32 写入
//    tag + transfer 索引），接收方 ReadValue 调 `Delegate::ReadHostObject`
//    在接收 env 里重新绑定。
// 字节流是线性 op 流：0x01=stash 引用；0x03=宿主对象（其后是 delegate 写入的
// raw chunk）；每个 raw chunk 编码为 0x02 + u32 长度 + 字节。
// 注意：跨 pthread 的 worker 消息不走本实现（各自的 realm 有独立 stash）。
class ValueSerializer {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;
		virtual void ThrowDataCloneError(Local<String> message) = 0;
		virtual Maybe<bool> WriteHostObject(Isolate *isolate, Local<Object> object) = 0;
		virtual Maybe<uint32_t> GetSharedArrayBufferId(Isolate *isolate, Local<SharedArrayBuffer> array_buffer) {
			jsb_unused(isolate);
			jsb_unused(array_buffer);
			return Nothing<uint32_t>();
		}
		virtual Maybe<uint32_t> GetWasmModuleTransferId(Isolate *isolate, Local<WasmModuleObject> module) {
			jsb_unused(isolate);
			jsb_unused(module);
			return Nothing<uint32_t>();
		}
	};

	explicit ValueSerializer(Isolate *isolate) : isolate_(isolate) {}
	ValueSerializer(Isolate *isolate, Delegate *delegate) : isolate_(isolate), delegate_(delegate) {}

	void WriteHeader();

	Maybe<bool> WriteValue(Local<Context> context, Local<Value> value);

	// delegate 协议的写入端：内容原样进流（按 chunk 编码），顺序与调用顺序一致
	void WriteRawBytes(const void *source, size_t length);
	void WriteUint32(uint32_t value);

	// 字节流 = magic(4) + op 流。缓冲由 ::malloc 分配，配对 impl::Helper::free(= ::free) 释放。
	std::pair<uint8_t *, size_t> Release();

private:
	Isolate *isolate_ = nullptr;
	Delegate *delegate_ = nullptr;
	std::vector<uint8_t> buffer_;

	Maybe<bool> WriteValue(Local<Context> context, Local<Value> value, int depth);
};

class ValueDeserializer {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;
		virtual MaybeLocal<Object> ReadHostObject(Isolate *isolate) = 0;
	};

	ValueDeserializer(Isolate *isolate, const uint8_t *data, size_t size)
			: isolate_(isolate), delegate_(nullptr), data_(data), size_(size) {}
	ValueDeserializer(Isolate *isolate, const uint8_t *data, size_t size, Delegate *delegate)
			: isolate_(isolate), delegate_(delegate), data_(data), size_(size) {}

	Maybe<bool> ReadHeader(Local<Context> context);
	MaybeLocal<Value> ReadValue(Local<Context> context);

	// delegate 协议的读取端：按写入时的 chunk 顺序消费
	bool ReadRawBytes(size_t length, const void **out_data);
	bool ReadUint32(uint32_t *out_value);

private:
	Isolate *isolate_ = nullptr;
	Delegate *delegate_ = nullptr;
	const uint8_t *data_ = nullptr;
	size_t size_ = 0;
	size_t cursor_ = 0;

	MaybeLocal<Value> ReadValue(Local<Context> context, int depth);
};

} //namespace v8
