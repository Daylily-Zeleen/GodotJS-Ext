/************************************************************************/
/*  jsb_inline_allocator.h                                              */
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

#include "jsb_format.h"
#include "jsb_macros.h"

namespace jsb::internal {
template <size_t ElementNum>
struct InlineAllocator {
	enum { kInitialElementNum = ElementNum };

	template <size_t MemorySize>
	struct ByteCompat {
		uint8_t data[MemorySize];
	};

	template <typename TElementType>
	struct ForType {
		enum { kByteSize = ElementNum * sizeof(TElementType) };

		ForType() = default;
		~ForType() = default;

		ForType(ForType &&other) noexcept {
			memcpy(compat.data, other.compat.data, kByteSize);
			num = other.num;
		}

		ForType &operator=(ForType &&other) noexcept {
			memcpy(compat.data, other.compat.data, kByteSize);
			num = other.num;
			return *this;
		}

		ForType(const ForType &other) = delete;
		ForType &operator=(const ForType &other) = delete;

		void resize(size_t p_last_num, size_t p_num) {
			jsb_checkf(p_num <= ElementNum, "can't allocate elements more than inline allocator allowed");
			if (p_num > p_last_num) {
				const size_t added_count = p_num - p_last_num;
				memset((void *)(get_data() + p_last_num), 0, added_count * sizeof(TElementType));
				num = p_num;
			}
		}

		TElementType *get_data() const {
			return (TElementType *)(void *)compat.data;
		}

		constexpr size_t capacity() const { return num; }

		ByteCompat<kByteSize> compat;
		size_t num = 0;
	};
};
} //namespace jsb::internal

