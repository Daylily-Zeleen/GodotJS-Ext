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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include <runtime/jsb.config.h>
#include "jsb_macros.h"

// All project/editor setting key names in one place (pure constexpr, safe to
// compile into both extensions). Registration ownership:
//   - godotjs_ext/runtime/** keys        -> runtime extension
//   - godotjs_ext/editor/** keys         -> editor extension
//   - godotjs_ext/codegen/** keys        -> editor extension (runtime only reads)
//   - EditorSettings defaults            -> editor extension

#define JSB_SETTING_KEY(category, name) JSB_MODULE_NAME_STRING "/" category "/" name

// ---- runtime-owned project settings ----
static constexpr char kRtBridgeLoggingEnabled[] = JSB_MODULE_NAME_STRING "/runtime/bridge_logging_enabled";
static constexpr char kRtDebuggerPort[] = JSB_MODULE_NAME_STRING "/runtime/debugger/debugger_port";
static constexpr char kRtDebuggerSourceMapBaseUrl[] = JSB_MODULE_NAME_STRING "/runtime/debugger/source_map_base_url";
static constexpr char kRtDebuggerWaitForDebugger[] = JSB_MODULE_NAME_STRING "/runtime/debugger/wait_for_debugger";
static constexpr char kRtSourceMapEnabled[] = JSB_MODULE_NAME_STRING "/runtime/logger/source_map_enabled";
static constexpr char kRtAdditionalSearchPaths[] = JSB_MODULE_NAME_STRING "/runtime/core/additional_search_paths";
static constexpr char kRtEntryScriptPath[] = JSB_MODULE_NAME_STRING "/runtime/core/entry_script_path";
static constexpr char kRtCamelCaseBindingsEnabled[] = JSB_MODULE_NAME_STRING "/runtime/core/camel_case_bindings_enabled";

// editor-specific behavior, but configured project-wise instead of global-wise;
// consumed by the runtime (jsb_resource_loader) so registered by the runtime side
static constexpr char kScriptInlineResourceUID[] = JSB_MODULE_NAME_STRING "/editor/script/inline_uid";

// ---- editor-owned project settings (registered by the editor extension;
// the runtime reads them with fallbacks, so the names must exist everywhere) ----
static constexpr char kEdPackagingWithSourceMap[] = JSB_MODULE_NAME_STRING "/editor/packaging/source_map_included";
static constexpr char kEdPackagingIncludeFiles[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_files";
static constexpr char kEdPackagingIncludeDirectories[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_directories";
static constexpr char kEdPackagingReferencedNodeModules[] = JSB_MODULE_NAME_STRING "/editor/packaging/referenced_node_modules";

// ignored classes 不生成对应的 .d.ts 声明代码；哪些类该生成是项目特定的，不应作为编辑器设置。
// 语义：ignored classes 的子类也会被忽略。键归 editor 注册，runtime 只读。
static constexpr char kEdIgnoredClasses[] = JSB_MODULE_NAME_STRING "/codegen/ignored_classes";

static constexpr char kEdResourceDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/include_path_wildcards";
static constexpr char kEdResourceDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/exclude_path_wildcards";
static constexpr char kEdSceneDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/include_path_wildcards";
static constexpr char kEdSceneDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/exclude_path_wildcards";
static constexpr char kEdSceneDTSGenerateStrategic[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/generate_strategic";

#ifdef TOOLS_ENABLED
// ---- EditorSettings defaults (editor extension registers these) ----
static constexpr char kEditorDebuggerPort[] = JSB_MODULE_NAME_STRING "/debugger/editor_port";
static constexpr char kEditorAutogenPath[] = JSB_MODULE_NAME_STRING "/codegen/autogen_path";
static constexpr char kEditorAutogenSceneDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_scene_dts_settings";
static constexpr char kEditorAutogenResourceDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_resource_dts_settings";
static constexpr char kEditorCodegenUseProjectSettings[] = JSB_MODULE_NAME_STRING "/codegen/use_project_settings";
#endif // TOOLS_ENABLED
