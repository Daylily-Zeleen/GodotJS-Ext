/************************************************************************/
/*  jsb_runtime_settings.cpp                                            */
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

#include "jsb_runtime_settings.h"
#include "compat/project_settings.h"
#include "jsb_internal_pch.h"
#include "jsb_logger.h"
#include "jsb_macros.h"
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_uid.hpp>

// TODO: 摆脱 editor 依赖
#ifdef TOOLS_ENABLED
#	include <godot_cpp/classes/engine.hpp>
#	include "compat/editor_settings.h"
#	include "internal/jsb_settings.h"
#endif // TOOLS_ENABLED

#include <cstdint>

#define JSB_SET_RESTART(val) (val)
#define JSB_SET_IGNORE_DOCS(val) (val)
#define JSB_SET_BASIC(val) (val)
#define JSB_SET_INTERNAL(val) (val)

namespace jsb::internal::settings {

static constexpr char kRtBridgeLoggingEnabled[] = JSB_MODULE_NAME_STRING "/runtime/bridge_logging_enabled";
static constexpr char kRtDebuggerPort[] = JSB_MODULE_NAME_STRING "/runtime/debugger/debugger_port";
static constexpr char kRtDebuggerSourceMapBaseUrl[] = JSB_MODULE_NAME_STRING "/runtime/debugger/source_map_base_url";
static constexpr char kRtDebuggerWaitForDebugger[] = JSB_MODULE_NAME_STRING "/runtime/debugger/wait_for_debugger";
static constexpr char kRtAdditionalSearchPaths[] = JSB_MODULE_NAME_STRING "/runtime/core/additional_search_paths";
static constexpr char kRtEntryScriptPath[] = JSB_MODULE_NAME_STRING "/runtime/core/entry_script_path";
static constexpr char kScriptInlineResourceUID[] = JSB_MODULE_NAME_STRING "/editor/script/inline_uid";
static constexpr char kRtSourceMapEnabled[] = JSB_MODULE_NAME_STRING "/runtime/logger/source_map_enabled";

void init_runtime_settings() {
	// TODO: 考虑挪到 jsb_editor_setting 中，并移除 godot-jsb 模块 (BridgeModuleLoader) 中的依赖，让runtime不再需要
	_GLOBAL_DEF(kRtCamelCaseBindingsEnabled, false, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));

	_GLOBAL_DEF(kRtSourceMapEnabled, true, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	{
		PropertyInfo EntryScriptPath;
		EntryScriptPath.type = Variant::STRING;
		EntryScriptPath.name = kRtEntryScriptPath;
		EntryScriptPath.hint = PROPERTY_HINT_FILE;
		EntryScriptPath.hint_string = js_files_filter;
		_GLOBAL_DEF(EntryScriptPath, String(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	}

	_GLOBAL_DEF(kScriptInlineResourceUID, false, false);

	_GLOBAL_DEF(kRtBridgeLoggingEnabled, false, JSB_SET_RESTART(false));
	_GLOBAL_DEF(kRtDebuggerPort, 9229, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
	_GLOBAL_DEF(kRtDebuggerSourceMapBaseUrl, "http://localhost:9230", JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
	_GLOBAL_DEF(kRtDebuggerWaitForDebugger, false, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
	_GLOBAL_DEF(kRtAdditionalSearchPaths, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
}

namespace project {
bool is_bridge_logging_enabled() {
	return GLOBAL_GET(kRtBridgeLoggingEnabled);
}

uint16_t get_debugger_port() {
	// Try get port override from cmdline arguments.
	static uint16_t port_override = [] {
		const PackedStringArray &args = OS::get_singleton()->get_cmdline_args();
		for (int i = 0; i < args.size() - 1; i++) {
			if (args[i] == "--js-debugger-port") {
				const String &t = args[i + 1];
				if (!t.is_empty() && t.is_valid_int()) {
					const uint16_t port = t.to_int();
					if (port >= 0 && port <= UINT16_MAX) {
						jsb_notice(true, "debugger port overridden to %s", itos(port));
						return (uint16_t)port;
					} else {
						JSB_LOG(Error, "Invalid debugger port \"%s\", it is out of range.", port);
						break;
					}
				}
				break;
			}
		}
		return (uint16_t)0;
	}();
	if (port_override != 0) return port_override;

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		if (get_editor_settings().is_valid()) {
			return EDITOR_GET(kEditorDebuggerPort);
		}
	}
#endif
	return (uint16_t)GLOBAL_GET(kRtDebuggerPort);
}

String get_debugger_source_map_base_url() {
	return GLOBAL_GET(kRtDebuggerSourceMapBaseUrl);
}

bool is_wait_for_debugger() {
	return GLOBAL_GET(kRtDebuggerWaitForDebugger);
}

PackedStringArray get_additional_search_paths() {
	return GLOBAL_GET(kRtAdditionalSearchPaths);
}

String get_entry_script_path() {
	return ResourceUID::ensure_path(GLOBAL_GET(kRtEntryScriptPath));
}

bool is_script_inline_resource_uid() {
	return GLOBAL_GET(kScriptInlineResourceUID);
}

bool is_sourcemap_enabled() {
	return GLOBAL_GET(kRtSourceMapEnabled);
}
} //namespace project

} //namespace jsb::internal::settings