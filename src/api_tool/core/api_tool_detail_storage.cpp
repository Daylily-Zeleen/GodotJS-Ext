/************************************************************************/
/*  This file is part of: GodotJS-Ext (GNU LGPL v2.1+).                 */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/************************************************************************/

// core/api_tool_detail_storage.cpp
// Runtime-path definitions of ApiMethodDetailStorage (compiled into BOTH the
// runtime and the editor extension).

#include "api_tool/core/api_tool_detail_storage.h"
#include "api_tool/core/api_tool_store.h"

using namespace godot;

namespace api_tool::internal {

void internal::ApiMethodDetailStorage::configure_lazy(const String &p_path, uint64_t p_detail_offset, uint64_t p_detail_size, uint64_t p_defaults_offset, uint32_t p_file_method_count) {
	path_ = p_path;
	detail_offset_ = p_detail_offset;
	detail_size_ = p_detail_size;
	defaults_offset_ = p_defaults_offset;
	file_method_count_ = p_file_method_count;
	lazy_ = true;
}

void internal::ApiMethodDetailStorage::push_injected(const internal::ApiMethodDetail &p_detail, const Variant *p_defaults, uint32_t p_default_count) {
	injected_details_.push_back(p_detail);
	injected_default_counts_.push_back(p_default_count);
	if (p_default_count > 0) {
		const uint32_t base = (uint32_t)injected_default_values_.size();
		injected_default_values_.resize(base + p_default_count);
		for (uint32_t i = 0; i < p_default_count; i++) {
			injected_default_values_[base + i] = p_defaults[i];
		}
	}
}

void internal::ApiMethodDetailStorage::ensure_details() const {
	if (details_loaded_.load(std::memory_order_acquire)) return;
	std::lock_guard<std::mutex> lock(mutex_);
	if (details_loaded_.load(std::memory_order_relaxed)) return;

	if (lazy_ && file_method_count_ > 0) {
		if (!ApiStoreReader::read_method_details(path_, detail_offset_, detail_size_, file_method_count_, arg_counts_, details_)) {
			ERR_PRINT("[API Tool] failed to lazily read method details: " + path_);
			details_.clear();
			return; // not published: a later access retries
		}
	}
	// In-memory tail (Object's FLAG_OBJECT_CORE virtuals) sits after the file records.
	for (uint32_t i = 0; i < injected_details_.size(); i++) {
		details_.push_back(injected_details_[i]);
	}
	details_loaded_.store(true, std::memory_order_release);
}

void internal::ApiMethodDetailStorage::ensure_defaults() const {
	if (defaults_loaded_.load(std::memory_order_acquire)) return;
	std::lock_guard<std::mutex> lock(mutex_);
	if (defaults_loaded_.load(std::memory_order_relaxed)) return;

	if (lazy_ && file_method_count_ > 0) {
		if (!ApiStoreReader::read_method_defaults(path_, defaults_offset_, file_method_count_, default_counts_, default_values_, default_offsets_)) {
			ERR_PRINT("[API Tool] failed to lazily read method defaults: " + path_);
			default_values_.clear();
			default_offsets_.clear();
			return; // not published: a later access retries
		}
	} else if (default_offsets_.size() == 0) {
		// No file records (editor-built entity, or a class whose methods are all
		// injected): start from an empty table. resize() does not initialise
		// trivially-constructible elements, so seed the sentinel explicitly.
		default_offsets_.clear();
		default_offsets_.push_back(0);
	}
	// Append the in-memory tail, rebasing its slice offsets.
	const uint32_t injected_count = (uint32_t)injected_default_counts_.size();
	if (injected_count > 0) {
		uint32_t acc = default_offsets_[default_offsets_.size() - 1];
		const uint32_t value_base = (uint32_t)default_values_.size();
		uint32_t injected_total = 0;
		// drop the sentinel; it is re-appended once the tail is in place
		default_offsets_.resize(default_offsets_.size() - 1);
		for (uint32_t i = 0; i < injected_count; i++) {
			default_offsets_.push_back(acc);
			acc += injected_default_counts_[i];
			injected_total += injected_default_counts_[i];
		}
		default_offsets_.push_back(acc);
		if (injected_total > 0) {
			default_values_.resize(value_base + injected_total);
			for (uint32_t i = 0; i < injected_total; i++) {
				default_values_[value_base + i] = injected_default_values_[i];
			}
		}
	}
	defaults_loaded_.store(true, std::memory_order_release);
}

const internal::ApiMethodDetail &internal::ApiMethodDetailStorage::get_detail(uint32_t p_index) const {
	ensure_details();
	static const internal::ApiMethodDetail kEmpty;
	if (p_index >= details_.size()) return kEmpty;
	return details_[p_index];
}

const Variant *internal::ApiMethodDetailStorage::get_defaults(uint32_t p_index, uint32_t &r_count) const {
	ensure_defaults();
	r_count = 0;
	if (default_offsets_.size() < 2 || p_index + 1 >= default_offsets_.size()) return nullptr;
	const uint32_t start = default_offsets_[p_index];
	const uint32_t end = default_offsets_[p_index + 1];
	r_count = end - start;
	if (r_count == 0) return nullptr;
	return default_values_.ptr() + start;
}

} //namespace api_tool::internal
