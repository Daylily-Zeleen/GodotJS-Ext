#include "jsb_source_map_cache.h"
#include "jsb_format.h"
#include "jsb_logger.h"
#include "jsb_path_util.h"
#include "jsb_settings.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>

namespace jsb::internal {
#if JSB_WITH_SOURCEMAP
bool SourceMapCache::match(const String &p_line, MatchResult &r_result) {
#	if JSB_WITH_QUICKJS && !JSB_PREFER_QUICKJS_NG
	if (source_map_match1_.is_null()) source_map_match1_ = RegEx::create_from_string(R"(\s+at\s(.+)\s\((.+\.js):(\d+)\))"); // e.g. at xxx (file.js:1)
	const Ref<RegEx> &regex = source_map_match1_;
	const Ref<RegExMatch> match = regex->search(p_line);
	if (!match.is_valid()) return false;

	const int group_index = match->get_group_count() - 2;
	const int one_base_stack_line = (int)match->get_string(group_index + 2).to_int();
#		if !JSB_TESTS_ENABLED
	jsb_ensuref(one_based_stack_line > 0, "Invalid stack line number %d. They should one-based, impossible to reach 0 or lesser.", one_base_stack_line);
#		endif // !JSB_TESTS_ENABLED
	if (one_based_stack_line <= 0) return false;

	r_result.function = match->get_string(group_index);
	r_result.filename = match->get_string(group_index + 1);
	r_result.line = one_based_stack_line - 1;
	r_result.col = 0; // quickjs has not stack column.
	return true;
#	else // ! JSB_WITH_QUICKJS || JSB_PREFER_QUICKJS_NG
	if (source_map_match1_.is_null()) source_map_match1_ = RegEx::create_from_string(R"(\s+at\s(.+)\s\((.+\.js):(\d+):(\d+)\))"); // e.g. at xxx (file.js:1:2)
	if (source_map_match2_.is_null()) source_map_match2_ = RegEx::create_from_string(R"(\s+at\s(.+\.js):(\d+):(\d+))"); // e.g. at file.js:1:2
	const Ref<RegEx> &regex = p_line.contains("(") && p_line.contains(")")
			? source_map_match1_
			: source_map_match2_;

	const Ref<RegExMatch> match = regex->search(p_line);
	if (!match.is_valid()) return false;
	const int group_index = match->get_group_count() - 3;
	const int one_based_stack_line = (int)match->get_string(group_index + 2).to_int();
	const int one_based_stack_col = (int)match->get_string(group_index + 3).to_int();
#		if !JSB_TESTS_ENABLED
	jsb_ensuref(one_based_stack_line > 0 && one_based_stack_col > 0, "Invalid stack line number %d or column %d. They should one-based, impossible to reach 0 or lesser.", one_based_stack_line, one_based_stack_col);
#		endif // !JSB_TESTS_ENABLED
	if (one_based_stack_line <= 0 || one_based_stack_col <= 0) return false;

	r_result.function = group_index == 0 ? String() : match->get_string(group_index);
	r_result.filename = match->get_string(group_index + 1);
	r_result.line = one_based_stack_line - 1;
	r_result.col = one_based_stack_col - 1;
	return true;
#	endif
}

String SourceMapCache::process_source_position(const String &p_stacktrace, SourcePosition *r_position) {
	if (!internal::Settings::get_sourcemap_enabled()) return p_stacktrace;
	if (p_stacktrace.length() == 0) return p_stacktrace;

	bool is_position_set = r_position == nullptr;
	PackedStringArray st_lines = p_stacktrace.split("\n");
	MatchResult result;
	for (String &st_line : st_lines) {
		if (!match(st_line, result)) continue;
		const SourceMap *map = find_source_map(result.filename);
		if (!map) continue;
		IndexedSourcePosition position;
		if (!map->find(result.line, result.col, position)) continue;
		const String &source = map->get_source(position.index);
		const String &source_root = map->get_source_root();
		// Supports external maps beside the generated JS when sources resolve to local paths.
		// URI sources such as webpack:///, inline/eval maps, and sourcesContent are unsupported.
		const String original_path = PathUtil::to_platform_specific_path(PathUtil::combine(PathUtil::dirname(result.filename), source_root, source));

		// Convert to one-based for ide-friendly stack trace.
		const int source_line = position.line + 1;
		const int source_column = position.column + 1;

		if (result.function.is_empty()) st_line = jsb_format("    at %s:%d:%d", original_path, source_line, source_column);
		else st_line = jsb_format("    at %s (%s:%d:%d)", result.function, original_path, source_line, source_column);

		if (!is_position_set) {
			is_position_set = true;
			r_position->filename = original_path;
			r_position->line = source_line;
			r_position->column = source_column;
			r_position->function = result.function;
		}
	}

	String ret;
	for (int i = 0, n = (int)st_lines.size(); i < n; ++i) {
		const String &line = st_lines[i];
		// skip the leading 'Error' in the `stacktrace` message
		if (i == 0 && line == "Error") continue;
		// use static string `newline` to avoid `strlen` in `String::operator +=(const char*)`
		if (!ret.is_empty()) ret += "\n";
		ret += line;
	}
	return ret;
}

void SourceMapCache::invalidate(const String &p_filename) {
	if (cached_source_maps_.erase(p_filename)) {
		JSB_LOG(Verbose, "invalidating source map cache of file %s", p_filename);
	}
}

void SourceMapCache::clear() {
	source_map_match1_.unref();
	source_map_match2_.unref();
	cached_source_maps_.clear();
}

SourceMap *SourceMapCache::find_source_map(const String &p_filename) {
	HashMap<String, SourceMap>::Iterator it = cached_source_maps_.find(p_filename);
	if (it != cached_source_maps_.end()) {
		return &it->value;
	}

	it = cached_source_maps_.insert(p_filename, {});
	SourceMap &map = it->value;
	const String map_filename = p_filename + String(".map");
	// check before reading file to avoid annoying error prompt in get_file_as_string
	const String json_data = FileAccess::file_exists(map_filename) ? FileAccess::get_file_as_string(map_filename) : "";
	if (json_data.length() != 0) {
		map.parse(json_data);
	}
	return &map;
}
#else
String SourceMapCache::process_source_position(const String &p_stacktrace) { return p_stacktrace; }
void SourceMapCache::invalidate(const String &p_filename) {}
#endif
} //namespace jsb::internal
