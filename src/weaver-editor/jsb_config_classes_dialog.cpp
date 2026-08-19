/************************************************************************/
/*  jsb_config_classes_dialog.cpp                                       */
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

#include "jsb_config_classes_dialog.h"

#include "jsb_editor_pch.h"

#include "../internal/jsb_naming_util.h"
#include "../internal/jsb_settings.h"
#include "jsb_editor_plugin.h"

#include <compat/editor_settings.h>
#include <compat/misc.h>

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/file_dialog.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>

// TODO: 低优先级，优化搜索框的模糊搜索。
using namespace godot;

namespace {
constexpr int COL_MAIN = 0;

// Distinct color to flag editor-only classes (API_EDITOR / API_EDITOR_EXTENSION).
const Color kEditorClassColor = Color(0.6f, 0.8f, 1.0f);

constexpr int kFuzzyNoMatch = -2000000000;
constexpr int kFuzzyMaxMisses = 2;

// A word starts at index 0, after a separator, or at an uppercase letter following a non-uppercase (CamelCase).
bool _is_word_start(const String &p_str, int p_index) {
	if (p_index <= 0) {
		return true;
	}
	if (p_index >= p_str.length()) {
		return false;
	}
	const char32_t prev = p_str[p_index - 1];
	if (prev == '/' || prev == '\\' || prev == '-' || prev == '_' || prev == '.' || prev == ' ') {
		return true;
	}
	return is_ascii_upper_case(p_str[p_index]) && !is_ascii_upper_case(prev);
}

int _find_char(const String &p_str, char32_t p_char, int p_from) {
	for (int i = p_from; i < p_str.length(); i++) {
		if (p_str[i] == p_char) {
			return i;
		}
	}
	return -1;
}

// Ported (single-token) from Godot's FuzzySearch: greedy subsequence match with miss budget,
// scoring contiguous runs quadratically and rewarding word-boundary/exact matches.
int _fuzzy_score(const String &p_query_lower, const String &p_target) {
	const String adjusted = p_target.to_lower();
	const int token_length = p_query_lower.length();

	int offset = 0;
	int run_start = -1;
	int run_len = 0;
	int miss_budget = kFuzzyMaxMisses;
	int matched_length = 0;
	Vector<Vector2i> substrings;

	for (int i = 0; i < token_length; i++) {
		const int new_offset = _find_char(adjusted, p_query_lower[i], offset);
		if (new_offset < 0) {
			if (--miss_budget < 0) {
				return kFuzzyNoMatch;
			}
		} else {
			if (run_start == -1 || offset != new_offset) {
				if (run_start != -1) {
					substrings.push_back(Vector2i(run_start, run_len));
					matched_length += run_len;
				}
				run_start = new_offset;
				run_len = 1;
			} else {
				run_len += 1;
			}
			offset = new_offset + 1;
		}
	}
	if (run_start != -1) {
		substrings.push_back(Vector2i(run_start, run_len));
		matched_length += run_len;
	}
	if (substrings.is_empty()) {
		return kFuzzyNoMatch;
	}

	int score = -20 * (token_length - matched_length);
	for (const Vector2i &s : substrings) {
		int substring_score = s.y * s.y;
		if (_is_word_start(p_target, s.x)) {
			substring_score += 8;
		}
		if (s.y == token_length) {
			substring_score += 100;
		}
		score += substring_score;
	}
	return score;
}
} //namespace

GodotJSConfigClassesDialog::GodotJSConfigClassesDialog() {
	set_title(TTR("Config Enabled Classes Bindings"));
	set_min_size(Size2i(500, 600) * EDSCALE);
	set_exclusive(true);

	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_anchors_preset(Control::LayoutPreset::PRESET_FULL_RECT);
	add_child(vbox);

	search_box_ = memnew(LineEdit);
	search_box_->set_placeholder(TTR("Search classes..."));
	search_box_->set_clear_button_enabled(true);
	search_box_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	search_box_->connect("text_changed", callable_mp(this, &GodotJSConfigClassesDialog::_on_search_changed));
	search_box_->connect("text_submitted", callable_mp(this, &GodotJSConfigClassesDialog::_on_search_submitted));
	vbox->add_child(search_box_);

	tree_ = memnew(Tree);
	tree_->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tree_->set_hide_root(true);
	tree_->set_columns(1);
	tree_->connect("item_edited", callable_mp(this, &GodotJSConfigClassesDialog::_on_item_edited));
	vbox->add_child(tree_);

	// OK button acts as "Close" (no persistence); dedicated Save button persists.
	set_ok_button_text(TTR("Close"));

	save_button_ = add_button(TTR("Save"), true, "save");
	save_button_->connect("pressed", callable_mp(this, &GodotJSConfigClassesDialog::_on_save_pressed));
	export_button_ = add_button(TTR("Export"), true, "export");
	export_button_->connect("pressed", callable_mp(this, &GodotJSConfigClassesDialog::_on_export_pressed));
	import_button_ = add_button(TTR("Import"), true, "import");
	import_button_->connect("pressed", callable_mp(this, &GodotJSConfigClassesDialog::_on_import_pressed));

	export_dialog_ = memnew(FileDialog);
	export_dialog_->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
	export_dialog_->set_access(FileDialog::ACCESS_FILESYSTEM);
	export_dialog_->set_exclusive(true);
	export_dialog_->add_filter("*.txt", TTR("Ignored Classes Preset"));
	export_dialog_->connect("file_selected", callable_mp(this, &GodotJSConfigClassesDialog::_on_export_file_selected));
	add_child(export_dialog_);

	import_dialog_ = memnew(FileDialog);
	import_dialog_->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	import_dialog_->set_access(FileDialog::ACCESS_FILESYSTEM);
	import_dialog_->set_exclusive(true);
	import_dialog_->add_filter("*.txt", TTR("Ignored Classes Preset"));
	import_dialog_->connect("file_selected", callable_mp(this, &GodotJSConfigClassesDialog::_on_import_file_selected));
	add_child(import_dialog_);

	regenerate_dialog_ = memnew(ConfirmationDialog);
	regenerate_dialog_->set_title(TTR("Regenerate Bindings?"));
	regenerate_dialog_->set_text(TTR("Ignored classes saved.\nRegenerate type bindings now?"));
	regenerate_dialog_->set_exclusive(true);
	regenerate_dialog_->connect("confirmed", callable_mp(this, &GodotJSConfigClassesDialog::_on_regenerate_confirmed));
	add_child(regenerate_dialog_);
}

