/************************************************************************/
/*  jsb_settings.h                                                      */
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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Runtime-facing settings facade: reads ProjectSettings keys owned by the
// runtime extension (godotjs_ext/runtime/** plus godotjs_ext/editor/script/
// inline_uid) and, in editor builds, a couple of EditorSettings values the
// runtime itself needs (debugger port).
//
// Editor-hosted settings (EditorSettings defaults + the editor/codegen/**
// project settings consumed only by the editor extension) live in
// src/editor/jsb_editor_settings.{h,cpp}.

#include "compat/jsb_compat.h"
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {
template <typename T>
class BitField;
}
namespace jsb::internal {

// Register runtime-owned project settings (godotjs_ext/runtime/** keys plus
// godotjs_ext/editor/script/inline_uid which the runtime consumes). ProjectSettings
// exists long before SERVERS-level extension init, so this is safe here.
// Idempotent; called once from the runtime extension's SERVERS initializer.
void init_project_settings();

class Settings {
public:
	static bool is_bridge_logging_enabled();
	static uint16_t get_debugger_port();
	static String get_debugger_source_map_base_url();
	static bool get_wait_for_debugger();
	static bool get_sourcemap_enabled();

	/**
	 * get the project relative path for `outDir` (it refers to `.godot/godotjs_ext` by default)
	 */
	static String get_jsb_out_dir_name();

	/**
	 * get path for .tsbuildinfo (.godot/.tsbuildinfo)
	 */
	static String get_tsbuildinfo_path();

	/**
	 * get the res path for `outDir`, it's equivalent to `res://` + get_jsb_out_dir_name()
	 */
	static String get_jsb_out_res_path();

	static String get_indentation();

	static String get_project_data_dir_name();

	static PackedStringArray get_additional_search_paths();

	static String get_entry_script_path();

	static bool get_camel_case_bindings_enabled();

	static bool is_script_inline_resource_uid();
};
} //namespace jsb::internal
