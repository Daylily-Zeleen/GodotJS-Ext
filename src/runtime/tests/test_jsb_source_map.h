/************************************************************************/
/*  test_jsb_source_map.h                                               */
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

#pragma once

#include "../internal/jsb_source_map.h"
#include "../internal/jsb_source_map_cache.h"
#include "../weaver/jsb_script_language.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#if JSB_WITH_SOURCEMAP

// engine-agnostic unit tests for the source map stacktrace translation
// NOTE: these tests are self-contained and must NOT depend on the tsc-compiled artifacts under `project/`
//       (C++ tests run before the TS integration tests in CI).
namespace jsb::tests {
TEST_CASE("[runtime] [jsb.sourcemap] parse and find") {
	// hand-written source map json: a single segment on generated line 0, mapping
	// generated column 0 -> sources[0], source line 1, source column 3 (all zero-based)
	// segment 'AACG' decodes as [gen_col_delta=0, source_index_delta=0, source_line_delta=1, source_col_delta=3]
	static constexpr const char map_json[] = R"({
	"version": 3,
	"file": "testScript.js",
	"sourceRoot": "src/",
	"sources": ["中文路径/testScript.ts"],
	"names": [],
	"mappings": "AACG"
})";

	internal::SourceMap map;
	CHECK(map.parse(String(map_json)));
	CHECK(map.get_source_root() == "src/");
	CHECK(map.get_source(0) == "中文路径/testScript.ts");

	{
		internal::IndexedSourcePosition pos;
		CHECK(map.find(0, 0, pos));
		CHECK(pos.index == 0);
		CHECK(pos.line == 1);
		CHECK(pos.column == 3);
	}

	{
		// a generated column beyond the mapped range should still fall back to the only mapping
		internal::IndexedSourcePosition pos;
		CHECK(map.find(0, 16, pos));
		CHECK(pos.index == 0);
		CHECK(pos.line == 1);
		CHECK(pos.column == 3);
	}

	{
		// no mapping at all for the requested generated line
		// a segment only applies to the generated line it is defined on; a position past the end of
		// the mapped range must not fall back to an earlier line (matches the reference `source-map`
		// library, whose `originalPositionFor` returns null when `mapping.generatedLine !== needle.generatedLine`)
		internal::IndexedSourcePosition pos;
		pos.index = -1;
		pos.line = -1;
		pos.column = -1;
		CHECK(!map.find(99, 0, pos));
		CHECK(pos.index == 0);
		CHECK(pos.line == 0);
		CHECK(pos.column == 0);
	}
}

#if !JSB_WITH_QUICKJS || JSB_PREFER_QUICKJS_NG
TEST_CASE("[runtime] [jsb.sourcemap] match one-based to zero-based") {
	internal::SourceMapCache cache;
	internal::SourceMapCache::MatchResult result;

	// V8 style: at <function> (<filename>:<line>:<col>), one-based positions
	{
		CHECK(cache.match("    at __esDecorate (F:\\中文路径\\testScript.js:20:40)", result));
		CHECK(result.function == "__esDecorate");
		CHECK(result.filename == "F:\\中文路径\\testScript.js");
		CHECK(result.line == 19);
		CHECK(result.col == 39);
	}

	// V8 style without function name: at <filename>:<line>:<col>
	{
		CHECK(cache.match("    at F:\\中文路径\\testScript.js:91:26", result));
		CHECK(result.function.is_empty());
		CHECK(result.filename == "F:\\中文路径\\testScript.js");
		CHECK(result.line == 90);
		CHECK(result.col == 25);
	}

	// a line without any stack frame should not match
	{
		CHECK(!cache.match("TypeError: Cannot read properties of undefined (reading 'name')", result));
	}

	// stacktrace coordinates are one-based and zero is invalid
	{
		CHECK(!cache.match("    at fn (F:\\中文路径\\testScript.js:0:1)", result));
		CHECK(!cache.match("    at fn (F:\\中文路径\\testScript.js:1:0)", result));
	}
}
#else // !JSB_WITH_QUICKJS || JSB_PREFER_QUICKJS_NG
TEST_CASE("[runtime] [jsb.sourcemap] classic quickjs match without column") {
	internal::SourceMapCache cache;
	internal::SourceMapCache::MatchResult result;

	CHECK(cache.match("    at fn (F:\\中文路径\\testScript.js:20)", result));
	CHECK(result.function == "fn");
	CHECK(result.filename == "F:\\中文路径\\testScript.js");
	CHECK(result.line == 19);
	CHECK(result.col == 0);

	CHECK(!cache.match("    at fn (F:\\中文路径\\testScript.js:0)", result));
}
#endif // JSB_WITH_QUICKJS && !JSB_PREFER_QUICKJS_NG