bool GodotJSConfigClassesDialog::_is_locked(const StringName &p_class_name) const {
	return p_class_name == StringName("Object") || omitted_classes_.has(p_class_name);
}

TreeItem *GodotJSConfigClassesDialog::_ensure_item(const StringName &p_class_name) {
	if (const HashMap<StringName, TreeItem *>::Iterator it = items_.find(p_class_name); it) {
		return it->value;
	}

	const StringName parent_name = ClassDB::get_parent_class(p_class_name);
	TreeItem *parent_item = nullptr;
	if (parent_name != StringName() && ClassDB::class_exists(parent_name)) {
		parent_item = _ensure_item(parent_name);
	}

	TreeItem *item = tree_->create_item(parent_item ? parent_item : tree_->get_root());
	item->set_cell_mode(COL_MAIN, TreeItem::CELL_MODE_CHECK);
	item->set_auto_translate_mode(COL_MAIN, Node::AUTO_TRANSLATE_MODE_DISABLED);
	item->set_text(COL_MAIN, p_class_name);
	item->set_editable(COL_MAIN, true);
	item->set_collapsed(p_class_name != Object::get_class_static()); // Object 展开
	item->set_metadata(COL_MAIN, String(p_class_name));

	if (has_theme_icon(p_class_name, "EditorIcons")) {
		item->set_icon(COL_MAIN, get_theme_icon(p_class_name, "EditorIcons"));
	} else if (parent_item) {
		// Fall back to the nearest ancestor's icon.
		item->set_icon(COL_MAIN, parent_item->get_icon(COL_MAIN));
	}

	const ClassDB::APIType api_type = ClassDB::class_get_api_type(p_class_name);
	if (api_type == ClassDB::API_EDITOR || api_type == ClassDB::API_EDITOR_EXTENSION) {
		item->set_custom_color(COL_MAIN, kEditorClassColor);
		item->set_tooltip_text(COL_MAIN, TTR("Editor-only class (only available in the editor)."));
	}

	items_.insert(p_class_name, item);
	return item;
}

void GodotJSConfigClassesDialog::_build_tree() {
	tree_->clear();
	items_.clear();
	tree_->create_item(); // hidden root

	omitted_classes_ = jsb::internal::NamingUtil::get_omitted_original_classes();
	const PackedStringArray ignored = jsb::internal::Settings::get_ignored_classes();
	const PackedStringArray all_classes = ClassDB::get_class_list();

	for (int i = 0; i < all_classes.size(); i++) {
		const StringName class_name = all_classes[i];
		if (ClassDB::class_get_api_type(class_name) == ClassDB::API_NONE) {
			continue;
		}
		_ensure_item(class_name);
	}

	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		const StringName &class_name = kv.key;
		TreeItem *item = kv.value;
		const bool locked = _is_locked(class_name);

		// Locked classes are always considered enabled and cannot be toggled.
		const bool checked = locked ? true : !ignored.has(class_name);
		item->set_checked(COL_MAIN, checked);

		if (locked) {
			item->set_editable(COL_MAIN, false);
			item->set_custom_color(COL_MAIN, get_theme_color("disabled_font_color", "Editor"));
		}
	}

	// Cascade-disable descendants of unchecked classes.
	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		if (!_is_locked(kv.key) && !kv.value->is_checked(COL_MAIN)) {
			_set_branch_disabled(kv.value, true);
		}
	}
}

