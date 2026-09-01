/************************************************************************/
/*  test_jsb_bridge_table.h                                             */
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

// Unit tests for the C ABI bridge table contract (jsb_bridge_table.h).
//
// These cases call the table THROUGH ITS FUNCTION POINTERS -- the exact
// surface the editor extension consumes -- instead of calling internal
// runtime helpers directly, so signature/lifetime drift breaks the tests
// and not just production.
//
// Cross-DLL consumption (EditorBridge -> ClassDB get_bridge -> eval) is
// covered separately by the editor suite ([editor][bridge] cases).

#include "../../internal/jsb_statistics.h"
#include "../tests/jsb_test_utils.h"
#include "../internal/jsb_bridge_table.h"
#include "jsb_test_helpers.h"
#include <cstring>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace jsb::tests {

namespace bridge_test {

struct CapturedWrite {
	std::string text;

	static void write(void *p_userdata, int32_t p_severity, const char *p_text_utf8, int64_t p_length) {
		(void)p_severity;
		CapturedWrite *self = static_cast<CapturedWrite *>(p_userdata);
		self->text.assign(p_text_utf8, (size_t)p_length);
	}
};

} //namespace bridge_test

TEST_CASE("[runtime] [bridge] table integrity: struct_size matches and every slot is filled") {
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);
	CHECK(table->struct_size == sizeof(JsbBridgeTable));
	CHECK(table->eval != nullptr);
	CHECK(table->eval_with_arg != nullptr);
	CHECK(table->get_module_source_info != nullptr);
	CHECK(table->get_module_direct_dependencies != nullptr);
	CHECK(table->fill_statistics != nullptr);
	CHECK(table->scan_external_changes != nullptr);
	CHECK(table->is_global_class_generic != nullptr);
	CHECK(table->request_gc != nullptr);
	CHECK(table->add_console_output != nullptr);
	CHECK(table->remove_console_output != nullptr);
}

TEST_CASE("[runtime] [bridge] eval writes the JS result into caller-owned Variant storage") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	Variant result;
	static constexpr char kSrc[] = "1 + 2";
	const godot::Error err = table->eval(kSrc, (int64_t)(sizeof(kSrc) - 1), result._native_ptr());
	CHECK(err == OK);
	CHECK(result.get_type() == Variant::INT);
	CHECK((int)result == 3);
}

TEST_CASE("[runtime] [bridge] eval propagates JS syntax errors as Error values") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	Variant result;
	static constexpr char kBroken[] = "this is definitely (( not javascript";
	const godot::Error err = table->eval(kBroken, (int64_t)(sizeof(kBroken) - 1), result._native_ptr());
	CHECK(err != OK);
}

TEST_CASE("[runtime] [bridge] eval tolerates a null result pointer (fire-and-forget)") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	static constexpr char kSrc[] = "globalThis.__bridge_null_probe = 41 + 1";
	const godot::Error err = table->eval(kSrc, (int64_t)(sizeof(kSrc) - 1), nullptr);
	CHECK(err == OK);
}

TEST_CASE("[runtime] [bridge] eval_with_arg exposes the argument as __jsb_arg") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	// string roundtrip
	Variant arg(String("bridge-payload"));
	Variant result;
	static constexpr char kEcho[] = "__jsb_arg";
	godot::Error err = table->eval_with_arg(kEcho, (int64_t)(sizeof(kEcho) - 1), arg._native_ptr(), result._native_ptr());
	CHECK(err == OK);
	CHECK(result == Variant(String("bridge-payload")));

	// numeric roundtrip through the same transient global
	arg = Variant((int64_t)21);
	err = table->eval_with_arg("__jsb_arg * 2", (int64_t)(strlen("__jsb_arg * 2")), arg._native_ptr(), result._native_ptr());
	CHECK(err == OK);
	CHECK((int)result == 42);
}

