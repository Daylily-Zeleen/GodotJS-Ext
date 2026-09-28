/************************************************************************/
/*  register_editor_types.cpp                                           */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "register_editor_types.h"

#include <godot_cpp/godot.hpp>

#include "api_tool/api_tool.h"
#include "internal/jsb_class_visibility.h"
#include "internal/jsb_settings.h"
#include "weaver-editor/jsb_weaver_editor.h"

using namespace godot;

void _initialize_godotjs_editor_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	// P1 之后 codegen 为纯 C++（jsb::codegen），GodotJSEditorHelper 已删除，
	// 无需再为 api store 的 JS 反射注册 exposed 类。
	GDREGISTER_INTERNAL_CLASS(GodotJSExportPlugin);
	GDREGISTER_INTERNAL_CLASS(GodotJSEditorPlugin);
	EditorPlugins::add_by_type<GodotJSEditorPlugin>();
}

void _uninitialize_godotjs_editor_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	EditorPlugins::remove_by_type<GodotJSEditorPlugin>();
}
