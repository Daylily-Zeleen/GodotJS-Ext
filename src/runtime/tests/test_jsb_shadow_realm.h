/************************************************************************/
/*  test_jsb_shadow_realm.h                                             */
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

#include "../bridge/jsb_shadow_realm.h"
#include "../weaver/jsb_script_instance.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/node.hpp>

// 测试背景（见 src/bridge/jsb_shadow_realm.cpp 顶部 TODO 注释）：
// ShadowRealm 会在宿主线程上创建新的 Environment，导致同一线程存在多个 Environment。
// GodotJSScript 各入口通过 JSEnvironment -> Environment::_access()（无参版）解析"当前环境的
// Environment"，该解析不能依赖"线程"（否则无法区分主环境与 ShadowRealm 环境），必须依赖
// "调用来源"：有 JS 正在执行时返回该 JS 所属环境，无 JS 执行（纯 Godot 调用）时返回线程常驻环境。
namespace jsb::tests {

// 契约 1（Fix 2，防回归）：无 JS 执行时（纯 Godot 冷调用），Environment::_access() 必须
// 确定性地命中主环境，即使同线程已存在多个由 ShadowRealm 创建的额外 Environment。
//
// 注意：由于 godot-cpp HashSet 按插入顺序迭代（主环境永远先插入），修复前的线程扫描
// 恰好也返回主环境，因此本用例无法区分修复前后——它验证的是修复后行为不回归，
// Fix 2 的"排除 ShadowRealm 环境"语义由代码审查保证。
TEST_CASE("[runtime] [jsb] ShadowRealm: cold Environment::_access resolves the main environment") {
	GodotJSScriptLanguageIniter initer;
	jsb::Environment *main_env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(main_env != nullptr);

	// 冷调用基线：创建 realm 之前必须返回主环境
	CHECK(jsb::Environment::_access().get() == main_env);

	// 在同线程创建两个 ShadowRealm -> 同线程注册两个额外的（ShadowRealm）Environment
	{
		Error err;
		GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__realm1 = new JSShadowRealm();
globalThis.__realm2 = new JSShadowRealm();
)--",
				err);
		REQUIRE(err == OK);
	}

	// 调用栈上没有任何 JS 在执行：多次调用都必须确定性地返回主环境，
	// 且不能命中任一 ShadowRealm 环境（若命中则与 main_env 不等）。
	for (int i = 0; i < 10; ++i) {
		const std::shared_ptr<jsb::Environment> accessed = jsb::Environment::_access();
		CHECK(accessed != nullptr);
		CHECK(accessed.get() == main_env);
	}
}

