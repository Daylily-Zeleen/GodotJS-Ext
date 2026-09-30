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
#endif // JSB_TOOLS

} //namespace jsb::tests
