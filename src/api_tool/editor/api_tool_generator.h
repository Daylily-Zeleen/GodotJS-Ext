#pragma once

// editor/api_tool_generator.h
// Subprocess generation (TOOLS_ENABLED only).
// Detects project path and Godot executable, launches subprocess with
// --headless --path {project} --dump-extension-api-with-docs.
// Handles backup/restore of existing extension_api.json.

#ifdef TOOLS_ENABLED

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/global_constants.hpp>

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

} // namespace api_tool

#endif // TOOLS_ENABLED
