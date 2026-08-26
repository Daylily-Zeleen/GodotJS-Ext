/************************************************************************/
/*  jsb_editor_settings.cpp                                             */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_editor_settings.h"

#include "../compat/editor_settings.h"
#include "../internal/jsb_macros.h"
#include "../internal/jsb_setting_keys.h"
#include "../internal/jsb_settings.h"
#include "jsb_editor_keys.h"
#include <compat/project_settings.h>
#include <internal/jsb_logger.h>

#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#define JSB_SET_RESTART(val) (val)
#define JSB_SET_IGNORE_DOCS(val) (val)
#define JSB_SET_BASIC(val) (val)
#define JSB_SET_INTERNAL(val) (val)

#ifdef TOOLS_ENABLED

// File-filter fragment shared by the registrations that hint at script files.
static const char *editor_settings_file_filter() {
	static constexpr char filter[] = "*." JSB_JAVASCRIPT_EXT ",*." JSB_COMMONJS_EXT ",*." JSB_MODULE_EXT
#	if JSB_USE_TYPESCRIPT
									 ",*." JSB_TYPESCRIPT_EXT
#	endif
			;
	return filter;
}

void jsb::internal::init_editor_project_settings() {
	static bool inited = false;
	if (!inited) {
		inited = true;
		const char *filter = editor_settings_file_filter();

		// The editor owns ALL godotjs_ext project-setting registrations (it hosts
		// the settings UI). The runtime extension only reads values with local
		// fallbacks, so exported games (no editor) still get sane defaults.
		_GLOBAL_DEF(kRtBridgeLoggingEnabled, false, false);
		_GLOBAL_DEF(kRtDebuggerPort, 9229, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerSourceMapBaseUrl, "http://localhost:9230", JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerWaitForDebugger, false, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtSourceMapEnabled, true, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtAdditionalSearchPaths, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtCamelCaseBindingsEnabled, false, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		{
			PropertyInfo EntryScriptPath;
			EntryScriptPath.type = Variant::STRING;
			EntryScriptPath.name = kRtEntryScriptPath;
			EntryScriptPath.hint = PROPERTY_HINT_FILE;
			EntryScriptPath.hint_string = filter;
			_GLOBAL_DEF(EntryScriptPath, String(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}
		_GLOBAL_DEF(kScriptInlineResourceUID, true, false);

		_GLOBAL_DEF(kEdPackagingWithSourceMap, true, false);

		{
			PropertyInfo PackagingIncludeFiles;
			PackagingIncludeFiles.type = Variant::ARRAY;
			PackagingIncludeFiles.name = kEdPackagingIncludeFiles;
			PackagingIncludeFiles.hint = PROPERTY_HINT_ARRAY_TYPE;
			PackagingIncludeFiles.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_FILE, filter);
			_GLOBAL_DEF(PackagingIncludeFiles, Array(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo PackagingIncludeDirectories;
			PackagingIncludeDirectories.type = Variant::ARRAY;
			PackagingIncludeDirectories.name = kEdPackagingIncludeDirectories;
			PackagingIncludeDirectories.hint = PROPERTY_HINT_ARRAY_TYPE;
			PackagingIncludeDirectories.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_DIR, filter);
			_GLOBAL_DEF(PackagingIncludeDirectories, Array(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		_GLOBAL_DEF(kEdPackagingReferencedNodeModules, true, false);

		_GLOBAL_DEF(kEdIgnoredClasses, PackedStringArray(), false);
		{
			PropertyInfo ResourceDTSIncludePathWildcards;
			ResourceDTSIncludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			ResourceDTSIncludePathWildcards.name = kEdResourceDTSIncludePathWildcards;
			ResourceDTSIncludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			ResourceDTSIncludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(ResourceDTSIncludePathWildcards, PackedStringArray{ "res://" }, false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo ResourceDTSExcludePathWildcards;
			ResourceDTSExcludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			ResourceDTSExcludePathWildcards.name = kEdResourceDTSExcludePathWildcards;
			ResourceDTSExcludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			ResourceDTSExcludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(ResourceDTSExcludePathWildcards, PackedStringArray(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo SceneDTSIncludePathWildcards;
			SceneDTSIncludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			SceneDTSIncludePathWildcards.name = kEdSceneDTSIncludePathWildcards;
			SceneDTSIncludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			SceneDTSIncludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(SceneDTSIncludePathWildcards, PackedStringArray{ "res://" }, false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo SceneDTSExcludePathWildcards;
			SceneDTSExcludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			SceneDTSExcludePathWildcards.name = kEdSceneDTSExcludePathWildcards;
			SceneDTSExcludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			SceneDTSExcludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(SceneDTSExcludePathWildcards, PackedStringArray(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			using SceneDTSGenerateStrategicEnum = jsb::internal::SceneDTSGenerateStrategic;
			PropertyInfo SceneDTSGenerateStrategic;
			SceneDTSGenerateStrategic.type = Variant::INT;
			SceneDTSGenerateStrategic.name = kEdSceneDTSGenerateStrategic;
			SceneDTSGenerateStrategic.hint = PROPERTY_HINT_FLAGS;

			// NOTE: Keep this map sync with std::internal::SceneDTSGenerateStrategic
			const Pair<String, SceneDTSGenerateStrategicEnum> options[]{
				{ "Origin Name Node", SceneDTSGenerateStrategicEnum::ORIGIN_NAME_NODE },
				{ "Unique Name Node", SceneDTSGenerateStrategicEnum::UNIQUE_NAME_NODE }
			};

			PackedStringArray flag_hints;
			for (const auto &[name, value] : options) {
				flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
			}

			SceneDTSGenerateStrategic.hint_string = String(",").join(flag_hints);
			_GLOBAL_DEF(SceneDTSGenerateStrategic, int64_t(SceneDTSGenerateStrategicEnum::ORIGIN_NAME_NODE), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}
	}
}

void jsb::internal::init_editor_settings_defaults() {
	static bool inited = false;
	if (inited) {
		return;
	}
	// check before read to avoid redundant warnings
	if (Ref<EditorSettings> editor_settings = get_editor_settings(); editor_settings.is_valid()) {
		inited = true;
		_EDITOR_DEF(kEditorDebuggerPort, 9230, true);
		_EDITOR_DEF(kEditorAutogenPath, "gen/godot", false);
		{
			PropertyInfo AutogenSceneDTSSettings;
			AutogenSceneDTSSettings.type = Variant::INT;
			AutogenSceneDTSSettings.name = kEditorAutogenSceneDTSSettings;
			AutogenSceneDTSSettings.hint = PROPERTY_HINT_FLAGS;
			AutogenSceneDTSSettings.usage = PROPERTY_USAGE_NONE;

			// NOTE: Keep this map sync with jsb::internal::AutoGenSettingFlags
			const Pair<String, AutoGenSettingFlags> options[]{
				{ "Enabled", AutoGenSettingFlags::ENABLED },
				{ "Generate On Save", AutoGenSettingFlags::GEN_ON_SAVE },
				{ "Changed Files only", AutoGenSettingFlags::CHANGED_FILE_ONLY },
			};

			PackedStringArray flag_hints;
			for (const auto &[name, value] : options) {
				flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
			}

			AutogenSceneDTSSettings.hint_string = String(",").join(flag_hints);
			_EDITOR_DEF(kEditorAutogenSceneDTSSettings, int64_t(ENABLED | GEN_ON_SAVE | CHANGED_FILE_ONLY), false, JSB_SET_BASIC(true));
			editor_settings->add_property_info(AutogenSceneDTSSettings);
		}

		{
			PropertyInfo AutogenResourceDTSSettings;
			AutogenResourceDTSSettings.type = Variant::INT;
			AutogenResourceDTSSettings.name = kEditorAutogenResourceDTSSettings;
			AutogenResourceDTSSettings.hint = PROPERTY_HINT_FLAGS;
			AutogenResourceDTSSettings.usage = PROPERTY_USAGE_NONE;

			// NOTE: Keep this map sync with jsb::internal::AutoGenSettingFlags
			const Pair<String, AutoGenSettingFlags> options[]{
				{ "Enabled", AutoGenSettingFlags::ENABLED },
				{ "Generate On Save", AutoGenSettingFlags::GEN_ON_SAVE },
				{ "Changed Files only", AutoGenSettingFlags::CHANGED_FILE_ONLY },
			};

			PackedStringArray flag_hints;
			for (const auto &[name, value] : options) {
				flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
			}

			AutogenResourceDTSSettings.hint_string = String(",").join(flag_hints);
			_EDITOR_DEF(kEditorAutogenResourceDTSSettings, int64_t(ENABLED | GEN_ON_SAVE | CHANGED_FILE_ONLY), false, JSB_SET_BASIC(true));
			editor_settings->add_property_info(AutogenResourceDTSSettings);
		}
		_EDITOR_DEF(kEditorCodegenUseProjectSettings, true, false);
	}
}

// ---- accessors for the editor-hosted values (moved out of shared Settings) ----

namespace jsb::internal {

bool editor_settings_available() {
	return get_editor_settings().is_valid();
}

String get_autogen_path() {
	return EDITOR_GET(kEditorAutogenPath);
}

BitField<AutoGenSettingFlags> get_autogen_scene_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenSceneDTSSettings));
}

BitField<AutoGenSettingFlags> get_autogen_resource_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenResourceDTSSettings));
}

bool get_codegen_use_project_settings() {
	return EDITOR_GET(kEditorCodegenUseProjectSettings);
}

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

void set_ignored_classes(const PackedStringArray &p_ignored_classes) {
	ProjectSettings::get_singleton()->set_setting(kEdIgnoredClasses, p_ignored_classes);
	ProjectSettings::get_singleton()->save();
}

} //namespace jsb::internal

#endif // TOOLS_ENABLED
