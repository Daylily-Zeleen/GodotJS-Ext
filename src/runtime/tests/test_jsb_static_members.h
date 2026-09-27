/************************************************************************/
/*  test_jsb_static_members.h                                           */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
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

#include "../bridge/jsb_class_info.h"
#include "../bridge/jsb_shared_statics.h"
#include "../weaver/jsb_script.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <initializer_list>

namespace jsb::tests {

// Parsing of the annotated static members of a script class: the annotation gate
// (`ClassConstants` / `ClassSharedStatics` symbols), the R2.3 Variant type whitelist, enum
// normalization and the recursive read-only flagging of container constants.
//
// The JS class deliberately declares members of every rejected shape, so a regression that
// widens the accepted set shows up as an extra entry in `constants`.
static constexpr char k_static_members_source[] = R"--((function() {
class C {}
C.PLAIN_INT = 42;
C.PLAIN_FLOAT = 1.5;
C.PLAIN_STR = "hello";
C.PLAIN_BOOL = true;
C.PLAIN_BIG = 123n;
C.PLAIN_NULL = null;
C.UNDEF = undefined;
C.FN = function () {};
C.ANON = 7;
var E; (function (E) { E[E["A"] = 0] = "A"; E[E["B"] = 1] = "B"; })(E || (E = {}));
C.ENUM = E;
var ES; (function (ES) { ES["X"] = "x"; ES["Y"] = "y"; })(ES || (ES = {}));
C.SENUM = ES;
const gd = require("godot");
C.ARR = gd.GArray.create([1, 2, 3]);
C.NESTED = gd.GArray.create([gd.GArray.create([1, 2]), 3]);
C.DICT = gd.GDictionary.create({ a: 1 });
C.VEC = new gd.Vector2(1, 2);
C.LIT_ARR = [1, 2, 3];
C.LIT_OBJ = { a: 1 };
C.SHARED = 5;
C.CONFLICT = 9;
// `@…shared()` 侧的容器形态：静态变量**不受** R2.3 常量白名单约束，
// 但跨桥 `Variant` 转换走 `js_to_gd_var`，而**无提示**路径对 JS 原生数组返回 false
// （`research/phase0-findings.md` §0.2）⇒ 必须由 `IsArray()` 回退到 `Variant::ARRAY`
// 提示重载（见 `jsb_class_info.cpp` 的 `_shared_static_setter` 与初值播种处）。
// 这两条此前**无任何覆盖**，是 design.md §10 遗留的「阶段 3 阻塞项」。
C.SHARED_ARR = [1, 2, 3];
C.SHARED_DICT = { a: 1 };
globalThis.__C = C;
return 1;
})()
)--";

