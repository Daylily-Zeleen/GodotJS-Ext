/************************************************************************/
/*  jsb_config_classes_dialog.h                                         */
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

#include <godot_cpp/classes/accept_dialog.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>

namespace godot {
class LineEdit;
class Tree;
class TreeItem;
class FileDialog;
class ConfirmationDialog;
class Button;
} //namespace godot

// Editor dialog to configure which native classes get JavaScript bindings generated.
// Unchecked classes are stored as ignored classes in ProjectSettings (raw class names).
class GodotJSConfigClassesDialog : public godot::AcceptDialog {
	godot::LineEdit *search_box_ = nullptr;
	godot::Tree *tree_ = nullptr;
	godot::FileDialog *export_dialog_ = nullptr;
	godot::FileDialog *import_dialog_ = nullptr;
	godot::ConfirmationDialog *regenerate_dialog_ = nullptr;

	godot::Button *save_button_ = nullptr;
	godot::Button *export_button_ = nullptr;
	godot::Button *import_button_ = nullptr;

	godot::HashMap<godot::StringName, godot::TreeItem *> items_;
	godot::HashSet<godot::StringName> omitted_classes_;

	godot::Vector<godot::TreeItem *> search_matches_;
	int current_match_ = -1;

	void _build_tree();
	godot::TreeItem *_ensure_item(const godot::StringName &p_class_name);
	bool _is_locked(const godot::StringName &p_class_name) const;
	void _set_branch_disabled(godot::TreeItem *p_item, bool p_disabled);

	void _on_item_edited();
	void _on_search_changed(const godot::String &p_text);
	void _on_search_submitted(const godot::String &p_text);
	void _focus_match(int p_index);

	void _on_save_pressed();
	void _on_export_pressed();
	void _on_import_pressed();
	void _on_export_file_selected(const godot::String &p_path);
	void _on_import_file_selected(const godot::String &p_path);
	void _on_regenerate_confirmed();

	godot::PackedStringArray _collect_unchecked() const;

public:
	GodotJSConfigClassesDialog();

	// Rebuild the tree from ClassDB and current ProjectSettings before showing.
	void refresh();
};
