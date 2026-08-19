/************************************************************************/
/*  jsb_editor_progress.cpp                                             */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_editor_progress.h"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/label.hpp"
#include "godot_cpp/classes/margin_container.hpp"
#include "godot_cpp/classes/progress_bar.hpp"
#include "godot_cpp/classes/v_box_container.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/vector2i.hpp"

#include "compat/editor_settings.h"

namespace godot {

// ========= EditorProgress ===========
EditorProgress::EditorProgress(const String &p_task_name, int p_total) : name(p_task_name) {
	EditorProgressDialog::get_singleton()->add(p_task_name, p_total);
}
EditorProgress::~EditorProgress() {
	EditorProgressDialog::get_singleton()->finish(name);
}

void EditorProgress::step(const String &p_state, int p_step) {
	if (p_step < 0) {
		current++;
	} else {
		current = p_step;
	}
	EditorProgressDialog::get_singleton()->update(name, p_state, current);
}

// =========== EditorProgressDialog ============
EditorProgressDialog *EditorProgressDialog::singleton{ nullptr };

void EditorProgressDialog::update_internal(const String &p_task_name, const String &p_state, int p_total, int p_current) {
	uint32_t total = Math::max(p_total, 1);
	if (int *exists = tasks.getptr(p_task_name)) {
		tasks[p_task_name] = Math::max(p_total, *exists);
	} else {
		tasks[p_task_name] = total;
	}

	title_label->set_text(p_state);
	progress_bar->set_max(p_total);
	progress_bar->set_value(Math::min(p_total, p_current));

	if (is_visible()) {
		return;
	}

	Vector2i min_size = main->get_combined_minimum_size();
	min_size.x = Math::max(min_size.x, (int32_t)(500 * EDSCALE));
	popup_centered(min_size);
}

void EditorProgressDialog::add(const String &p_task_name, int p_total) {
	update_internal(p_task_name, p_task_name, Math::max(p_total, 1), 0);
}
void EditorProgressDialog::update(const String &p_task_name, const String &p_state, int p_current) {
	int *total = tasks.getptr(p_task_name);
	update_internal(p_task_name, p_state, total ? *total : 1, p_current);
}
void EditorProgressDialog::finish(const String &p_task_name) {
	tasks.erase(p_task_name);
	if (tasks.is_empty()) {
		hide();
	}
}

EditorProgressDialog::EditorProgressDialog() {
	CRASH_COND_MSG(singleton, "EditorProgressDialog is instantiated twice?");
	singleton = this;

	set_exclusive(true);
	set_flag(Window::FLAG_BORDERLESS, true);
	hide();

	main = memnew(MarginContainer);
	main->add_theme_constant_override("margin_top", 10);
	main->add_theme_constant_override("margin_left", 10);
	main->add_theme_constant_override("margin_bottom", 10);
	main->add_theme_constant_override("margin_right", 10);
	main->set_anchors_preset(Control::LayoutPreset::PRESET_FULL_RECT);
	add_child(main);

	VBoxContainer *vbox = memnew(VBoxContainer);
	main->add_child(vbox);

	title_label = memnew(Label);
	vbox->add_child(title_label);

	progress_bar = memnew(ProgressBar);
	progress_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	progress_bar->set_show_percentage(true);
	vbox->add_child(progress_bar);
}

EditorProgressDialog::~EditorProgressDialog() {
	singleton = nullptr;
}
EditorProgressDialog *singleton{ nullptr };

}; //namespace godot
