/************************************************************************/
/*  jsb_repl.cpp                                                        */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_repl.h"
#include "compat/jsb_compat.h"
#include "jsb_editor_pch.h"
#include "jsb_editor_plugin.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <compat/editor_settings.h>
#include <compat/misc.h>
#include <runtime/bridge/jsb_environment.h>
#include <runtime/weaver/jsb_script_language.h>

void GodotJSREPL::_bind_methods() {
}

GodotJSREPL::GodotJSREPL() {
	// This REPL is an jsb::internal::IConsoleOutput: the base constructor already
	// registered it as a console sink (arming the node console hook in node
	// builds), so no explicit registration is needed here.

	input_submitting_ = false;
	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	add_child(vbox);

	HBoxContainer *tool_bar_box = memnew(HBoxContainer);
	vbox->add_child(tool_bar_box);
	{
		clear_button_ = memnew(Button);
		tool_bar_box->add_child(clear_button_);
		clear_button_->set_theme_type_variation("FlatButton");
		clear_button_->set_focus_mode(FOCUS_NONE);
		clear_button_->set_tooltip_text(TTR("Clear Output"));
		clear_button_->connect("pressed", callable_mp(this, &GodotJSREPL::_clear_pressed));
	}
	{
		gc_button_ = memnew(Button);
		tool_bar_box->add_child(gc_button_);
		gc_button_->set_theme_type_variation("FlatButton");
		gc_button_->set_focus_mode(FOCUS_NONE);
		gc_button_->set_tooltip_text(TTR("Explicit GC"));
		gc_button_->connect("pressed", callable_mp(this, &GodotJSREPL::_gc_pressed));
	}
	{
		generate_types_button_ = memnew(Button);
		tool_bar_box->add_child(generate_types_button_);
		generate_types_button_->set_theme_type_variation("FlatButton");
		generate_types_button_->set_focus_mode(FOCUS_NONE);
		generate_types_button_->set_tooltip_text(TTR("Generate types"));
		generate_types_button_->connect("pressed", callable_mp(this, &GodotJSREPL::_generate_types_pressed));
	}
	{
		install_project_files_button_ = memnew(Button);
		tool_bar_box->add_child(install_project_files_button_);
		install_project_files_button_->set_theme_type_variation("FlatButton");
		install_project_files_button_->set_focus_mode(FOCUS_NONE);
		install_project_files_button_->set_tooltip_text(TTR("Install GodotJS project files"));
		install_project_files_button_->connect("pressed", callable_mp(this, &GodotJSREPL::_install_project_files_pressed));
	}
	{
		install_project_files_hint_label_ = memnew(Label);
		tool_bar_box->add_child(install_project_files_hint_label_);
		install_project_files_hint_label_->set_text(TTR("Suggest re-installing GodotJS project files."));
	}
	{
		realm_selector_ = memnew(OptionButton);
		tool_bar_box->add_child(realm_selector_);
		realm_selector_->set_theme_type_variation("FlatButton");
		realm_selector_->set_focus_mode(FOCUS_NONE);
		realm_selector_->set_tooltip_text(TTR("Realm to evaluate in"));
		realm_selector_->connect("item_selected", callable_mp(this, &GodotJSREPL::_realm_selected));
		realm_selector_->get_popup()->connect("about_to_popup", callable_mp(this, &GodotJSREPL::_refresh_realms));
	}
#if JSB_USE_TYPESCRIPT
	{
		start_tsc_button_ = memnew(Button);
		tool_bar_box->add_child(start_tsc_button_);
		start_tsc_button_->set_theme_type_variation("FlatButton");
		start_tsc_button_->set_focus_mode(FOCUS_NONE);
		start_tsc_button_->set_tooltip_text(TTR("Start TSC"));
		start_tsc_button_->connect("pressed", callable_mp(this, &GodotJSREPL::_start_tsc_pressed));
	}
#endif

	Panel *output_container = memnew(Panel);
	output_container->set_h_size_flags(SIZE_EXPAND_FILL);
	output_container->set_v_size_flags(SIZE_EXPAND_FILL);
	output_container->set_h_grow_direction(GROW_DIRECTION_BOTH);
	output_container->set_v_grow_direction(GROW_DIRECTION_BOTH);
	vbox->add_child(output_container);

	input_box_ = memnew(LineEdit);
	input_box_->set_h_size_flags(SIZE_EXPAND_FILL);
	input_box_->set_placeholder(TTR("Enter expressions"));
	input_box_->set_clear_button_enabled(true);
	input_box_->set_visible(true);
	input_box_->connect("text_submitted", callable_mp(this, &GodotJSREPL::_input_submitted));
	input_box_->connect("text_changed", callable_mp(this, &GodotJSREPL::_input_changed));
	input_box_->connect("gui_input", callable_mp(this, &GodotJSREPL::_input_gui_input));
	input_box_->connect("focus_exited", callable_mp(this, &GodotJSREPL::_input_focus_exit));
	vbox->add_child(input_box_);

	output_box_ = memnew(RichTextLabel);
	output_box_->set_threaded(true);
	output_box_->set_use_bbcode(true);
	output_box_->set_scroll_follow(true);
	output_box_->set_selection_enabled(true);
	output_box_->set_context_menu_enabled(true);
	output_box_->set_focus_mode(FOCUS_CLICK);
	output_box_->set_deselect_on_focus_loss_enabled(false);
	output_box_->set_v_size_flags(SIZE_EXPAND_FILL);
	output_box_->set_h_size_flags(SIZE_EXPAND_FILL);
	output_box_->set_h_grow_direction(GROW_DIRECTION_BOTH);
	output_box_->set_v_grow_direction(GROW_DIRECTION_BOTH);
	output_box_->set_offsets_preset(PRESET_FULL_RECT);
	output_box_->set_anchors_preset(PRESET_FULL_RECT);
	output_container->add_child(output_box_);

	candidate_list_ = memnew(ItemList);
	candidate_list_->hide();
	candidate_list_->set_focus_mode(FOCUS_NONE);
	candidate_list_->set_mouse_filter(MOUSE_FILTER_IGNORE);
	// TODO: GDExtension: set_disable_visibility_clip not available in godot-cpp, skip
	candidate_list_->set_size(Size2(600, 160));
	output_container->add_child(candidate_list_);

	connect("ready", callable_mp(this, &GodotJSREPL::_on_ready));
	connect("tree_entered", callable_mp(this, &GodotJSREPL::_on_tree_entered));
	connect("theme_changed", callable_mp(this, &GodotJSREPL::_on_theme_changed));

	jsb::Environment::add_disposed_callback(this, [this](jsb::Environment *p_env) {
		if (p_env == selected_realm_) {
			selected_realm_ = nullptr;
			add_line(TTR("-- Selected realm is disposed. --"));
		}
	});
}

