/************************************************************************/
/*  api_tool_store_writer.h                                             */
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

// editor/api_tool_store_writer.h
// Binary file writing layer (editor-only, TOOLS_ENABLED).
// Serializes API data structures to disk files.

#include "../api_tool_types.h"
#include "api_tool_doc_types.h"
#include <godot_cpp/variant/string.hpp>

namespace api_tool::internal {

class ApiStoreWriter {
public:
	static godot::Error write_header(const godot::String &p_path, const ApiHeader &p_data);
	static godot::Error write_utility_functions(const godot::String &p_path, const godot::LocalVector<ApiUtilityFunction> &p_data);
	static godot::Error write_builtin_class(const godot::String &p_path, const ApiBuiltinClass &p_data);
	static godot::Error write_class(const godot::String &p_path, const ApiClass &p_data);
	static godot::Error write_global_enum(const godot::String &p_path, const ApiEnumInfo &p_data);
	static godot::Error write_global_constant(const godot::String &p_path, const ApiConstantInfo &p_data);
	static godot::Error write_singletons(const godot::String &p_path, const godot::LocalVector<ApiSingleton> &p_data);
	static godot::Error write_native_structures(const godot::String &p_path, const godot::LocalVector<ApiNativeStructure> &p_data);

	static godot::Error write_compatibility_hashes(const godot::String &p_path, const ApiCompatibilityHashData &p_data);

	static godot::Error write_document(const godot::String &p_path, const ApiClassDocument &p_data);
	static godot::Error write_utility_function_document(const godot::String &p_path, const ApiUtilityFunctionDocument &p_data);
	static godot::Error write_global_enum_document(const godot::String &p_path, const ApiGlobalEnumDocument &p_data);
	static godot::Error write_global_constant_document(const godot::String &p_path, const ApiGlobalConstantDocument &p_data);
};

} //namespace api_tool::internal
