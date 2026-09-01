/************************************************************************/
/*  api_tool_editor.cpp                                                 */
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

#include "api_tool_editor.h"
#include "api_tool/api_tool.h"
#include "api_tool/core/api_tool_payload.h"
#include "api_tool_generator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace api_tool {
using namespace internal;

static String get_api_dumping_dir() {
	String ret;
	get_api_dumping_dir(&ret);
	return ret;
}

using PayloadReader = internal::ApiToolPayload<true>;
namespace DocLoader {
static void deserialize_property_info(PayloadReader &r, PropertyInfo &pi) {
	r.read(pi.type);
	r.read(pi.name);
	r.read(pi.class_name);
	r.read(pi.hint);
	r.read(pi.hint_string);
	r.read(pi.usage);
}
// ============================================================================
// ApiStoreReader: Class Document
// ============================================================================
static void deserialize_signal_document(PayloadReader &r, ApiSignalDocument &d) {
	r.read(d.name);
	r.read(d.description);
	r.read(d.arguments, deserialize_property_info);
}

static void deserialize_name_description_document(PayloadReader &r, ApiOperatorDocument &d) {
	r.read(d.name);
	r.read(d.description);
}

static void deserialize_enum_document(PayloadReader &r, ApiEnumDocument &d) {
	r.read(d.name);
	r.read(d.values, deserialize_name_description_document);
}

static void deserialize_constructor_document(PayloadReader &r, ApiConstructorDocument &d) {
	r.read(d.description);
}

Error read_document(const godot::String &p_path, ApiClassDocument &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.name);
	r.read(r_data.brief_description);
	r.read(r_data.description);
	r.read(r_data.methods, deserialize_name_description_document);
	r.read(r_data.signals, deserialize_signal_document);
	r.read(r_data.properties, deserialize_name_description_document);
	r.read(r_data.enums, deserialize_enum_document);
	r.read(r_data.constants, deserialize_name_description_document);
	r.read(r_data.operators, deserialize_name_description_document);
	r.read(r_data.constructors, deserialize_constructor_document);
	return OK;
}

Error read_utility_function_document(const godot::String &p_path, ApiUtilityFunctionDocument &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.name);
	r.read(r_data.description);
	return OK;
}

Error read_global_enum_document(const godot::String &p_path, ApiGlobalEnumDocument &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.name);
	r.read(r_data.values, deserialize_name_description_document);
	return OK;
}

Error read_global_constant_document(const godot::String &p_path, ApiGlobalConstantDocument &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.name);
	r.read(r_data.description);
	return OK;
}

}; //namespace DocLoader

// ============================================================================
// Document query interface implementation (delegate to loader, no cache)
// ============================================================================

// ============================================================================
// Document queries (no cache, direct file read, TOOLS_ENABLED only)
// ============================================================================

std::unique_ptr<ApiClassDocument> find_document(const StringName &p_name) {
	String base_dir = get_api_dumping_dir();
	if (base_dir.is_empty()) return nullptr;

	// Try class first
	String path = base_dir + "/" + String(DIR_DOC_CLASSES) + "/" + String(p_name) + String(FILE_EXT_DOC);
	auto doc = std::make_unique<ApiClassDocument>();
	if (FileAccess::file_exists(path)) {
		Error err = DocLoader::read_document(path, *doc);
		if (err == OK) {
			return doc;
		}
	}
	// Try builtin class
	path = base_dir + "/" + String(DIR_DOC_BUILTIN_CLASSES) + "/" + String(p_name) + String(FILE_EXT_DOC);
	if (FileAccess::file_exists(path)) {
		Error err = DocLoader::read_document(path, *doc);
		if (err == OK) {
			return doc;
		}
	}
	return nullptr;
}

std::unique_ptr<ApiUtilityFunctionDocument> find_utility_function_document(const StringName &p_name) {
	String base_dir = get_api_dumping_dir();
	if (base_dir.is_empty()) return nullptr;

	String path = base_dir + "/" + String(DIR_DOC_UTILITY_FUNCTIONS) + "/" + String(p_name) + String(FILE_EXT_DOC);
	auto doc = std::make_unique<ApiUtilityFunctionDocument>();
	Error err = DocLoader::read_utility_function_document(path, *doc);
	if (err != OK) return nullptr;
	return doc;
}

std::unique_ptr<ApiGlobalEnumDocument> find_global_enum_document(const StringName &p_name) {
	String base_dir = get_api_dumping_dir();
	if (base_dir.is_empty()) return nullptr;

	String path = base_dir + "/" + String(DIR_DOC_GLOBAL_ENUMS) + "/" + String(p_name) + String(FILE_EXT_DOC);
	auto doc = std::make_unique<ApiGlobalEnumDocument>();
	Error err = DocLoader::read_global_enum_document(path, *doc);
	if (err != OK) return nullptr;
	return doc;
}

