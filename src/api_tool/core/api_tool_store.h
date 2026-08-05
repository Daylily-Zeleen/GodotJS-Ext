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
#ifdef TOOLS_ENABLED
	static godot::Error read_document(const godot::String &p_path, ApiClassDocument &r_data);
	static godot::Error read_utility_function_document(const godot::String &p_path, ApiUtilityFunctionDocument &r_data);
	static godot::Error read_global_enum_document(const godot::String &p_path, ApiGlobalEnumDocument &r_data);
	static godot::Error read_global_constant_document(const godot::String &p_path, ApiGlobalConstantDocument &r_data);
#endif // TOOLS_ENABLED
	static godot::Error read_compatibility_hashes(const godot::String &p_path, ApiCompatibilityHashData &r_data);
};

} //namespace api_tool::internal