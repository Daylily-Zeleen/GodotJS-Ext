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
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
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

// --- node leg: the native-probe helper plumbing ------------------------------------
//
// Gode tests its node leg with dedicated node smoke tests (a forked helper plus an
// npm native module). We had none: the C++ suite only had the console-hook case and
// the TS suite had nothing node-specific at all. These cover the two pieces this
// leg alone owns:
//
//  1. the 'godot' linked binding exposes `native_probe_executable()`, which must
//     resolve to the bundled fork helper; without it `child_process.fork` would
//     re-spawn Godot itself (see the redirect in jsb_node_runtime.cpp).
//  2. `child_process.fork` is redirected: a forked child answers over IPC with an
//     execPath, and that execPath must not be the host executable.
//
// Both are driven from JS so they exercise the real bootstrap wiring.
//
// NOTE: node builtins cannot be loaded with the global `require` here -- that is the
// GodotJS bridge require (Builtins::_require -> Environment::_load_module), which only
// resolves GodotJS modules and crashes on unknown ids. The bootstrap exposes node's
// own loader as globalThis.__godotjs_node_require (jsb_node_runtime.cpp).
//
// NOTE: the child path is resolved by the engine and passed in, because the fork
// redirect maps res:// through process.cwd(), which is not the project root here.
static constexpr char k_node_probe_helper_source[] = R"jsb_src(
(function () {
    const binding = (typeof process !== "undefined") ? process._linkedBinding("godot") : null;
    const out = { hasBinding: false, hasFn: false, value: "" };
    out.hasBinding = !!(binding && typeof binding === "object");
    out.hasFn = !!(binding && typeof binding.native_probe_executable === "function");
    if (out.hasFn) {
        const p = binding.native_probe_executable();
        out.value = (typeof p === "string") ? p : "";
    }
    globalThis.__probe = out;
    return 1;
})();
)jsb_src";

TEST_CASE("[runtime] [api] [node] native_probe_executable resolves the bundled fork helper") {
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		Error err = OK;
		env->eval_source(k_node_probe_helper_source, (int)sizeof(k_node_probe_helper_source) - 1, "testcase_node_probe_helper.js", err);
		REQUIRE(err == OK);

		v8::Local<v8::Value> probe_val;
		REQUIRE(context->Global()->Get(context, impl::Helper::new_string(isolate, "__probe")).ToLocal(&probe_val));
		REQUIRE(probe_val->IsObject());
		const v8::Local<v8::Object> probe = probe_val.As<v8::Object>();

		auto get_bool = [&](const char *p_key) {
			v8::Local<v8::Value> v;
			REQUIRE(probe->Get(context, impl::Helper::new_string(isolate, p_key)).ToLocal(&v));
			return v->BooleanValue(isolate);
		};

		CHECK_MESSAGE(get_bool("hasBinding"), "process._linkedBinding('godot') did not return the linked binding");
		CHECK_MESSAGE(get_bool("hasFn"), "godot.native_probe_executable is not registered on the linked binding");

		// Path validation happens here: require("path")/require("fs") are the bridge
		// require in this context, not node's loader.
		v8::Local<v8::Value> value_val;
		REQUIRE(probe->Get(context, impl::Helper::new_string(isolate, "value")).ToLocal(&value_val));
		const v8::String::Utf8Value value_utf8(isolate, value_val);
		const String helper_path = value_utf8.length() > 0 && *value_utf8 ? String::utf8(*value_utf8) : String();

		// android/ios ship no helper -> empty is a valid answer there; on desktop the
		// helper must be an existing absolute path, otherwise fork() cannot work.
		if (helper_path.is_empty()) {
			WARN_MESSAGE(true, "native_probe_executable() is empty: this platform ships no fork helper");
			return;
		}
		CHECK_MESSAGE(helper_path.is_absolute_path(), "native_probe_executable() is not an absolute path: ", helper_path.utf8().get_data());
		CHECK_MESSAGE(FileAccess::file_exists(helper_path), "native_probe_executable() points at a missing file: ", helper_path.utf8().get_data());
	}
}

