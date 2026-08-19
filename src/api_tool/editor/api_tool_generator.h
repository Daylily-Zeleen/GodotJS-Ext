/************************************************************************/
/*  api_tool_generator.h                                                */
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

// editor/api_tool_generator.h
// Subprocess generation (TOOLS_ENABLED only).
// Detects project path and Godot executable, launches subprocess with
// --headless --path {project} --dump-extension-api-with-docs.
// Handles backup/restore of existing extension_api.json.

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/string.hpp>

namespace api_tool::internal {

class ApiGenerator {
public:
	// Generate extension_api.json by running Godot as a subprocess.
	// Detects project path via ProjectSettings and godot exe via OS.
	// Returns godot::OK on success.
	static godot::Error generate_api_json(const godot::String &p_project_dir, godot::String &r_api_file_path);

	// Run the full pipeline: generate JSON -> parse -> write binary files.
	// p_output_dir: output directory for binary files.
	static godot::Error generate(const godot::String &p_extension_api_json_path, const godot::String &p_output_dir);
};

} //namespace api_tool::internal
