/************************************************************************/
/*  jsb_settings.cpp                                                    */
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

#include "jsb_settings.h"
#include "jsb_setting_keys.h"

#include "jsb_internal_pch.h"
#include "jsb_logger.h"
#include "jsb_macros.h"

#include <godot_cpp/classes/resource_uid.hpp>

#include "compat/project_settings.h"
#ifdef TOOLS_ENABLED
#	include <common/compat/editor_settings.h>
#	include <godot_cpp/classes/engine.hpp>
#endif // TOOLS_ENABLED

#define JSB_SET_RESTART(val) (val)
#define JSB_SET_IGNORE_DOCS(val) (val)
#define JSB_SET_BASIC(val) (val)
#define JSB_SET_INTERNAL(val) (val)

namespace jsb::internal {

// Register runtime-owned project settings (godotjs_ext/runtime/** keys plus
// godotjs_ext/editor/script/inline_uid which the runtime consumes). ProjectSettings
// exists long before SERVERS-level extension init, so this is safe here.
// Idempotent; called once from the runtime extension's SERVERS initializer.
void init_project_settings() {
	static bool inited = false;
	if (!inited) {
		inited = true;

		_GLOBAL_DEF(kRtDebuggerPort, 9229, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerSourceMapBaseUrl, "http://localhost:9230", JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerWaitForDebugger, false, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtSourceMapEnabled, true, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtAdditionalSearchPaths, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtCamelCaseBindingsEnabled, false, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));

		static constexpr char filter[] = "*." JSB_JAVASCRIPT_EXT ",*." JSB_COMMONJS_EXT ",*." JSB_MODULE_EXT
#if JSB_USE_TYPESCRIPT
										 ",*." JSB_TYPESCRIPT_EXT
#endif
				;
		{
			PropertyInfo EntryScriptPath;
			EntryScriptPath.type = Variant::STRING;
			EntryScriptPath.name = kRtEntryScriptPath;
			EntryScriptPath.hint = PROPERTY_HINT_FILE;
			EntryScriptPath.hint_string = filter;
			_GLOBAL_DEF(EntryScriptPath, String(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		_GLOBAL_DEF(kRtBridgeLoggingEnabled, false, false);
		_GLOBAL_DEF(kScriptInlineResourceUID, true, false);
	}
}

// File-filter fragment shared by the settings registrations that hint at script files.
static const char *jsb_settings_file_filter() {
	static constexpr char filter[] = "*." JSB_JAVASCRIPT_EXT ",*." JSB_COMMONJS_EXT ",*." JSB_MODULE_EXT
#if JSB_USE_TYPESCRIPT
									 ",*." JSB_TYPESCRIPT_EXT
#endif
			;
	return filter;
}

// use unnecessary first category layer (runtime and editor) to make the second layer shown as sections in project settings

#ifdef TOOLS_ENABLED
bool Settings::editor_settings_available() {
	return get_editor_settings().is_valid(); // || Engine::get_singleton()->is_editor_hint();
}

String Settings::get_autogen_path() {
	return EDITOR_GET(kEditorAutogenPath);
}

BitField<AutoGenSettingFlags> Settings::get_autogen_scene_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenSceneDTSSettings));
}

BitField<AutoGenSettingFlags> Settings::get_autogen_resource_dts_settings() {
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEditorAutogenResourceDTSSettings));
}

bool Settings::get_codegen_use_project_settings() {
	return EDITOR_GET(kEditorCodegenUseProjectSettings);
}
#endif // TOOLS_ENABLED

bool Settings::is_packaging_with_source_map() {
	return GLOBAL_GET(kEdPackagingWithSourceMap);
}

PackedStringArray Settings::get_packaging_include_files() {
	// rely on auto variant convert from Array
	return (PackedStringArray)GLOBAL_GET(kEdPackagingIncludeFiles);
}

PackedStringArray Settings::get_packaging_include_directories() {
	// rely on auto variant convert from Array
	return (PackedStringArray)GLOBAL_GET(kEdPackagingIncludeDirectories);
}

bool Settings::is_packaging_referenced_node_modules() {
	return GLOBAL_GET(kEdPackagingReferencedNodeModules);
}

