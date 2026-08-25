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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "register_editor_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

#include "internal/jsb_settings.h"
#include "common/internal/jsb_class_visibility.h"
#include "../common/tests/jsb_test_runner.h"
#include "weaver-editor/jsb_weaver_editor.h"
#include "api_tool/api_tool.h"

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

#ifdef JSB_TESTS_ENABLED
// Editor-side doctest entry (--jsb-run-editor-tests). While the build is a
// single extension library this is forwarded to from the runtime's
// jsb_startup() (the only registered startup callback); post P4 it becomes
// the editor library's own startup callback. Must NOT be wired to the
// MODULE_INITIALIZATION_LEVEL_EDITOR initializer: at that stage the main
// loop does not exist yet, so SceneTree::quit() is unreachable (see
// TASK_STATUS.md 9.3).
void _editor_tests_startup() {
	jsb::testing::try_run("--jsb-run-editor-tests");
}
#endif // JSB_TESTS_ENABLED


extern "C" {
GDExtensionBool GDE_EXPORT jsb_editor_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

#ifdef JSB_TESTS_ENABLED
	// Editor doctest suite entry (see _editor_tests_startup above).
	init_obj.register_startup_callback(_editor_tests_startup);
#endif

	init_obj.register_initializer(_initialize_godotjs_editor_module);
	init_obj.register_terminator(_uninitialize_godotjs_editor_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_EDITOR);

	return init_obj.init();
}
}
