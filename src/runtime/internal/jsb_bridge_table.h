/************************************************************************/
/*  jsb_bridge_table.h                                                  */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// C ABI bridge table between the editor and runtime extensions.
//
// Threat model / rationale (TASK_STATUS.md 14.2):
//  - Nothing that can execute JS may be reachable through ClassDB: project
//    scripts must not be able to trigger arbitrary evaluation.
//  - A raw integer (function pointer address) handed to a script is inert:
//    scripts cannot dereference or call it. Native extensions are inside the
//    trust boundary anyway.
//
// Contract:
//  - The runtime extension owns the single table instance and exposes its
//    address through a neutral ClassDB method (`get_bridge` on
//    GodotJSScriptLanguage). Callers convert the returned integer back to
//    `JsbBridgeTable*` after checking `struct_size`.
//  - Variant results are written through `GDExtensionVariantPtr`
//    (= GDExtensionVariantPtr): the CALLER owns the variant storage; the
//    callee constructs into it using its own godot-cpp copy. This mirrors how
//    the engine itself hands variants across extension boundaries.
//  - All pointers are valid for the duration of the call only.

#include <cstdint>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb {

using JsbEvalFn = void (*)(const char *p_source_utf8, int64_t p_length,
                           GDExtensionVariantPtr r_result_variant, int64_t *r_error);

/// Evaluate source with one argument Variant exposed as the `__jsb_arg` global.
using JsbEvalArgFn = void (*)(const char *p_source_utf8, int64_t p_length,
                              GDExtensionConstVariantPtr p_argument_variant,
                              GDExtensionVariantPtr r_result_variant, int64_t *r_error);

using JsbQueryFn = void (*)(const char *p_arg_utf8, int64_t p_length,
                            GDExtensionVariantPtr r_result_variant, int64_t *r_error);

using JsbFillStatsFn = void (*)(void *p_statistics_raw, int64_t *r_error);

using JsbVoidFn = void (*)(int64_t *r_error);

struct JsbBridgeTable {
	/// sizeof(JsbBridgeTable) at the time the runtime built it. Reject tables
	/// whose size differs from the local struct definition.
	uint64_t struct_size = 0;

	JsbEvalFn eval = nullptr;

	/// eval with a single argument Variant (available as global `__jsb_arg`)
	JsbEvalArgFn eval_with_arg = nullptr;

	/// arg = module path; result = Dictionary {source: String, package: String}
	JsbQueryFn get_module_source_info = nullptr;

	/// arg = module path; result = PackedStringArray (one-level children)
	JsbQueryFn get_module_direct_dependencies = nullptr;

	/// arg = raw pointer to a caller-allocated Statistics structure
	JsbFillStatsFn fill_statistics = nullptr;

	/// rescan external file changes
	JsbVoidFn scan_external_changes = nullptr;

	/// arg = script path; result = bool (whether the global class is generic)
	JsbQueryFn is_global_class_generic = nullptr;

	/// request a full garbage collection pass
	JsbVoidFn request_gc = nullptr;

	/// result = PackedStringArray of the language's reserved words
	JsbQueryFn get_reserved_words = nullptr;
};

/// Runtime-side singleton accessor (defined in jsb_bridge_table.cpp).
const JsbBridgeTable *get_bridge_table();

} //namespace jsb
