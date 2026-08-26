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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include <compat/jsb_compat.h>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {
template <typename T>
class BitField;
}
namespace jsb::internal {

// Register runtime-owned project settings (godotjs_ext/runtime/** keys plus
// godotjs_ext/editor/script/inline_uid which the runtime consumes). Called once
// from the runtime extension's SERVERS-level initializer; idempotent.
void init_project_settings();
enum SceneDTSGenerateStrategic {
	ORIGIN_NAME_NODE = 1 << 0,
	UNIQUE_NAME_NODE = 1 << 1,
};

enum AutoGenSettingFlags {
	ENABLED = 1 << 0,
	GEN_ON_SAVE = 1 << 1,
	CHANGED_FILE_ONLY = 1 << 2,
};

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

	static bool is_packaging_with_source_map();

	static PackedStringArray get_packaging_include_files();

	static PackedStringArray get_packaging_include_directories();

	static bool is_packaging_referenced_node_modules();

	static void set_ignored_classes(const PackedStringArray &p_ignored_classes);
	static PackedStringArray get_ignored_classes();

	static PackedStringArray get_resource_dts_include_path_wildcards();
	static PackedStringArray get_resource_dts_exclude_path_wildcards();
	static PackedStringArray get_scene_dts_include_path_wildcards();
	static PackedStringArray get_scene_dts_exclude_path_wildcards();

	static BitField<SceneDTSGenerateStrategic> get_scene_dts_generate_strategic();

	static bool is_script_inline_resource_uid();

#ifdef TOOLS_ENABLED
	// [EDITOR ONLY]
	static bool editor_settings_available();
	static String get_autogen_path();

	static BitField<AutoGenSettingFlags> get_autogen_scene_dts_settings();
	static BitField<AutoGenSettingFlags> get_autogen_resource_dts_settings();

	static bool get_codegen_use_project_settings();
#endif
};
} //namespace jsb::internal
