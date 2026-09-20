/************************************************************************/
/*  This file is part of: GodotJS-Ext (GNU LGPL v2.1+).                 */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/************************************************************************/

// editor/api_tool_detail_storage_editor.cpp
// Editor-only definitions of ApiMethodColdAccess: the cold-data push path the
// JSON parser fills while decoding. Deliberately NOT in the runtime extension.

#include "api_tool_detail_storage_editor.h"

using namespace godot;

namespace api_tool::internal {

void internal::ApiMethodColdAccess::push_cold(internal::ApiMethodDetailStorage &r_storage, const internal::ApiMethodDetail &p_detail, const Variant *p_defaults, uint32_t p_default_count) {
	r_storage.details_.push_back(p_detail);
	r_storage.default_counts_.push_back((uint16_t)p_default_count);
	if (p_default_count > 0) {
		const uint32_t base = (uint32_t)r_storage.default_values_.size();
		r_storage.default_values_.resize(base + p_default_count);
		for (uint32_t i = 0; i < p_default_count; i++) {
			r_storage.default_values_[base + i] = p_defaults[i];
		}
	}
}

void internal::ApiMethodColdAccess::seal_cold(internal::ApiMethodDetailStorage &r_storage) {
	r_storage.default_offsets_.clear();
	r_storage.default_offsets_.resize(r_storage.default_counts_.size() + 1);
	uint32_t acc = 0;
	for (uint32_t i = 0; i < r_storage.default_counts_.size(); i++) {
		r_storage.default_offsets_[i] = acc;
		acc += r_storage.default_counts_[i];
	}
	r_storage.default_offsets_[r_storage.default_counts_.size()] = acc;
	// Publish: the runtime reads the same flags to know the cold data is already
	// in memory and must not be read from the file.
	r_storage.details_loaded_.store(true);
	r_storage.defaults_loaded_.store(true);
}

} //namespace api_tool::internal