TEST_CASE("[runtime] [jsb] script static members parsing") {
	GodotJSScriptLanguageIniter initer;

	std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		NativeClassID native_class_id;
		CHECK(!!env->expose_godot_object_class("Object", &native_class_id));

		Error err;
		env->eval_source(k_static_members_source, (int)sizeof(k_static_members_source) - 1, "testcase_static_members.js", err);
		REQUIRE(err == OK);

		v8::Local<v8::Value> class_val;
		REQUIRE(context->Global()->Get(context, impl::Helper::new_string(isolate, "__C")).ToLocal(&class_val));
		REQUIRE(class_val->IsObject());
		const v8::Local<v8::Object> class_obj = class_val.As<v8::Object>();

		// annotation payload: constants and shared statics are both arrays of member names
		const v8::Local<v8::Array> constants_arr = v8::Array::New(isolate);
		{
			uint32_t slot = 0;
			auto add_constant = [&](const char *p_name) {
				constants_arr->Set(context, slot++, impl::Helper::new_string(isolate, p_name)).Check();
			};
			add_constant("PLAIN_INT");
			add_constant("PLAIN_FLOAT");
			add_constant("PLAIN_STR");
			add_constant("PLAIN_BOOL");
			add_constant("PLAIN_BIG");
			add_constant("PLAIN_NULL");
			add_constant("UNDEF");
			add_constant("FN");
			add_constant("ENUM");
			add_constant("SENUM");
			add_constant("ARR");
			add_constant("NESTED");
			add_constant("DICT");
			add_constant("VEC");
			add_constant("LIT_ARR");
			add_constant("LIT_OBJ");
			// annotated as BOTH a constant and a shared static: the constant must win and the
			// shared-static annotation must be dropped (see the assertion below)
			add_constant("CONFLICT");
		}
		class_obj->Set(context, env->get_symbol(Symbols::ClassConstants), constants_arr).Check();

		const v8::Local<v8::Array> shared_arr = v8::Array::New(isolate);
		shared_arr->Set(context, 0, impl::Helper::new_string(isolate, "SHARED")).Check();
		shared_arr->Set(context, 1, impl::Helper::new_string(isolate, "CONFLICT")).Check();
		class_obj->Set(context, env->get_symbol(Symbols::ClassSharedStatics), shared_arr).Check();

		ScriptClassID class_id;
		const ScriptClassInfoPtr class_info = env->add_script_class(class_id);
		REQUIRE(!!class_info);
		class_info->native_class_id = native_class_id;

		REQUIRE(internal::_parse_script_class_iterate(context, class_info, class_obj));

		// primitive constants survive with the expected Variant type and value
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_INT");
			REQUIRE(info != nullptr);
			CHECK(info->kind == ScriptConstantKind::Value);
			CHECK(info->value.get_type() == Variant::INT);
			CHECK((int64_t)info->value == 42);
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_FLOAT");
			REQUIRE(info != nullptr);
			CHECK(info->value.get_type() == Variant::FLOAT);
			CHECK((double)info->value == 1.5);
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_STR");
			REQUIRE(info != nullptr);
			CHECK(info->value.get_type() == Variant::STRING);
			CHECK((String)info->value == "hello");
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_BOOL");
			REQUIRE(info != nullptr);
			CHECK(info->value.get_type() == Variant::BOOL);
			CHECK((bool)info->value);
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_BIG");
			REQUIRE(info != nullptr);
			CHECK(info->value.get_type() == Variant::INT);
			CHECK((int64_t)info->value == 123);
		}
		{
			// `null` is a legitimate NIL constant, unlike `undefined`
			const ScriptConstantInfo *info = class_info->constants.getptr("PLAIN_NULL");
			REQUIRE(info != nullptr);
			CHECK(info->value.get_type() == Variant::NIL);
		}

		// numeric enum: normalized into Dictionary{name: int}, reverse-mapping keys dropped
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("ENUM");
			REQUIRE(info != nullptr);
			CHECK(info->kind == ScriptConstantKind::Enum);
			const Dictionary enum_dict = info->value;
			CHECK(enum_dict.size() == 2);
			CHECK(enum_dict.has("A"));
			CHECK(enum_dict.has("B"));
			CHECK((int64_t)enum_dict["A"] == 0);
			CHECK((int64_t)enum_dict["B"] == 1);
			CHECK(!enum_dict.has(0));
			CHECK(enum_dict.is_read_only());
		}

		// a string enum has no reverse mapping: exposed as a plain Dictionary constant
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("SENUM");
			REQUIRE(info != nullptr);
			CHECK(info->kind == ScriptConstantKind::Container);
			const Dictionary enum_dict = info->value;
			CHECK(enum_dict.size() == 2);
			CHECK((String)enum_dict["X"] == "x");
			CHECK((String)enum_dict["Y"] == "y");
			CHECK(enum_dict.is_read_only());
		}

		// container constants are read-only, and the flag is applied recursively
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("ARR");
			REQUIRE(info != nullptr);
			CHECK(info->kind == ScriptConstantKind::Container);
			const Array array = info->value;
			CHECK(array.size() == 3);
			CHECK(array.is_read_only());
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("NESTED");
			REQUIRE(info != nullptr);
			const Array array = info->value;
			CHECK(array.is_read_only());
			const Array inner = array[0];
			CHECK(inner.is_read_only()); // recursion must reach the nested container
		}
		{
			const ScriptConstantInfo *info = class_info->constants.getptr("DICT");
			REQUIRE(info != nullptr);
			CHECK(info->kind == ScriptConstantKind::Container);
			const Dictionary dict = info->value;
			CHECK((int64_t)dict["a"] == 1);
			CHECK(dict.is_read_only());
		}

		// R2.3 whitelist: Godot value wrappers convert successfully but must still be rejected
		CHECK(!class_info->constants.has("VEC"));
		// plain JS literals cannot be converted into a Variant at all
		CHECK(!class_info->constants.has("LIT_ARR"));
		CHECK(!class_info->constants.has("LIT_OBJ"));
		// the `typeof` prefilter drops `undefined` and functions
		CHECK(!class_info->constants.has("UNDEF"));
		CHECK(!class_info->constants.has("FN"));
		// the annotation is the only gate: an unannotated member is ignored
		CHECK(!class_info->constants.has("ANON"));

		// shared statics are collected by name and stay writable
		CHECK(class_info->static_variables.has("SHARED"));
		CHECK(!class_info->static_variables.has("PLAIN_INT"));

		// GDScript parity for the property usage: a static variable carries `SCRIPT_VARIABLE` and
		// nothing else. Measured on the engine: GDScript's own `static var` reaches
		// `_get_property_list` with usage 4096. `STORAGE` would advertise the value as
		// node-serializable state, which it is not - it lives in the shared store.
		{
			const ScriptStaticVariableInfo *shared_info = class_info->static_variables.getptr("SHARED");
			REQUIRE(shared_info != nullptr);
			CHECK((shared_info->details.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) != 0);
			CHECK((shared_info->details.usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR)) == 0);
		}

		// A member annotated as both a constant and a shared static resolves as a constant: the
		// shared-static annotation is dropped, so it is not declared writable anywhere. Without the
		// guard the same name would land in both maps, and `_get` (constant) would disagree with
		// `_set` (store) about what the member is.
		CHECK(class_info->constants.has("CONFLICT"));
		CHECK(!class_info->static_variables.has("CONFLICT"));
	}

	env.reset();
}

