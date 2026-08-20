/************************************************************************/
/*  jsb_buffer.h                                                        */
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
#include "jsb_bridge_pch.h"
namespace jsb {
struct Buffer {
private:
	uint8_t *ptr_ = nullptr;
	size_t size_ = 0;

	Buffer(uint8_t *p_ptr, size_t p_size) : ptr_(p_ptr), size_(p_size) {}

	//NOTE only free the memory, ptr_ itself won't be changed.
	_FORCE_INLINE_ void drop() {
		if (ptr_) impl::Helper::free(ptr_);
	}

public:
	static Buffer steal(uint8_t *p_ptr, size_t p_size) {
		return { p_ptr, p_size };
	}

	static Buffer copy(const uint8_t *p_ptr, size_t p_size) {
		uint8_t *ptr = (uint8_t *)memalloc(p_size);
		memcpy(ptr, p_ptr, p_size);
		return { ptr, p_size };
	}

	Buffer() = default;
	~Buffer() { drop(); }

	Buffer(const Buffer &p_other) = delete;
	Buffer &operator=(const Buffer &p_other) = delete;

	Buffer(Buffer &&p_other) noexcept {
		ptr_ = p_other.ptr_;
		size_ = p_other.size_;
		p_other.ptr_ = nullptr;
		p_other.size_ = 0;
	}
	Buffer &operator=(Buffer &&p_other) noexcept {
		drop();

		ptr_ = p_other.ptr_;
		size_ = p_other.size_;
		p_other.ptr_ = nullptr;
		p_other.size_ = 0;
		return *this;
	}

	_FORCE_INLINE_ bool is_empty() const { return size_ == 0; }

	_FORCE_INLINE_ size_t size() const { return size_; }
	_FORCE_INLINE_ const uint8_t *ptr() const { return ptr_; }
	_FORCE_INLINE_ uint8_t *ptr() { return ptr_; }
};

} //namespace jsb
