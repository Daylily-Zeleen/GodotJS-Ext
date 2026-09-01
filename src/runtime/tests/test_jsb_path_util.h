/************************************************************************/
/*  test_jsb_path_util.h                                                */
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

#include "../internal/jsb_path_util.h"
#include "../internal/jsb_settings.h"
#include "../weaver/jsb_script_language.h"
#include "internal/jsb_runtime_settings.h"
#include "jsb_test_helpers.h"


#include <godot_cpp/classes/project_settings.hpp>

namespace jsb::tests {
TEST_CASE("[runtime] [jsb.path] dirname and get_last_component") {
	// godot virtual paths
	CHECK(internal::PathUtil::dirname("res://main.js") == "res://");
	CHECK(internal::PathUtil::dirname("res://dir/main.js") == "res://dir");
	CHECK(internal::PathUtil::dirname("res://dir/sub/main.js") == "res://dir/sub");
	CHECK(internal::PathUtil::dirname("res://") == "res://");
	CHECK(internal::PathUtil::dirname("user://main.js") == "user://");

	// os absolute paths
	CHECK(internal::PathUtil::dirname("F:/main.js") == "F:/");
	CHECK(internal::PathUtil::dirname("F:\\main.js") == "F:\\");
	CHECK(internal::PathUtil::dirname("F:\\dir\\main.js") == "F:\\dir");
	CHECK(internal::PathUtil::dirname("/") == "/");
	CHECK(internal::PathUtil::dirname("/usr/local/bin") == "/usr/local");
	CHECK(internal::PathUtil::dirname("C:\\") == "C:\\");

	// relative / bare filenames
	CHECK(internal::PathUtil::dirname("main.js").is_empty());
	CHECK(internal::PathUtil::dirname("dir/main.js") == "dir");

	// last component
	CHECK(internal::PathUtil::get_last_component("res://main.js") == "main.js");
	CHECK(internal::PathUtil::get_last_component("res://dir/main.js") == "main.js");
	CHECK(internal::PathUtil::get_last_component("F:\\dir\\main.js") == "main.js");
	CHECK(internal::PathUtil::get_last_component("/") == "");
	CHECK(internal::PathUtil::get_last_component("main.js") == "main.js");
}

TEST_CASE("[runtime] [jsb.path] extract normalization") {
	String out;

	// resolve `.` and `..` while preserving the protocol root
	CHECK(internal::PathUtil::extract("res://dir/../test.ts", out) == OK);
	CHECK(out == "res://test.ts");
	CHECK(internal::PathUtil::extract("res://dir/./sub/x.ts", out) == OK);
	CHECK(out == "res://dir/sub/x.ts");

	// collapse duplicate separators and convert backslashes
	CHECK(internal::PathUtil::extract("res://a//b", out) == OK);
	CHECK(out == "res://a/b");
	CHECK(internal::PathUtil::extract("a\\b\\c.ts", out) == OK);
	CHECK(out == "a/b/c.ts");

	// os absolute path
	CHECK(internal::PathUtil::extract("F:/out/../src/test.ts", out) == OK);
	CHECK(out == "F:/src/test.ts");

	// `..` above the protocol root is kept (pure string normalization, no clamping)
	CHECK(internal::PathUtil::extract("res://../x", out) == OK);
	CHECK(out == "res://../x");
}

TEST_CASE("[runtime] [jsb.path] is_absolute_path") {
	CHECK(internal::PathUtil::is_absolute_path("res://x"));
	CHECK(internal::PathUtil::is_absolute_path("user://x"));
	CHECK(internal::PathUtil::is_absolute_path("F:/x"));
	CHECK(internal::PathUtil::is_absolute_path("F:\\x"));
	CHECK(internal::PathUtil::is_absolute_path("/x"));
	CHECK(internal::PathUtil::is_absolute_path("\\\\server\\share"));
	CHECK(!internal::PathUtil::is_absolute_path("relative/x"));
	CHECK(!internal::PathUtil::is_absolute_path("./x"));
	CHECK(!internal::PathUtil::is_absolute_path("x"));
	CHECK(!internal::PathUtil::is_absolute_path(""));
}

TEST_CASE("[runtime] [jsb.path] combine") {
	CHECK(internal::PathUtil::combine("res://", "main.js") == "res://main.js");
	CHECK(internal::PathUtil::combine("res://dir", "main.js") == "res://dir/main.js");
	CHECK(internal::PathUtil::combine("res://dir/", "main.js") == "res://dir/main.js");
	// an add starting with '/' is joined without inserting another separator (path_join semantics)
	CHECK(internal::PathUtil::combine("res://dir", "/main.js") == "res://dir/main.js");
	CHECK(internal::PathUtil::combine("", "x.js") == "x.js");
	CHECK(internal::PathUtil::combine("res://a", "b", "c.js") == "res://a/b/c.js");
}

TEST_CASE("[runtime] [jsb.path] convert typescript/javascript paths") {
	GodotJSScriptLanguageIniter initer;
	const String out_root = internal::settings::get_jsb_out_res_path();
	CHECK(out_root.begins_with("res://"));

	// ts -> js
	CHECK(internal::PathUtil::convert_typescript_path("res://test.ts") == out_root.path_join("test.js"));
	CHECK(internal::PathUtil::convert_typescript_path("res://dir/test.ts") == out_root.path_join("dir/test.js"));
	// non-typescript input passes through unchanged
	CHECK(internal::PathUtil::convert_typescript_path("res://test.js") == "res://test.js");

	// js -> ts (boundary-aware)
	CHECK(internal::PathUtil::convert_javascript_path(out_root.path_join("test.js")) == "res://test.ts");
	CHECK(internal::PathUtil::convert_javascript_path(out_root.path_join("dir/test.cjs")) == "res://dir/test.ts");
	CHECK(internal::PathUtil::convert_javascript_path(out_root.path_join("test.mjs")) == "res://test.ts");
	// non-javascript input passes through unchanged
	CHECK(internal::PathUtil::convert_javascript_path("res://test.ts") == "res://test.ts");
}

TEST_CASE("[runtime] [jsb.path] to_platform_specific_path") {
	GodotJSScriptLanguageIniter initer;

	// a file that exists on disk (inside the project) is globalized into a platform path
	const String res_root = ProjectSettings::get_singleton()->globalize_path("res://");
	const String expected = res_root.path_join("project.godot").simplify_path();
	// normalize the separator so the assertion holds on both posix and windows
	CHECK(internal::PathUtil::to_platform_specific_path("res://project.godot").replace("\\", "/") == expected);

	// a path with no filesystem backing keeps its (normalized) virtual form
	CHECK(internal::PathUtil::to_platform_specific_path("res://__nonexistent_dir__/x.ts") == "res://__nonexistent_dir__/x.ts");

	// a plain relative path stays relative (no project-root guessing)
	CHECK(internal::PathUtil::to_platform_specific_path("src/test.ts") == "src/test.ts");
}

} //namespace jsb::tests
