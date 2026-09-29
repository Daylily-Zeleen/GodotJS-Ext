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
