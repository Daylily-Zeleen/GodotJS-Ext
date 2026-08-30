/************************************************************************/
/*  jsb_codegen_scene_descriptors.cpp                                   */
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

#include "jsb_codegen_scene_descriptors.h"

#include "../weaver-editor/jsb_api_tool_session.h"
#include "../weaver-editor/jsb_editor_bridge.h"
#include "jsb_editor_settings.h"
#include <api_tool/api_tool.h>
#include <internal/jsb_class_visibility.h>
#include <internal/jsb_logger.h>
#include <internal/jsb_naming_util.h>
#include <internal/jsb_path_util.h>
#include <internal/jsb_settings.h>
#include <godot_cpp/classes/script.hpp>

#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::codegen {

using SceneDTSGenerateStrategic = jsb::internal::settings::SceneDTSGenerateStrategic;

namespace {
enum class DescriptorType {
	Godot,
	User,
	FunctionLiteral,
	ObjectLiteral,
	StringLiteral,
	NumericLiteral,
	BooleanLiteral,
	Union,
	Intersection,
	Conditional,
	Tuple,
	Infer,
	Mapped,
	Indexed,
};

bool _request_codegen(const String &p_script_path, const Dictionary &p_request, Dictionary &p_result) {
	jsb::JsbBridgeTable bridge_copy;
	const jsb::JsbBridgeTable *bridge = jsb::editor::EditorBridge::get_bridge();
	if (bridge == nullptr || bridge->eval_with_arg == nullptr) {
		JSB_LOG(Warning, "Codegen failed: runtime bridge is not available.");
		return false;
	}
	bridge_copy = *bridge;

	// module_id: the .ts -> compiled .js mapping used by the runtime module cache
	const String module_id = jsb::internal::PathUtil::convert_typescript_path(p_script_path);

	// The request travels as the transient `__jsb_arg` global (engine objects
	// included); the user module's `codegen(request)` result comes back as a
	// Dictionary through the variant out-parameter -- no JSON involved.
	String source;
	source += "(function() {\n";
	source += "  const m = require(\"" + module_id + "\");\n";
	source += "  if (!m || typeof m.codegen !== \"function\") { return null; }\n";
	source += "  return m.codegen(__jsb_arg);\n";
	source += "})()\n";

	Variant result;
	{
		const Variant request_variant = p_request; // Variant wrapping the request dictionary
		const godot::Error err = bridge_copy.eval_with_arg(source.utf8().get_data(),
				source.utf8().length(),
				request_variant._native_ptr(),
				result._native_ptr());
		if (err != OK) {
			JSB_LOG(Error, "Codegen failed for script '%s' (error %d).", p_script_path, (int)err);
			return false;
		}
	}
	if (result.get_type() != Variant::DICTIONARY) {
		return false;
	}
	p_result = result;
	return true;
}

StringName _get_exposed_node_class_name(const StringName &class_name) {
	StringName exposed_class_name = class_name;

	while (!jsb::internal::ClassVisibility::is_original_class_exposed(exposed_class_name)) {
		exposed_class_name = ClassDB::get_parent_class(exposed_class_name);
		if (exposed_class_name.ends_with("Extension")) {
			exposed_class_name = ClassDB::get_parent_class(exposed_class_name);
		}
	}

	return jsb::internal::NamingUtil::get_class_name(exposed_class_name);
}

Dictionary _build_node_type_descriptor(const godot::BitField<SceneDTSGenerateStrategic> p_strategic, Node *p_node, const Node *p_root_node, Dictionary &r_unique_name_nodes) {
	jsb_check(p_strategic != 0);

	Dictionary descriptor;
	Dictionary children;
	int child_count = p_node->get_child_count(true);

	// If p_node is PackedScene, only editable instance's children should be collected.
	if (p_node == p_root_node || p_node->get_scene_file_path().is_empty() || p_root_node->is_editable_instance(p_node)) {
		for (int i = 0; i < child_count; i++) {
			Node *child = p_node->get_child(i, true);
			children[child->get_name()] = _build_node_type_descriptor(p_strategic, child, p_root_node, r_unique_name_nodes);
		}
	}

	Ref<Script> script = p_node->get_script();

	if (script.is_valid()) {
		Dictionary codegen_request;
		codegen_request["type"] = (int32_t)CodeGenType::ScriptNodeTypeDescriptor;
		codegen_request["node"] = p_node;
		codegen_request["children"] = children;

		if (!_request_codegen(script->get_path(), codegen_request, descriptor)) {
			descriptor.clear();
		}
	}

	if (descriptor.is_empty()) {
		// By default, only scene (and sub-scene) roots are typed with a user defined type. This ensures that classes are
		// able to use SceneNodes in their type declaration without illegally referencing their own type. Users can use
		// codegen to override this behavior.
		const jsb::JsbBridgeTable *bridge = jsb::editor::EditorBridge::get_bridge();
		bool generic_global_class = true;
		if (bridge != nullptr && bridge->is_global_class_generic != nullptr && script.is_valid()) {
			Variant result;
			const String spath = script->get_path();
			const CharString spath_utf8 = spath.utf8();
			const godot::Error qerr = bridge->is_global_class_generic(spath_utf8.get_data(), spath_utf8.length(), result._native_ptr());
			generic_global_class = (qerr == OK) ? (bool)result : true;
		}
		if (!script.is_valid()
				|| p_node->get_scene_file_path().is_empty()
				|| generic_global_class
				|| ((String)(Variant)script->call("get_global_name")).is_empty()) {
			Array generic_arguments;

			bool use_scene_nodes_interface = false;
			// Optionally replace children literal with SceneNodes["path/to/scene.tscn"]
			if (const String scene_file_path = p_node->get_scene_file_path();
					!scene_file_path.is_empty()) {
				PackedStringArray exclude_wildcards = jsb::internal::settings::project::get_scene_dts_exclude_path_wildcards();
				PackedStringArray include_wildcards = jsb::internal::settings::project::get_scene_dts_include_path_wildcards();
				if (is_path_matchn(include_wildcards, scene_file_path)
						&& !is_path_matchn(exclude_wildcards, scene_file_path)) {
					Dictionary scene_nodes;
					scene_nodes["type"] = (int32_t)DescriptorType::Godot;
					scene_nodes["name"] = "SceneNodes";

					Dictionary string_literal;
					string_literal["type"] = (int32_t)DescriptorType::StringLiteral;
					string_literal["value"] = scene_file_path.substr(6); // Remove leading res://

					Dictionary indexed_scene_nodes;
					indexed_scene_nodes["type"] = (int32_t)DescriptorType::Indexed;
					indexed_scene_nodes["base"] = scene_nodes;
					indexed_scene_nodes["index"] = string_literal;

					generic_arguments.push_back(indexed_scene_nodes);

					use_scene_nodes_interface = true;
				}
			}

			if (!use_scene_nodes_interface) {
				Dictionary object_literal;
				object_literal["type"] = (int32_t)DescriptorType::ObjectLiteral;
				object_literal["properties"] = children;
				generic_arguments.push_back(object_literal);
			}

			AnimationMixer *animation_mixer = Object::cast_to<AnimationMixer>(p_node);

			if (animation_mixer) {
				TypedArray<StringName> library_names = animation_mixer->get_animation_library_list();

				Dictionary animation_libraries_object_literal;
				Dictionary animation_libraries_properties;
				animation_libraries_object_literal["type"] = (int32_t)DescriptorType::ObjectLiteral;
				animation_libraries_object_literal["properties"] = animation_libraries_properties;

				for (const StringName &library_name : library_names) {
					Ref<AnimationLibrary> library = animation_mixer->get_animation_library(library_name);

					Array animation_names_union_array;

					TypedArray<StringName> animation_names = library->get_animation_list();

					for (const StringName &animation_name : animation_names) {
						Dictionary string_literal;
						string_literal["type"] = (int32_t)DescriptorType::StringLiteral;
						string_literal["value"] = animation_name;
						animation_names_union_array.push_back(string_literal);
					}

					Dictionary animation_names_union;
					animation_names_union["type"] = (int32_t)DescriptorType::Union;
					animation_names_union["types"] = animation_names_union_array;

					Array animation_generic_arguments;
					animation_generic_arguments.push_back(animation_names_union);

					Dictionary animation_library_descriptor;
					animation_library_descriptor["type"] = (int32_t)DescriptorType::Godot;
					animation_library_descriptor["name"] = jsb::internal::NamingUtil::get_class_name("AnimationLibrary");
					animation_library_descriptor["arguments"] = animation_generic_arguments;

					animation_libraries_properties[library_name] = animation_library_descriptor;
				}

				generic_arguments.push_back(animation_libraries_object_literal);
			}

			descriptor["type"] = (int32_t)DescriptorType::Godot;
			descriptor["name"] = _get_exposed_node_class_name(p_node->get_class());
			descriptor["arguments"] = generic_arguments;
		} else {
			descriptor["type"] = (int32_t)DescriptorType::User;
			descriptor["name"] = script->get_global_name();
			descriptor["resource"] = script->get_path();
		}
	}

	if (p_strategic.has_flag(SceneDTSGenerateStrategic::UNIQUE_NAME_NODE)) {
		if (p_node->is_unique_name_in_owner()) {
			r_unique_name_nodes["%" + p_node->get_name()] = descriptor;
		}
	}
	return descriptor;
}

void _log_load_error(const String &p_file, const String &p_type, godot::Error p_error) {
	if (p_error) {
		switch (p_error) {
			case ERR_CANT_OPEN: {
				JSB_LOG(Error, "Can't open file '%s'. The file could have been moved or deleted.", p_file.get_file());
				break;
			}
			case ERR_PARSE_ERROR: {
				JSB_LOG(Error, "Error while parsing file '%s'.", p_file.get_file());
				break;
			}
			case ERR_FILE_CORRUPT: {
				JSB_LOG(Error, "%s file '%s' appears to be invalid/corrupt.", p_type, p_file.get_file());
				break;
			}
			case ERR_FILE_NOT_FOUND: {
				JSB_LOG(Error, "Missing file '%s' or one of its dependencies.", p_file.get_file());
				break;
			}
			case ERR_FILE_UNRECOGNIZED: {
				JSB_LOG(Error, "File '%s' is saved in a format that is newer than the formats supported by this version of Godot, so it can't be opened.", p_file.get_file());
				break;
			}
			default: {
				JSB_LOG(Error, "Error while loading file '%s'.", p_file.get_file());
				break;
			}
		}
	}
}

} //namespace