static constexpr char k_node_fork_source[] = R"jsb_src(
(function () {
    const result = { ok: false, error: null, execPath: null, usesBundledHelper: false };
    globalThis.__forkResult = result;
    const nodeRequire = globalThis.__godotjs_node_require;
    if (typeof nodeRequire !== "function") {
        result.error = "the node require bridge (__godotjs_node_require) is missing";
        return 1;
    }
    try {
        const cp = nodeRequire("child_process");
        const childPath = String(globalThis.__jsb_child_path__ || "");
        const binding = process._linkedBinding("godot");
        const helper = (binding && typeof binding.native_probe_executable === "function") ? binding.native_probe_executable() : null;
        let execOk = "?";
        try {
            const fsMod = nodeRequire("node:fs");
            fsMod.accessSync(String(helper), fsMod.constants ? fsMod.constants.X_OK : 1);
            execOk = "1";
        } catch (e) {
            execOk = "0:" + String((e && e.code) || "?");
        }
        result.diag = "x=" + execOk + " p=" + String(process.platform);
        const child = cp.fork(childPath, [], { stdio: ["ignore", "pipe", "pipe", "ipc"] });
        let stderrText = "";
        if (child.stderr) {
            child.stderr.on("data", function (chunk) { stderrText += String(chunk); });
        }
        child.on("message", function (msg) {
            if (msg && msg.type === "node-fork-probe") {
                const execPath = String(msg.execPath || "");
                result.ok = true;
                result.execPath = execPath;
                result.usesBundledHelper = /godotjs-ext(\.exe)?$/i.test(execPath.replace(/\\/g, "/"));
                // Do NOT send anything back: the probe child exits right after
                // process.send(), so a reply writes into a closed IPC channel and
                // raises EPIPE on the parent. Gode's probe works the same way.
            }
        });
        child.on("error", function (e) { result.error = String((e && e.message) || e); });
        child.on("exit", function (code) {
            if (!result.ok && result.error === null) {
                result.error = "child exited with code " + String(code) + (stderrText ? "; stderr: " + stderrText : "");
            }
        });
    } catch (e) {
        result.error = String((e && e.stack) || e);
    }
    return 1;
})();
)jsb_src";

