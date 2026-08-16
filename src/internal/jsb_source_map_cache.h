#pragma once

#include "jsb_internal_pch.h"
#include "jsb_source_map.h"
#include <godot_cpp/classes/reg_ex.hpp>

namespace jsb::internal {
struct SourceMapCache {
	// try to translate the source positions in stacktrace
	String process_source_position(const String &p_stacktrace, SourcePosition *r_position = nullptr);

	void invalidate(const String &p_filename);

	void clear();

#if JSB_WITH_SOURCEMAP
	struct MatchResult {
		String function;
		String filename;
		int line = 0; // zero-based

		// not available in quickjs.impl (but quickjs-ng has column info)
		int col = 0; // zero-based
	};

	// match a single stacktrace line (e.g. `at xxx (file.js:1:2)`) and extract the frame info.
	bool match(const String &p_line, MatchResult &r_result);

private:
	SourceMap *find_source_map(const String &p_filename);

	Ref<RegEx> source_map_match1_;
	Ref<RegEx> source_map_match2_;
	HashMap<String, SourceMap> cached_source_maps_;
#endif
};
} //namespace jsb::internal
