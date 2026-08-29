/************************************************************************/
/*  jsb_editor_keys.h                                                   */
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

#include "jsb_macros.h"
#include <runtime/jsb.config.h>

// Setting key names owned by the EDITOR extension (registered and read
// exclusively on this side). Runtime-owned keys live in
// <internal/jsb_setting_keys.h>.

#define JSB_SETTING_KEY(name) JSB_MODULE_NAME_STRING name

// ---- editor-owned project settings ----
static constexpr char kEdPackagingWithSourceMap[] = JSB_SETTING_KEY("/editor/packaging/source_map_included");
static constexpr char kEdPackagingIncludeFiles[] = JSB_SETTING_KEY("/editor/packaging/include_files");
static constexpr char kEdPackagingIncludeDirectories[] = JSB_SETTING_KEY("/editor/packaging/include_directories");
static constexpr char kEdPackagingReferencedNodeModules[] = JSB_SETTING_KEY("/editor/packaging/referenced_node_modules");

// ignored classes 不生成对应的 .d.ts 声明代码；哪些类该生成是项目特定的，不应作为编辑器设置。
// 语义：ignored classes 的子类也会被忽略。
static constexpr char kEdIgnoredClasses[] = JSB_SETTING_KEY("/codegen/ignored_classes");

static constexpr char kEdResourceDTSIncludePathWildcards[] = JSB_SETTING_KEY("/codegen/resource_dts/include_path_wildcards");
static constexpr char kEdResourceDTSExcludePathWildcards[] = JSB_SETTING_KEY("/codegen/resource_dts/exclude_path_wildcards");
static constexpr char kEdSceneDTSIncludePathWildcards[] = JSB_SETTING_KEY("/codegen/scene_dts/include_path_wildcards");
static constexpr char kEdSceneDTSExcludePathWildcards[] = JSB_SETTING_KEY("/codegen/scene_dts/exclude_path_wildcards");
static constexpr char kEdSceneDTSGenerateStrategic[] = JSB_SETTING_KEY("/codegen/scene_dts/generate_strategic");

#ifdef TOOLS_ENABLED
// ---- EditorSettings defaults ----
static constexpr char kEditorAutogenPath[] = JSB_SETTING_KEY("/codegen/autogen_path");
static constexpr char kEditorAutogenSceneDTSSettings[] = JSB_SETTING_KEY("/codegen/autogen_scene_dts_settings");
static constexpr char kEditorAutogenResourceDTSSettings[] = JSB_SETTING_KEY("/codegen/autogen_resource_dts_settings");
static constexpr char kEditorCodegenUseProjectSettings[] = JSB_SETTING_KEY("/codegen/use_project_settings");
#endif // TOOLS_ENABLED