// 复现（Fix 1）：ShadowRealm 的 JS 正在执行时，GodotJSScript 解析到的环境必须是
// ShadowRealm 环境本身，而不是主环境。
// 触发路径：guest JS 实例化一个 Godot 脚本类 ->
//   GodotJSScript::instance_and_native_object_create -> GodotJSScript::instance_create
//   -> JSEnvironment -> Environment::_access()
// 修复前：_access() 按线程扫描返回主环境 -> 实例被错误绑定到主环境（跨 isolate 使用句柄）；
// 修复后：_access() 按"当前正在执行的 isolate"返回 ShadowRealm 环境。
TEST_CASE("[runtime] [jsb] ShadowRealm: Environment::_access during guest JS execution resolves the shadow realm environment") {
	GodotJSScriptLanguageIniter initer;
	jsb::Environment *main_env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(main_env != nullptr);

	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__realm = new JSShadowRealm({ allowImportAnyModule: true });
)--",
			err);
	REQUIRE(err == OK);

	// guest JS 创建 Godot 脚本实例，并把实例的 instance_id 传回主环境。
	// 注意：evaluate 内部用 `(function() { return (%s); })()` 包裹源码，必须是单表达式，
	// 因此这里用 IIFE 承载多条语句。
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
globalThis.__instance_id = globalThis.__realm.evaluate(`
(function() {
  try {
    const gd = require("godot");
    const mod = require(".godot/godotjs_ext/test_01");
    const inst = new mod.default();
	const inst_id = gd.is_instance_valid(inst) ? inst.get_instance_id() : 0;
    return new String(inst_id).toString();
  } catch (e) {
    return "GUEST_ERR: " + (e && e.message ? e.message : String(e));
  }
})()
`);
)--",
			err);
	REQUIRE(err == OK);

	int64_t instance_id = 0;
	{
		JSB_TESTS_EXECUTION_SCOPE(main_env);
		v8::Isolate *isolate = main_env->get_isolate();
		v8::Local<v8::Context> context = main_env->get_context();
		v8::Local<v8::Value> id_val;
		REQUIRE(context->Global()->Get(context, impl::Helper::new_string(isolate, "__instance_id")).ToLocal(&id_val));
		REQUIRE(id_val->IsString());
		const String str = jsb::impl::Helper::to_string(isolate, id_val.As<v8::String>());
		instance_id = str.to_int(); // 未转换为 get_instance_id() 的 uint64_t, 这里仅用于判断实例是否创建成功
	}
	REQUIRE(instance_id != 0);

	// 该 Godot 对象对应的脚本实例必须绑定在 ShadowRealm 环境（而非主环境）
	Object *obj = ObjectDB::get_instance(ObjectID(instance_id));
	REQUIRE(obj != nullptr);
	ScriptInstance *script_instance = ScriptInstance::get_script_instance(obj);
	REQUIRE(script_instance != nullptr);
	REQUIRE(!script_instance->is_shadow());
	GodotJSScriptInstance *js_instance = dynamic_cast<GodotJSScriptInstance *>(script_instance);
	REQUIRE(js_instance != nullptr);
	CHECK(js_instance->get_env() != nullptr);
	CHECK(js_instance->get_env() != main_env);

	REQUIRE(obj->is_class("Node")); // 本测试中约定测试对象是一个 Node.
	static_cast<Node *>(obj)->queue_free(); // 释放测试对象
}