// The process-wide store behind `@bind.exposed.shared()`. Two properties carry the whole cross-
// environment contract, and neither is observable from the parsing test above:
//  - `ensure` must NOT clobber a value already in use - every JS isolate imports the module
//    independently and would otherwise reset the store to the initializer on each import;
//  - `retain` must drop the names whose annotation is gone while keeping the surviving values -
//    that is what makes a hot reload refresh the member set instead of leaking stale members.
// The second parse below is the exact shape a reload produces (same class object, narrower
// annotation array), so it exercises `retain` without needing a live editor reload.
TEST_CASE("[runtime] [jsb] script shared static store") {
	GodotJSScriptLanguageIniter initer;

	const StringName module_id = StringName("res://testcase_shared_static_store.ts");
	SharedStatics::clear();

	std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		NativeClassID native_class_id;
		CHECK(!!env->expose_godot_object_class("Object", &native_class_id));

		Error err;
		env->eval_source(k_static_members_source, (int)sizeof(k_static_members_source) - 1, "testcase_shared_static_store.js", err);
		REQUIRE(err == OK);

		v8::Local<v8::Value> class_val;
		REQUIRE(context->Global()->Get(context, impl::Helper::new_string(isolate, "__C")).ToLocal(&class_val));
		REQUIRE(class_val->IsObject());
		const v8::Local<v8::Object> class_obj = class_val.As<v8::Object>();

		auto set_shared_names = [&](std::initializer_list<const char *> p_names) {
			const v8::Local<v8::Array> arr = v8::Array::New(isolate);
			uint32_t slot = 0;
			for (const char *name : p_names) {
				arr->Set(context, slot++, impl::Helper::new_string(isolate, name)).Check();
			}
			class_obj->Set(context, env->get_symbol(Symbols::ClassSharedStatics), arr).Check();
		};

		ScriptClassID class_id;
		const ScriptClassInfoPtr class_info = env->add_script_class(class_id);
		REQUIRE(!!class_info);
		class_info->native_class_id = native_class_id;
		class_info->module_id = module_id;

		// the first parse declares both names and seeds the store from the class field (`C.SHARED = 5`)
		set_shared_names({ "SHARED", "GONE" });
		REQUIRE(internal::_parse_script_class_iterate(context, class_info, class_obj));
		CHECK(class_info->static_variables.has("SHARED"));
		CHECK(class_info->static_variables.has("GONE"));

		Variant value;
		REQUIRE(SharedStatics::get(module_id, "SHARED", value));
		CHECK((int64_t)value == 5);
		REQUIRE(SharedStatics::set(module_id, "SHARED", 7));

		// the re-parse sees the installed accessor (not the field), so the seed comes from the store
		set_shared_names({ "SHARED" });
		REQUIRE(internal::_parse_script_class_iterate(context, class_info, class_obj));
		CHECK(!class_info->static_variables.has("GONE"));
		CHECK(!SharedStatics::get(module_id, "GONE", value));
		REQUIRE(SharedStatics::get(module_id, "SHARED", value));
		CHECK((int64_t)value == 7); // the live value must survive the re-parse

		// A write of an unconvertible value must be *dropped*, not stored: `js_to_gd_var` leaves its
		// output default-constructed when it fails, so storing that unconditionally would silently
		// wipe the shared value to NIL for every environment. A plain JS object is the shape that
		// still reaches the conversion (undefined/symbol/function are filtered earlier).
		{
			const v8::Local<v8::Object> plain = v8::Object::New(isolate);
			plain->Set(context, impl::Helper::new_string(isolate, "a"), v8::Integer::New(isolate, 1)).Check();
			// the installed accessor routes this write through `_shared_static_setter`
			CHECK(class_obj->Set(context, impl::Helper::new_string(isolate, "SHARED"), plain).ToChecked());
			REQUIRE(SharedStatics::get(module_id, "SHARED", value));
			CHECK((int64_t)value == 7); // unchanged - the rejected write did not become NIL
		}

		// a later environment loading the same module must observe the live value, not the initializer
		SharedStatics::ensure(module_id, "SHARED", 99);
		REQUIRE(SharedStatics::get(module_id, "SHARED", value));
		CHECK((int64_t)value == 7);

		// an undeclared name is not silently created by a write
		CHECK(!SharedStatics::set(module_id, "NOPE", 1));
		CHECK(!SharedStatics::get(module_id, "NOPE", value));
		// 容器型静态变量：`js_to_gd_var` 的**无提示**路径对 JS 原生数组返回 false，
		// 必须回退到 `Variant::ARRAY` 提示重载才能装进 store（design.md §10 的「阶段 3 阻塞项」）。
		// 这条路径此前**无任何覆盖**。
		set_shared_names({ "SHARED", "SHARED_ARR", "SHARED_DICT" });
		REQUIRE(internal::_parse_script_class_iterate(context, class_info, class_obj));
		{
			// 原生数组 ⇒ 提示重载 ⇒ 新建 Array（不与作者的字面量共享 `_p`）
			REQUIRE(SharedStatics::get(module_id, "SHARED_ARR", value));
			REQUIRE(value.get_type() == Variant::ARRAY);
			const Array arr = value;
			CHECK(arr.size() == 3);
			CHECK((int)arr[0] == 1);
			CHECK((int)arr[2] == 3);
			// 纯 JS 对象字面量在**任何**路径都转不成 Dictionary ⇒ 槽位存在但值为 NIL
			REQUIRE(SharedStatics::get(module_id, "SHARED_DICT", value));
			CHECK(value.get_type() == Variant::NIL);
		}
		// 通过安装的访问器写入原生数组，同样必须走提示重载而非被丢弃
		{
			const v8::Local<v8::Array> js_arr = v8::Array::New(isolate);
			js_arr->Set(context, 0, v8::Integer::New(isolate, 9)).Check();
			js_arr->Set(context, 1, v8::Integer::New(isolate, 8)).Check();
			CHECK(class_obj->Set(context, impl::Helper::new_string(isolate, "SHARED_ARR"), js_arr).ToChecked());
			REQUIRE(SharedStatics::get(module_id, "SHARED_ARR", value));
			CHECK(value.get_type() == Variant::ARRAY);
			const Array arr = value;
			CHECK(arr.size() == 2);
			CHECK((int)arr[0] == 9);
		}
	}

	SharedStatics::clear();
	env.reset();
}

