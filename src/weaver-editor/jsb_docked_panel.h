#ifndef GODOTJS_DOCKED_PANEL_H
#define GODOTJS_DOCKED_PANEL_H

#include "jsb_editor_pch.h"

#include <godot_cpp/classes/editor_dock.hpp>
#include <godot_cpp/classes/tab_container.hpp>

class GodotJSDockedPanel : public EditorDock {
private:
	TabContainer *tabs;

public:
	GodotJSDockedPanel();
	virtual ~GodotJSDockedPanel() override;

private:
	void on_tab_changed(int p_tab_index);
};

#endif
