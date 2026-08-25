/************************************************************************/
/*  jsb_format.h                                                        */
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

#include "compat/jsb_compat.h"
#include "jsb_sindex.h"

#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal {
template <typename T>
struct TFormat {
	static Variant from(const T &p_item) { return p_item; }
};
template <>
struct TFormat<IndexSafe64> {
	static Variant from(const IndexSafe64 &p_item) { return *p_item; }
};
template <>
struct TFormat<Index64> {
	static Variant from(const Index64 &p_item) { return *p_item; }
};
template <>
struct TFormat<Index32> {
	static Variant from(const Index32 &p_item) { return *p_item; }
};
template <>
struct TFormat<uintptr_t> {
	static Variant from(const uintptr_t &p_item) { return Variant((uint64_t)p_item); }
};
template <typename T>
static Variant convert(const T &p_item) { return TFormat<T>::from(p_item); }

template <typename... VarArgs>
String format(const String &p_text, const VarArgs... p_args) {
	return vformat(p_text, convert(p_args)...);
}
} //namespace jsb::internal