// 回归（修复前会 trap）：guest isolate 里的异常必须变成调用方（host）realm 里的 Error，
// 而不是崩溃、也不是把 undefined 交给调用方。修复前 `evaluate` 用 `ToLocalChecked()` 取
// `Script::Compile`/`Run` 的返回值，而异常时它们返回空 MaybeLocal -> `jsb_check` 触发 trap。
TEST_CASE("[runtime] [jsb] ShadowRealm: an exception from evaluate becomes a host-realm Error") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__throw_realm = new JSShadowRealm();
globalThis.__throw_probe = (function () {
	try {
		globalThis.__throw_realm.evaluate(`(function () { throw new Error("boom"); })()`);
		return "NO-THROW";
	} catch (e) {
		return (e instanceof Error ? "Error:" : "NotError:") + (e && e.message ? e.message : String(e));
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__throw_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	// 调用方拿到的是 host realm 的 Error，文本里带着 guest 的原始信息
	CHECK(text.begins_with("Error:"));
	CHECK(text.contains("boom"));
}

// 回归：`importValueSync` 失败时要在调用方 realm 抛 Error（以前抛的是字符串，没有 stack、instanceof 不成立）。
TEST_CASE("[runtime] [jsb] ShadowRealm: importValueSync failure throws a host-realm Error") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__import_realm = new JSShadowRealm();
globalThis.__import_probe = (function () {
	try {
		globalThis.__import_realm.importValueSync("res://__no_such_module__", "x");
		return "NO-THROW";
	} catch (e) {
		return (e instanceof Error ? "Error:" : "NotError:") + (e && e.message ? e.message : String(e));
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__import_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.begins_with("Error:"));
}

// 回归：`importValue`（异步 API）失败时要用 Error 去 reject（以前是字符串，而且是"同步抛 + reject"双重报错）。
TEST_CASE("[runtime] [jsb] ShadowRealm: importValue rejects a host-realm Error") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__reject_realm = new JSShadowRealm();
globalThis.__reject_probe = "PENDING";
globalThis.__reject_realm.importValue("res://__no_such_module__", "x").then(
	() => { globalThis.__reject_probe = "RESOLVED"; },
	(e) => { globalThis.__reject_probe = (e instanceof Error ? "Error:" : "NotError:") + (e && e.message ? e.message : String(e)); });
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	// promise 的 then/catch 是微任务：手动跑一次 checkpoint
	isolate->PerformMicrotaskCheckpoint();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__reject_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.begins_with("Error:"));
}

// 回归：跨隔离区的错误带上 name/message/stack 与可携带的自定义字段；
// 带不走的字段路径进包裹错误的未携带清单（`CrossEnvError.untransferred`）。
TEST_CASE("[runtime] [jsb] ShadowRealm: the error record carries fields and lists what it could not") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__record_realm = new JSShadowRealm();
globalThis.__record_probe = (function () {
	try {
		globalThis.__record_realm.evaluate(`
(function () {
	const e = new Error("with-fields");
	e.code = 42;
	e.text = "carried";
	e.flag = true;
	e.detail = { a: "x", b: [1, 2] };
	e.plain = { obj: 1 };
	e.fn = function () {};
	const inner = new Error("inner-cause");
	inner.tip = "t";
	e.cause = inner;
	throw e;
})()`);
		return "NO-THROW";
	} catch (e) {
		// `e` 是 CrossEnvError 包装（只表示"从别的 realm 抛了出来"），源异常在 `e.cause`
		const src = e && e.cause;
		const lost = e && e.untransferred;
		return JSON.stringify({
			isError: e instanceof Error,
			isCrossEnv: e.name === "CrossEnvError",
			message: e.message,
			hasStack: typeof e.stack === "string" && e.stack.length > 0,
			causeIsError: src instanceof Error,
			causeMessage: src && src.message,
			code: src ? src.code : null,
			text: src ? src.text : null,
			flag: src ? src.flag : null,
			detail: typeof (src ? src.detail : undefined),
			plain: typeof (src ? src.plain : undefined),
			causeTip: src && src.cause && src.cause.tip,
			lost: Array.isArray(lost) ? lost.slice().sort() : null
		});
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__record_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.contains(R"("isError":true)"));      // CrossEnvError extends Error
	CHECK(text.contains(R"("isCrossEnv":true)"));
	CHECK(text.contains(R"("message":"shadowRealm: with-fields")")); // message = "<realm>: <cause.message>"
	CHECK(text.contains(R"("hasStack":true)"));
	// 源异常重建成 wrapper 的 cause：字段都在那儿
	CHECK(text.contains(R"("causeIsError":true)"));
	CHECK(text.contains(R"("causeMessage":"with-fields")"));
	// 只搬 JS 基础类型
	CHECK(text.contains(R"("code":42)"));
	CHECK(text.contains(R"("text":"carried")"));
	CHECK(text.contains(R"("flag":true)"));
	// 数组 / 普通对象 / 函数一律不搬运，只登记路径（由用户自己显式转换）
	CHECK(text.contains(R"("detail":"undefined")"));
	CHECK(text.contains(R"("plain":"undefined")"));
	CHECK(text.contains(R"(["detail","fn","plain"])"));      // wrapper.untransferred
	// 源异常自己的 cause 也递归重建了
	CHECK(text.contains(R"("causeTip":"t")"));
}

// 回归（有界性）：错误记录的采集不能触发 getter、不能被环/深链/大数组拖垮，未携带清单本身也要有上限。
TEST_CASE("[runtime] [jsb] ShadowRealm: the error record is bounded and never runs getters") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__bound_realm = new JSShadowRealm();
globalThis.__cap_realm = new JSShadowRealm();
globalThis.__getter_calls = 0;

// 例 1：getter 不被触发；环、深链、大数组不失控
globalThis.__bound_probe = (function () {
	try {
		globalThis.__bound_realm.evaluate(`
(function () {
	const e = new Error("bounded");
	Object.defineProperty(e, "lazy", { get: () => { globalThis.__getter_calls++; return 1; }, enumerable: true, configurable: true });
	const cyclic = {}; cyclic.self = cyclic; e.cycle = cyclic;
	let deep = {}; const root = deep;
	for (let i = 0; i < 64; ++i) { deep.next = {}; deep = deep.next; }
	e.deep = root;
	e.big = new Array(100000).fill(1);
	throw e;
})()`);
		return "NO-THROW";
	} catch (e) {
		const lost = (e.untransferred || []);
		return JSON.stringify({
			isError: e instanceof Error,
			getterCalls: globalThis.__getter_calls,
			lostHasLazy: lost.indexOf("lazy") >= 0,
			lostHasCycle: lost.some((x) => String(x).indexOf("cycle") === 0),
			lostHasDeep: lost.some((x) => String(x).indexOf("deep") === 0)
		});
	}
})();

// 例 2：未携带清单本身有上限（200 个函数字段 -> 64 条 + 一个 "..." 标记）
globalThis.__cap_probe = (function () {
	try {
		globalThis.__cap_realm.evaluate(`
(function () {
	const e = new Error("cap");
	for (let i = 0; i < 200; ++i) { e["fn" + i] = function () {}; }
	throw e;
})()`);
		return "NO-THROW";
	} catch (e) {
		const lost = (e.untransferred || []);
		return JSON.stringify({ lostCount: lost.length, hasMarker: lost.indexOf("...") >= 0 });
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();

	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__bound_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.contains(R"("isError":true)"));
	CHECK(text.contains(R"("getterCalls":0)")); // accessor 一律不读
	CHECK(text.contains(R"("lostHasLazy":true)"));
	CHECK(text.contains(R"("lostHasCycle":true)"));
	CHECK(text.contains(R"("lostHasDeep":true)"));

	v8::Local<v8::Value> cap_probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__cap_probe")).ToLocal(&cap_probe));
	REQUIRE(cap_probe->IsString());
	const String cap_text = jsb::impl::Helper::to_string(isolate, cap_probe.As<v8::String>());
	CHECK(cap_text.contains(R"("lostCount":65)"));
	CHECK(cap_text.contains(R"("hasMarker":true)"));
}

// 回归：`console.log(err)` 不能崩（`stringify` 只把 `GodotObject` 当绑定对象），且 `cause` 懒物化仍可用。
TEST_CASE("[runtime] [jsb] ShadowRealm: logging a CrossEnvError is safe") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__log_realm = new JSShadowRealm();
globalThis.__log_probe = (function () {
	try {
		globalThis.__log_realm.evaluate(`(function () { throw new Error("log-me"); })()`);
		return "NO-THROW";
	} catch (e) {
		console.log(e); // 关键：走 BridgeHelper::stringify（非 GodotObject 原生类不能在那里断言）
		const cause = e.cause; // 首次访问：懒物化
		return JSON.stringify({
			isError: e instanceof Error,
			isCrossEnv: e instanceof CrossEnvError,
			name: e.name,
			causeMessage: cause && cause.message,
			hasOwnCause: Object.prototype.hasOwnProperty.call(e, "cause"),
		});
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__log_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.contains(R"("isError":true)"));
	CHECK(text.contains(R"("isCrossEnv":true)"));
	CHECK(text.contains(R"("name":"CrossEnvError")"));
	CHECK(text.contains(R"("causeMessage":"log-me")"));
	CHECK(text.contains(R"("hasOwnCause":true)")); // 首次读 `cause` 后变成自有属性（懒物化缓存）
}

// 回归：原始值异常也包成 CrossEnvError，本体塞进 `cause`。
TEST_CASE("[runtime] [jsb] ShadowRealm: a primitive thrown value is wrapped with the primitive in cause") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__prim_realm = new JSShadowRealm();
globalThis.__prim_probe = (function () {
	try {
		globalThis.__prim_realm.evaluate(`(function () { throw "primitive-boom"; })()`);
		return "NO-THROW";
	} catch (e) {
		return JSON.stringify({
			isCrossEnv: e instanceof CrossEnvError,
			isError: e instanceof Error,
			message: e.message,
			causeType: typeof e.cause,
			causeValue: String(e.cause),
		});
	}
})();
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__prim_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.contains(R"("isCrossEnv":true)"));
	CHECK(text.contains(R"("isError":true)"));
	CHECK(text.contains(R"("message":"shadowRealm: primitive-boom")"));
	CHECK(text.contains(R"("causeType":"string")"));
	CHECK(text.contains(R"("causeValue":"primitive-boom")"));
}

// 回归：同一 guest 对象在**同一个宿主环境**里二次传给宿主时，必须复用同一个宿主侧包装器
// （`CrossWrapper::try_get_cache` 的命中路径），而不是每次新建一个。
//
// 这条断言针对一个真实修过的语义错：注册表里存的是 `CrossWrapper *`，但取回时曾写成
// `get_obj(p_host_isolate)` —— 那是**guest 对象**，而缓存语义要求返回**宿主侧包装器**
// （函数包装 / `ObjectCrossWrapper` 的 Proxy）。已加 `host_obj_`（宿主侧弱句柄）修正。
//
// 断言方式：让 guest 把同一个对象返回两次，宿主侧比较两个包装器 `===`。取消复用（每次都新建
// Proxy）时 `same` 会变 false —— 即这条断言能真正区分"命中缓存"与"每次都新建"。
TEST_CASE("[runtime] [jsb] ShadowRealm: a guest object is wrapped once per host isolate (cache reuse)") {
	GodotJSScriptLanguageIniter initer;
	Error err;
	GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
const { JSShadowRealm } = require("godot.shadowRealm");
globalThis.__reuse_realm = new JSShadowRealm();
// NOTE `evaluate` 把源码包成 `(function() { return (%s); })()`，因此**每个参数必须是单表达式**
//      （赋值表达式可以，`globalThis.x = ...;` 这种带分号的语句不行 —— 实测 err=36 / ERR_PARSE_ERROR）。
//      下面这次赋值把同一个 guest 对象挂到 guest 的 globalThis 上，供后续两次取用。
globalThis.__reuse_realm.evaluate(`globalThis.__shared = { marker: "shared-obj" }`);
globalThis.__reuse_probe = JSON.stringify({
	// 同一个 guest 对象连续两次传出来：必须命中缓存 -> 同一个宿主包装器
	same: globalThis.__reuse_realm.evaluate(`globalThis.__shared`) === globalThis.__reuse_realm.evaluate(`globalThis.__shared`),
	// 不同 guest 对象不能相等（防止"一律返回同一个对象"这种退化实现蒙对）
	diff: globalThis.__reuse_realm.evaluate(`globalThis.__shared`) === globalThis.__reuse_realm.evaluate(`({ marker: "other" })`),
	// guest 侧自读（不经宿主 Proxy）
	guestRead: String(globalThis.__reuse_realm.evaluate(`globalThis.__shared.marker`)),
	// 宿主侧经 Proxy 读同一字段：走 `proxy_get` -> `transfer_key` -> `_transfer_string`。
	// 回归意义：`_transfer_string` 曾把键写成带尾随 NUL 的字符串（V8 的 `WriteUtf8`
	// 返回值含 NUL、而 shim 不含，代码把它当显式长度传给了 `NewFromUtf8`），
	// 于是 guest 侧按键取属性永远 miss —— 这条断言就是那个 bug 的哨兵。
	hostRead: String(globalThis.__reuse_realm.evaluate(`globalThis.__shared`).marker)
});
)--",
			err);
	REQUIRE(err == OK);

	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();
	v8::Local<v8::Context> context = env->get_context();
	v8::Local<v8::Value> probe;
	REQUIRE(context->Global()->Get(context, jsb::impl::Helper::new_string(isolate, "__reuse_probe")).ToLocal(&probe));
	REQUIRE(probe->IsString());
	const String text = jsb::impl::Helper::to_string(isolate, probe.As<v8::String>());
	CHECK(text.contains(R"("same":true)")); // 命中缓存：同一个宿主包装器
	CHECK(text.contains(R"("diff":false)")); // 不同 guest 对象仍是不同包装器
	CHECK(text.contains(R"("guestRead":"shared-obj")")); // guest 对象本身可用
	CHECK(text.contains(R"("hostRead":"shared-obj")")); // 宿主经 Proxy 读到 guest 字段（键转移不带尾随 NUL）
}

} //namespace jsb::tests