// `_get_members()` is what the remote debugger reads to tell script members apart from exported
// properties (`scene_debugger_object.cpp:104`). It has no scripting binding, so this C++ entry point
// is the only way to exercise it. It reports the script's *own* instance members only, like
// `GDScript::get_members` (`gdscript.cpp:919-925`): the single caller walks the base chain itself
// (`scene_debugger_object.cpp:111-122`) and files each script's members under that script, so
// merging the base here would report an inherited member twice.
TEST_CASE("[runtime] [jsb] script members: own members only") {
	GodotJSScriptLanguageIniter initer;

	const Ref<GodotJSScript> target = ResourceLoader::get_singleton()->load("res://tests/static-members/static-members-target.ts", jsb_typename(GodotJSScript));
	REQUIRE(target.is_valid());
	{
		const TypedArray<StringName> members = target->_get_members();
		CHECK(members.has(StringName("tag")));
		CHECK(members.has(StringName("baseOnly")));
		// a static member is not an instance member
		CHECK(!members.has(StringName("score")));
	}

	const Ref<GodotJSScript> derived = ResourceLoader::get_singleton()->load("res://tests/static-members/static-members-derived.ts", jsb_typename(GodotJSScript));
	REQUIRE(derived.is_valid());
	{
		// `_get_members()` reports nothing at all for a script whose module did not attach (`loaded_` false
		// or the parsed class info invalid), which would make the `tag_count` assertion below report `0 == 1`
		// for a reason that has nothing to do with member collection. Keep the two apart.
		CHECK(derived->_is_valid());
		const TypedArray<StringName> members = derived->_get_members();
		// the derived script re-declares `tag`, so its own list carries it exactly once
		int64_t tag_count = 0;
		for (int64_t index = 0; index < members.size(); ++index) {
			//NOTE compared by name, not with `== StringName(...)`: `StringName::operator==` is an
			//     identity test on the interned pointer (`_data == p_name._data`), and the name
			//     built here and the one the parser interned are two different entries - a
			//     `StringName` literal is created as a *static* name
			//     (`string_name_new_with_latin1_chars`) while the parser interns through
			//     `string_name_new_with_utf8_chars`. They coalesce on MSVC/clang but not on
			//     g++/Linux (`has()`/`find()` still agree there because `Array` compares STRING
			//     and STRING_NAME through `StringLikeVariantComparator`). The contract under test
			//     is the member *name*, so compare that.
			if (StringName((String)members[index]) == StringName(::String("tag"))) {
				++tag_count;
			}
		}
		CHECK(tag_count == 1);
		// `baseOnly` is declared by the base alone: the debugger collects it from the base script,
		// so it must NOT appear in the derived script's list. Merging the base chain in would
		// report it twice.
		CHECK(!members.has(StringName("baseOnly")));
	}
}

