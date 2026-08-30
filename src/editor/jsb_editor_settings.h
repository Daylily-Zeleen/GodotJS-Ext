/************************************************************************/
/*  jsb_editor_settings.h                                               */
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

// Editor-side settings (editor extension only).
// Ownership split:
//   - the runtime extension registers godotjs_ext/runtime/** project settings
//     and reads them through <internal/jsb_settings.h>
//   - the editor extension registers godotjs_ext/editor/** and
//     godotjs_ext/codegen/** project settings plus EditorSettings defaults,
//     and reads them through the accessors below

#include <compat/jsb_compat.h>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace jsb::internal::settings {

enum SceneDTSGenerateStrategic {
	ORIGIN_NAME_NODE = 1 << 0,
	UNIQUE_NAME_NODE = 1 << 1,
};

enum AutoGenSettingFlags {
	ENABLED = 1 << 0,
	GEN_ON_SAVE = 1 << 1,
	CHANGED_FILE_ONLY = 1 << 2,
};

void init_project_settings();
void init_editor_settings();

String get_tsbuildinfo_path();

namespace project {
bool is_packaging_with_source_map();
PackedStringArray get_packaging_include_files();
PackedStringArray get_packaging_include_directories();
bool is_packaging_referenced_node_modules();

PackedStringArray get_resource_dts_include_path_wildcards();
PackedStringArray get_resource_dts_exclude_path_wildcards();
PackedStringArray get_scene_dts_include_path_wildcards();
PackedStringArray get_scene_dts_exclude_path_wildcards();
BitField<SceneDTSGenerateStrategic> get_scene_dts_generate_strategic();

PackedStringArray get_ignored_classes();
void set_ignored_classes(const PackedStringArray &p_ignored_classes);
} //namespace project

namespace editor {
String get_autogen_path();
BitField<AutoGenSettingFlags> get_autogen_scene_dts_settings();
BitField<AutoGenSettingFlags> get_autogen_resource_dts_settings();
bool is_codegen_use_project_settings();
} //namespace editor

} //namespace jsb::internal::settings