/************************************************************************/
/*  jsb_preset_source.h                                                 */
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
#include "jsb_internal_pch.h"
#include "jsb_logger.h"
#include "jsb_macros.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal {
struct PresetSource {
private:
	String filename_;

	// identical to uncompressed_data_.size
	// we need this because it's lazily uncompressed
	size_t uncompressed_size_;

	// uncompressed source (if raw source is not static)
	Vector<uint8_t> uncompressed_data_;

	// raw source (raw source is static, do not free it)
	size_t data_size_;
	const char *data_;

	bool is_zero_terminated_;

public:
	bool is_valid() const { return !filename_.is_empty() && data_ != nullptr && data_size_ != 0; }

	PresetSource()
			: filename_()
			, uncompressed_size_(0)
			, uncompressed_data_()
			, data_size_(0)
			, data_(nullptr)
			, is_zero_terminated_(false) {}

	PresetSource(const String &p_filename, const char *p_data, size_t p_size, size_t p_uncompressed_size, bool p_is_zero_terminated)
			: filename_(p_filename)
			, uncompressed_size_(p_uncompressed_size)
			, uncompressed_data_()
			, data_size_(p_size)
			, data_(p_data)
			, is_zero_terminated_(p_is_zero_terminated) {}

	PresetSource(PresetSource &&p_other) noexcept { *this = std::move(p_other); }
	PresetSource(const PresetSource &p_other) noexcept { *this = p_other; }

	PresetSource &operator=(PresetSource &&p_other) noexcept {
		if (this != &p_other) {
			filename_ = p_other.filename_;
			uncompressed_size_ = p_other.uncompressed_size_;
			uncompressed_data_ = p_other.uncompressed_data_;
			data_size_ = p_other.data_size_;
			data_ = p_other.data_;

			p_other.filename_ = String();
			p_other.uncompressed_data_ = {};
			p_other.uncompressed_size_ = 0;
			p_other.data_ = nullptr;
			p_other.data_size_ = 0;
		}
		return *this;
	}

	PresetSource &operator=(const PresetSource &p_other) noexcept {
		if (this != &p_other) {
			filename_ = p_other.filename_;
			uncompressed_size_ = p_other.uncompressed_size_;
			uncompressed_data_ = p_other.uncompressed_data_;
			data_size_ = p_other.data_size_;
			data_ = p_other.data_;
		}
		return *this;
	}

	~PresetSource() = default;

	const String &get_filename() const { return filename_; }

	// return the uncompressed data if it's compressed.
	// the preset data size includes the null terminator, therefore the returned data len is `len - 1` if `is_zero_terminated_` is true.
	const char *get_data(size_t &r_len) const {
		if (uncompressed_size_) {
			if (uncompressed_data_.is_empty()) {
				const_cast<PresetSource *>(this)->uncompress();
			}
			jsb_check((size_t)uncompressed_data_.size() == uncompressed_size_);
			r_len = is_zero_terminated_ ? uncompressed_size_ - 1 : uncompressed_size_;
			return (const char *)uncompressed_data_.ptr();
		}

		r_len = is_zero_terminated_ ? data_size_ - 1 : data_size_;
		return data_;
	}

private:
	void uncompress() {
		jsb_check((size_t)(int)uncompressed_size_ == uncompressed_size_);
		jsb_check((size_t)(int)data_size_ == data_size_);

		// TODO: 调整生成的数据，考虑直接生成 PackedByteArray，避免内存拷贝
		PackedByteArray data;
		data.resize((int)data_size_);
		memcpy(data.ptrw(), (const uint8_t *)data_, (int)data_size_);

		const PackedByteArray &decompressed = data.decompress(uncompressed_size_, FileAccess::COMPRESSION_DEFLATE);

		uncompressed_data_.resize(decompressed.size());
		memcpy(uncompressed_data_.ptrw(), (const uint8_t *)decompressed.ptr(), decompressed.size());
	}
};
} //namespace jsb::internal