TEST_CASE("[runtime] [jsb.sourcemap] process_source_position rewrites stacktrace") {
	GodotJSScriptLanguageIniter initer;

	// a temporary generated `.js` with a hand-written `.js.map` next to it
	// (fixture only for exercising `process_source_position`; the real end-to-end test
	//  with tsc-generated maps lives in the TS integration tests)
	// NOTE: the fixture mirrors a real tsc map WITHOUT a `sourceRoot` (the spec-conformant
	// form): `sources` are relative to the map file's location, so they need `..` segments
	// that must be resolved instead of leaking into the output path.
	// The stack frame filename must be an absolute OS path (as V8 emits) so that Godot's
	// simplify_path -- the resolver inside to_platform_specific_path -- actually collapses
	// the `..` segments (it deliberately keeps leading `..` on relative paths, and on
	// res:// paths above the root, see its GLTF-importer FIXME).
	const String js_path = ProjectSettings::get_singleton()->globalize_path("res://").path_join("test_source_map_fixture.js");
	const String map_path = js_path + String(".map");
	{
		Ref<FileAccess> file = FileAccess::open(js_path, FileAccess::WRITE);
		CHECK(file.is_valid());
		file->store_string("// fixture\nthrow new Error('fixture');\n");
		file->close();
	}
	{
		Ref<FileAccess> file = FileAccess::open(map_path, FileAccess::WRITE);
		CHECK(file.is_valid());
		file->store_string(R"({
	"version": 3,
	"file": "test_source_map_fixture.js",
	"sources": ["../../../../tests/中文路径/testScript.ts"],
	"names": [],
	"mappings": "AACG"
})");
		file->close();
	}

	internal::SourceMapCache cache;
	// one-based positions in the stacktrace (line 1, column 1) must be converted into
	// zero-based ones before the lookup, then printed back as one-based (line 2, column 4)
	String stacktext = String("Error: SOURCE_MAP_TEST_MARKER\n") + String("    at fn (") + js_path + String(":1:1)");
	const String rewritten = cache.process_source_position(stacktext);
	// the `..` segments in `sources` must be resolved; the output is an absolute OS path whose
	// prefix and separators are platform-dependent, so assert on the resolved content only
	CHECK(rewritten.contains("testScript.ts:2:4"));
	CHECK(rewritten.contains("中文路径"));
	CHECK(!rewritten.contains(".."));

	cache.invalidate(js_path);
	cache.clear();
	// cleanup the fixture files
	DirAccess::remove_absolute(js_path);
	DirAccess::remove_absolute(map_path);
}

TEST_CASE("[runtime] [jsb.sourcemap] chinese path filename in stacktrace") {
	// regression for the utf-8 byte-length bug in `impl::Helper::compile_function`:
	// the generated filename was truncated when it contained non-ascii characters
	// (e.g. 'testScript.js' became 'cardDefin'), which also broke the `.js.map` lookup.
	// NOTE: engine-agnostic on purpose; `compile_function`/`TryCatch` are provided by
	//       the compat layer for every JS engine.
	GodotJSScriptLanguageIniter initer;

	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	JSB_TESTS_EXECUTION_SCOPE(env.get());

	v8::Isolate *isolate = env->get_isolate();
	const v8::Local<v8::Context> context = env->get_context();
	v8::HandleScope scope(isolate);

	// compile a function which throws when called
	static constexpr char source[] = R"--((function() {
	throw new Error("SOURCE_MAP_UTF8_TEST");
}))--";

	impl::TryCatch try_catch(isolate);
	v8::MaybeLocal<v8::Value> eval = impl::Helper::compile_function(context, source, ::std::size(source) - 1, "中文路径/testScript.js");
	CHECK(!eval.IsEmpty());
	CHECK(eval.ToLocalChecked()->IsFunction());

	v8::Local<v8::Function> func = eval.ToLocalChecked().As<v8::Function>();
	v8::MaybeLocal<v8::Value> rval = func->Call(context, v8::Undefined(isolate), 0, nullptr);
	CHECK(rval.IsEmpty());
	CHECK(try_catch.has_caught());

	String stacktrace;
	try_catch.get_message(nullptr, &stacktrace);
	CHECK(!stacktrace.is_empty());
	// the full generated filename must be preserved (with the `.js` extension)
	CHECK(stacktrace.contains("testScript.js"));
	CHECK(stacktrace.contains("中文路径"));
}
} //namespace jsb::tests

#endif // JSB_WITH_SOURCEMAP
