/************************************************************************/
/*  jsb_settings.h                                                      */
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

#pragma once

#include "compat/jsb_compat.h"
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "jsb_macros.h"

namespace jsb::internal::settings {

static constexpr char kRtCamelCaseBindingsEnabled[] = JSB_MODULE_NAME_STRING "/runtime/core/camel_case_bindings_enabled";

static constexpr char kEditorDebuggerPort[] = JSB_MODULE_NAME_STRING "/debugger/editor_port";

static constexpr uint16_t DEFAULT_EDITOR_DEBUGGER_PORT = 9230;

String get_project_data_dir_name();
static constexpr char js_files_filter[] = "*." JSB_JAVASCRIPT_EXT ",*." JSB_COMMONJS_EXT ",*." JSB_MODULE_EXT
#if JSB_USE_TYPESCRIPT
										  ",*." JSB_TYPESCRIPT_EXT
#endif
		;

String get_jsb_out_dir_name();
String get_jsb_out_res_path();

namespace project {

bool is_camel_case_bindings_enabled();
} //namespace project

namespace editor {
String get_indentation();
} //namespace editor

} //namespace jsb::internal::settings