GodotJSREPL::~GodotJSREPL() {
	jsb::Environment::remove_disposed_callback(this);

	// ensure self removed before any member destruction to avoid deadlock
	remove_from_output_list();

	// avoid warning due to unhandled strings
	output_backlog_.swap().clear();
}

void GodotJSREPL::_on_window_focus_entered() {
	check_install();
	check_tsc();
}

void GodotJSREPL::_on_tree_entered() {
	_update_theme();
	check_install();
	// _load_state();
}

void GodotJSREPL::_on_theme_changed() {
	_update_theme();
	// _rebuild_log();
}

void GodotJSREPL::_on_ready() {
	if (Node *root = get_tree()->get_root()) {
		root->connect("focus_entered", callable_mp(this, &GodotJSREPL::_on_window_focus_entered));
	}

	_refresh_realms();
}

void GodotJSREPL::_refresh_realms() {
	if (!realm_selector_) return;

	const LocalVector<jsb::Environment *> realms = []() {
		LocalVector<jsb::Environment *> ret;
		for (std::shared_ptr<jsb::Environment> env : jsb::Environment::get_all_environments()) {
			if (!env->is_disposing()) {
				ret.push_back(env.get());
			}
		}
		return ret;
	}();

	int selected_idx = -1;
	int main_idx = -1;
	realm_selector_->clear();
	for (size_t i = 0; i < realms.size(); ++i) {
		const jsb::Environment *realm = realms[i];
		const jsb::Environment::Type type = realm->get_realm_type();
		String label;
		switch (type) {
			case jsb::Environment::Type::Worker:
				label = jsb_format("worker #%d", (int)i);
				break;
			case jsb::Environment::Type::Shadow:
				label = jsb_format("shadow #%d", (int)i);
				break;
			case jsb::Environment::Type::ShadowRealm:
				label = jsb_format("shadow realm #%d", (int)i);
				break;
			case jsb::Environment::Type::Default:
			default:
				label = i == 0 ? TTR("main") : jsb_format("main #%d", (int)i);
				main_idx = i;
				break;
		}
		realm_selector_->add_item(label);
		realm_selector_->set_item_metadata(i, (uint64_t)realm);
		if (realm == selected_realm_) selected_idx = i;
	}

	if (realms.is_empty()) {
		// The language is not initialized yet (or the store is empty): nothing to
		// evaluate in. The selector is not disabled -- a realm can appear later and
		// _on_realm_poll re-enables it -- but eval_source reports the state.
		selected_realm_ = nullptr;
		realm_selector_->set_disabled(true);
		return;
	}
	realm_selector_->set_disabled(false);

	if (selected_idx >= 0) {
		realm_selector_->select(selected_idx);
	} else if (main_idx >= 0) {
		realm_selector_->select(main_idx);
	}
}

