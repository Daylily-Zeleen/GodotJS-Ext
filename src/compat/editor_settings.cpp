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

#	include <godot_cpp/classes/editor_paths.hpp>
#	include <godot_cpp/classes/config_file.hpp>
#	include <godot_cpp/classes/engine.hpp>
#	include <godot_cpp/classes/file_access.hpp>
#	include <godot_cpp/classes/resource_loader.hpp>

namespace godot {

Ref<EditorSettings> get_editor_settings() {
	if (!Engine::get_singleton()->is_editor_hint()) return nullptr;

	if (EditorInterface *ei = EditorInterface::get_singleton()) {
		return ei->get_editor_settings();
	}
	return nullptr;
}

static Ref<EditorSettings> try_load_editor_settings() {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (!ei) return {};

	// Resolve the editor settings file path via EditorPaths.
	// EditorPaths is created before GDExtension init in the editor build.
	EditorPaths *paths = ei->get_editor_paths();
	if (!paths) return {};

	Ref<ConfigFile> extra_config = memnew(ConfigFile);
	if (paths->is_self_contained()) {
		Error err = extra_config->load(paths->get_self_contained_file());
		if (err != OK) {
			ERR_PRINT("Can't load extra config from path: " + paths->get_self_contained_file());
		}
	}

	const Dictionary version_info = Engine::get_singleton()->get_version_info();
	auto get_existing_settings_path = [&]() {
		const String config_dir = paths->get_config_dir();
		int minor = version_info["minor"];
		String filename;

		do {
			if (GODOT_VERSION_MAJOR == 4 && minor < 3) {
				// Minor version is used since 4.3, so special case to load older settings.
				filename = vformat("editor_settings-%d.tres", version_info["major"]);
				minor = -1;
			} else {
				filename = vformat("editor_settings-%d.%d.tres", version_info["major"], minor);
				minor--;
			}
		} while (minor >= 0 && !FileAccess::file_exists(config_dir.path_join(filename)));
		return config_dir.path_join(filename);
	};

	// Validate editor config file.

	String config_file_path = get_existing_settings_path();
	if (!FileAccess::file_exists(config_file_path)) {
		config_file_path = [&]() {
			const String config_file_name = vformat("editor_settings-%d.%d.tres", version_info["major"], version_info["minor"]);
			return paths->get_config_dir().path_join(config_file_name);
		}();
	}

	if (ResourceLoader::get_singleton()->exists(config_file_path, EditorSettings::get_class_static())) {
		return ResourceLoader::get_singleton()->load(config_file_path, EditorSettings::get_class_static());
	}
	return {};
}

Variant EDITOR_GET(const String &p_setting, const Variant &p_default) {
	// Primary path: formal singleton from the editor.
	if (Ref<EditorSettings> es = get_editor_settings(); es.is_valid()) {
		if (es->has_setting(p_setting)) {
			return es->get_setting(p_setting);
		}
		return p_default;
	}

	// Fallback: temporary instance for read-only access when the editor
	// subsystem is present but the formal singleton isn't ready yet.
	if (Ref<EditorSettings> es = try_load_editor_settings(); es.is_valid()) {
		if (es->has_setting(p_setting)) {
			return es->get_setting(p_setting);
		}
	}
	return p_default;
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