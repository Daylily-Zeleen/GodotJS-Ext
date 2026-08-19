/************************************************************************/
/*  jsb_process.h                                                       */
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

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal {
class Process {
public:
	virtual ~Process();
	static std::shared_ptr<Process> create(const String &p_name, const String &p_path, const Vector<String> &p_arguments);

	void stop();
	bool is_running() const;

protected:
	Process() = default;
	void start(const String &p_name, const String &p_path, const Vector<String> &p_arguments);

	virtual Error on_start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) = 0;
	virtual void on_stop() = 0;
	virtual bool _is_running() const = 0;
};
} //namespace jsb::internal
