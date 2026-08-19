/************************************************************************/
/*  jsb_editor_helper.h                                                 */
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

#pragma once
#include "jsb_editor_pch.h"

#include <compat/editor_settings.h>

namespace godot {
class Node;
}

class GodotJSEditorHelper : public Object {
	GDCLASS(GodotJSEditorHelper, Object);

private:
	using SceneDTSGenerateStrategic = jsb::internal::SceneDTSGenerateStrategic;

	static bool _request_codegen(jsb::JSEnvironment &p_env, GodotJSScript *p_script, const Dictionary &p_request, Dictionary &p_result);
	static StringName _get_exposed_node_class_name(const StringName &class_name);
	static Dictionary _build_node_type_descriptor(const BitField<SceneDTSGenerateStrategic> p_strategic, jsb::JSEnvironment &p_env, Node *p_node, const godot::Node *p_root_node, Dictionary &r_unique_name_nodes);
	static void _log_load_error(const String &p_file, const String &p_type, Error p_error);

protected:
	static void _bind_methods();

public:
	virtual ~GodotJSEditorHelper() override = default;

	static Dictionary get_resource_type_descriptor(const String &p_path);
	static Dictionary get_scene_nodes(const String &p_path);
	static void show_toast(const String &p_text, int p_severity);

	static bool has_api_tool_data();
	static void generate_api_tool_data();

	static bool is_path_matchn(const PackedStringArray &p_wildcards, const String &p_path);
};