std::unique_ptr<ApiGlobalConstantDocument> find_global_constant_document(const StringName &p_name) {
	String base_dir = get_api_dumping_dir();
	if (base_dir.is_empty()) return nullptr;

	String path = base_dir + "/" + String(DIR_DOC_GLOBAL_CONSTANTS) + "/" + String(p_name) + String(FILE_EXT_DOC);
	auto doc = std::make_unique<ApiGlobalConstantDocument>();
	Error err = DocLoader::read_global_constant_document(path, *doc);
	if (err != OK) return nullptr;
	return doc;
}

std::unique_ptr<ApiClassDocument> find_document(const Variant::Type &p_type) {
	return find_document(Variant::get_type_name(p_type));
}

// ============================================================================
// Generate interface implementation (only TOOLS_ENABLED)
// Cache is invalidated internally at start of generate() (req 10)
// ============================================================================

void full_generate_and_reboot() {
	const String project_dir = ProjectSettings::get_singleton()->globalize_path("res://");
	const String godot_executable_path = OS::get_singleton()->get_executable_path();
	const String api_file_path = project_dir.path_join("extension_api.json");
	const int my_pid = OS::get_singleton()->get_process_id();

	const String api_file_bak = api_file_path + String(".bak");
	if (FileAccess::file_exists(api_file_path)) {
		DirAccess::rename_absolute(api_file_path, api_file_bak);
	}

	const String dump_cmd = vformat("\"%s\" --headless --path \"%s\" --dump-extension-api-with-docs",
			godot_executable_path,
			project_dir);
	const String reboot_cmd = vformat("\"%s\" --editor --path \"%s\" --godotjs-api-generate \"%s\"",
			godot_executable_path,
			project_dir,
			api_file_path);
	// Use platform-appropriate command separator: & on Windows, ; on Unix
	const String separator = OS::get_singleton()->has_feature("windows") ? String(" & ") : String(" ; ");
	const String chain = dump_cmd + separator + reboot_cmd;

	// Write a temporary shell script that polls until the current editor process
	// fully exits, then runs the dump + reboot chain. This ensures no file/resource
	// conflicts between the shutting-down editor and the headless dump.
	String script_path;
	String shell_program;
	PackedStringArray shell_args;

	if (OS::get_singleton()->has_feature("windows")) {
		script_path = project_dir.path_join(".godot/.reboot_chain.bat");
		Ref<FileAccess> f = FileAccess::open(script_path, FileAccess::WRITE);
		ERR_FAIL_NULL_MSG(f, "[API Tool] Failed to create reboot script");
		// Write a UTF-8 BOM so cmd.exe recognizes the script as UTF-8 (Windows 10+).
		f->store_string(String::chr(0xFEFF));
		f->store_string("@echo off\r\n");
		// Switch cmd to the UTF-8 code page before any non-ASCII (e.g. Chinese)
		// project path appears in the dump/reboot commands below. Without this,
		// cmd decodes the UTF-8 bytes with the system ANSI/OEM code page (e.g. GBK)
		// and the commands receive a mojibake path ("Invalid project path specified").
		f->store_string("chcp 65001 >nul\r\n");
		f->store_string("echo Waiting for editor to exit...\r\n");
		f->store_string(vformat(":wait\r\n"));
		f->store_string(vformat("tasklist /fi \"PID eq %d\" 2>nul | findstr /i \"%d\" >nul\r\n", my_pid, my_pid));
		f->store_string("if %errorlevel%==0 (timeout /t 1 /nobreak >nul & goto wait)\r\n");
		f->store_string("echo Editor exited. Starting dump and reboot...\r\n");
		f->store_string(chain + String("\r\n"));
		f->store_string("echo Reboot chain completed. Press any key to close this window...\r\n");
		f->store_string("pause\r\n");
		f->close();
		shell_program = "cmd";
		shell_args.push_back("/c");
		shell_args.push_back(script_path);
	} else if (OS::get_singleton()->has_feature("linux") || OS::get_singleton()->has_feature("macos") || OS::get_singleton()->has_feature("android")) {
		script_path = project_dir.path_join(".godot/.reboot_chain.sh");
		Ref<FileAccess> f = FileAccess::open(script_path, FileAccess::WRITE);
		ERR_FAIL_NULL_MSG(f, "[API Tool] Failed to create reboot script");
		// POSIX sh (unlike cmd.exe) does not re-decode the script text with a
		// system code page: UTF-8 bytes are passed through to execve() unchanged,
		// which is how Godot stores file paths on Unix, so no BOM/chcp needed.
		f->store_string("#!/bin/sh\n");
		f->store_string("echo 'Waiting for editor to exit...'\n");
		f->store_string(vformat("while kill -0 %d 2>/dev/null; do sleep 1; done\n", my_pid));
		f->store_string("echo 'Editor exited. Starting dump and reboot...'\n");
		f->store_string(chain + String("\n"));
		f->store_string("echo 'Reboot chain completed. Press Enter to exit...'\n");
		// `read _ || true` is POSIX-compatible (no `-p`, which dash rejects) and
		// tolerates stdin being closed/EOF in non-interactive launches.
		f->store_string("read _ || true\n");
		f->close();
		shell_program = "/bin/sh";
		shell_args.push_back(script_path);
	} else {
		UtilityFunctions::push_warning("[API Tool] Reboot chain not supported on OS. Please restart editor manually.");
		return;
	}

	UtilityFunctions::print("[API Tool] Launching reboot chain: wait for exit -> dump API -> relaunch editor...");
	int pid = OS::get_singleton()->create_process(shell_program, shell_args, true);
	if (pid < 0) {
		UtilityFunctions::push_error("[API Tool] Failed to launch reboot chain script");
		return;
	}

	// Step 1: Close current editor. The script will poll until we fully exit before proceeding.
	Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())->quit();
}

