/************************************************************************/
/*  jsb_bit_field.h                                                     */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include <godot_cpp/core/defs.hpp>

namespace templates {

template <typename T>
class BitField {
	using UnderlyingType = std::underlying_type<T>::type;
	UnderlyingType value = 0;

public:
	_FORCE_INLINE_ void set_flag(T p_flag) { value |= p_flag; }
	_FORCE_INLINE_ bool has_flag(T p_flag) const { return value & p_flag; }
	_FORCE_INLINE_ void clear_flag(T p_flag) { value &= ~p_flag; }
	_FORCE_INLINE_ BitField(T p_value) { value = p_value; }
	_FORCE_INLINE_ BitField() = default;
	_FORCE_INLINE_ operator UnderlyingType() const { return value; }
};

} //namespace templates