TEST_CASE("[runtime] [bridge] get_module_source_info reports source and package paths") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	// the fixture guarantees res://test_01.ts compiled to .godot/godotjs_ext/test_01.js
	static constexpr char kModule[] = ".godot/godotjs_ext/test_01";
	Variant result;
	const godot::Error err = table->get_module_source_info(kModule, (int64_t)(sizeof(kModule) - 1), result._native_ptr());
	CHECK(err == OK);

	const Dictionary info = result;
	CHECK(info.has("source"));
	CHECK(info.has("package"));
	const String source = info["source"];
	CHECK(source.ends_with("test_01.js"));
	// NOTE: `package` may legitimately be empty -- several resolver paths
	// assign String() to it (jsb_module_resolver.cpp) -- only key presence
	// and source path are part of the bridge contract.

	// unknown module -> ERR_CANT_OPEN, not a crash
	static constexpr char kMissing[] = "__definitely_not_a_module__";
	Variant missing_result;
	const godot::Error err2 = table->get_module_source_info(kMissing, (int64_t)(sizeof(kMissing) - 1), missing_result._native_ptr());
	CHECK(err2 == ERR_CANT_OPEN);
}

TEST_CASE("[runtime] [bridge] get_module_direct_dependencies returns a list (empty for builtin-only deps)") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	// test_01 requires only builtins (`godot`, `godot.annotations`); those are
	// served by module_loaders_ (jsb_environment.cpp:348) and never attached
	// to the parent's `children` array -- an empty result is the CORRECT
	// behavior here. The contract under test: the query succeeds and yields
	// a well-formed PackedStringArray.
	static constexpr char kModule[] = ".godot/godotjs_ext/test_01";
	Variant result;
	const godot::Error err = table->get_module_direct_dependencies(kModule, (int64_t)(sizeof(kModule) - 1), result._native_ptr());
	CHECK(err == OK);

	const PackedStringArray deps = result;
	(void)deps; // well-formed extraction without exception is the assertion

	// query functions reject a null result storage explicitly
	const godot::Error err2 = table->get_module_direct_dependencies(kModule, (int64_t)(sizeof(kModule) - 1), nullptr);
	CHECK(err2 == ERR_INVALID_PARAMETER);
}

TEST_CASE("[runtime] [bridge] fill_statistics populates a caller-provided Statistics") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	jsb::Statistics stats{};
	const godot::Error err = table->fill_statistics(&stats);
	CHECK(err == OK);
	CHECK(stats.objects >= 0);

	// null storage is rejected by contract
	CHECK(table->fill_statistics(nullptr) == ERR_INVALID_PARAMETER);
}

TEST_CASE("[runtime] [bridge] scan_external_changes and request_gc succeed while initialized") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	CHECK(table->scan_external_changes() == OK);
	CHECK(table->request_gc() == OK);
}

TEST_CASE("[runtime] [bridge] is_global_class_generic answers false for a plain script") {
	GodotJSScriptLanguageIniter initer;
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	// test_01.ts declares no class_name and no generics
	static constexpr char kPath[] = "res://test_01.ts";
	Variant result;
	const godot::Error err = table->is_global_class_generic(kPath, (int64_t)(sizeof(kPath) - 1), result._native_ptr());
	CHECK(err == OK);
	CHECK(result.get_type() == Variant::BOOL);
	CHECK_FALSE((bool)result);
}

TEST_CASE("[runtime] [bridge] console sinks: register, deliver, remove, idempotent remove") {
	const JsbBridgeTable *table = jsb::get_bridge_table();
	REQUIRE(table != nullptr);

	bridge_test::CapturedWrite sink;
	const int64_t handle = table->add_console_output(&sink, &bridge_test::CapturedWrite::write);
	CHECK(handle > 0);

	// dispatch through the runtime's console-output list; the trampoline
	// registered above must receive the write with UTF-8 payload
	internal::IConsoleOutput::internal_write(internal::ELogSeverity::Info, String("bridge-console-probe"));
	CHECK_MESSAGE(sink.text.find("bridge-console-probe") != std::string::npos,
			"sink did not receive the dispatched write; captured='",
			sink.text.c_str(),
			"'");

	// removal stops delivery and stays idempotent
	CHECK(table->remove_console_output(handle) == OK);
	sink.text.clear();
	internal::IConsoleOutput::internal_write(internal::ELogSeverity::Info, String("after-remove"));
	CHECK(sink.text.find("after-remove") == std::string::npos);

	CHECK(table->remove_console_output(handle) == OK); // second removal: still OK

	// registering without a write callback is rejected with the sentinel handle
	CHECK(table->add_console_output(nullptr, nullptr) == -1);
}

} // namespace jsb::tests
