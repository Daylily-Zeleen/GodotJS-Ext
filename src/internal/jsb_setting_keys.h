/************************************************************************/
/*  jsb_setting_keys.h                                                  */
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

#include "jsb_macros.h"
#include <runtime/jsb.config.h>

// Setting key names owned by the RUNTIME extension (pure constexpr, safe to
// include anywhere; unused constants emit nothing).
//
// Keys registered/read exclusively by the editor extension live in
// src/editor/jsb_editor_keys.h.

#define JSB_SETTING_KEY(name) JSB_MODULE_NAME_STRING name

// ---- runtime-owned project settings (registered + read by the runtime) ----
static constexpr char kRtBridgeLoggingEnabled[] = JSB_SETTING_KEY("/runtime/bridge_logging_enabled");
static constexpr char kRtDebuggerPort[] = JSB_SETTING_KEY("/runtime/debugger/debugger_port");
static constexpr char kRtDebuggerSourceMapBaseUrl[] = JSB_SETTING_KEY("/runtime/debugger/source_map_base_url");
static constexpr char kRtDebuggerWaitForDebugger[] = JSB_SETTING_KEY("/runtime/debugger/wait_for_debugger");
static constexpr char kRtSourceMapEnabled[] = JSB_SETTING_KEY("/runtime/logger/source_map_enabled");
static constexpr char kRtAdditionalSearchPaths[] = JSB_SETTING_KEY("/runtime/core/additional_search_paths");
static constexpr char kRtEntryScriptPath[] = JSB_SETTING_KEY("/runtime/core/entry_script_path");
static constexpr char kRtCamelCaseBindingsEnabled[] = JSB_SETTING_KEY("/runtime/core/camel_case_bindings_enabled");

// editor-specific behavior, but configured project-wise instead of global-wise;
// consumed by the runtime (jsb_resource_loader) so registered by the runtime side
static constexpr char kScriptInlineResourceUID[] = JSB_SETTING_KEY("/editor/script/inline_uid");

#ifdef TOOLS_ENABLED
// ---- EditorSettings value the runtime itself reads (debugger port override) ----
// Registered by the editor extension; declared here because the shared runtime
// Settings::get_debugger_port() reads it in editor builds.
static constexpr char kEditorDebuggerPort[] = JSB_SETTING_KEY("/debugger/editor_port");
#endif // TOOLS_ENABLED
