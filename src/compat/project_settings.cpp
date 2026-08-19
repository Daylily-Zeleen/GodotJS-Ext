/************************************************************************/
/*  project_settings.cpp                                                */
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

#include "project_settings.h"

namespace godot {

Variant _GLOBAL_DEF(const String &p_var, const Variant &p_default, bool p_restart_if_changed, bool p_ignore_value_in_docs, bool p_basic, bool p_internal) {
	Variant ret;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	ERR_FAIL_NULL_V_MSG(settings, p_default, "ProjectSettings not instantiated yet.");
	if (!settings->has_setting(p_var)) {
		settings->set(p_var, p_default);
	}
	ret = GLOBAL_GET(p_var);

	settings->set_initial_value(p_var, p_default);
	// settings->set_builtin_order(p_var);
	settings->set_as_basic(p_var, p_basic);
	settings->set_restart_if_changed(p_var, p_restart_if_changed);
	// settings->set_ignore_value_in_docs(p_var, p_ignore_value_in_docs);
	settings->set_as_internal(p_var, p_internal);
	return ret;
}

Variant _GLOBAL_DEF(const PropertyInfo &p_info, const Variant &p_default, bool p_restart_if_changed, bool p_ignore_value_in_docs, bool p_basic, bool p_internal) {
	Variant ret = _GLOBAL_DEF(p_info.name, p_default, p_restart_if_changed, p_ignore_value_in_docs, p_basic, p_internal);
	Dictionary info = p_info.operator Dictionary();
	info.erase("usage");
	ProjectSettings::get_singleton()->add_property_info(info);
	return ret;
}

} //namespace godot