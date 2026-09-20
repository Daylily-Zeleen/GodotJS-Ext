/************************************************************************/
/*  This file is part of: GodotJS-Ext (GNU LGPL v2.1+).                 */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/************************************************************************/

#pragma once

// editor/api_tool_detail_storage_editor.h
// Editor-only access to ApiMethodDetailStorage's cold fill path. The store's
// editor-only surface lives here rather than in the class (which the runtime
// extension also compiles), so core/api_tool_detail_storage.h declares only the
// runtime interface. Same shape as ApiMethodHotWriter in api_tool_store_writer.h:
// one friend struct, the target passed in.
//
// Editor only: the definitions are in api_tool_detail_storage_editor.cpp, which
// the runtime target does not compile.

#include "api_tool/core/api_tool_detail_storage.h"

namespace api_tool::internal {

struct ApiMethodColdAccess {
	// ---- fill (ApiParser, while decoding the JSON) ----
	static void push_cold(ApiMethodDetailStorage &r_storage, const ApiMethodDetail &p_detail, const godot::Variant *p_defaults, uint32_t p_default_count);
	// Closes the fill: builds the prefix-sum table and publishes the cold data.
	// The store's `details_loaded_` / `defaults_loaded_` flags are NOT editor
	// state -- they are the runtime's "already loaded, do not read the file"
	// short circuit (see ensure_details / ensure_defaults), and the editor path
	// reaches the same state by having filled the store in memory.
	static void seal_cold(ApiMethodDetailStorage &r_storage);

	// ---- read back (ApiStoreWriter, serialising to the store file) ----
	// Kept inline: the writer calls these once per method while walking the
	// entity (16k+ methods for the biggest classes).
	static _FORCE_INLINE_ const godot::LocalVector<ApiMethodDetail> &cold_details(const ApiMethodDetailStorage &p_storage) { return p_storage.details_; }
	static _FORCE_INLINE_ uint32_t cold_default_count(const ApiMethodDetailStorage &p_storage, uint32_t p_index) { return p_index + 1 < p_storage.default_offsets_.size() ? p_storage.default_offsets_[p_index + 1] - p_storage.default_offsets_[p_index] : 0; }
	static _FORCE_INLINE_ const godot::Variant *cold_defaults(const ApiMethodDetailStorage &p_storage, uint32_t p_index) { return p_storage.default_offsets_[p_index] < p_storage.default_values_.size() ? p_storage.default_values_.ptr() + p_storage.default_offsets_[p_index] : nullptr; }
};

} //namespace api_tool::internal
