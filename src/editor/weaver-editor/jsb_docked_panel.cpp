/************************************************************************/
/*  jsb_docked_panel.cpp                                                */
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

#include "jsb_docked_panel.h"
#include "jsb_editor_pch.h"
#include "jsb_repl.h"
#include "jsb_statistics_viewer.h"

#include <godot_cpp/classes/margin_container.hpp>

#include <runtime/compat/misc.h>
namespace {
static constexpr int kTabREPL = 0;
static constexpr int kTabViewer = 1;
} //namespace

GodotJSDockedPanel::GodotJSDockedPanel() {
	set_name(TTR("GodotJS"));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);

	MarginContainer *margin = memnew(MarginContainer);
	add_child(margin);

	tabs = memnew(TabContainer);
	margin->add_child(tabs);
	tabs->connect("tab_changed", callable_mp(this, &GodotJSDockedPanel::on_tab_changed));

	{
		jsb_check(tabs->get_tab_count() == kTabREPL);
		GodotJSREPL *repl = memnew(GodotJSREPL);
		repl->set_name(TTR("REPL"));
		tabs->add_child(repl);
	}
	{
		jsb_check(tabs->get_tab_count() == kTabViewer);
		GodotJSStatisticsViewer *viewer = memnew(GodotJSStatisticsViewer);
		viewer->set_name(TTR("Statistics"));
		tabs->add_child(viewer);
	}
}

GodotJSDockedPanel::~GodotJSDockedPanel() {
}

void GodotJSDockedPanel::on_tab_changed(int p_tab_index) {
	GodotJSStatisticsViewer *viewer = (GodotJSStatisticsViewer *)tabs->get_tab_control(kTabViewer);
	viewer->activate(p_tab_index == kTabViewer);
}
