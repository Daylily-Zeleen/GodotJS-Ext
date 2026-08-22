/************************************************************************/
/*  jsb_statistics_viewer.h                                             */
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

#include "runtime/compat/jsb_compat.h"
#include "runtime/impl/shared/jsb_custom_field.h"
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
