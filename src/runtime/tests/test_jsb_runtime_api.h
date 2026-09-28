/************************************************************************/
/*  test_jsb_runtime_api.h                                              */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Tests for the runtime entry points the editor side consumes directly:
// GodotJSScriptLanguage::eval_source / eval_source_with_arg /
// scan_external_changes / is_global_class_generic, and
// Environment::get_module_source_info / get_module_direct_dependencies /
// get_statistics / gc -- plus the console sink list (IConsoleOutput).
//
// These are called as ordinary C++ members (single library, no cross-DLL
// boundary), so the cases double as a signature/lifetime guard: a drift in
// any of these APIs breaks this file and not only production code.

#include "../../internal/jsb_statistics.h"
#include "../tests/jsb_test_utils.h"
#include "jsb_test_helpers.h"
#include <runtime/bridge/jsb_environment.h>
#if JSB_WITH_NODE
#	include <runtime/impl/node/jsb_node_console_hook.h>
#endif
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <string>

namespace jsb::tests {

namespace runtime_api_test {

// A console sink that counts dispatches and keeps the last payload. Two
// contracts under test: constructing one registers it / destroying it
// de-registers it, and a JS `console.log` reaches it (in node mode that leg
// runs through jsb_node_console_hook).
//
// NOTE: state is per-instance on purpose -- a file-scope godot object (e.g.
// `static String`) would be constructed during DLL load, before godot-cpp's
// interface table exists, and fail DllMain with ERROR_DLL_INIT_FAILED (1114).
struct CapturedSink final : public jsb::internal::IConsoleOutput {
	int writes = 0;
	std::string last_text;