Dictionary get_resource_type_descriptor(const String &p_path) {
	ERR_FAIL_COND_V_MSG(!api_tool::has_generated_data(), {}, "Please generate api tool data first.");

	Dictionary descriptor;
	Ref<Resource> resource = ResourceLoader::get_singleton()->load(p_path, "", ResourceLoader::CACHE_MODE_REUSE);

	if (resource.is_null()) {
		_log_load_error(p_path, "Resource", FileAccess::file_exists(p_path) ? ERR_FILE_UNRECOGNIZED : ERR_FILE_NOT_FOUND);
		return descriptor;
	}

	PackedScene *scene = Object::cast_to<PackedScene>(resource.ptr());

	if (scene) {
		Node *instantiated_scene = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
		if (!instantiated_scene) {
			JSB_LOG(Error, "Error instantiating scene from %s", p_path);
			return descriptor;
		}

		godot::BitField<SceneDTSGenerateStrategic> strategic = jsb::internal::settings::project::get_scene_dts_generate_strategic();
		if (strategic == 0) {
			strategic.set_flag(SceneDTSGenerateStrategic::ORIGIN_NAME_NODE);
			JSB_LOG(Warning, "Scene DTS generate strategic is undefine, use ORIGIN_NAME_NODE (please configure it through project setting).");
		}

		Array generic_arguments;
		Dictionary unique_name_nodes;
		generic_arguments.push_back(_build_node_type_descriptor(strategic, instantiated_scene, instantiated_scene, unique_name_nodes));

		descriptor["type"] = (int32_t)DescriptorType::Godot;
		descriptor["name"] = "PackedScene";
		descriptor["arguments"] = generic_arguments;

		return descriptor;
	}

	Ref<Script> script = resource->get_script();

	if (script.is_valid()) {
		Dictionary codegen_request;
		codegen_request["type"] = (int32_t)CodeGenType::ScriptResourceTypeDescriptor;
		codegen_request["resource"] = resource;

		if (_request_codegen(script->get_path(), codegen_request, descriptor)) {
			return descriptor;
		}
	}

	bool generic_global_class = true;
	{
		const jsb::JsbBridgeTable *bridge = jsb::editor::EditorBridge::get_bridge();
		if (bridge != nullptr && bridge->is_global_class_generic != nullptr && script.is_valid()) {
			Variant result;
			const String spath = script->get_path();
			const CharString spath_utf8 = spath.utf8();
			const godot::Error qerr = bridge->is_global_class_generic(spath_utf8.get_data(), spath_utf8.length(), result._native_ptr());
			generic_global_class = (qerr == OK) ? (bool)result : true;
		}
	}

	if (!script.is_valid() || generic_global_class) {
		descriptor["type"] = (int32_t)DescriptorType::Godot;
		descriptor["name"] = _get_exposed_node_class_name(resource->get_class());
	} else {
		String class_name = ((bool)script->call("can_instantiate"))
				? static_cast<String>(script->get_global_name())
				: resource->get_class(); // GDExtension: ResourceLoader::get_resource_script_class not available

		if (class_name.is_empty()) {
			descriptor["type"] = (int32_t)DescriptorType::Godot;
			descriptor["name"] = jsb::internal::NamingUtil::get_class_name("Object");
		} else {
			descriptor["type"] = (int32_t)DescriptorType::User;
			descriptor["name"] = script->get_global_name();
			descriptor["resource"] = script->get_path();
		}
	}

	return descriptor;
}

