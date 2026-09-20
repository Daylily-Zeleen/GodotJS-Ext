/************************************************************************/
/*  api_tool_detail_storage.h                                           */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// core/api_tool_detail_storage.h
// Entity-level store for the hot argument block plus the lazily loaded cold
// per-method data (full PropertyInfo detail + default values).
//
// This is an internal implementation detail of the store / lazy-loading
// machinery, kept OUT of api_tool_types.h: that header is the caller-facing hot
// layer. The runtime-path definitions live in core/ (compiled into both the
// runtime and editor extensions), the editor-path ones in editor/ (editor only).

#include "api_tool/api_tool_types.h"
#include <atomic>
#include <cstdint>
#include <mutex>

namespace api_tool::internal {

// Owns one entity's hot argument block plus the cold per-method data (the full
// PropertyInfo detail and default values).
//
// Two fill paths share the same backing store, so consumers only ever see one
// accessor pair (`get_detail` / `get_defaults`):
//   * editor  - ApiParser pushes the cold data while decoding the JSON, and
//               ApiStoreWriter reads it back out to serialise the file;
//   * runtime - ApiStoreReader records the cold section offsets and the data is
//               read from the file on first access (design.md §6), once for the
//               whole entity.
class ApiMethodDetailStorage {
public:
	ApiMethodDetailStorage() = default;
	~ApiMethodDetailStorage() = default;
	ApiMethodDetailStorage(const ApiMethodDetailStorage &) = delete;
	ApiMethodDetailStorage &operator=(const ApiMethodDetailStorage &) = delete;

	// ---- hot: one flat argument block for the whole entity ----
	// ApiMethodArg is trivial, so the block can be handed out uninitialised and
	// filled in place while the methods are decoded.
	ApiMethodArg *build_arg_block(uint32_t p_count) {
		args_.resize_uninitialized(p_count);
		return args_.ptr();
	}
	// Same, from an already-built flat list (the parser's path).
	ApiMethodArg *build_arg_block_from(const godot::LocalVector<ApiMethodArg> &p_flat) {
		ApiMethodArg *block = build_arg_block((uint32_t)p_flat.size());
		for (uint32_t i = 0; i < p_flat.size(); i++) block[i] = p_flat[i];
		return block;
	}

	// ---- editor path ----
	void push_cold(const ApiMethodDetail &p_detail, const godot::Variant *p_defaults, uint32_t p_default_count);
	void seal_cold();
	_FORCE_INLINE_ const godot::LocalVector<ApiMethodDetail> &cold_details() const { return details_; }
	_FORCE_INLINE_ uint32_t cold_default_count(uint32_t p_index) const { return p_index + 1 < default_offsets_.size() ? default_offsets_[p_index + 1] - default_offsets_[p_index] : 0; }
	_FORCE_INLINE_ const godot::Variant *cold_defaults(uint32_t p_index) const { return default_offsets_[p_index] < default_values_.size() ? default_values_.ptr() + default_offsets_[p_index] : nullptr; }

	// ---- runtime path ----
	void configure_lazy(const godot::String &p_path, uint64_t p_detail_offset, uint64_t p_detail_size, uint64_t p_defaults_offset, uint32_t p_file_method_count);
	// Hot per-method counts, kept so the lazily read cold sections can be
	// cross-checked against them (AC7).
	void set_hot_counts(godot::LocalVector<uint16_t> &&p_arg_counts, godot::LocalVector<uint16_t> &&p_default_counts) {
		arg_counts_ = std::move(p_arg_counts);
		default_counts_ = std::move(p_default_counts);
	}

	// ---- runtime-synthesised methods (Object's FLAG_OBJECT_CORE virtuals) ----
	// They exist in no store file, so their detail and defaults stay in memory
	// and are appended after the file records.
	void push_injected(const ApiMethodDetail &p_detail, const godot::Variant *p_defaults, uint32_t p_default_count);
	ApiMethodArg *build_injected_arg_block(uint32_t p_count) {
		injected_args_.resize_uninitialized(p_count);
		return injected_args_.ptr();
	}

	void set_owner_name(const godot::StringName &p_name) { owner_name_ = p_name; }
	_FORCE_INLINE_ const godot::StringName &get_owner_name() const { return owner_name_; }

	// ---- cold access (either path) ----
	const ApiMethodDetail &get_detail(uint32_t p_index) const;
	const godot::Variant *get_defaults(uint32_t p_index, uint32_t &r_count) const;

private:
	void ensure_details() const;
	void ensure_defaults() const;

	// lazy source
	godot::String path_;
	uint64_t detail_offset_ = 0;
	uint64_t detail_size_ = 0; // AC7: consumed bytes must equal this
	uint64_t defaults_offset_ = 0;
	uint32_t file_method_count_ = 0;
	bool lazy_ = false;

	// hot
	godot::LocalVector<ApiMethodArg> args_;
	godot::LocalVector<uint16_t> arg_counts_; // 1:1 with the file's methods
	godot::LocalVector<uint16_t> default_counts_;

	// cold; index-aligned with the entity's methods (file records, then the
	// injected tail)
	mutable godot::LocalVector<ApiMethodDetail> details_;
	mutable godot::LocalVector<godot::Variant> default_values_;
	mutable godot::LocalVector<uint32_t> default_offsets_; // prefix sums, size = methods + 1
	mutable std::atomic<bool> details_loaded_{ false };
	mutable std::atomic<bool> defaults_loaded_{ false };

	// injected tail, kept in memory
	godot::LocalVector<ApiMethodDetail> injected_details_;
	godot::LocalVector<godot::Variant> injected_default_values_;
	godot::LocalVector<uint32_t> injected_default_counts_;
	godot::LocalVector<ApiMethodArg> injected_args_;

	mutable std::mutex mutex_;
	godot::StringName owner_name_;
};

} //namespace api_tool::internal
