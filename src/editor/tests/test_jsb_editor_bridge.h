/************************************************************************/
/*  test_jsb_editor_bridge.h                                            */
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

// Editor-suite end-to-end tests of the C ABI bridge.
//
// Unlike the runtime suite's direct jsb::get_bridge_table() calls, these
// cases exercise the PRODUCTION path: EditorBridge::get_bridge() walks the
// engine script-language registry, resolves GodotJSScriptLanguage by class
// name, and fetches the table address through the neutral `get_bridge`
// ClassDB method -- across the editor/runtime DLL boundary. The eval cases
// additionally make the RUNTIME emit a console write and assert it arrives
// at an editor-side sink through the registered trampoline.

#include "../tests/jsb_test_utils.h"
#include "../weaver-editor/jsb_editor_bridge.h"
#include <cstring>
#include <godot_cpp/variant/dictionary.hpp>
#include <string>

namespace jsb::editor::tests {

namespace bridge_e2e {

struct CapturedWrite {
	std::string text;
	int writes = 0;

	static void write(void *p_userdata, int32_t p_severity, const char *p_text_utf8, int64_t p_length) {
		(void)p_severity;
		CapturedWrite *self = static_cast<CapturedWrite *>(p_userdata);
		self->text.assign(p_text_utf8, (size_t)p_length);
		self->writes++;
	}
};

} //namespace bridge_e2e

TEST_CASE("[editor] [bridge] bridge resolves through ClassDB with matching struct_size") {
	const JsbBridgeTable *bridge = EditorBridge::get_bridge();
	CHECK_MESSAGE(bridge != nullptr,
			"EditorBridge could not resolve the runtime bridge; "
			"is the runtime extension loaded and its language initialized?");
	if (!bridge) {
		return;
	}
	CHECK(bridge->struct_size == sizeof(JsbBridgeTable));
	CHECK(bridge->eval != nullptr);
	CHECK(bridge->eval_with_arg != nullptr);
	CHECK(bridge->add_console_output != nullptr);
}

TEST_CASE("[editor] [bridge] cross-DLL eval: result lands in editor-side Variant storage") {
	const JsbBridgeTable *bridge = EditorBridge::get_bridge();
	if (bridge == nullptr) {
		FAIL("runtime bridge not resolvable via ClassDB");
		return;
	}

	godot::Variant result;
	static constexpr char kSrc[] = "(6 * 7)";
	const godot::Error err = bridge->eval(kSrc, (int64_t)(sizeof(kSrc) - 1), result._native_ptr());
	CHECK(err == godot::OK);
	CHECK(result.get_type() == godot::Variant::INT);
	CHECK((int)result == 42);
}

TEST_CASE("[editor] [bridge] console sink receives writes emitted from JS executed via the bridge") {
	const JsbBridgeTable *bridge = EditorBridge::get_bridge();
	if (bridge == nullptr) {
		FAIL("runtime bridge not resolvable via ClassDB");
		return;
	}

	// register an editor-side sink exactly like GodotJSREPL does
	bridge_e2e::CapturedWrite sink;
	const int64_t handle = bridge->add_console_output(&sink, &bridge_e2e::CapturedWrite::write);
	if (handle <= 0) {
		FAIL("add_console_output returned an invalid handle");
		return;
	}

	// make the RUNTIME emit a console.log while evaluating through the
	// bridge: editor -> bridge -> runtime V8 -> IConsoleOutput list ->
	// trampoline -> editor callback
	static constexpr char kEmit[] = "console.log('editor-bridge-console-e2e')";
	const godot::Error err = bridge->eval(kEmit, (int64_t)(sizeof(kEmit) - 1), nullptr);
	CHECK(err == godot::OK);

	bool delivered = sink.text.find("editor-bridge-console-e2e") != std::string::npos;
	CHECK_MESSAGE(delivered, "console output emitted in the runtime did not reach the editor-side sink");

	// after removal the same emission must NOT reach us anymore
	if (bridge->remove_console_output(handle) != godot::OK) {
		FAIL("remove_console_output failed");
		return;
	}
	const int before = sink.writes;
	static constexpr char kEmit2[] = "console.log('after-unregister')";
	CHECK(bridge->eval(kEmit2, (int64_t)(sizeof(kEmit2) - 1), nullptr) == godot::OK);
	CHECK(sink.writes == before);
}

} // namespace jsb::editor::tests