void GodotJSREPL::_realm_selected(int p_idx) {
	jsb::Environment *selected = (jsb::Environment *)((uint64_t)realm_selector_->get_item_metadata(p_idx));
	std::shared_ptr<jsb::Environment> selected_env = jsb::Environment::_access(selected);
	if (selected_env.get() == nullptr || selected_env->is_disposing()) {
		add_line(jsb_format("[color=yellow]-- %s %s --[/color]", realm_selector_->get_item_text(p_idx), TTR("is invalid.")));
		return;
	}
	selected_realm_ = selected;
	add_line(jsb_format("[color=dim_gray]-- %s %s --[/color]", TTR("Select realm:"), realm_selector_->get_item_text(p_idx)));
}

Ref<Texture2D> GodotJSREPL::get_editor_theme_icon(const StringName &p_name) const {
	return get_theme_icon(p_name, "EditorIcons");
}

void GodotJSREPL::_update_theme() {
	gc_button_->set_button_icon(get_editor_theme_icon("CollapseTree"));
	clear_button_->set_button_icon(get_editor_theme_icon("Clear"));
	if (generate_types_button_) {
		generate_types_button_->set_button_icon(get_editor_theme_icon("BoxMesh"));
	}
	install_project_files_button_->set_button_icon(get_editor_theme_icon("Window"));
	check_tsc();
}

void GodotJSREPL::check_tsc() {
#if JSB_USE_TYPESCRIPT
	if (GodotJSEditorPlugin *editor_plugin = GodotJSEditorPlugin::get_singleton(); editor_plugin && editor_plugin->is_tsc_watching()) {
		start_tsc_button_->set_button_icon(get_editor_theme_icon("Stop"));
		start_tsc_button_->set_tooltip_text(TTR("Stop tsc"));
	} else {
		start_tsc_button_->set_button_icon(get_editor_theme_icon("Play"));
		start_tsc_button_->set_tooltip_text(TTR("Start tsc (watch)"));
	}
#endif
}

void GodotJSREPL::check_install() {
	do {
		GodotJSEditorPlugin *editor_plugin = GodotJSEditorPlugin::get_singleton();
		if (!editor_plugin) break;
		if (!editor_plugin->verify_ts_project()) break;
		install_project_files_hint_label_->set_visible(false);
		return;
	} while (false);
	install_project_files_hint_label_->set_visible(true);
}

void GodotJSREPL::_gc_pressed() {
	if (!Thread::is_main_thread()) {
		JSB_LOG(Error, "explicit GC must be requested from the main thread.");
		return;
	}
	jsb::Environment::gc();
	add_line("Explicit GC requested");
}

void GodotJSREPL::_clear_pressed() {
	output_box_->clear();
}

void GodotJSREPL::_install_project_files_pressed() {
	GodotJSEditorPlugin::try_install_project_files();
}

void GodotJSREPL::_generate_types_pressed() {
	GodotJSEditorPlugin::generate_types();
}

String GodotJSREPL::encode_string(const String &p_text) {
	return p_text.replace("'", "\\'");
}

static bool is_auto_complete_allowed(const String &p_text) {
#if JSB_REPL_AUTO_COMPLETE
	// a rough rule to allow auto-complete
	return !p_text.is_empty() && !p_text.contains("(");
#else
	return false;
#endif
}

void GodotJSREPL::_input_changed(const String &p_text) {
	// if (input_submitting_) return;

	//TODO we haven't implemented the js function invocation from outside of Realm, just temporarily call as source code eval
	const PackedStringArray results =
			is_auto_complete_allowed(p_text)
			? (PackedStringArray)(Variant)eval_source(jsb_format("require('jsb.editor.main').auto_complete('%s')", encode_string(p_text)))
			: PackedStringArray();
	_show_candidates(results);
}

