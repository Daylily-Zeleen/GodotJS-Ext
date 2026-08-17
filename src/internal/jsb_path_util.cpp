#include "jsb_path_util.h"

#include "jsb_internal_pch.h"
#include "jsb_logger.h"
#include "jsb_macros.h"
#include "jsb_settings.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::internal {
String PathUtil::to_platform_specific_path(const String &p_path) {
	const String simplified = p_path.simplify_path();
	const String globalized = ProjectSettings::get_singleton()->globalize_path(simplified);
	// only return the platform path when the file actually exists on disk; files inside a packed
	// PCK have no meaningful filesystem location, keep the (res://) virtual path instead.
	if (FileAccess::file_exists(globalized)) {
#ifdef WINDOWS_ENABLED
		return globalized.simplify_path().replace("/", "\\");
#else
		return globalized.simplify_path();
#endif
	}
	return simplified;
}

String PathUtil::extends_with(const String &p_path, const String &p_ext) {
	return p_path.ends_with(p_ext) ? p_path : p_path + p_ext;
}

String PathUtil::convert_typescript_path(const String &p_source_path) {
	if (!p_source_path.ends_with("." JSB_TYPESCRIPT_EXT)) {
		return p_source_path;
	}

	constexpr int64_t begin = std::size("res://") - 1;
	constexpr int64_t len_ofs = std::size("res://") - 1 + std::size(JSB_TYPESCRIPT_EXT) - 1;
	const int64_t len = p_source_path.length() - len_ofs;
	return Settings::get_jsb_out_res_path().path_join(
			p_source_path.substr(begin, len)
			+ String(JSB_JAVASCRIPT_EXT));
}

String PathUtil::convert_javascript_path(const String &p_source_path) {
	bool is_js_extension = p_source_path.ends_with("." JSB_JAVASCRIPT_EXT);
	if (is_js_extension || p_source_path.ends_with("." JSB_COMMONJS_EXT) || p_source_path.ends_with("." JSB_MODULE_EXT)) {
		int extension_length = is_js_extension ? 3 : 4;
		const String root_path = Settings::get_jsb_out_res_path();
		jsb_checkf(p_source_path.begins_with(root_path + String("/")), "can not proceed javascript sources not under the project data directory");
		const String replaced = String("res://").path_join(
				p_source_path.substr(root_path.length() + 1, p_source_path.length() - root_path.length() - extension_length)
				+ JSB_TYPESCRIPT_EXT);
		return replaced;
	}
	return p_source_path;
}

bool PathUtil::is_recognized_javascript_extension(const String &p_path) {
	return p_path.ends_with("." JSB_JAVASCRIPT_EXT) || p_path.ends_with("." JSB_COMMONJS_EXT) || p_path.ends_with("." JSB_MODULE_EXT);
}

bool PathUtil::delete_file(const String &p_path) {
	const Error err = DirAccess::remove_absolute(p_path);
	if (err != OK) {
		JSB_LOG(Warning, "failed to delete %s (%s)", p_path, UtilityFunctions::error_string(err));
		return false;
	}
	return true;
}

} //namespace jsb::internal