Dictionary get_scene_nodes(const String &p_path) {
	ERR_FAIL_COND_V_MSG(!api_tool::has_generated_data(), {}, "Please generate api tool data first.");

	Ref<PackedScene> scene_data = ResourceLoader::get_singleton()->load(p_path, "", ResourceLoader::CACHE_MODE_REPLACE);

	if (scene_data.is_null()) {
		_log_load_error(p_path, "Resource", FileAccess::file_exists(p_path) ? ERR_FILE_UNRECOGNIZED : ERR_FILE_NOT_FOUND);
		return Dictionary();
	}

	Node *instantiated_scene = scene_data->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);

	if (!instantiated_scene) {
		JSB_LOG(Error, "Error instantiating scene from %s", p_path);
		return Dictionary();
	}

	Dictionary nodes;
	Dictionary unique_name_nodes;
	int child_count = instantiated_scene->get_child_count(true);

	godot::BitField<SceneDTSGenerateStrategic> strategic = jsb::internal::settings::project::get_scene_dts_generate_strategic();
	if (strategic == 0) {
		strategic.set_flag(SceneDTSGenerateStrategic::ORIGIN_NAME_NODE);
		JSB_LOG(Warning, "Scene DTS generate strategic is undefine, use ORIGIN_NAME_NODE (please configure it through project setting).");
	}
	for (int i = 0; i < child_count; i++) {
		Node *child = instantiated_scene->get_child(i, true);
		nodes[child->get_name()] = _build_node_type_descriptor(strategic, child, instantiated_scene, unique_name_nodes);
	}

	instantiated_scene->queue_free();

	if (!strategic.has_flag(SceneDTSGenerateStrategic::ORIGIN_NAME_NODE)) {
		nodes.clear();
	}
	nodes.merge(unique_name_nodes);

	return nodes;
}

bool is_path_matchn(const PackedStringArray &p_wildcards, const String &p_path) {
	for (const String &wildcard : p_wildcards) {
		if ((wildcard.contains("*") || wildcard.contains("?")) && p_path.match(wildcard)) {
			return true;
		} else {
			const String &lower_case_path = p_path.to_lower();
			String lower_case_wildcard = wildcard.to_lower();
			if (lower_case_path == lower_case_wildcard) {
				return true; // Exact match file.
			} else {
				if (!lower_case_wildcard.ends_with("/")) {
					// Cheat as directory.
					lower_case_wildcard += "/";
				}

				if (lower_case_path.begins_with(lower_case_wildcard)) {
					return true; // Match directory.
				}
			}
		}
	}

	return false;
}

} //namespace jsb::codegen
