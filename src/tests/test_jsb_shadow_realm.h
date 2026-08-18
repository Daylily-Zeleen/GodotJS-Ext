#pragma once

#include "../bridge/jsb_shadow_realm.h"
#include "../weaver/jsb_script_instance.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/object.hpp>

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
TEST_CASE("[jsb] ShadowRealm: cold Environment::_access resolves the main environment") {
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
TEST_CASE("[jsb] ShadowRealm: Environment::_access during guest JS execution resolves the shadow realm environment") {
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
	CHECK(js_instance->get_env() != main_env); // 修复前：等于 main_env（bug）；修复后：等于 ShadowRealm 环境
}

} //namespace jsb::tests
