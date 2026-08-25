/************************************************************************/
/*  jsb_ansi_allocator.h                                                */
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

#include <common/compat/jsb_compat.h>
#include <godot_cpp/core/math_funcs_binary.hpp>

namespace jsb::internal {
struct AnsiAllocator {
	enum { kInitialElementNum = 8 };

	struct AnyType {
	};

	template <size_t kSizeOfElement>
	struct AnyTypeAllocator {
		AnyTypeAllocator() : data(nullptr), num(0) {
		}

		~AnyTypeAllocator() {
			if (data) {
				memfree(data);
			}
		}

		AnyTypeAllocator(AnyTypeAllocator &&other) noexcept {
			data = other.data;
			num = other.num;
			other.data = nullptr;
			other.num = 0;
		}

		AnyTypeAllocator &operator=(AnyTypeAllocator &&other) noexcept {
			data = other.data;
			num = other.num;
			other.data = nullptr;
			other.num = 0;
			return *this;
		}

		AnyTypeAllocator(const AnyTypeAllocator &other) = delete;
		AnyTypeAllocator &operator=(const AnyTypeAllocator &other) = delete;

		void resize(size_t p_last_num, size_t p_num) {
			data = (AnyType *)memrealloc(data, ::godot::Math::next_power_of_2((unsigned int)(p_num * kSizeOfElement)));
			jsb_check(data);
			const size_t added_count = p_num - p_last_num;
			memset((void *)((unsigned char *)data + p_last_num * kSizeOfElement), 0, added_count * kSizeOfElement);
			num = p_num;
		}

		AnyType *get_data() const {
			return data;
		}

		size_t capacity() const { return num; }

		AnyType *data;
		size_t num;
	};

	template <typename T>
	struct ForType : AnyTypeAllocator<sizeof(T)> {
		T *get_data() const {
			return (T *)AnyTypeAllocator<sizeof(T)>::get_data();
		}
	};
};
} //namespace jsb::internal
