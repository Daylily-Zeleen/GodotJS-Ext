/************************************************************************/
/*  jsb_codegen_scene_descriptors.h                                     */
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

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {
class Node;
}
namespace jsb::codegen {

enum class CodeGenType {
	ScriptNodeTypeDescriptor,
	ScriptResourceTypeDescriptor,
};

// Builds the type descriptor dictionary for a scene/resource path by loading
// the user module and calling its `codegen(request)` export through the
// runtime bridge. Previously lived in GodotJSEditorHelper.
godot::Dictionary get_resource_type_descriptor(const godot::String &p_path);
godot::Dictionary get_scene_nodes(const godot::String &p_path);

bool is_path_matchn(const godot::PackedStringArray &p_wildcards, const godot::String &p_path);

} //namespace jsb::codegen
