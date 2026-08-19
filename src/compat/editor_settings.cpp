/************************************************************************/
/*  editor_settings.cpp                                                 */
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

#ifdef TOOLS_ENABLED

#	include "editor_settings.h"

#	include <godot_cpp/classes/engine.hpp>

namespace godot {

Ref<EditorSettings> get_editor_settings() {
	if (!Engine::get_singleton()->is_editor_hint()) return nullptr;

	if (EditorInterface *ei = EditorInterface::get_singleton()) {
		return ei->get_editor_settings();
	}
	return nullptr;
}

Variant EDITOR_GET(const String &p_setting) {
	if (Ref<EditorSettings> settings = get_editor_settings(); settings.is_valid()) {
		if (settings->has_setting(p_setting)) {
			return settings->get_setting(p_setting);
		}
	}
	return Variant();
}

Variant _EDITOR_DEF(const String &p_setting, const Variant &p_default, bool p_restart_if_changed, bool p_basic) {
	Ref<EditorSettings> editor_settings = get_editor_settings();
	ERR_FAIL_NULL_V_MSG(editor_settings.ptr(), p_default, "EditorSettings not instantiated yet.");

	Variant ret = p_default;
	if (editor_settings->has_setting(p_setting)) {
		ret = EDITOR_GET(p_setting);
	} else {
		bool blocking_signals = editor_settings->is_blocking_signals();
		editor_settings->set_block_signals(true);
		editor_settings->set_setting(p_setting, p_default);
		editor_settings->set_block_signals(blocking_signals);
	}
	// editor_settings->set_restart_if_changed(p_setting, p_restart_if_changed); // TODO: EditorSettings 未暴露该接口
	// editor_settings->set_basic(p_setting, p_basic);

	editor_settings->set_initial_value(p_setting, p_default, false);
	return ret;
}

} //namespace godot

#endif