/************************************************************************/
/*  jsb_export_plugin.cpp                                               */
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

#include "jsb_export_plugin.h"

#include "jsb_editor_bridge.h"
#include "../jsb_editor_settings.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#if JSB_WITH_NODE
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/project_settings.hpp>
#endif // JSB_WITH_NODE

#include <compat/misc.h>

#include "api_tool/editor/api_tool_editor.h"
#define JSB_EXPORTER_LOG(Severity, Format, ...) JSB_LOG_IMPL(JSExporter, Severity, Format, ##__VA_ARGS__)

#if JSB_WITH_NODE
namespace {
constexpr const char *kNodeRuntimeBinDir = "res://addons/godotjs-ext.daylily-zeleen/bin";
constexpr const char *kNodeRuntimeHelperName = "godotjs-ext";

String node_runtime_helper_res_path(const String &p_platform) {
	const String exe_name = String(kNodeRuntimeHelperName) + (p_platform == "windows" ? ".exe" : "");
	return String(kNodeRuntimeBinDir).path_join(p_platform).path_join(exe_name);
}

bool is_node_runtime_helper_path(const String &p_path) {
	const String prefix = String(kNodeRuntimeBinDir) + "/";
	if (!p_path.begins_with(prefix)) {
		return false;
	}
	const String file_name = p_path.get_file();
	return file_name == kNodeRuntimeHelperName || file_name == String(kNodeRuntimeHelperName) + ".exe";
}
} //namespace
#endif // JSB_WITH_NODE

const HashSet<String> &GodotJSExportPlugin::get_ignored_paths() {
	static const HashSet<String> ignored_paths{
		"res://jsconfig.json",
		"res://tsconfig.json",
		"res://package.json",
		"res://package-lock.json"
	};
	return ignored_paths;
}

GodotJSExportPlugin::GodotJSExportPlugin() {
}

PackedStringArray GodotJSExportPlugin::_get_export_features(const Ref<EditorExportPlatform> &p_export_platform, bool p_debug) const {
	// if (FileAccess::file_exists("res://tsconfig.json"))
	// {
	//     return { "typescript" };
	// }
	return {};
}

void GodotJSExportPlugin::export_raw_files(const PackedStringArray &p_paths, bool p_permit_typescript) {
	for (const String &file_path : p_paths) {
		// in this situation, we do not call `load module` to avoid unexpected side effects
		// (for example, it's impossible to directly load worker scripts in main env).

		if (!file_path.ends_with("." JSB_TYPESCRIPT_EXT)) {
			export_raw_file(file_path, false);
		} else if (p_permit_typescript) {
			const String compiled_script_path = jsb::internal::PathUtil::convert_typescript_path(file_path);
			export_raw_file(compiled_script_path, true);
		}
	}
}

void GodotJSExportPlugin::get_script_resources(const String &p_dir, PackedStringArray &r_list, bool p_is_node_module) {
	Ref<DirAccess> dir = DirAccess::open(p_dir);

	if (!dir.is_valid()) {
		JSB_EXPORTER_LOG(Warning, "Could not open explicit script directory for traversal: %s", p_dir);
		return;
	}

	dir->list_dir_begin();
	String filename = dir->get_next();

	while (!filename.is_empty()) {
		if (filename == "." || filename == ".." || (p_is_node_module && filename == "node_modules")) {
			filename = dir->get_next();
			continue;
		}

		String path = p_dir.path_join(filename);

		if (dir->current_is_dir()) {
			get_script_resources(path, r_list, p_is_node_module);
		} else if (ResourceLoader::get_singleton()->exists(path, "GodotJSScript") && !get_ignored_paths().has(path)) {
			r_list.push_back(path);
		}

		filename = dir->get_next();
	}

	dir->list_dir_end();
}

// p_path is the exported package full path (like X:/Folder1/Folder2/test.zip)
void GodotJSExportPlugin::_export_begin(const PackedStringArray &p_features, bool p_debug, const String &p_path, uint32_t p_flags) {
	JSB_EXPORTER_LOG(Verbose, "export_begin path: %s", p_path);
	exported_paths_.clear();

	// add all explicitly included file paths in settings
	const PackedStringArray file_paths = jsb::internal::get_packaging_include_files();
	export_raw_files(file_paths, true);

	// add all explicitly included directory paths
	const PackedStringArray dir_paths = jsb::internal::get_packaging_include_directories();
	for (const String &dir_path : dir_paths) {
		PackedStringArray script_paths;
		get_script_resources(dir_path, script_paths);
		export_raw_files(script_paths, true);
	}

	// add api data files
	for (const String &file_path : api_tool::get_api_data_files(true, false)) {
		export_raw_file(file_path, false);
	}

#if JSB_WITH_NODE
	// package the native-probe helper executable (used as the execPath of `child_process.fork`).
	// the node runtime library itself (e.g. node.dll) is referenced by the .gdextension
	// `[dependencies]` table and is packed automatically by Godot.
	add_node_runtime_helpers(p_features);
#endif
}