// `_get_method_info()` is reachable *before* the module was ever loaded:
// `Object::get_method_argument_count` (`object.cpp:1964`) -> `ScriptExtension::
// get_script_method_argument_count` (`script_language_extension.h:114-124`) -> the fallback
// `Script::get_script_method_argument_count` (`script_language.cpp:122-136`) -> `get_method_info()`.
// The fallback is taken for any name our own-methods-only hook does not answer: an unknown name,
// or a name that belongs to ClassDB rather than to the script (the case below uses the latter's
// shape - an arbitrary identifier).
// So `_get_method_info()` has to self-load rather than assert `loaded_` - and `jsb_check` is a no-op
// in release, which is how the previous build ended up returning `{"name": p_method}` for *every*
// name (that shadowed the signal/constant lookups of `reduce_identifier_from_base`).
// The script is built the way the resource loader builds it (`jsb_resource_loader.cpp:129-136`) but
// without touching the resource cache, so `loaded_ == false` at entry is structural here rather than
// dependent on which test ran first.
TEST_CASE("[runtime] [jsb] script method info: queried before the module is loaded") {
	GodotJSScriptLanguageIniter initer;

	const String path = "res://tests/static-members/static-members-namespaced.ts";
	Ref<GodotJSScript> script;
	script.instantiate();
	REQUIRE(script->load_source_code(path) == OK);
	script->set_path(path);

	// an unknown name yields an empty dictionary, i.e. `MethodInfo::name == StringName()`; the
	// analyzer ignores such a probe and falls through to its signal/constant lookups
	CHECK(script->_get_method_info(StringName("__not_a_method__")).is_empty());

	const Dictionary known = script->_get_method_info(StringName("greet"));
	REQUIRE(known.has("name"));
	CHECK((StringName)known["name"] == StringName("greet"));
}

