#pragma once

// editor/api_tool_parser.h
// JSON parsing + file splitting (only TOOLS_ENABLED).
// Reads extension_api.json -> parses into types -> writes binary files.
// Parser methods take const godot::Dictionary& (no void*).

#include "../api_tool_types.h"
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace api_tool::internal {

class ApiParser {
public:
	// Parse extension_api.json and generate binary files to output directory.
	// p_json_root: parsed JSON root dictionary (const ref, no void*)
	// p_output_dir: output directory
	// All internal functions return godot::Error.
	static godot::Error generate(const godot::Dictionary &p_json_root, const godot::String &p_output_dir);

private:
	// Clean and create output directory structure
	static godot::Error prepare_output_dirs(const godot::String &p_output_dir);

	// Section parsers: Dictionary -> binary files
	static godot::Error parse_and_write_header(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_utility_functions(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_builtin_classes(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_classes(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_global_enums(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_global_constants(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_singletons(const godot::Dictionary &p_root, const godot::String &p_output_dir);
	static godot::Error parse_and_write_native_structures(const godot::Dictionary &p_root, const godot::String &p_output_dir);
};

} //namespace api_tool::internal
