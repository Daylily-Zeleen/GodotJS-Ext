/************************************************************************/
/*  api_tool_store.h                                                    */
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

// core/api_tool_store.h
// Binary file reading layer (runtime, core/).
// Deserializes API data structures from disk files.
// Only ApiStoreReader lives here; ApiStoreWriter is in editor/api_tool_store_writer.h.
// File format: [4B magic][4B version][4B flags][payload]

#include "../api_tool_types.h"
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace api_tool::internal {

class ApiStoreReader {
public:
	static godot::Error read_header(const godot::String &p_path, ApiHeader &r_data);
	static godot::Error read_utility_functions(const godot::String &p_path, godot::LocalVector<ApiUtilityFunction> &r_data);
	static godot::Error read_builtin_class(const godot::String &p_path, ApiBuiltinClass &r_data);
	static godot::Error read_class(const godot::String &p_path, ApiClass &r_data);
	static godot::Error read_global_enum(const godot::String &p_path, ApiEnumInfo &r_data);
	static godot::Error read_global_constant(const godot::String &p_path, ApiConstantInfo &r_data);
	static godot::Error read_singletons(const godot::String &p_path, godot::LocalVector<ApiSingleton> &r_data);
	static godot::Error read_native_structures(const godot::String &p_path, godot::LocalVector<ApiNativeStructure> &r_data);

	static godot::Error read_compatibility_hashes(const godot::String &p_path, ApiCompatibilityHashData &r_data);
};

} //namespace api_tool::internal