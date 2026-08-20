/************************************************************************/
/*  api_tool.h                                                          */
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

// api_tool.h
// Minimal, stable public interface for the api_tool module.
// This file rarely changes. Detailed type definitions are in api_tool_types.h.
// Provides lazy-loading queries, listing, and generation of API data.

#include <cstdint>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/variant.hpp>

// Include type definitions (including global-scope callback types)
#include "api_tool_types.h"

namespace api_tool {

// ============================================================================
// Core interface (stable)
// ============================================================================
extern "C" {
// Initialize the module with a data root directory. Called at module load.
// Returns true on success.
godot::Error GDE_EXPORT initialize();
void GDE_EXPORT finalize();
void GDE_EXPORT get_api_dumping_dir(godot::String *r_dir);
}

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

const godot::LocalVector<MethodHash> *get_builtin_method_compatibility_hashes(godot::Variant::Type p_type, const godot::StringName &p_name);
const godot::LocalVector<MethodHash> *get_class_method_compatibility_hashes(const godot::StringName &p_class_name, const godot::StringName &p_name);

bool has_generated_data();

} // namespace api_tool