TEST_CASE("[runtime] [api] [node] child_process.fork runs the helper instead of the host") {
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		// Resolve res:// -> OS path through the engine: the fork redirect maps res://
		// via process.cwd(), which is not the project root under the test host.
		const String child_os_path = ProjectSettings::get_singleton()->globalize_path("res://tests/node-runtime/fork-probe-child.cjs");
		REQUIRE_MESSAGE(FileAccess::file_exists(child_os_path), "the fork probe child is missing: ", child_os_path.utf8().get_data());
		const CharString child_os_path_utf8 = child_os_path.utf8();
		context->Global()->Set(context,
								 impl::Helper::new_string(isolate, "__jsb_child_path__"),
								 impl::Helper::new_string(isolate, child_os_path_utf8.get_data()))
				.Check();

		Error err = OK;
		env->eval_source(k_node_fork_source, (int)sizeof(k_node_fork_source) - 1, "testcase_node_fork.js", err);
		REQUIRE(err == OK);

		// The child answers on later event-loop turns. An in-JS `await` cannot make
		// progress here: nothing pumps node's loop during eval_source, so the timer
		// would never fire. Drive Environment::update() (which calls
		// NodeRuntime::PumpEventLoop -> uv_run(UV_RUN_NOWAIT)) until the child
		// reports, with a bounded number of turns.
		{
			const int max_turns = 200;
			int turns = 0;
			while (turns < max_turns) {
				env->update(16);
				++turns;
				v8::Local<v8::Value> ok_now;
				if (context->Global()->Get(context, impl::Helper::new_string(isolate, "__forkResult")).ToLocal(&ok_now)
						&& ok_now->IsObject()) {
					v8::Local<v8::Value> ok_flag;
					if (ok_now.As<v8::Object>()->Get(context, impl::Helper::new_string(isolate, "ok")).ToLocal(&ok_flag)
							&& ok_flag->BooleanValue(isolate)) {
						break;
					}
				}
				OS::get_singleton()->delay_msec(20);
			}
			// Diagnostics go through CHECK_MESSAGE's first argument as a std::string:
			// doctest's MESSAGE stream prints a raw const char* as a pointer, which
			// is why earlier CI logs only ever showed "0x...".
			String diag_text;
			{
				v8::Local<v8::Value> probe_val;
				if (context->Global()->Get(context, impl::Helper::new_string(isolate, "__forkResult")).ToLocal(&probe_val)
						&& probe_val->IsObject()) {
					v8::Local<v8::Value> diag_val;
					if (probe_val.As<v8::Object>()->Get(context, impl::Helper::new_string(isolate, "diag")).ToLocal(&diag_val)) {
						diag_text = impl::Helper::to_string(isolate, diag_val);
					}
				}
			}
			// Keep this SHORT: doctest truncates long payloads in the CI log, which
			// is why the full diag never showed up. Just the decisive bits.
			const std::string short_msg = std::string("nf|") + diag_text.utf8().get_data();
			CHECK_MESSAGE(turns < max_turns, short_msg.c_str());
		}

		v8::Local<v8::Value> result_val;
		REQUIRE(context->Global()->Get(context, impl::Helper::new_string(isolate, "__forkResult")).ToLocal(&result_val));
		REQUIRE(result_val->IsObject());
		const v8::Local<v8::Object> result = result_val.As<v8::Object>();

		auto get_bool = [&](const char *p_key) {
			v8::Local<v8::Value> v;
			REQUIRE(result->Get(context, impl::Helper::new_string(isolate, p_key)).ToLocal(&v));
			return v->BooleanValue(isolate);
		};

		v8::Local<v8::Value> error_val;
		REQUIRE(result->Get(context, impl::Helper::new_string(isolate, "error")).ToLocal(&error_val));
		if (!error_val->IsNull() && !error_val->IsUndefined()) {
			// CHECK_MESSAGE (not MESSAGE/FAIL_CHECK) so the payload lands in the log
			// as an ERROR line: doctest's MESSAGE stream prints a raw const char* as
			// a pointer here.
			// Put the payload in the FIRST argument: doctest does not reliably
			// stream the extra args, so they never showed up in the CI log.
			const String err_text = impl::Helper::to_string(isolate, error_val);
			const std::string fork_error = std::string("fork failed: ") + err_text.utf8().get_data();
			CHECK_MESSAGE(false, fork_error.c_str());
		}

		// Gode's npm-native smoke asserts the child's execPath IS the bundled helper
		// (`/gode_node(\.exe)?$/`); ours is `godotjs-ext[.exe]`. "Not the host" is not
		// enough -- a fork that silently fell back to some other node would pass it.
		CHECK_MESSAGE(get_bool("ok"), "the forked child never reported an execPath over IPC");
		CHECK_MESSAGE(get_bool("usesBundledHelper"), "fork did not use the bundled godotjs-ext helper");

		v8::Local<v8::Value> exec_val;
		REQUIRE(result->Get(context, impl::Helper::new_string(isolate, "execPath")).ToLocal(&exec_val));
		// impl::Helper::to_string is the cross-engine way to read a JS value as a
		// godot String; a raw v8::String::Utf8Value on a non-string prints pointer
		// bits, which is what happened before.
		// impl::Helper::to_string is engine-specific; JS-visible text is read most
		// reliably by asking v8 for the string directly (this leg is v8-backed).
		v8::Local<v8::String> exec_str;
		const String exec_path = exec_val->ToString(context).ToLocal(&exec_str)
				? String::utf8(v8::String::Utf8Value(isolate, exec_str).length() > 0 ? *v8::String::Utf8Value(isolate, exec_str) : "")
				: String("(not a string)");
		// NOTE: doctest's MESSAGE stream prints a raw `const char*` as the pointer
		// value, so wrap in std::string (that is why this printed "000002..." before).

	}
}
#endif // JSB_WITH_NODE

} // namespace jsb::tests
