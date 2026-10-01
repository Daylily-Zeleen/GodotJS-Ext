/************************************************************************/
/*  test_jsb_script_language_queries.h                                  */
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

#include "../weaver/jsb_script.h"
#include "../weaver/jsb_script_language.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

namespace jsb::tests {

// 这批查询是编辑器钩子的实现，契约是「按标识符在源码里找到声明所在行（1 基）」与
// 「文件名基名必须是合法标识符」。断言的边界都是实测踩过的坑：
// 注释里的同名文字、长名的前缀、修饰符前缀、以及必须返回 -1 的调用点。
#if JSB_TOOLS
TEST_CASE("[runtime] [jsb.lang] find_identifier_line locates declarations, not mentions") {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);

	// 基本定位（1 基行号）
	CHECK(lang->find_identifier_line("bar", "class Foo {\n    bar() {}\n}\n") == 2);

	// 注释里的同名文字不算声明
	CHECK(lang->find_identifier_line("bar", "class Foo {\n\n    // bar mentioned in a comment\n    bar() {}\n}\n") == 4);
	// 注释里有、源码里没有 ⇒ 找不到
	CHECK(lang->find_identifier_line("bar", "class Foo {\n\n    // bar mentioned in a comment\n    baz() {}\n}\n") == -1);

	// 长名的前缀不算命中
	CHECK(lang->find_identifier_line("bar", "class Foo {\n    barbaz() {}\n}\n") == -1);

	// 字段与属性声明
	CHECK(lang->find_identifier_line("bar", "class A {\n  bar;\n}\n") == 2);
	CHECK(lang->find_identifier_line("bar", "class A {\n  bar: number = 1;\n}\n") == 2);
	CHECK(lang->find_identifier_line("baz", "class A {\n\n\n  baz = () => {};\n}\n") == 4);

	// 修饰符前缀
	CHECK(lang->find_identifier_line("foo", "class A {\n  static foo() {}\n}\n") == 2);
	CHECK(lang->find_identifier_line("bar", "class A {\n  private static bar(): void {}\n}\n") == 2);
	CHECK(lang->find_identifier_line("run", "class A {\n  async run() {}\n}\n") == 2);
	CHECK(lang->find_identifier_line("baz", "class A {\n  get baz() {}\n}\n") == 2);

	// 重复声明取第一处（与 Godot 的 `member_lines` 覆盖语义一致：首个声明即入口）
	CHECK(lang->find_identifier_line("bar", "class A {\n  bar() {}\n  bar() {}\n}\n") == 2);

	// 没有源码 / 没有名字
	CHECK(lang->find_identifier_line("bar", "") == -1);
	CHECK(lang->find_identifier_line("", "class A {\n  bar() {}\n}\n") == -1);
}

TEST_CASE("[runtime] [jsb.lang] validate_path checks the class name derived from the file name") {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);

	// 文件名基名会成为类名，必须是合法标识符（同 C# 的 `CSharpLanguage::validate_path`）
	// 实现在 `_validate_path`（ScriptLanguageExtension 的虚函数），公开入口是 `validate_path`。
	CHECK(lang->_validate_path("res://Foo.ts").is_empty());
	CHECK(lang->_validate_path("res://dir/SomeClass.ts").is_empty());

	// 保留字与非法标识符给出错误信息
	CHECK(!lang->_validate_path("res://class.ts").is_empty());
	CHECK(!lang->_validate_path("res://1abc.ts").is_empty());
}

