/************************************************************************/
/*  register_editor_types.h                                             */
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

#include <godot_cpp/godot.hpp>

void _initialize_godotjs_editor_module(godot::ModuleInitializationLevel p_level);
void _uninitialize_godotjs_editor_module(godot::ModuleInitializationLevel p_level);
#if defined(JSB_TESTS_ENABLED) && defined(TOOLS_ENABLED)
// Editor doctest entry (--jsb-run-tests). Declared here so the single
// real startup callback (runtime jsb_startup) can forward to it while the
// build is still one extension library: jsb_editor_library_init is only used
// post P4, and godot-cpp's register_startup_callback is a single slot anyway
// (startup_func assignment overwrites, no chaining).
void _editor_tests_startup();
#endif