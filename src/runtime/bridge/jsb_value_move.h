/************************************************************************/
/*  jsb_value_move.h                                                    */
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
class Environment;

struct JSValueMove {
	friend class Environment;

private:
	std::shared_ptr<Environment> env_;
	v8::Global<v8::Value> value_;

	JSValueMove() = default;
	JSValueMove(const std::shared_ptr<Environment> &p_env, const v8::Local<v8::Value> &p_value);

public:
	// disable copy to avoid unpredictable behaviours (for now)
	JSValueMove(const JSValueMove &p_other) = delete;
	JSValueMove &operator=(const JSValueMove &p_other) = delete;

	JSValueMove(JSValueMove &&p_other) noexcept = default;
	JSValueMove &operator=(JSValueMove &&p_other) noexcept = default;

	_FORCE_INLINE_ void ignore() const {}
	bool is_valid() const;
	String to_string() const;
	Variant to_variant() const;
	// Vector<String> to_strings() const;
};
} //namespace jsb