Error generate_api_tool_data(const String &p_extension_api_json_path) {
	// Ensure the loader exists and its base dir is resolved. Do NOT destroy
	// it around regeneration: consumers (JS environments, cached documents)
	// hold pointers into the loader's store, so after writing the new files
	// we reload the store IN PLACE via api_tool::reload().
	initialize();
	String out_dir = get_api_dumping_dir();

	// Use generator to launch subprocess and parse
	Error err = ApiGenerator::generate(p_extension_api_json_path, out_dir);
	if (err != OK) {
		return err;
	}

	// Delete generated file and restore backup if exists.
	if (FileAccess::file_exists(p_extension_api_json_path)) {
		DirAccess::remove_absolute(p_extension_api_json_path);
	}

	String backup_path = p_extension_api_json_path + String(".bak");
	if (FileAccess::file_exists(backup_path)) {
		DirAccess::rename_absolute(backup_path, p_extension_api_json_path);
	}

	reload();
	return OK;
}

Vector<String> get_api_data_files(bool p_exclude_editor_types, bool p_extension_types_only) {
	initialize();
	const String base_dir = ProjectSettings::get_singleton()->localize_path(get_api_dumping_dir());

	Vector<String> result;

	// DIR_CLASSES
	const String classes_dir_path = base_dir.path_join(DIR_CLASSES);
	if (!DirAccess::dir_exists_absolute(classes_dir_path)) {
		return result;
	}
	for (const String &file : DirAccess::get_files_at(classes_dir_path)) {
		const String class_name = file.get_basename();
		ClassDB::APIType api_type = ClassDB::class_get_api_type(class_name);
		if (api_type == ClassDB::APIType::API_NONE) continue;
		if (p_exclude_editor_types && (api_type == ClassDB::APIType::API_EDITOR || api_type == ClassDB::APIType::API_EDITOR_EXTENSION)) continue;
		if (p_extension_types_only && (api_type != ClassDB::APIType::API_EXTENSION || api_type != ClassDB::APIType::API_EDITOR_EXTENSION)) continue;
		result.push_back(classes_dir_path.path_join(file));
	}

	// Others
	if (!p_extension_types_only) {
		// Utility Functions
		const String utility_function_file = base_dir.path_join(FILE_UTILITY_FUNCTIONS);
		if (FileAccess::file_exists(utility_function_file)) result.push_back(utility_function_file);

		// Other folders
		for (const char *folder : {
					 DIR_BUILTIN_CLASSES,
					 DIR_GLOBAL_ENUMS,
					 DIR_GLOBAL_CONSTANTS,
					 DIR_SINGLETONS,
					 DIR_NATIVE_STRUCTURES,
#ifndef DISABLED_DEPRECATED
					 DIR_COMPAT_HASHES,
#endif // DISABLED_DEPRECATED
			 }) {
			const String dir_path = base_dir.path_join(folder);
			for (const String &file : DirAccess::get_files_at(classes_dir_path)) {
				result.push_back(dir_path.path_join(file));
			}
		}
	}

	// Header
	const String header_file = base_dir.path_join(FILE_HEADER);
	if (FileAccess::file_exists(header_file)) result.push_back(header_file);

	return result;
}

} //namespace api_tool