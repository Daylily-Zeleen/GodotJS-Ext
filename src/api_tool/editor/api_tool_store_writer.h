#pragma once

// editor/api_tool_store_writer.h
// Binary file writing layer (editor-only, TOOLS_ENABLED).
// Serializes API data structures to disk files.
// File format: [4B magic][4B version][4B flags][payload]

#ifdef TOOLS_ENABLED

#include "../api_tool_types.h"
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
    static godot::Error write_document(const godot::String &p_path, const ApiClassDocument &p_data);
    static godot::Error write_utility_function_document(const godot::String &p_path, const ApiUtilityFunctionDocument &p_data);
    static godot::Error write_global_enum_document(const godot::String &p_path, const ApiGlobalEnumDocument &p_data);
    static godot::Error write_global_constant_document(const godot::String &p_path, const ApiGlobalConstantDocument &p_data);
    static godot::Error write_compatibility_hashes(const godot::String &p_path, const ApiCompatibilityHashData &p_data);
};

} // namespace api_tool

#endif // TOOLS_ENABLED