bool Settings::is_bridge_logging_enabled() {
	return GLOBAL_GET(kRtBridgeLoggingEnabled);
}

PackedStringArray Settings::get_resource_dts_include_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdResourceDTSIncludePathWildcards);
}

PackedStringArray Settings::get_resource_dts_exclude_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdResourceDTSExcludePathWildcards);
}

PackedStringArray Settings::get_scene_dts_include_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdSceneDTSIncludePathWildcards);
}

PackedStringArray Settings::get_scene_dts_exclude_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdSceneDTSExcludePathWildcards);
}

BitField<SceneDTSGenerateStrategic> Settings::get_scene_dts_generate_strategic() {
	return BitField<SceneDTSGenerateStrategic>(GLOBAL_GET(kEdSceneDTSGenerateStrategic));
}

bool Settings::is_script_inline_resource_uid() {
	return GLOBAL_GET(kScriptInlineResourceUID);
}

uint16_t Settings::get_debugger_port() {
	static uint16_t debugger_port_override = [] {
		// Check for --js-debugger-port <port> command line argument.
		const PackedStringArray &cmdline_args = OS::get_singleton()->get_cmdline_args();
		for (int i = 0; i < cmdline_args.size() - 1; i++) {
			if (cmdline_args[i] == "--js-debugger-port") {
				const String port_text = cmdline_args[i + 1];
				if (!port_text.is_empty() && port_text.is_valid_int()) {
					uint16_t port = port_text.to_int();
					jsb_notice(port > 0, "Found \"--js-debugger-port\" argument, debugger will start on port %d", port);
					return port;
				}
				break;
			}
		}
		return (uint16_t)0;
	}();

	if (debugger_port_override != 0) return debugger_port_override;
#ifdef TOOLS_ENABLED
	if (editor_settings_available()) {
		return EDITOR_GET(kEditorDebuggerPort);
	} else {
		return 0; // 确保使用 0 无法启动调试功能
	}
#endif
	return GLOBAL_GET(kRtDebuggerPort);
}

String Settings::get_debugger_source_map_base_url() {
	return GLOBAL_GET(kRtDebuggerSourceMapBaseUrl);
}

bool Settings::get_wait_for_debugger() {
	return GLOBAL_GET(kRtDebuggerWaitForDebugger);
}

bool Settings::get_sourcemap_enabled() {
	return GLOBAL_GET(kRtSourceMapEnabled);
}

String Settings::get_project_data_dir_name() {
	bool use_hidden_directory = GLOBAL_GET("application/config/use_hidden_project_data_directory");
	return use_hidden_directory ? ".godot" : "godot";
}

String Settings::get_jsb_out_dir_name() {
	return get_project_data_dir_name().path_join(JSB_MODULE_NAME_STRING);
}

String Settings::get_tsbuildinfo_path() {
	return get_project_data_dir_name().path_join(".tsbuildinfo");
}

String Settings::get_jsb_out_res_path() {
	return "res://" + get_jsb_out_dir_name();
}

PackedStringArray Settings::get_additional_search_paths() {
	return GLOBAL_GET(kRtAdditionalSearchPaths);
}

String Settings::get_entry_script_path() {
	const String path = GLOBAL_GET(kRtEntryScriptPath);
	return ResourceUID::ensure_path(path);
}

bool Settings::get_camel_case_bindings_enabled() {
	return GLOBAL_GET(kRtCamelCaseBindingsEnabled);
}

void Settings::set_ignored_classes(const PackedStringArray &p_ignored_classes) {
	ProjectSettings::get_singleton()->set_setting(kEdIgnoredClasses, p_ignored_classes);
	ProjectSettings::get_singleton()->save();
}

PackedStringArray Settings::get_ignored_classes() {
	return GLOBAL_GET(kEdIgnoredClasses);
}

String Settings::get_indentation() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		// use_space_indentation
		if (!!EDITOR_GET("text_editor/behavior/indent/type")) {
			const int indent_size = EDITOR_GET("text_editor/behavior/indent/size");
			return String(" ").repeat(indent_size);
		}
	}
#endif
	return "\t";
}

} //namespace jsb::internal