bool GodotJSExportPlugin::export_raw_file(const String &p_path, bool p_remap) {
	if (exported_paths_.has(p_path)) {
		return true;
	}
	const PackedByteArray content = FileAccess::get_file_as_bytes(p_path);
	if (FileAccess::get_open_error()) {
		return false;
	}
	exported_paths_.insert(p_path);
	add_file(p_path, content, p_remap);
	JSB_EXPORTER_LOG(Verbose, "include raw: %s", p_path);
	return true;
}

bool GodotJSExportPlugin::export_compiled_script(const String &p_path, bool p_remap) {
	static constexpr char kNodeModulesPrefix[] = "res://node_modules/";

	if (p_path.is_empty() || exported_paths_.has(p_path)) {
		return false;
	}
	if (!p_path.begins_with("res://")) {
		JSB_EXPORTER_LOG(Warning, "can not export external source: %s", p_path);
		return false;
	}
	if (jsb::internal::is_packaging_referenced_node_modules() && p_path.begins_with(kNodeModulesPrefix)) {
		// Node modules may dynamically require files within themselves, and thus these modules won't end up in our
		// module's "children" array. The kRtPackagingReferencedNodeModules setting (on by default) allows us to play
		// it safe and export all JS scripts found in referenced packages. However, this won't cover the case where
		// entirely new packages are dynamically imported. kRtPackagingIncludeDirectories must be used to handle that
		// case.
		int package_path_slash_index = p_path.find(String("/"), sizeof(kNodeModulesPrefix) - 1);

		if (p_path[sizeof(kNodeModulesPrefix) - 1] == '@' && package_path_slash_index >= 0) {
			package_path_slash_index = p_path.find(String("/"), package_path_slash_index + 1);
		}

		String package_path = p_path.substr(0, package_path_slash_index);
		PackedStringArray script_paths;
		get_script_resources(package_path, script_paths, true);

		const String package_json_path = jsb::internal::PathUtil::combine(package_path, "package.json");

		if (FileAccess::file_exists(package_json_path)) {
			script_paths.append(package_json_path);
		}

		export_raw_files(script_paths, false);
		return true;
	}

	// export dependent files.
	// force module loading. ensure the module hierarchy available.
	if (const jsb::JsbBridgeTable *bridge = jsb::editor::EditorBridge::get_bridge();
			bridge != nullptr && bridge->get_module_source_info != nullptr) {
		// source + optional package.json of THIS module
		Variant source_info;
		const CharString path_utf8 = p_path.utf8();
		godot::Error qerr = bridge->get_module_source_info(path_utf8.get_data(), path_utf8.length(), source_info._native_ptr());
		if (qerr == OK && source_info.get_type() == Variant::DICTIONARY) {
			Dictionary info = source_info;
			export_raw_file(info.get("source", String()), p_remap);
			const String package_path = info.get("package", String());
			if (!package_path.is_empty()) {
				export_raw_file(package_path, false);
			}
		}

		// one-level dependencies (recursion happens through this very function)
		if (bridge->get_module_direct_dependencies != nullptr) {
			Variant deps_var;
			qerr = bridge->get_module_direct_dependencies(path_utf8.get_data(), path_utf8.length(), deps_var._native_ptr());
			if (qerr == OK && deps_var.get_type() == Variant::PACKED_STRING_ARRAY) {
				for (const String &filename : (PackedStringArray)deps_var) {
					if (export_compiled_script(filename, false)) {
						JSB_EXPORTER_LOG(Verbose, "export dependent source: %s", filename);
					}
				}
			}
		}
	} else {
		JSB_EXPORTER_LOG(Warning, "runtime bridge is not available for module: %s", p_path);
	}
	return true;
}

void GodotJSExportPlugin::_export_file(const String &p_path, const String &p_type, const PackedStringArray &p_features) {
	//TODO when exporting for web.impl, need to reorganize all scripts into a monolithic script (like webpack)? and preload it before everything get run.

#if JSB_WITH_NODE
	// the native-probe helper is packaged explicitly in `_export_begin` (add_shared_object /
	// add_macos_plugin_file), skip it here so it isn't duplicated into the export.
	if (is_node_runtime_helper_path(p_path)) {
		skip();
		JSB_EXPORTER_LOG(Verbose, "node runtime: skip helper binary: %s", p_path);
		return;
	}
#endif

	if (p_path.ends_with("." JSB_TYPESCRIPT_EXT)) {
		const String compiled_script_path = jsb::internal::PathUtil::convert_typescript_path(p_path);
		export_compiled_script(compiled_script_path, true);

		// always skip the typescript source from packing
		JSB_EXPORTER_LOG(Verbose, "export source: %s => %s", p_path, compiled_script_path);
	} else {
		if (get_ignored_paths().has(p_path)) {
			skip();
			JSB_EXPORTER_LOG(Verbose, "ignored: %s", p_path);
		}

		//TODO handle module deps if it's a .js file ?
		// if (p_path.ends_with("." JSB_JAVASCRIPT_EXT))
		// {
		//     export_compiled_script(p_path);
		// }
	}
}

String GodotJSExportPlugin::_get_name() const {
	return jsb_typename(GodotJSExportPlugin);
}

