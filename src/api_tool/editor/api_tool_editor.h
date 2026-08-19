/************************************************************************/
/*  api_tool_editor.h                                                   */
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

#include "api_tool_doc_types.h"

#include <memory>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace api_tool {

// ============================================================================
// Document queries (no cache, direct file read, TOOLS_ENABLED only)
// Returns std::unique_ptr<T> (caller owns). Returns nullptr if file missing/corrupted.
// Unified query for both Class and BuiltInClass.
// ============================================================================
std::unique_ptr<ApiClassDocument> find_document(const godot::StringName &p_name);
std::unique_ptr<ApiClassDocument> find_document(const godot::Variant::Type &p_type);
std::unique_ptr<ApiUtilityFunctionDocument> find_utility_function_document(const godot::StringName &p_name);
std::unique_ptr<ApiGlobalEnumDocument> find_global_enum_document(const godot::StringName &p_name);
std::unique_ptr<ApiGlobalConstantDocument> find_global_constant_document(const godot::StringName &p_name);

// ============================================================================
// Editor-only: API generation (only TOOLS_ENABLED)
// Cache is invalidated internally at the start of generate().
// ============================================================================

void full_generate_and_reboot();

// Generate API data by launching Godot subprocess with --dump-extension-api-with-docs,
// then parsing the generated JSON and writing binary files.
// Storage path follows project setting: application/config/use_hidden_project_data_directory
//   true  -> res://.godot/.api_dumping/
//   false -> res://godot/.api_dumping/
// Cache is invalidated at the start.
// Returns godot::OK on success, error code on failure.
godot::Error generate_api_tool_data(const godot::String &p_extension_api_json_path);

/**
 * @brief 返回 res://.godot/.api_dumping/ (或 res://godot/.api_dumping/) 下除了 documents 文件夹外的所有文件
 * 实现细节:
 * 1. ".api_dumping" 文件夹的路径要根据 ApiLoader 的 get_api_dumping_dir() 进行获取
 * 2. 不需要通过 ApiLoader 实际加载对应数据来查看classes中的类的 APIType，直接通过 godot::ClassDB 即可查得，注意如果查出来是 API_NONE 则说明不存在，跳过它即可
 *
 * @param p_exclude_editor_types 是否剔除 APIType 为 API_EDITOR 与 API_EDITOR_EXTENSION 的类
 * @param p_extension_types_only 是否只返回 APIType 为 API_EXTENSION 或 API_EDITOR_EXTENSION 的类（为true时只可能返回 classes 文件夹里的部分文件）
 * @return godot::Vector<godot::String> 基于项目路径(“res://")的文件路径列表
 */
godot::Vector<godot::String> get_api_data_files(bool p_exclude_editor_types = true, bool p_extension_types_only = false);

} //namespace api_tool