// `_auto_indent_code` is the `ScriptLanguageExtension` hook the editor's
// `EditorAdapter::format_code` forwards to (script_language_extension.h:310-312 ->
// :573 -> GDVIRTUAL_CALL :575, a REQUIRED virtual). The editor passes the whole
// text plus the line range the caret/selection covers and writes the returned lines
// back (script_text_editor.cpp:1804-1808); it may call once per caret range, so the
// hook must be a pure function of its inputs.
TEST_CASE("[runtime] [jsb.lang] auto_indent_code re-indents by brace depth") {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);

	// Depth accumulates over the whole text, not just the requested range, so
	// indenting a selection in the middle of a file knows how deep it starts.
	const String nested = "class A {\nfoo() {\nbar();\n}\n}\n";
	CHECK(lang->_auto_indent_code(nested, 0, 4) == "class A {\n\tfoo() {\n\t\tbar();\n\t}\n}\n");

	// Only the requested range is rewritten; lines outside it keep their text.
	CHECK(lang->_auto_indent_code(nested, 2, 2) == "class A {\nfoo() {\n\t\tbar();\n}\n}\n");

	// A line that starts with a closing bracket lines up with the line that opened
	// the block. `deltas` already accounts for that bracket, so the running depth
	// must not drop twice: `} else {` stays at the `if` level.
	const String chain = "function f() {\nif (a) {\nb();\n} else {\nc();\n}\n}\n";
	CHECK(lang->_auto_indent_code(chain, 0, 6) == "function f() {\n\tif (a) {\n\t\tb();\n\t} else {\n\t\tc();\n\t}\n}\n");

	// Brackets inside strings, template literals and comments are not depth.
	const String quoted = "const s = \"{ }\";\nif (a) {\nb();\n}\n";
	CHECK(lang->_auto_indent_code(quoted, 0, 3) == "const s = \"{ }\";\nif (a) {\n\tb();\n}\n");
	const String templated = "const t = `${{a: 1}}`;\nif (a) {\nb();\n}\n";
	CHECK(lang->_auto_indent_code(templated, 0, 3) == "const t = `${{a: 1}}`;\nif (a) {\n\tb();\n}\n");
	const String commented = "if (a) {\nb(); // }\n}\n";
	CHECK(lang->_auto_indent_code(commented, 0, 2) == "if (a) {\n\tb(); // }\n}\n");

	// Blank lines stay blank (no trailing indentation), and the call is pure: a
	// second pass over its own output changes nothing.
	const String blank = "if (a) {\n\nb();\n}\n";
	CHECK(lang->_auto_indent_code(blank, 0, 3) == "if (a) {\n\n\tb();\n}\n");
	const String once = lang->_auto_indent_code(chain, 0, 6);
	CHECK(lang->_auto_indent_code(once, 0, 6) == once);

	// A leading `)` / `]` (a continuation line) does not move: depth counts braces
	// only, so `);` stays at the depth its statement started at.
	const String multiline_call = "foo(\nbar,\nbaz\n);\nif (a) {\nb();\n}\n";
	CHECK(lang->_auto_indent_code(multiline_call, 0, 6) == "foo(\nbar,\nbaz\n);\nif (a) {\n\tb();\n}\n");

	// Degenerate ranges: reversed, out of bounds and empty input are returned as-is.
	CHECK(lang->_auto_indent_code("if (a) {\n}\n", 1, 0) == "if (a) {\n}\n");
	CHECK(lang->_auto_indent_code("", 0, 0) == "");
	CHECK(lang->_auto_indent_code("x();\n", 0, 5) == "x();\n");
}

