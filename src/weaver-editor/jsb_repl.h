/************************************************************************/
/*  jsb_repl.h                                                          */
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

#pragma once
#include "jsb_editor_pch.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>

class GodotJSREPL : public HBoxContainer, public jsb::internal::IConsoleOutput {
	struct OutputLine {
		String text;
	};

private:
	bool input_submitting_;
	LineEdit *input_box_;
	RichTextLabel *output_box_;

	Button *clear_button_;
	Button *gc_button_;
	Button *generate_types_button_ = nullptr;
	Button *install_project_files_button_;
	Label *install_project_files_hint_label_;
	Button *start_tsc_button_ = nullptr;

	ItemList *candidate_list_;

	Vector<OutputLine> lines_;

	enum { kMaxHistoryCount = 10 };
	PackedStringArray history_;

	jsb::internal::DoubleBuffered<String> output_backlog_;

private:
	Ref<Texture2D> get_editor_theme_icon(const StringName &p_name) const;
	void _update_theme();

	void _on_tree_entered();
	void _on_ready();
	void _on_theme_changed();
	void _on_window_focus_entered();

protected:
	static void _bind_methods();

	void _input_submitted(const String &p_text);
	void _input_changed(const String &p_text);
	void _input_gui_input(const Ref<InputEvent> &p_event);
	void _input_focus_exit();
	void _clear_pressed();
	void _gc_pressed();
	void _generate_types_pressed();
	void _install_project_files_pressed();
	void _start_tsc_pressed();
	void _show_candidates(const PackedStringArray &p_items);
	void _backlog_flush();

	void add_string(const String &p_str);
	void add_line(const String &p_line);
	void add_history(const String &p_text);
	jsb::JSValueMove eval_source(const String &p_code);
	String encode_string(const String &p_text);
	void check_install();
	void check_tsc();

public:
	GodotJSREPL();
	virtual ~GodotJSREPL() override;

	void write(jsb::internal::ELogSeverity::Type p_severity, const String &p_text) override;
};
