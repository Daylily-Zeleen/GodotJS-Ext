/************************************************************************/
/* jsb_web_serializer.cpp                                               */
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

#include "jsb_web_serializer.h"

#include "jsb_web_isolate.h"

namespace v8 {

namespace {
const uint8_t kWebSerMagic[4] = { 'W', 'S', 'E', 'R' };
constexpr uint8_t kOpValue = 0x01; // stash 引用（原始值/函数等同 realm 可共享的值）
constexpr uint8_t kOpRawChunk = 0x02; // 0x02 + u32 长度 + 字节（delegate 写入）
constexpr uint8_t kOpHostObject = 0x03; // 宿主对象，随后是 delegate 的 raw chunk
constexpr uint8_t kOpObject = 0x04; // 普通对象开始，之后是 (key, value)*，以 kOpEnd 结束
constexpr uint8_t kOpEnd = 0x05; // 对象/数组结束
constexpr uint8_t kOpArray = 0x06; // 数组开始：u32 长度 + 元素*
constexpr int kMaxDepth = 64;
} //namespace

void ValueSerializer::WriteHeader() {
	buffer_.insert(buffer_.end(), kWebSerMagic, kWebSerMagic + sizeof(kWebSerMagic));
}

Maybe<bool> ValueSerializer::WriteValue(Local<Context> context, Local<Value> value) {
	return WriteValue(context, value, 0);
}

Maybe<bool> ValueSerializer::WriteValue(Local<Context> context, Local<Value> value, int depth) {
	jsb_unused(context);
	if (depth > kMaxDepth) {
		JSB_LOG(Error, "web serializer: nesting too deep (> %d)", kMaxDepth);
		return Maybe<bool>();
	}

	// 带内部字段的对象交给 delegate（内部字段按 env 绑定，不能引用直通）；
	// delegate 拒绝（Just(false)）时按普通对象/数组继续拆解。
	if (delegate_ && value->IsObject() && value.As<Object>()->InternalFieldCount() > 0) {
		buffer_.push_back(kOpHostObject);
		const Maybe<bool> result = delegate_->WriteHostObject(isolate_, value.As<Object>());
		if (result.IsNothing()) {
			return result;
		}
		if (result.ToChecked()) {
			return Maybe<bool>(true);
		}
		buffer_.pop_back();
	}

	if (value->IsObject()) {
		const jsb::impl::JSRuntime rt = isolate_->rt();
		const jsb::impl::StackPosition sp = static_cast<jsb::impl::StackPosition>(value);
		const int32_t kind = jsbi_SerKind(rt, sp);
		if (kind == 1) {
			// 数组：0x06 + u32 长度 + 元素* + 0x05
			buffer_.push_back(kOpArray);
			const int32_t length = jsbi_ArrayLen(rt, sp);
			WriteUint32(static_cast<uint32_t>(length));
			for (int32_t i = 0; i < length; ++i) {
				const Maybe<bool> element = WriteValue(context,
						Local<Value>(Data(isolate_, jsbi_ArrayGet(rt, sp, i))), depth + 1);
				if (element.IsNothing() || !element.ToChecked()) {
					return element;
				}
			}
			buffer_.push_back(kOpEnd);
			return Maybe<bool>(true);
		}
		if (kind == 2) {
			// 普通对象：0x04 + (key, value)* + 0x05
			buffer_.push_back(kOpObject);
			const jsb::impl::StackPosition keys_sp = jsbi_OwnKeys(rt, sp);
			const int32_t count = jsbi_ArrayLen(rt, keys_sp);
			for (int32_t i = 0; i < count; ++i) {
				const jsb::impl::StackPosition key_sp = jsbi_ArrayGet(rt, keys_sp, i);
				if (jsbi_SerializerStash(rt, key_sp) < 0) {
					return Maybe<bool>();
				}
				buffer_.push_back(kOpValue);
				const Maybe<bool> member = WriteValue(context,
						Local<Value>(Data(isolate_, jsbi_GetProp(rt, sp, key_sp))), depth + 1);
				if (member.IsNothing() || !member.ToChecked()) {
					return member;
				}
			}
			buffer_.push_back(kOpEnd);
			return Maybe<bool>(true);
		}
		// kind == 0（函数/类实例等）：同 realm 内按引用共享
	}

	if (jsbi_SerializerStash(isolate_->rt(), static_cast<jsb::impl::StackPosition>(value)) < 0) {
		return Maybe<bool>();
	}
	buffer_.push_back(kOpValue);
	return Maybe<bool>(true);
}

void ValueSerializer::WriteRawBytes(const void *source, size_t length) {
	buffer_.push_back(kOpRawChunk);
	uint32_t len = static_cast<uint32_t>(length);
	uint8_t *len_bytes = reinterpret_cast<uint8_t *>(&len);
	buffer_.insert(buffer_.end(), len_bytes, len_bytes + sizeof(len));
	const uint8_t *bytes = static_cast<const uint8_t *>(source);
	buffer_.insert(buffer_.end(), bytes, bytes + length);
}

void ValueSerializer::WriteUint32(uint32_t value) {
	WriteRawBytes(&value, sizeof(value));
}

std::pair<uint8_t *, size_t> ValueSerializer::Release() {
	// magic 已由 WriteHeader 追加进流，这里整体交付
	const size_t total = buffer_.size();
	uint8_t *out = static_cast<uint8_t *>(::malloc(total));
	jsb_checkf(out != nullptr, "failed to allocate serializer buffer");
	if (total > 0) {
		::memcpy(out, buffer_.data(), total);
	}
	buffer_.clear();
	return { out, total };
}

Maybe<bool> ValueDeserializer::ReadHeader(Local<Context> context) {
	jsb_unused(context);
	constexpr size_t kMagicSize = sizeof(kWebSerMagic);
	if (size_ < kMagicSize || ::memcmp(data_, kWebSerMagic, kMagicSize) != 0) {
		return Maybe<bool>();
	}
	cursor_ = kMagicSize;
	return Maybe<bool>(true);
}

MaybeLocal<Value> ValueDeserializer::ReadValue(Local<Context> context) {
	return ReadValue(context, 0);
}

MaybeLocal<Value> ValueDeserializer::ReadValue(Local<Context> context, int depth) {
	jsb_unused(context);
	if (depth > kMaxDepth || cursor_ >= size_ || data_[cursor_] == kOpRawChunk) {
		return {};
	}
	const uint8_t op = data_[cursor_++];
	const jsb::impl::JSRuntime rt = isolate_->rt();
	if (op == kOpValue) {
		// stash 已空（发送/接收不配对）时返回 undefined，让上层日志暴露问题
		const jsb::impl::StackPosition sp = jsbi_SerializerTake(rt);
		return MaybeLocal<Value>(Data(isolate_, sp));
	}
	if (op == kOpHostObject) {
		if (!delegate_) {
			return {};
		}
		return delegate_->ReadHostObject(isolate_);
	}
	if (op == kOpObject) {
		const jsb::impl::StackPosition object_sp = jsbi_NewPlainObject(rt);
		for (;;) {
			if (cursor_ >= size_ || data_[cursor_] == kOpRawChunk) {
				return {};
			}
			if (data_[cursor_] == kOpEnd) {
				++cursor_;
				break;
			}
			MaybeLocal<Value> key = ReadValue(context, depth + 1);
			if (key.IsEmpty()) {
				return {};
			}
			MaybeLocal<Value> member = ReadValue(context, depth + 1);
			if (member.IsEmpty()) {
				return {};
			}
			jsbi_SetProp(rt, object_sp, static_cast<jsb::impl::StackPosition>(key.ToLocalChecked()),
					static_cast<jsb::impl::StackPosition>(member.ToLocalChecked()));
		}
		return MaybeLocal<Value>(Data(isolate_, object_sp));
	}
	if (op == kOpArray) {
		uint32_t length = 0;
		if (!ReadUint32(&length)) {
			return {};
		}
		const jsb::impl::StackPosition array_sp = jsbi_SerNewArray(rt, static_cast<int32_t>(length));
		for (uint32_t i = 0; i < length; ++i) {
			MaybeLocal<Value> element = ReadValue(context, depth + 1);
			if (element.IsEmpty()) {
				return {};
			}
			jsbi_ArraySet(rt, array_sp, static_cast<int32_t>(i),
					static_cast<jsb::impl::StackPosition>(element.ToLocalChecked()));
		}
		if (cursor_ >= size_ || data_[cursor_] != kOpEnd) {
			return {};
		}
		++cursor_;
		return MaybeLocal<Value>(Data(isolate_, array_sp));
	}
	return {};
}

bool ValueDeserializer::ReadRawBytes(size_t length, const void **out_data) {
	if (out_data == nullptr || cursor_ + 1 + sizeof(uint32_t) > size_) {
		return false;
	}
	if (data_[cursor_] != kOpRawChunk) {
		return false;
	}
	uint32_t chunk_len = 0;
	::memcpy(&chunk_len, data_ + cursor_ + 1, sizeof(chunk_len));
	if (chunk_len != length || cursor_ + 1 + sizeof(chunk_len) + chunk_len > size_) {
		return false;
	}
	*out_data = data_ + cursor_ + 1 + sizeof(chunk_len);
	cursor_ += 1 + sizeof(chunk_len) + chunk_len;
	return true;
}

bool ValueDeserializer::ReadUint32(uint32_t *out_value) {
	const void *bytes = nullptr;
	if (!ReadRawBytes(sizeof(uint32_t), &bytes)) {
		return false;
	}
	::memcpy(out_value, bytes, sizeof(uint32_t));
	return true;
}

} //namespace v8