	void write(jsb::internal::ELogSeverity::Type p_severity, const String &p_text) override {
		jsb_unused(p_severity);
		writes++;
		const CharString utf8 = p_text.utf8();
		last_text.assign(utf8.get_data(), (size_t)utf8.length());
	}
};

} //namespace runtime_api_test

TEST_CASE("[runtime] [api] eval_source returns the JS result as a Variant") {
	GodotJSScriptLanguageIniter initer;

	Error err = OK;
	const jsb::JSValueMove result = GodotJSScriptLanguage::get_singleton()->eval_source(String("1 + 2"), err);
	REQUIRE(err == OK);
	REQUIRE(result.is_valid());
	const Variant value = result.to_variant();
	CHECK(value.get_type() == Variant::INT);
	CHECK((int)value == 3);
}

TEST_CASE("[runtime] [api] eval_source propagates JS syntax errors as Error values") {
	GodotJSScriptLanguageIniter initer;

	Error err = OK;
	GodotJSScriptLanguage::get_singleton()->eval_source(String("this is definitely (( not javascript"), err).ignore();
	CHECK(err != OK);
}

TEST_CASE("[runtime] [api] eval_source_with_arg exposes the argument as __jsb_arg") {
	GodotJSScriptLanguageIniter initer;

	// string roundtrip
	Error err = OK;
	jsb::JSValueMove result = GodotJSScriptLanguage::get_singleton()->eval_source_with_arg(String("__jsb_arg"), Variant(String("payload")), err);
	REQUIRE(err == OK);
	CHECK(result.to_variant() == Variant(String("payload")));

	// numeric roundtrip through the same transient global
	result = GodotJSScriptLanguage::get_singleton()->eval_source_with_arg(String("__jsb_arg * 2"), Variant((int64_t)21), err);
	REQUIRE(err == OK);
	CHECK((int)result.to_variant() == 42);
}

TEST_CASE("[runtime] [api] get_module_source_info reports source and package paths") {
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();

	// the fixture guarantees res://test_01.ts compiled to .godot/godotjs_ext/test_01.js
	Dictionary info;
	const Error err = env->get_module_source_info(String(".godot/godotjs_ext/test_01"), info);
	CHECK(err == OK);
	CHECK(info.has("source"));
	CHECK(info.has("package"));
	const String source = info["source"];
	CHECK(source.ends_with("test_01.js"));
	// NOTE: `package` may legitimately be empty -- several resolver paths
	// assign String() to it (jsb_module_resolver.cpp) -- only key presence
	// and source path are part of the contract.

	// unknown module -> ERR_CANT_OPEN, not a crash
	Dictionary missing;
	CHECK(env->get_module_source_info(String("__definitely_not_a_module__"), missing) == ERR_CANT_OPEN);
}

TEST_CASE("[runtime] [api] get_module_direct_dependencies returns a well-formed list") {
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();

	// test_01 requires only builtins (`godot`, `godot.annotations`); those are
	// served by module_loaders_ and never attached to the parent's `children`
	// array -- an empty result is the CORRECT behavior here. The contract
	// under test: the query succeeds and yields a PackedStringArray.
	PackedStringArray deps;
	const Error err = env->get_module_direct_dependencies(String(".godot/godotjs_ext/test_01"), deps);
	CHECK(err == OK);
	CHECK(deps.is_empty());
}

TEST_CASE("[runtime] [api] get_statistics populates a caller-provided Statistics") {
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();

	jsb::Statistics stats{};
	env->get_statistics(stats);
	CHECK(stats.objects >= 0);
}

TEST_CASE("[runtime] [api] scan_external_changes and gc succeed while initialized") {
	GodotJSScriptLanguageIniter initer;

	GodotJSScriptLanguage::get_singleton()->scan_external_changes();
	jsb::Environment::gc();
	CHECK(true); // reaching here without a crash is the assertion
}

TEST_CASE("[runtime] [api] is_global_class_generic answers false for a plain script") {
	GodotJSScriptLanguageIniter initer;

	// test_01.ts declares no class_name and no generics
	CHECK_FALSE(GodotJSScriptLanguage::get_singleton()->is_global_class_generic(String("res://test_01.ts")));
}

TEST_CASE("[runtime] [api] console sinks receive writes until they are destroyed") {
	using namespace runtime_api_test;
	CapturedSink sink;
	internal::IConsoleOutput::internal_write(internal::ELogSeverity::Info, String("api-console-probe"));
	CHECK_MESSAGE(sink.writes == 1, "a live sink must receive the dispatched write");
	{
		CapturedSink scratch;
		CHECK(scratch.writes == 0);
	}
	// scratch was destroyed with its scope: the list must have dropped it
	internal::IConsoleOutput::internal_write(internal::ELogSeverity::Info, String("api-console-after"));
	CHECK_MESSAGE(sink.writes == 2, "the surviving sink keeps receiving writes");
}

#if JSB_WITH_NODE
// End-to-end guard on the node console hook (src/runtime/impl/node/
// jsb_node_console_hook.cpp): the node bootstrap installs its own `console`
// whose output goes to stdout and never reaches the sink list, so the hook
// wraps it and mirrors every call into IConsoleOutput::internal_write. This is
// the behavior the editor console depends on.
//
// The hook arms on the first sink; installing it on an already-live
// environment is what NodeRuntime's constructor does for environments created
// after arming. The install is performed explicitly here so the case does not
// depend on this environment having been created after the arm (it is created
// at engine boot, before any sink exists).
TEST_CASE("[runtime] [api] node console hook mirrors JS console output into sinks") {
	using namespace runtime_api_test;
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();

	CapturedSink sink; // constructs the first sink: arms the hook

	{
		// get_context() creates a Local, so the isolate scope + HandleScope
		// must be held (same contract NodeRuntime follows).
		const V8ContextScope scope(env.get());
		jsb::impl::console_hook_ensure(env->get_isolate(), env->get_context());
	}

	Error err = OK;
	GodotJSScriptLanguage::get_singleton()->eval_source(String("console.log('api-console-e2e')"), err).ignore();
	REQUIRE(err == OK);

	CHECK_MESSAGE(sink.writes >= 1,
			"console.log from JS did not reach the sink (node console hook did not mirror it)");
	CHECK_MESSAGE(sink.last_text.find("api-console-e2e") != std::string::npos,
			"sink received a write without the JS payload; got '",
			sink.last_text.c_str(),
			"'");
}
#endif // JSB_WITH_NODE

} // namespace jsb::tests
