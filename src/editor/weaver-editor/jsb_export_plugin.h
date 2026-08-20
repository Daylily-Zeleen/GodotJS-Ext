/************************************************************************/
/*  jsb_export_plugin.h                                                 */
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

#include "jsb_editor_pch.h"
#include <godot_cpp/classes/editor_export_platform.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>

namespace jsb {
class Environment;
}

// improve the pipeline of using typescripts
class GodotJSExportPlugin : public EditorExportPlugin {
	GDCLASS(GodotJSExportPlugin, EditorExportPlugin)

protected:
	static void _bind_methods();

public:
	GodotJSExportPlugin();
	virtual String _get_name() const override;
	virtual bool _supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override;

	static const HashSet<String> &get_ignored_paths();

	virtual void _export_begin(const PackedStringArray &p_features, bool p_debug, const String &p_path, uint32_t p_flags) override;
	virtual void _export_file(const String &p_path, const String &p_type, const PackedStringArray &p_features) override;

	virtual PackedStringArray _get_export_features(const Ref<EditorExportPlatform> &p_export_platform, bool p_debug) const override;

private:
	bool export_compiled_script(const String &p_path, bool p_remap);
	bool export_module_files(const jsb::JavaScriptModule &p_module, bool p_remap);
	bool export_raw_file(const String &p_path, bool p_remap);
	void export_raw_files(const PackedStringArray &p_paths, bool p_permit_typescript);
	void get_script_resources(const String &p_dir, PackedStringArray &r_list, bool p_is_node_module = false);

#if JSB_WITH_NODE
	// package the standalone node native-probe helper executable (used as the execPath of `child_process.fork`)
	bool add_node_runtime_helpers(const PackedStringArray &p_features);
	bool add_node_runtime_helper_shared_object(const String &p_res_path, const PackedStringArray &p_features);
	bool stage_macos_helper_framework(const String &p_res_path);
#endif // JSB_WITH_NODE

	HashSet<String> exported_paths_;
	std::shared_ptr<jsb::Environment> env_;
};
