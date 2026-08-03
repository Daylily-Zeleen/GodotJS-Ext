#pragma once

// api_tool.h
// Minimal, stable public interface for the api_tool module.
// This file rarely changes. Detailed type definitions are in api_tool_types.h.
// Provides lazy-loading queries, listing, and generation of API data.

#include <cstdint>
#include <memory>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/templates/hash_set.hpp>

#ifdef TOOLS_ENABLED
#include <godot_cpp/templates/vector.hpp>
#endif // TOOLS_ENABLED

// Include type definitions (including global-scope callback types)
#include "api_tool_types.h"

namespace api_tool {

// ============================================================================
// Core interface (stable)
// ============================================================================

// Initialize the module with a data root directory. Called at module load.
// Returns true on success.
godot::Error initialize();

void finalize();

// Check if API data is loaded.
bool is_loaded();

// Get version info (convenience, no need to include api_tool_types.h).
void get_version(int32_t &r_major, int32_t &r_minor, int32_t &r_patch);

// Get the full header (requires api_tool_types.h to dereference).
const ApiHeader &get_header();

// ============================================================================
// Name-based queries (lazy-loaded + cached, thread-safe)
// Returns nullptr if not found. Pointers valid until cache is invalidated.
// ============================================================================

const ApiUtilityFunction *find_utility_function(const godot::StringName &p_name);
const ApiBuiltinClass *find_builtin_class(const godot::StringName &p_name);
const ApiBuiltinClass *find_builtin_class(godot::Variant::Type p_type); // O(1) lookup via fixed-size array indexed by Variant::Type
const ApiClass *find_class(const godot::StringName &p_name);
const ApiEnumInfo *find_global_enum(const godot::StringName &p_name);
const ApiConstantInfo *find_global_constant(const godot::StringName &p_name);
const ApiSingleton *find_singleton(const godot::StringName &p_name);
const ApiNativeStructure *find_native_structure(const godot::StringName &p_name);

// ============================================================================
// Document queries (no cache, direct file read, TOOLS_ENABLED only)
// Returns std::unique_ptr<T> (caller owns). Returns nullptr if file missing/corrupted.
// Unified query for both Class and BuiltInClass.
// ============================================================================
std::unique_ptr<ApiClassDocument> find_document(const godot::StringName &p_name);
std::unique_ptr<ApiClassDocument> find_document(const godot::Variant::Type &p_type);
std::unique_ptr<ApiUtilityFunctionDocument> find_utility_function_document(const godot::StringName &p_name);
std::unique_ptr<ApiGlobalEnumDocument> find_global_enum_document(const godot::StringName &p_name);
std::unique_ptr<ApiGlobalConstantDocument> find_global_constant_document(const godot::StringName &p_name);

// ============================================================================
// List all names (O(1) lookup via HashSet)
// ============================================================================

godot::HashSet<godot::StringName> list_utility_functions();
const godot::HashSet<godot::StringName> &list_builtin_classes();
const godot::HashSet<godot::StringName> &list_classes();
const godot::HashSet<godot::StringName> &list_global_enums();
const godot::HashSet<godot::StringName> &list_global_constants();
godot::HashSet<godot::StringName> list_singletons();
godot::HashSet<godot::StringName> list_native_structures();

// ============================================================================
// Existence checks (O(1) via HashSet)
// ============================================================================

bool has_utility_function(const godot::StringName &p_name);
bool has_builtin_class(const godot::StringName &p_name);
bool has_class(const godot::StringName &p_name);
bool has_global_enum(const godot::StringName &p_name);
bool has_global_constant(const godot::StringName &p_name);
bool has_singleton(const godot::StringName &p_name);
bool has_native_structure(const godot::StringName &p_name);

// ============================================================================
// Cache invalidation callbacks
// ============================================================================

// Register a callback to be called when cache is invalidated.
// Returns a handle for later unregistration.
::CacheInvalidatedHandle register_cache_invalidated_callback(::CacheInvalidatedCallback p_callback);

// Unregister a previously registered callback by handle.
void unregister_cache_invalidated_callback(::CacheInvalidatedHandle p_handle);

// ============================================================================
// Count queries
// ============================================================================

int32_t get_utility_function_count();
int32_t get_builtin_class_count();
int32_t get_class_count();
int32_t get_global_enum_count();
int32_t get_global_constant_count();

// ============================================================================
// Compatibility hash queries (no cache, direct file read)
// ============================================================================

const godot::LocalVector<MethodHash>* get_builtin_method_compatibility_hashes(godot::Variant::Type p_type, const godot::StringName& p_name);
const godot::LocalVector<MethodHash>* get_class_method_compatibility_hashes(const godot::StringName &p_class_name, const godot::StringName& p_name);

// ============================================================================
// Editor-only: API generation (only TOOLS_ENABLED)
// Cache is invalidated internally at the start of generate().
// ============================================================================

#ifdef TOOLS_ENABLED
void full_generate_and_reboot();

// Generate API data by launching Godot subprocess with --dump-extension-api-with-docs,
// then parsing the generated JSON and writing binary files.
// Storage path follows project setting: application/config/use_hidden_project_data_directory
//   true  -> res://.godot/.api_dumping/
//   false -> res://godot/.api_dumping/
// Cache is invalidated at the start.
// Returns godot::OK on success, error code on failure.
godot::Error generate_api_tool_data(const godot::String &p_extension_api_json_path);


/**
 * @brief 返回 res://.godot/.api_dumping/ (或 res://godot/.api_dumping/) 下除了 documents 文件夹外的所有文件
 * 实现细节:
 * 1. ".api_dumping" 文件夹的路径要根据 ApiLoader 的 get_api_dumping_dir() 进行获取
 * 2. 不需要通过 ApiLoader 实际加载对应数据来查看classes中的类的 APIType，直接通过 godot::ClassDB 即可查得，注意如果查出来是 API_NONE 则说明不存在，跳过它即可
 *
 * @param p_exclude_editor_types 是否剔除 APIType 为 API_EDITOR 与 API_EDITOR_EXTENSION 的类
 * @param p_extension_types_only 是否只返回 APIType 为 API_EXTENSION 或 API_EDITOR_EXTENSION 的类（为true时只可能返回 classes 文件夹里的部分文件）
 * @return godot::Vector<godot::String> 基于项目路径(“res://")的文件路径列表
 */
godot::Vector<godot::String> get_api_data_files(bool p_exclude_editor_types = true, bool p_extension_types_only = false);

#endif // TOOLS_ENABLED

bool has_generated_data();

} // namespace api_tool