/************************************************************************/
/*  jsb_editor_settings.cpp                                             */
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

#include "jsb_editor_settings.h"

#include <compat/editor_settings.h>
#include <compat/project_settings.h>
#include <internal/jsb_macros.h>
#include <internal/jsb_settings.h>

#define JSB_SET_RESTART(val) (val)
#define JSB_SET_IGNORE_DOCS(val) (val)
#define JSB_SET_BASIC(val) (val)
#define JSB_SET_INTERNAL(val) (val)

namespace jsb::internal {
namespace settings {

static constexpr char kEdPackagingWithSourceMap[] = JSB_MODULE_NAME_STRING "/editor/packaging/source_map_included";
static constexpr char kEdPackagingIncludeFiles[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_files";
static constexpr char kEdPackagingIncludeDirectories[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_directories";
static constexpr char kEdPackagingReferencedNodeModules[] = JSB_MODULE_NAME_STRING "/editor/packaging/referenced_node_modules";

static constexpr char kEdIgnoredClasses[] = JSB_MODULE_NAME_STRING "/codegen/ignored_classes";

static constexpr char kEdResourceDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/include_path_wildcards";
static constexpr char kEdResourceDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/exclude_path_wildcards";
static constexpr char kEdSceneDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/include_path_wildcards";
static constexpr char kEdSceneDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/exclude_path_wildcards";
static constexpr char kEdSceneDTSGenerateStrategic[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/generate_strategic";

static constexpr char kEditorAutogenPath[] = JSB_MODULE_NAME_STRING "/codegen/autogen_path";
static constexpr char kEditorAutogenSceneDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_scene_dts_settings";
static constexpr char kEditorAutogenResourceDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_resource_dts_settings";
static constexpr char kEditorCodegenUseProjectSettings[] = JSB_MODULE_NAME_STRING "/codegen/use_project_settings";

void init_project_settings() {
	// Packing
	_GLOBAL_DEF(kEdPackagingWithSourceMap, true, JSB_SET_RESTART(false));
	{
		PropertyInfo pi;
		pi.type = Variant::ARRAY;
		pi.name = kEdPackagingIncludeFiles;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_FILE, js_files_filter);
		_GLOBAL_DEF(pi, Array(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	{
		PropertyInfo pi;
		pi.type = Variant::ARRAY;
		pi.name = kEdPackagingIncludeDirectories;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_DIR, js_files_filter);
		_GLOBAL_DEF(pi, Array(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	_GLOBAL_DEF(kEdPackagingReferencedNodeModules, true, JSB_SET_RESTART(false));

	// Ignored Classes
	_GLOBAL_DEF(kEdIgnoredClasses, PackedStringArray(), JSB_SET_RESTART(false));

	// Codegen
	{
		PropertyInfo pi;
		pi.type = Variant::PACKED_STRING_ARRAY;
		pi.name = kEdResourceDTSIncludePathWildcards;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
		_GLOBAL_DEF(pi, PackedStringArray{ "res://" }, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	{
		PropertyInfo pi;
		pi.type = Variant::PACKED_STRING_ARRAY;
		pi.name = kEdResourceDTSExcludePathWildcards;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
		_GLOBAL_DEF(pi, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	{
		PropertyInfo pi;
		pi.type = Variant::PACKED_STRING_ARRAY;
		pi.name = kEdSceneDTSIncludePathWildcards;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
		_GLOBAL_DEF(pi, PackedStringArray{ "res://" }, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	{
		PropertyInfo pi;
		pi.type = Variant::PACKED_STRING_ARRAY;
		pi.name = kEdSceneDTSExcludePathWildcards;
		pi.hint = PROPERTY_HINT_ARRAY_TYPE;
		pi.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
		_GLOBAL_DEF(pi, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
	{
		PropertyInfo pi;
		pi.type = Variant::INT;
		pi.name = kEdSceneDTSGenerateStrategic;
		pi.hint = PROPERTY_HINT_FLAGS;

		/** NOTE: Keep this map sync with std::internal::settings::SceneDTSGenerateStrategic */
		const Pair<String, SceneDTSGenerateStrategic> options[]{
			{ "Origin Name Node", SceneDTSGenerateStrategic::ORIGIN_NAME_NODE },
			{ "Unique Name Node", SceneDTSGenerateStrategic::UNIQUE_NAME_NODE },
		};

		PackedStringArray flag_hints;
		for (const auto &[name, value] : options) flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
		pi.hint_string = String(",").join(flag_hints);
		_GLOBAL_DEF(pi, int64_t(SceneDTSGenerateStrategic::UNIQUE_NAME_NODE), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}
}

void init_editor_settings() {
	if (Ref<EditorSettings> es = get_editor_settings(); es.is_valid()) {
		_EDITOR_DEF(kEditorDebuggerPort, 9230, JSB_SET_RESTART(true));
		_EDITOR_DEF(kEditorAutogenPath, "gen/godot", JSB_SET_RESTART(false));

		{
			const auto make_autogen_settings_flags_property_info = [](const String &p_prop_name) {
				PropertyInfo pi;
				pi.type = Variant::INT;
				pi.name = p_prop_name;
				pi.hint = PROPERTY_HINT_FLAGS;
				pi.usage = PROPERTY_USAGE_NONE;

				/** NOTE: Keep this map sync with jsb::internal::AutoGenSettingFlags */
				const Pair<String, AutoGenSettingFlags> options[]{
					{ "Enabled", AutoGenSettingFlags::ENABLED },
					{ "Generate On Save", AutoGenSettingFlags::GEN_ON_SAVE },
					{ "Changed Files only", AutoGenSettingFlags::CHANGED_FILE_ONLY },
				};

				PackedStringArray flag_hints;
				for (const auto &[name, value] : options) {
					flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
				}
				pi.hint_string = String(",").join(flag_hints);
				return pi;
			};

			const constexpr int64_t AUTO_GEN_SETTING_DEFAULT = int64_t(AutoGenSettingFlags::ENABLED | AutoGenSettingFlags::GEN_ON_SAVE | AutoGenSettingFlags::CHANGED_FILE_ONLY);

			_EDITOR_DEF(kEditorAutogenSceneDTSSettings, AUTO_GEN_SETTING_DEFAULT, JSB_SET_RESTART(false), JSB_SET_BASIC(true));
			es->add_property_info(make_autogen_settings_flags_property_info(kEditorAutogenSceneDTSSettings));

			_EDITOR_DEF(kEditorAutogenResourceDTSSettings, AUTO_GEN_SETTING_DEFAULT, JSB_SET_RESTART(false), JSB_SET_BASIC(true));
			es->add_property_info(make_autogen_settings_flags_property_info(kEditorAutogenResourceDTSSettings));
		}
		_EDITOR_DEF(kEditorCodegenUseProjectSettings, true, JSB_SET_RESTART(false));
	}
}

String get_tsbuildinfo_path() {
	return get_project_data_dir_name().path_join(".tsbuildinfo");
}

namespace project {
bool is_packaging_with_source_map() {
	return GLOBAL_GET(kEdPackagingWithSourceMap);
}

PackedStringArray get_packaging_include_files() {
	return (PackedStringArray)GLOBAL_GET(kEdPackagingIncludeFiles);
}

PackedStringArray get_packaging_include_directories() {
	return (PackedStringArray)GLOBAL_GET(kEdPackagingIncludeDirectories);
}

bool is_packaging_referenced_node_modules() {
	return GLOBAL_GET(kEdPackagingReferencedNodeModules);
}

PackedStringArray get_resource_dts_include_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdResourceDTSIncludePathWildcards);
}

PackedStringArray get_resource_dts_exclude_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdResourceDTSExcludePathWildcards);
}

PackedStringArray get_scene_dts_include_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdSceneDTSIncludePathWildcards);
}

PackedStringArray get_scene_dts_exclude_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdSceneDTSExcludePathWildcards);
}

BitField<SceneDTSGenerateStrategic> get_scene_dts_generate_strategic() {
	return BitField<SceneDTSGenerateStrategic>(GLOBAL_GET(kEdSceneDTSGenerateStrategic));
}

PackedStringArray get_ignored_classes() {
	return GLOBAL_GET(kEdIgnoredClasses);
}

void set_ignored_classes(const PackedStringArray &v) {
	ProjectSettings::get_singleton()->set_setting(kEdIgnoredClasses, v);
	ProjectSettings::get_singleton()->save();
}

} // namespace project

namespace editor {
String get_autogen_path() {
	return EDITOR_GET(kEditorAutogenPath);
}

BitField<AutoGenSettingFlags> get_autogen_scene_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenSceneDTSSettings));
}

BitField<AutoGenSettingFlags> get_autogen_resource_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenResourceDTSSettings));
}

bool is_codegen_use_project_settings() {
	return EDITOR_GET(kEditorCodegenUseProjectSettings);
}
} // namespace editor

} // namespace settings
} // namespace jsb::internal