// `_validate` is `GDVIRTUAL6RC_REQUIRED` (script_language_extension.h:393) and is
// reached by the script editor in two places: script_text_editor.cpp:176 fills the
// function outline from the "functions" key, script_text_editor.cpp:913 fills the
// error/warning gutter from "errors"/"warnings". Before this was implemented the
// hook answered `valid=true` with an empty "functions", so the outline was always
// empty; and on failure it emitted a bogus "NOT_IMPLEMENTED" error at line 0, which
// made the editor jump to an unrelated line.
TEST_CASE("[runtime] [jsb.lang] validate reports declared functions, and no bogus errors") {
	GodotJSScriptLanguageIniter initer;
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);
	REQUIRE(lang->is_initialized());

	const String source = "class A {\n"
						  "  bar() {}\n"
						  "  static baz() {}\n"
						  "  bar() {}   // declared twice: listed once\n"
						  "  // mentioned in a comment: not_a_declaration\n"
						  "  get prop() { return 1; }\n"
						  "}\n";

	const Dictionary result = lang->_validate(source, "res://test_01.ts", true, true, true, true);
	REQUIRE(result.has("valid"));
	CHECK((bool)result["valid"]);

	// the outline
	REQUIRE(result.has("functions"));
	const PackedStringArray functions = result["functions"];
	CHECK(functions.has("bar"));
	CHECK(functions.has("baz"));
	CHECK(functions.has("prop"));
	CHECK(!functions.has("not_a_declaration"));
	// a repeated declaration is listed once (it matches `_find_function`, which
	// resolves a name to its first declaration)
	// counted by hand: `PackedStringArray::count` needs the builtin method bindings,
	// which are not initialised this early in the test run.
	int bar_count = 0;
	for (const String &name : functions) {
		if (name == "bar") ++bar_count;
	}
	CHECK(bar_count == 1);

	// no `functions` requested -> no key, so the engine does no work
	CHECK(!lang->_validate(source, "res://test_01.ts", false, false, false, false).has("functions"));

	// A script whose COMPILED output is broken must be reported invalid, with a
	// position. The editable `.ts` is TypeScript and is not what gets validated --
	// `validate_script` maps the path to the compiled JS first (jsb_script.cpp:897),
	// so decorators and type annotations in the source are not mistaken for errors.
	const String broken = "res://__test_broken__.js";
	{
		const Ref<FileAccess> w = FileAccess::open(broken, FileAccess::WRITE);
		REQUIRE(w.is_valid());
		w->store_string("function ( {\n");
		w->close();
	}
	const Dictionary bad = lang->_validate("ignored", broken, false, true, false, false);
	CHECK((bool)bad["valid"] == false);
	REQUIRE(bad.has("errors"));
	const Array errors = bad["errors"];
	REQUIRE(errors.size() >= 1);
	const Dictionary first = errors[0];
	CHECK(first.has("line"));
	CHECK(first.has("column"));
	CHECK(first.has("message"));
	// and a well-formed module compiles: the project's own compiled fixture
	CHECK((bool)lang->_validate("ignored", "res://jslibs/empty.js", false, true, false, false)["valid"] == true);
	DirAccess::remove_absolute(broken);
}

