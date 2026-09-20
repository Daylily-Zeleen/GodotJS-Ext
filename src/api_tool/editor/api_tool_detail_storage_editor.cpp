/************************************************************************/
/*  This file is part of: GodotJS-Ext (GNU LGPL v2.1+).                 */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/************************************************************************/

// editor/api_tool_detail_storage_editor.cpp
// Editor-only definitions of ApiMethodDetailStorage: the cold-data push path the
// JSON parser fills while decoding. Deliberately NOT in the runtime extension.

#include "api_tool/core/api_tool_detail_storage.h"

using namespace godot;

namespace api_tool::internal {

void internal::ApiMethodDetailStorage::push_cold(const internal::ApiMethodDetail &p_detail, const Variant *p_defaults, uint32_t p_default_count) {
	details_.push_back(p_detail);
	default_counts_.push_back((uint16_t)p_default_count);
	if (p_default_count > 0) {
		const uint32_t base = (uint32_t)default_values_.size();
		default_values_.resize(base + p_default_count);
		for (uint32_t i = 0; i < p_default_count; i++) {
			default_values_[base + i] = p_defaults[i];
		}
	}
}

void internal::ApiMethodDetailStorage::seal_cold() {
	default_offsets_.clear();
	default_offsets_.resize(default_counts_.size() + 1);
	uint32_t acc = 0;
	for (uint32_t i = 0; i < default_counts_.size(); i++) {
		default_offsets_[i] = acc;
		acc += default_counts_[i];
	}
	default_offsets_[default_counts_.size()] = acc;
	details_loaded_.store(true);
	defaults_loaded_.store(true);
}

} //namespace api_tool::internal
