/************************************************************************/
/*  jsb_editor_progress.h                                               */
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

#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/templates/hash_map.hpp"

namespace godot {
class Label;
class ProgressBar;
class MarginContainer;

class EditorProgress {
private:
	const String name;
	int current = 0;

public:
	EditorProgress(const String &p_task_name, int p_total);
	~EditorProgress();

	void step(const String &p_state, int p_step = -1);
};

class EditorProgressDialog : public Window {
private:
	MarginContainer *main;
	Label *title_label;
	ProgressBar *progress_bar;

	HashMap<String, int> tasks; // task_name -> total steps

	static EditorProgressDialog *singleton;

private:
	void update_internal(const String &p_name, const String &p_state, int p_total, int p_current);

public:
	void add(const String &p_task_name, int p_total);
	void update(const String &p_task_name, const String &p_state, int p_current);
	void finish(const String &p_task_name);

public:
	EditorProgressDialog();
	~EditorProgressDialog();

	static EditorProgressDialog *get_singleton() { return singleton; }
};

}; //namespace godot
