#pragma once

#include "../compat/jsb_compat.h"
#include "../impl/shared/jsb_custom_field.h"

#include <godot_cpp/classes/timer.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

class GodotJSStatisticsViewer : public VBoxContainer {
private:
	Tree *tree;
	TreeItem *tree_root;
	Timer *timer;

public:
	GodotJSStatisticsViewer();
	virtual ~GodotJSStatisticsViewer() override;

	void activate(bool p_active);

private:
	void on_timer();
	void add_row(int p_index, const jsb::impl::CustomField &p_field);
	void add_row(int p_index, const String &p_name, const String &p_text);
};