// §10-c：`_get_constants()` 会**在模块加载前**被调用 —— `EditorFileSystem` 扫描期对每个脚本
// 走 `_get_global_script_class` ⇒ `Script::load` ⇒ 构造 `GodotJSScript`，随后 `get_constants()`
// 才被读取。故它必须像 `_get_method_info()` 那样 `ensure_module_loaded()` 自加载，而不能断言
// `loaded_`。构造方式与上面两个用例同型（`load_source_code` + `set_path`，不碰资源缓存）。
TEST_CASE("[runtime] [jsb] script constants: queried before the module is loaded") {
	GodotJSScriptLanguageIniter initer;

	const String path = "res://tests/static-members/static-members-target.ts";
	Ref<GodotJSScript> script;
	script.instantiate();
	REQUIRE(script->load_source_code(path) == OK);
	script->set_path(path);

	const Dictionary constants = script->_get_constants();
	// 自加载生效 ⇒ 真能读到常量；若退化成空表，常量在分析期全部解析不到
	REQUIRE(constants.has("N"));
	CHECK((int)constants["N"] == 42);
	CHECK(constants.has("S"));
	CHECK((String)constants["S"] == "hello");
	// 常量是解析期快照：不该混入静态变量（静态变量走 `_get_property_list` / `_get` 那一面）
	CHECK(!constants.has("score"));
}

