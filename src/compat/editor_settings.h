/************************************************************************/
/*  editor_settings.h                                                   */
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

#ifndef TOOLS_ENABLED
#	error "CAN NOT COMPILE WITHOUT TOOLS_ENABLED, PLEASE CHECK IT'S NOT UNEXPECTEDLY INCLUDED."
#endif

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>

namespace godot {
Ref<EditorSettings> get_editor_settings();

#ifndef EDSCALE
#	define EDSCALE EditorInterface::get_singleton()->get_editor_scale()
#endif

Variant EDITOR_GET(const String &p_setting, const Variant &p_default = {});
Variant _EDITOR_DEF(const String &p_setting, const Variant &p_default, bool p_restart_if_changed = false, bool p_basic = false);
} //namespace godot