void GodotJSConfigClassesDialog::_set_branch_disabled(TreeItem *p_item, bool p_disabled) {
	for (TreeItem *child = p_item->get_first_child(); child; child = child->get_next()) {
		const StringName child_class = child->get_metadata(COL_MAIN);
		if (!_is_locked(child_class)) {
			child->set_editable(COL_MAIN, !p_disabled);
			if (p_disabled) {
				child->set_custom_color(COL_MAIN, get_theme_color("disabled_font_color", "Editor"));
			} else {
				child->clear_custom_color(COL_MAIN);
			}
		}
		_set_branch_disabled(child, p_disabled);
	}
}

void GodotJSConfigClassesDialog::_on_item_edited() {
	TreeItem *edited = tree_->get_edited();
	if (!edited) {
		return;
	}
	const StringName class_name = edited->get_metadata(COL_MAIN);
	if (_is_locked(class_name)) {
		edited->set_checked(COL_MAIN, true);
		return;
	}
	_set_branch_disabled(edited, !edited->is_checked(COL_MAIN));
}

void GodotJSConfigClassesDialog::refresh() {
	_build_tree();
	search_box_->clear();
	search_matches_.clear();
	current_match_ = -1;
}

// ============ search ============

void GodotJSConfigClassesDialog::_on_search_changed(const String &p_text) {
	search_matches_.clear();
	current_match_ = -1;
	if (p_text.is_empty()) {
		return;
	}

	const String query = p_text.to_lower();
	struct Scored {
		int score;
		TreeItem *item;
	};
	struct ScoredComparator {
		bool operator()(const Scored &p_a, const Scored &p_b) const {
			if (p_a.score != p_b.score) {
				return p_a.score > p_b.score;
			}
			const String a = p_a.item->get_metadata(COL_MAIN);
			const String b = p_b.item->get_metadata(COL_MAIN);
			if (a.length() != b.length()) {
				return a.length() < b.length();
			}
			return a < b;
		}
	};
	Vector<Scored> scored;
	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		const int score = _fuzzy_score(query, String(kv.key));
		if (score != kFuzzyNoMatch) {
			scored.push_back({ score, kv.value });
		}
	}

	scored.sort_custom<ScoredComparator>();

	for (const Scored &s : scored) {
		search_matches_.push_back(s.item);
	}

	if (!search_matches_.is_empty()) {
		_focus_match(0);
	}
}

void GodotJSConfigClassesDialog::_on_search_submitted(const String &p_text) {
	if (search_matches_.is_empty()) {
		return;
	}
	_focus_match((current_match_ + 1) % search_matches_.size());
}

void GodotJSConfigClassesDialog::_focus_match(int p_index) {
	if (p_index < 0 || p_index >= search_matches_.size()) {
		return;
	}
	current_match_ = p_index;
	TreeItem *item = search_matches_[p_index];
	for (TreeItem *parent = item->get_parent(); parent; parent = parent->get_parent()) {
		parent->set_collapsed(false);
	}
	tree_->scroll_to_item(item, true);
	tree_->set_selected(item, COL_MAIN);
}

// ============ save / export / import ============

PackedStringArray GodotJSConfigClassesDialog::_collect_unchecked() const {
	PackedStringArray result;
	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		if (_is_locked(kv.key)) {
			continue;
		}
		if (!kv.value->is_checked(COL_MAIN)) {
			result.push_back(kv.key);
		}
	}
	return result;
}

void GodotJSConfigClassesDialog::_on_save_pressed() {
	jsb::internal::Settings::set_ignored_classes(_collect_unchecked());
	regenerate_dialog_->popup_centered();
}

void GodotJSConfigClassesDialog::_on_regenerate_confirmed() {
	hide();
	GodotJSEditorPlugin::generate_types();
}

void GodotJSConfigClassesDialog::_on_export_pressed() {
	export_dialog_->popup_centered_ratio(0.5f);
}

void GodotJSConfigClassesDialog::_on_import_pressed() {
	import_dialog_->popup_centered_ratio(0.5f);
}

void GodotJSConfigClassesDialog::_on_export_file_selected(const String &p_path) {
	const PackedStringArray unchecked = _collect_unchecked();
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		JSB_LOG(Error, "Failed to open '%s' for writing", p_path);
		return;
	}
	for (int i = 0; i < unchecked.size(); i++) {
		file->store_line(unchecked[i]);
	}
}

void GodotJSConfigClassesDialog::_on_import_file_selected(const String &p_path) {
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		JSB_LOG(Error, "Failed to open '%s' for reading", p_path);
		return;
	}

	HashSet<StringName> imported_ignored;
	while (!file->eof_reached()) {
		const String line = file->get_line().strip_edges();
		if (line.is_empty()) {
			continue;
		}
		if (!ClassDB::class_exists(line)) {
			JSB_LOG(Warning, "Ignoring unknown class '%s' from imported preset", line);
			continue;
		}
		imported_ignored.insert(line);
	}

	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		if (_is_locked(kv.key)) {
			continue;
		}
		kv.value->set_checked(COL_MAIN, !imported_ignored.has(kv.key));
	}

	for (const KeyValue<StringName, TreeItem *> &kv : items_) {
		if (!_is_locked(kv.key) && !kv.value->is_checked(COL_MAIN)) {
			_set_branch_disabled(kv.value, true);
		}
	}
}