bool GodotJSExportPlugin::_supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const {
	//TODO
	JSB_EXPORTER_LOG(VeryVerbose, "GodotJSExportPlugin::_supports_platform( %s )", p_export_platform.is_valid() ? p_export_platform->get_class() : String("null"));
	return true;
}

void GodotJSExportPlugin::_bind_methods() {}

#if JSB_WITH_NODE
bool GodotJSExportPlugin::add_node_runtime_helpers(const PackedStringArray &p_features) {
	// resolve the target platform from the export features
	String platform;
	for (const String &feature : p_features) {
		if (feature == "windows" || feature == "linux" || feature == "macos" || feature == "ios" || feature == "android") {
			platform = feature;
			break;
		}
	}
	if (platform.is_empty()) {
		JSB_EXPORTER_LOG(Warning, "node runtime: unrecognized export features, skip packaging the native probe helper.");
		return false;
	}

	if (platform == "android" || platform == "ios") {
		// node runs in-process on mobile platforms: no standalone helper executable is required.
		return true;
	}

	const String helper_path = node_runtime_helper_res_path(platform);
	if (!FileAccess::file_exists(helper_path)) {
		JSB_EXPORTER_LOG(Error, "node runtime: the bundled native probe helper '%s' was not found. Rebuild with use_node=yes (which produces bin/%s/godotjs-ext) or reinstall the addon.", helper_path, platform);
		return false;
	}

	if (platform == "macos") {
		return stage_macos_helper_framework(helper_path);
	}
	return add_node_runtime_helper_shared_object(helper_path, p_features);
}

bool GodotJSExportPlugin::add_node_runtime_helper_shared_object(const String &p_res_path, const PackedStringArray &p_features) {
	const String globalized_path = ProjectSettings::get_singleton()->globalize_path(p_res_path);
	const String target = p_res_path.get_base_dir().trim_prefix("res://");
	add_shared_object(globalized_path, p_features, target);
	JSB_EXPORTER_LOG(Verbose, "node runtime: add_shared_object( %s ) target: %s", globalized_path, target);
	return true;
}

bool GodotJSExportPlugin::stage_macos_helper_framework(const String &p_res_path) {
	const String framework_dir = ProjectSettings::get_singleton()->globalize_path("user://.godotjs-ext/export/godotjs-ext.framework");
	const String resources_dir = framework_dir.path_join("Resources");
	const String staged_helper = framework_dir.path_join(kNodeRuntimeHelperName);

	if (DirAccess::make_dir_recursive_absolute(resources_dir) != OK) {
		JSB_EXPORTER_LOG(Error, "node runtime: failed to create macOS framework staging directory: %s", resources_dir);
		return false;
	}

	// copy the helper executable into the framework root
	const PackedByteArray helper_bytes = FileAccess::get_file_as_bytes(p_res_path);
	if (FileAccess::get_open_error()) {
		JSB_EXPORTER_LOG(Error, "node runtime: failed to read the native probe helper '%s'.", p_res_path);
		return false;
	}
	{
		Ref<FileAccess> file = FileAccess::open(staged_helper, FileAccess::WRITE);
		if (file.is_null()) {
			JSB_EXPORTER_LOG(Error, "node runtime: failed to write the staged helper executable: %s", staged_helper);
			return false;
		}
		file->store_buffer(helper_bytes);
	}

	// write the framework Info.plist
	{
		Ref<FileAccess> file = FileAccess::open(resources_dir.path_join("Info.plist"), FileAccess::WRITE);
		if (file.is_null()) {
			JSB_EXPORTER_LOG(Error, "node runtime: failed to write the framework Info.plist: %s", resources_dir.path_join("Info.plist"));
			return false;
		}
		String plist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		plist += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
		plist += "<plist version=\"1.0\">\n";
		plist += "<dict>\n";
		plist += "\t<key>CFBundleDevelopmentRegion</key>\n\t<string>en</string>\n";
		plist += "\t<key>CFBundleExecutable</key>\n\t<string>";
		plist += kNodeRuntimeHelperName;
		plist += "</string>\n";
		plist += "\t<key>CFBundleIdentifier</key>\n\t<string>com.godotjs.ext.node</string>\n";
		plist += "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n";
		plist += "\t<key>CFBundleName</key>\n\t<string>";
		plist += kNodeRuntimeHelperName;
		plist += "</string>\n";
		plist += "\t<key>CFBundlePackageType</key>\n\t<string>FMWK</string>\n";
		plist += "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n";
		plist += "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n";
		plist += "\t<key>LSMinimumSystemVersion</key>\n\t<string>10.15</string>\n";
		plist += "</dict>\n";
		plist += "</plist>\n";
		file->store_string(plist);
	}

	// make the helper executable (the framework binary must be executable inside the .app bundle)
	OS::get_singleton()->execute("chmod", PackedStringArray{ "755", staged_helper });

	add_macos_plugin_file(framework_dir);
	JSB_EXPORTER_LOG(Verbose, "node runtime: registered macOS plugin framework: %s", framework_dir);
	return true;
}
#endif // JSB_WITH_NODE
