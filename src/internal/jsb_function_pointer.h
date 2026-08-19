/************************************************************************/
/*  jsb_function_pointer.h                                              */
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

#include "../compat/jsb_compat.h"
#include <cinttypes>

namespace jsb::internal {
struct CFunctionPointers {
	CFunctionPointers() : cursor_(0) { pointer_.resize(16 * 512); }

	template <typename Func>
	uint32_t add(Func func) {
		uint32_t last_cursor = cursor_;
		if (pointer_.size() - last_cursor <= sizeof(func)) {
			pointer_.resize(pointer_.size() * 2);
		}
		memcpy(pointer_.ptrw() + last_cursor, &func, sizeof(func));
		cursor_ += sizeof(func);
		return last_cursor;
	}

	uint8_t *operator[](uint32_t p_index) { return pointer_.ptrw() + p_index; }

	uint32_t cursor_;
	Vector<uint8_t> pointer_;
};
} //namespace jsb::internal