void GodotJSREPL::_show_candidates(const PackedStringArray &p_items) {
	candidate_list_->clear();
	if (p_items.is_empty()) {
		candidate_list_->hide();
		return;
	}

	for (const String &item : p_items) {
		candidate_list_->add_item(item);
	}
	const Size2 size = candidate_list_->get_size();
	const Vector2 origin = input_box_->get_position();
	const Vector2 input_size = input_box_->get_size();
	const Vector2 pos(origin.x, origin.y - size.y - input_size.y);
	candidate_list_->select(0);
	candidate_list_->set_position(pos);
	candidate_list_->show();
}

void GodotJSREPL::_input_focus_exit() {
	candidate_list_->hide();
}

void GodotJSREPL::_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (!k.is_valid()) return;
	if (!candidate_list_->is_visible()) {
		// fill out the candidate list with history if input is empty
		if (input_box_->get_text().is_empty() && (k->is_action_pressed("ui_text_caret_up", true) || k->is_action_pressed("ui_text_caret_down", true))) {
			_show_candidates(history_);
		}
		return;
	}

	const int item_count = candidate_list_->get_item_count();
	PackedInt32Array selected = candidate_list_->get_selected_items();
	const int current = selected.size() > 0 ? selected[0] : 0;
	if (k->is_action_pressed("ui_text_caret_up", true)) {
		candidate_list_->select(current > 0 ? current - 1 : item_count - 1);
		candidate_list_->ensure_current_is_visible();
		input_box_->accept_event();
	} else if (k->is_action_pressed("ui_text_caret_down", true)) {
		candidate_list_->select(current < item_count - 1 ? current + 1 : 0);
		candidate_list_->ensure_current_is_visible();
		input_box_->accept_event();
	} else if (k->is_action_pressed("ui_focus_next", true)) {
		const String text = candidate_list_->get_item_text(current);
		input_box_->set_text(text);
		candidate_list_->hide();
		input_box_->set_caret_column(text.length());
		input_box_->accept_event();
	}
}

void GodotJSREPL::_input_submitted(const String &p_text) {
	if (p_text.is_empty()) return;

	check_install();
	input_submitting_ = true;
	add_line(p_text);
	input_box_->clear();
	const Variant value = eval_source(p_text);
	add_string(value.get_type() == Variant::NIL ? String("undefined") : value.stringify());
	add_history(p_text);
	input_submitting_ = false;
}

Variant GodotJSREPL::eval_source(const String &p_code) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized()) {
		add_line(TTR("Cannot evaluate: the JavaScript language is not initialized."));
		return {};
	}

	if (!selected_realm_) {
		add_line(TTR("Cannot evaluate: no realm is selected (the JavaScript language is not ready yet)."));
		return {};
	}
	if (selected_realm_->is_disposing()) {
		add_line(TTR("Cannot evaluate: the selected realm has been destroyed."));
		return {};
	}

	Error err = OK;
	const CharString str = p_code.utf8();
	const jsb::JSValueMove result = selected_realm_->eval_source(str.get_data(), str.length(), "eval", err);
	if (err != OK) {
		return {};
	}
	return result.to_variant();
}

void GodotJSREPL::add_line(const String &p_line) {
	output_box_->add_text(p_line);
	output_box_->newline();
}

void GodotJSREPL::add_string(const String &p_str) {
	const PackedStringArray lines = p_str.split("\n", true);
	// const int line_count = lines.size();
	for (const String &line : lines) {
		add_line(line);
	}
}

void GodotJSREPL::_backlog_flush() {
	std::vector<String> &backlog = output_backlog_.swap();
	for (const String &str : backlog) {
		add_string(str);
	}
	backlog.clear();
}

void GodotJSREPL::write(jsb::internal::ELogSeverity::Type p_severity, const String &p_text) {
	jsb_unused(p_severity);
	output_backlog_.add(p_text);
	callable_mp(this, &GodotJSREPL::_backlog_flush).call_deferred();
}

void GodotJSREPL::add_history(const String &p_text) {
	int size = history_.size();
	if (size != 0) {
		const int index = history_.rfind(p_text);
		if (index == size - 1) {
			return;
		}
		if (index != -1) {
			history_.remove_at(index);
			--size;
		}
	}

	history_.append(p_text);
	if (size >= kMaxHistoryCount) {
		history_.remove_at(0);
	}
}

void GodotJSREPL::_start_tsc_pressed() {
	if (GodotJSEditorPlugin *editor_plugin = GodotJSEditorPlugin::get_singleton()) {
		if (editor_plugin->is_tsc_watching()) editor_plugin->kill_tsc();
		else editor_plugin->start_tsc_watch();
		check_tsc();
	}
}
