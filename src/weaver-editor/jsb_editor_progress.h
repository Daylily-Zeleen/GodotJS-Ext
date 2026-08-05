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

