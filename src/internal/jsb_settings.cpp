/************************************************************************/
/*  jsb_settings.cpp                                                    */
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

#include "jsb_settings.h"
#include "compat/project_settings.h"
#include "jsb_macros.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef TOOLS_ENABLED
#	include <compat/editor_settings.h>
#endif // TOOLS_ENABLED

namespace jsb::internal::settings {
String get_project_data_dir_name() {
	bool use_hidden = GLOBAL_GET("application/config/use_hidden_project_data_directory");
	return use_hidden ? ".godot" : "godot";
}

String get_jsb_out_dir_name() {
	return get_project_data_dir_name().path_join(JSB_MODULE_NAME_STRING);
}

String get_jsb_out_res_path() {
	return "res://" + get_jsb_out_dir_name();
}

namespace project {
bool is_camel_case_bindings_enabled() {
	return GLOBAL_GET(kRtCamelCaseBindingsEnabled);
}

} //namespace project

namespace editor {
String get_indentation() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		if (!!EDITOR_GET("text_editor/behavior/indent/type")) {
			return String(" ").repeat(EDITOR_GET("text_editor/behavior/indent/size"));
		}
	}
#endif
	return "\t";
}
} //namespace editor

} //namespace jsb::internal::settings