TEST_CASE("[runtime] [jsb.script] inherits_script walks the base chain") {
	// 契约同 `GDScript::inherits_script`（`gdscript.cpp:1213-1229`）：沿 base 链比较**脚本对象**
	// 身份。调用方是 `container_type_validate.h:141/185`（类型化容器/属性校验）与
	// `scene_tree_dock.cpp:3838`。之前恒返回 false，会把派生脚本的对象判成与基类槽位不兼容。
	GodotJSScriptLanguageIniter initer;
	const std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();

	REQUIRE(env->load(".godot/godotjs_ext/tests/extend/child") == OK);
	REQUIRE(env->load(".godot/godotjs_ext/tests/extend/test-extend") == OK);

	// 用**源文件路径**取资源：`load_module_immediately` 解析基类时走的是
	// `convert_javascript_path(base_module->source_info.source_filepath)`，即 `.ts` 那一份
	// （`jsb_script.cpp:952`）。用 `.js` 路径取到的是另一个 Script 对象，身份比较必然不相等。
	const Ref<GodotJSScript> child = ResourceLoader::get_singleton()->load("res://tests/extend/child.ts");
	const Ref<GodotJSScript> derived = ResourceLoader::get_singleton()->load("res://tests/extend/test-extend.ts");
	REQUIRE(child.is_valid());
	REQUIRE(derived.is_valid());

	// 自己是自己的祖先（与 GDScript 一致：循环从 `this` 起步）
	CHECK(child->_inherits_script(child));
	// 派生脚本继承基类脚本
	CHECK(derived->_inherits_script(child));
	// 反向不成立
	CHECK(!child->_inherits_script(derived));
	// 空引用不做任何事
	CHECK(!child->_inherits_script(Ref<Script>()));
}
// The debug hooks feed `DebuggerMarshalls::ScriptStackDump`
// (core/debugger/remote_debugger.cpp:483-495) and the engine's error handler
// (`:113-130`), both of which go through `debug_get_current_stack_info` and the
// `debug_get_stack_level_*` family. Before this the family reported one frame at
// line 1 with an empty source, i.e. the debugger showed a call stack that never
// existed -- worse than showing none.
TEST_CASE("[runtime] [jsb.lang] debug stack hooks report a real stack, never an invented one") {
	GodotJSScriptLanguageIniter initer;
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	REQUIRE(lang != nullptr);
	REQUIRE(lang->is_initialized());

	// Outside any JS call there is no stack, and the honest answer is zero frames.
	// The old stub answered 1, which made the debugger present a phantom frame.
	const int32_t idle_count = lang->_debug_get_stack_level_count();
	CHECK(idle_count == 0);
	CHECK(lang->_debug_get_current_stack_info().is_empty());
	CHECK(lang->_debug_get_error() == "");

	// Walking a LIVE JS stack is what the debugger actually needs, and "no frames
	// when idle" alone does not prove it. Bind a native probe as a JS global, call
	// it from JS, and inspect the stack from inside the call: this is the same
	// position the engine's debug hooks run in.
	const std::shared_ptr<jsb::Environment> env = lang->get_environment();
	REQUIRE(env->load(".godot/godotjs_ext/tests/extend/child") == OK);

	// A convertible global the globals map must report, and a value the debugger
	// expression box can resolve.
	const String global_key = "__jsb_debug_global_probe__";

	// Captured from the native probe below.
	static int32_t probe_hook_count;
	static String probe_hook_source;
	static String probe_hook_function;
	static TypedArray<Dictionary> probe_hook_enumerated;
	static String probe_expr_ok;
	static String probe_expr_err;
	static jsb::DebugStackFrameList probe_frames;
	probe_frames.clear();

	v8::Isolate *isolate = env->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = env->get_context();
	v8::Context::Scope context_scope(context);

	const v8::Local<v8::Function> probe = v8::Function::New(
			context,
			[](const v8::FunctionCallbackInfo<v8::Value> &info) {
		jsb::impl::Helper::snapshot_stack(info.GetIsolate(), probe_frames, jsb::internal::settings::project::get_debug_max_stack_frames());
		GodotJSScriptLanguage *language = GodotJSScriptLanguage::get_singleton();
		probe_expr_ok = language->_debug_parse_stack_level_expression(0, "__jsb_debug_global_probe__", -1, -1);
		probe_expr_err = language->_debug_parse_stack_level_expression(0, "__jsb_absent_symbol__", -1, -1);
		// the hook's own view of the stack, which is what the debugger reads
		probe_hook_count = language->_debug_get_stack_level_count();
		probe_hook_source = language->_debug_get_stack_level_source(0);
		probe_hook_function = language->_debug_get_stack_level_function(0);
		probe_hook_enumerated = language->_debug_get_current_stack_info();
	})
												  .ToLocalChecked();
	context->Global()->Set(context, jsb::impl::Helper::new_string(isolate, "__jsb_debug_probe__"), probe).Check();

	// The expression hook is evaluated from the debugger while a JS frame is
	// paused, so it is exercised from inside the call too -- at rest there is no
	// frame, and the hook correctly refuses by level (checked further below).
	Error probe_err = OK;
	lang->eval_source(jsb_format("globalThis.%s = 12345;\n"
								 "function __jsb_debug_outer__(){ __jsb_debug_probe__(); }\n"
								 "__jsb_debug_outer__();\n",
							  global_key),
			probe_err);
	CHECK(probe_err == OK);

	// The expression box, exercised while a frame WAS current: a global resolves, and
	// a name that exists nowhere yields the VM's own ReferenceError rather than a
	// fabricated answer.
	//
	// Guarded to the backends that can snapshot a live stack: the hook refuses every
	// level when the snapshot is empty (see `_debug_parse_stack_level_expression`), and
	// jsc/web expose no stack introspection at all -- so on those the frame does not
	// exist and there is nothing to evaluate against. Asserting here would demand
	// inventing a frame. (The out-of-range refusal in this block stays unconditional:
	// it must hold on every backend, empty snapshot or not.)
#	if JSB_WITH_V8 || JSB_WITH_NODE
	CHECK(probe_expr_ok == "12345");
	CHECK(probe_expr_err.contains("__jsb_absent_symbol__"));
#	endif
	jsb_unused(probe_expr_ok);
	jsb_unused(probe_expr_err);

	// The stack the hooks themselves report during that call: the frame count the
	// engine would enumerate, the frame's function, and the standalone stack-info
	// list -- all from the same live frame, so all must be populated.
#	if JSB_WITH_V8 || JSB_WITH_NODE
	// 帧数不得超过用户设置的上限 —— 证明上限确实来自设置，而非硬编码常量。
	CHECK(probe_frames.size() <= (size_t)jsb::internal::settings::project::get_debug_max_stack_frames());
	CHECK(probe_hook_count <= jsb::internal::settings::project::get_debug_max_stack_frames());
	CHECK(probe_hook_count >= 1);
	CHECK(probe_hook_function == "__jsb_debug_outer__");
	CHECK(!probe_hook_enumerated.is_empty());
	// the frame is named, and its path is one the editor can open: never the
	// generated artifact under `.godot/`, and preferably a `res://` path.
	CHECK(!probe_hook_source.is_empty());
	CHECK(!probe_hook_source.contains("godotjs_ext"));
#	endif
	jsb_unused(probe_hook_count);
	jsb_unused(probe_hook_source);
	jsb_unused(probe_hook_function);
	jsb_unused(probe_hook_enumerated);

	// v8/node can snapshot a live stack, so inside a JS call the probe MUST have
	// seen frames. Without this the block below would pass vacuously on an empty
	// snapshot. (jsc and web expose no stack introspection, hence the guard.)
#	if JSB_WITH_V8 || JSB_WITH_NODE
	REQUIRE(!probe_frames.is_empty());
#	endif
	{
		bool named_outer = false;
		for (const jsb::DebugStackFrame &frame : probe_frames) {
			if (frame.function == "__jsb_debug_outer__") {
				named_outer = true;
				break;
			}
		}
#	if JSB_WITH_V8 || JSB_WITH_NODE
		CHECK(named_outer);
		jsb_unused(named_outer);
#	else
		jsb_unused(named_outer);
#	endif
	}

	// `_debug_get_globals` mirrors what the engine reads for the globals panel: a
	// `name -> value` map (`script_language_extension.h:643-662`). GodotJS's own
	// globals are functions (require/setTimeout/...) and the converter does not
	// turn a function into a Variant, so the map is legitimately sparse -- the
	// probe global defined above is what proves the map works.
	const Dictionary globals = lang->_debug_get_globals(-1, -1);
	CHECK(globals.has(global_key));
	CHECK((int64_t)globals[global_key] == 12345);

	// A level out of range must answer empty/zero rather than index the snapshot.
	CHECK(lang->_debug_get_stack_level_source(9999) == "");
	CHECK(lang->_debug_get_stack_level_function(9999) == "");
	CHECK(lang->_debug_get_stack_level_line(9999) == 0);

	// Scope-level introspection is not exposed by the VM's embedder API, so these
	// must stay empty/nullptr rather than invent entries. `nullptr` from the
	// instance hook is load-bearing: the engine uses it to skip the `self` entry
	// (remote_debugger.cpp:507) and to bail out of `evaluate` (:554-556).
	CHECK(lang->_debug_get_stack_level_locals(0, -1, -1).is_empty());
	CHECK(lang->_debug_get_stack_level_members(0, -1, -1).is_empty());
	CHECK(lang->_debug_get_stack_level_instance(0) == nullptr);

	// Expression evaluation runs in the runtime's scope and reports the VM's own
	// outcome: a global resolves, and a name that exists nowhere yields the VM's
	// ReferenceError rather than this runtime's guess.
	// and a level that is not on the stack is refused by name, not evaluated
	CHECK(lang->_debug_parse_stack_level_expression(9999, "1", -1, -1).contains("9999"));
}
#endif // JSB_TOOLS

} //namespace jsb::tests
