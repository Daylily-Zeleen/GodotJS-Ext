/************************************************************************/
/*  jsb_message.h                                                       */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once
#include "jsb_bridge_pch.h"
#include "jsb_buffer.h"
#include "jsb_object_handle.h"

namespace jsb {
struct TransferData {
	NativeObjectID source_worker_id;
	uint32_t transfer_index;
	Variant variant;
	String script_path;
	List<Pair<StringName, Variant>> state;
	templates::BitField<ObjectBindingFlags> flags;

	TransferData() : transfer_index(0) {}

	TransferData(NativeObjectID p_source_worker_id, uint32_t p_transfer_index, const Variant &p_variant, const String &p_script_path = {}, const List<Pair<StringName, Variant>> &p_state = {})
			: source_worker_id(p_source_worker_id), transfer_index(p_transfer_index), variant(p_variant), script_path(p_script_path), state(p_state) {}

	~TransferData() = default;
};

/** 消息的附加数据基类（不是每条消息都带数据，所以 `Message` 按指针持有）。具体类型由生产者定义。 */
struct MessageRawData {
	virtual ~MessageRawData() = default;
};

struct Message {
public:
	enum Type {
		TYPE_NONE = 0,

		// worker ready
		TYPE_READY,

		// worker message
		TYPE_MESSAGE,

		// worker error：payload 是错误记录（`MessageRawData`），不是普通 onmessage 数据
		TYPE_ERROR,
	};

	Message() = delete;
	~Message() = default;

	Message(const Message &) = delete;
	Message &operator=(const Message &) = delete;

	Message(Message &&) noexcept = default;
	Message &operator=(Message &&) noexcept = default;

	Message(Type p_type, NativeObjectID p_id, Buffer &&p_buffer = Buffer(), std::vector<TransferData> &&p_transfers = std::vector<TransferData>())
			: type_(p_type), id_(p_id), buffer_(std::move(p_buffer)), transfers(std::move(p_transfers)) {
	}
	Message(Type p_type, NativeObjectID p_id, std::unique_ptr<MessageRawData> p_raw_data)
			: type_(p_type), id_(p_id), raw_data_(std::move(p_raw_data)) {
	}

	// object id of worker object in master env
	NativeObjectID get_id() const { return id_; }

	Type get_type() const { return type_; }

	const Buffer &get_buffer() const { return buffer_; }

	const std::vector<TransferData> &get_transfers() const { return transfers; }

	std::unique_ptr<MessageRawData> take_rawdata() { return std::move(raw_data_); }

private:
	Type type_;
	NativeObjectID id_;
	Buffer buffer_;
	std::vector<TransferData> transfers;
	std::unique_ptr<MessageRawData> raw_data_;
};

} //namespace jsb
