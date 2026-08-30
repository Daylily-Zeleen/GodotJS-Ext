/************************************************************************/
/*  jsb_runtime_settings.h                                              */
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

// Runtime-only settings keys and accessors.
// Keys are defined as static in jsb_runtime_settings.cpp.

#include "compat/jsb_compat.h"
#include <cstdint>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal::settings {

/// Register runtime-owned project settings.
/// Called from register_types.cpp at MODULE_INITIALIZATION_LEVEL_SERVERS.
void init_runtime_settings();

namespace project {
bool is_bridge_logging_enabled();
uint16_t get_debugger_port();
String get_debugger_source_map_base_url();
bool is_wait_for_debugger();
PackedStringArray get_additional_search_paths();
String get_entry_script_path();
bool is_script_inline_resource_uid();
bool is_sourcemap_enabled();
} //namespace project

} //namespace jsb::internal::settings