// `_get_script_method_argument_count()` is the `Script`-level channel of the declared parameter
// count. Two contracts are pinned here, both reachable before the module was ever loaded (same
// pre-load construction as the case above):
//  - the count is read off the function source text (`_count_declared_parameters`,
//    `jsb_class_info.cpp`). `Function.length` stops at the first default value or rest parameter and
//    would report 1 for `add(a, b = 2, ...rest)`, while the declared count is 2 - the rest parameter
//    is excluded, matching `GDScriptFunction::_argument_count` (`gdscript_byte_codegen.cpp:35-39`)
//    and the documented `Callable::get_argument_count` ("including optional arguments");
//  - a miss yields an **empty** Variant, not INT 0: `ScriptExtension::get_script_method_argument_count`
//    (`script_language_extension.h:114-124`) only accepts INT and otherwise delegates to
//    `Script::get_script_method_argument_count`, which reads `get_method_info()`. INT 0 would claim
//    every name is a 0-argument method of ours and that fallback would never be taken.
//
// Own methods only, like `GDScript::get_script_method_argument_count` (`gdscript.cpp:370-383`): the
// base chain is walked by the caller (`Object::get_method_argument_count`, `object.cpp:794-805`) or
// by `GodotJSScriptInstanceBase::get_method_argument_count`.
TEST_CASE("[runtime] [jsb] script method argument count") {
	GodotJSScriptLanguageIniter initer;

	const String target_path = "res://tests/static-members/static-members-target.ts";
	Ref<GodotJSScript> target;
	target.instantiate();
	REQUIRE(target->load_source_code(target_path) == OK);
	target->set_path(target_path);

	const Variant no_params = target->_get_script_method_argument_count(StringName("greet"));
	REQUIRE(no_params.get_type() == Variant::INT);
	CHECK((int64_t)no_params == 0);

	const Variant defaults_and_rest = target->_get_script_method_argument_count(StringName("add"));
	REQUIRE(defaults_and_rest.get_type() == Variant::INT);
	CHECK((int64_t)defaults_and_rest == 2);

	// 签名清单（sidecar）**真的被消费了**：`_get_script_method_list()` 只有在清单加载成功时才带
	// `args` / `default_args` / `flags` —— 没有清单时 `overloads` 为空，只交出名字。上面对
	// `add` 的计数断言不具备这个鉴别力（回退扫描同样得到 2），所以这里直接查方法表。
	// 清单是编辑器侧构建产物；本套件已硬依赖 `res://.godot/godotjs_ext/test_01.js`
	// （见 `check_required_files`），同一次编辑器产出即保证它存在。缺失时**明确报出**而不是静默通过，
	// 否则这条断言会退化成"什么都没测到"。
	{
		const String sig_path = "res://.godot/godotjs_ext/tests/static-members/static-members-target.sig";
		if (FileAccess::file_exists(sig_path)) {
			const TypedArray<Dictionary> methods = target->_get_script_method_list();
			bool found_add = false;
			for (int i = 0; i < methods.size(); ++i) {
				const Dictionary entry = methods[i];
				if (StringName(entry.get("name", StringName())) != StringName("add")) {
					continue;
				}
				found_add = true;
				const Array args = entry.get("args", Array());
				CHECK(args.size() == 2);
				const Array default_args = entry.get("default_args", Array());
				CHECK(default_args.size() == 1);
				// 剩余参数只置 VARARG（不入 args），否则 GDScript 会判 Too many arguments。
				CHECK(((int64_t)entry.get("flags", (int64_t)0) & (int64_t)GDEXTENSION_METHOD_FLAG_VARARG) != 0);
				// 参数名与类型都来自清单（`number` -> FLOAT）。
				if (args.size() == 2) {
					CHECK(StringName(Dictionary(args[0]).get("name", StringName())) == StringName("a"));
					CHECK((int)Dictionary(args[0]).get("type", -1) == Variant::FLOAT);
					CHECK(StringName(Dictionary(args[1]).get("name", StringName())) == StringName("b"));
				}
				// 返回值来自清单：`number` -> FLOAT（非 NIL ⇒ 不是 void）。
				CHECK((int)Dictionary(entry.get("return", Dictionary())).get("type", -1) == Variant::FLOAT);
			}
			CHECK_MESSAGE(found_add, "the signature manifest was not reflected into the method list");
		} else {
			MESSAGE("signature manifest absent at " << sig_path << "; skipped the manifest-integration assertions");
		}
	}

	// `test_01.ts` 的信号 `test_signal!: Signal<(value: number) => void>` 也必须带参数表
	// （design.md §12 / A7）。清单里的 `kind=1` 记录只有在被消费时才反映到信号表上。
	// 走 `ResourceLoader` 取**缓存实例**：`load_source_code + set_path` 构造新对象会与已缓存的
	// 同一路径冲突（实测 "Another resource is loaded from path"）。本套件已有同型先例
	// （`script members: own members only`）。
	{
		const String sig_path = "res://.godot/godotjs_ext/test_01.sig";
		if (FileAccess::file_exists(sig_path)) {
			const Ref<GodotJSScript> signal_script = ResourceLoader::get_singleton()->load("res://test_01.ts", jsb_typename(GodotJSScript));
			REQUIRE(signal_script.is_valid());
			REQUIRE(signal_script->_is_valid());
			const TypedArray<Dictionary> signals = signal_script->_get_script_signal_list();
			bool found = false;
			for (int i = 0; i < signals.size(); ++i) {
				const Dictionary entry = signals[i];
				if (StringName(entry.get("name", StringName())) != StringName("test_signal")) {
					continue;
				}
				found = true;
				const Array args = entry.get("args", Array());
				REQUIRE(args.size() == 1);
				CHECK(StringName(Dictionary(args[0]).get("name", StringName())) == StringName("value"));
				CHECK((int)Dictionary(args[0]).get("type", -1) == Variant::FLOAT);
			}
			CHECK_MESSAGE(found, "the signal manifest entry was not reflected into the signal list");
		} else {
			MESSAGE("test_01 manifest absent; skipped the signal-integration assertions");
		}
	}

	CHECK(target->_get_script_method_argument_count(StringName("__not_a_method__")).get_type() != Variant::INT);

	const String derived_path = "res://tests/static-members/static-members-derived.ts";
	Ref<GodotJSScript> derived;
	derived.instantiate();
	REQUIRE(derived->load_source_code(derived_path) == OK);
	derived->set_path(derived_path);

	// `_is_valid()` forces the module load, so the assertions below cannot pass merely because the
	// derived script failed to load (an invalid script reports no methods at all).
	CHECK(derived->_is_valid());
	// `add` is declared by the base alone and this hook reports own methods only
	CHECK(derived->_get_script_method_argument_count(StringName("add")).get_type() != Variant::INT);
}

} //namespace jsb::tests
