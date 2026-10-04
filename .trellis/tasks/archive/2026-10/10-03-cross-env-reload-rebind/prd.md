# 重载脚本实例时，必须在实例所属的 Environment 上重新绑定

## 目标

`GodotJSScript::instances_` 是进程级的，但每个脚本实例只绑定在某一个 `Environment` 上。
当前重载脚本时，`load_module_immediately` 会遍历**全部**实例，却只拿**调用方 env** 去断言，
因此只要 worker 线程创建过实例，重载就会直接崩溃。

要修成：重载时，每个实例都在**拥有它的那个 env** 上被重新绑定。

本任务同时解决两条明确描述了同一缺陷的 TODO：

- `src/runtime/weaver/jsb_script.cpp:942` — `will crash if reloading script instances in worker threads`
- `src/runtime/weaver/jsb_script.cpp:285` — `different env has different module state, we need to refresh the state in all envs when marking a module as dirty somewhere`

## 实测事实（均为实测，非推断）

2026-10-03 用临时探针测得，复现 3/3。探针已全部清除。

| 事实 | 证据 |
|---|---|
| `instances_` 跨线程共享 | `instances_.insert` 探针 135 次；36 个 `GodotJSScript` 中有 6 个同时收到 `worker=0` 与 `worker=1` 的插入 |
| 崩溃真实且可复现 | `rc = 0x80000003`（`STATUS_BREAKPOINT`），干净 `tsc` 后连跑 3 次 3/3 崩溃 |
| 精确陷阱位置 | `ERROR: FATAL: Condition "!(env->verify_object(obj))" is true.` @ `jsb_script.cpp:948` |
| 调用链 | `ObjectTemplate::constructor`(`jsb_transpiler.h:328`) → `ScriptClassInfo::instantiate`(`jsb_class_info.cpp:830`) → `_can_instantiate`(`jsb_script.cpp:177`) → `_is_valid`(`jsb_script.h:189`) → `ensure_module_loaded`(`jsb_script.h:286`) → `load_module_immediately` |
| `verify_object` 是 per-env 的 | `jsb_environment.h:608-609` → `object_db_.has_object(p_pointer)`；`object_db_` 是 `Environment` 成员 |
| headless 下无法从 JS 侧触发重载 | `jsb_resource_saver.cpp:126` 的 `source_changed` 恒为 false（`.uid` 已存在）；`:133` 需要 `EditorInterface::get_singleton()`，headless 直接拒绝 |
| `mark_as_reloading` 要求文件真的变了 | `jsb_module.cpp:48-57`：同时比对 mtime **和** md5，否则返回 `NoChanges` |
| 模块文件是 tsc 产物，不是 `.ts` | 探针显示 `source_filepath = res://.godot/godotjs_ext/**/*.js` |

## `load_module_immediately` 每一步的 env 依赖

`:906` 的 `env` 是 `JSEnvironment(get_path(), true)`，经 `Environment::_access()` 解析为
**调用线程自己的** env（`jsb_script_language.cpp:81` → `jsb_environment.cpp:762`）。
所以下面每一步都只对调用方 env 生效——这也正是当前代码无法服务其他 env 的原因。

| 行 | 步骤 | 依赖 | 别的 env 能否代做 |
|---|---|---|---|
| :894 | `if (loaded_) return` | 脚本级全局 | 不适用 |
| :897 | `.ts` → `.js` 路径解析 | 仅路径 | 可以，与 env 无关 |
| :908-910 | `loaded_ = true`、`base.unref()`、`source_changed_cache` | 脚本级全局 | 不适用 |
| :912 | `env->load(path, &module)` | **调用方 env 的 `module_cache_`** | **不能**。每个 env 各有 cache；`_load_module` 在 `jsb_environment.cpp:1088` 靠 `is_reloading()` 决定是否重解析，`:1099` 重新插入 |
| :930 | `env->find_script_class(...)` → `script_class_info_` | 读的是调用方 env 的 `script_classes_`，但**结果**是共享快照 | 读受 env 约束，落库不受 |
| :936 | `_apply_pending_source_doc` | Godot 侧文档 | 可以 |
| :947 | `jsb_check(obj->get_script() == this)` | 脚本级全局 | 可以 |
| :948 | `jsb_check(env->verify_object(obj))` | **拥有该对象的 env** | **不能**。这就是崩溃点 |
| :950 | `env->get_script_class()->native_class_name` | **调用方 env 的 `script_classes_`** | **不能** |
| :951 | `env->rebind(obj, id)` | **拥有该对象的 env**（isolate 亲和，`jsb_environment.cpp:1368-1373`） | **不能** |
| :954 | `obj->set_script(Ref<Script>())` | 该对象的绑定归属 | **不能** |
| :966 | `env->load(base_script_module_id)` | **调用方 env 的 `module_cache_`** | **不能** |
| :968 | `ResourceLoader::load(base js path)` | Godot 侧资源 | 可以 |
| :975 | `_update_exports()` | 用 `script_class_info_`（共享），且仅 `JSB_TOOLS` | 可以 |
| :990/:992 | return / stub 日志 | — | — |

**所以"只发通知让对方 rebind"会做漏三处**：

- **`:912` 在拥有者 env 里重新解析模块**。不做的话，拥有者的 `module_cache_` 里永远存着旧的
  `JavaScriptModule`，`is_reloading()` 一直挂着，之后每次查找都走 reload 分支却从不刷新
  （`jsb_environment.cpp:1088`）。
- **`:966` 在拥有者 env 里加载基类模块**。同理，拥有者的 cache 里会没有基类模块。
- **`:950` 类匹配判定**。拥有者必须用**自己**的 `script_classes_` 比对 `native_class_name`，
  不能用调用方的表。

这正是 `:285` 那条 TODO：`mark_as_reloading` 只标脏调用方 env 的 module cache
（`jsb_environment.cpp:1070`），**其他所有 env 都留着陈旧模块和陈旧 `script_classes_`**，
它们的实例永远不会被重绑。

`:954`（`set_script(Ref<Script>())`）同样要留给拥有者 env：它是在摘掉对象身上的脚本，
这是关于该对象绑定关系的决定。

## 一致性：最终一致（已定）

本任务只做最终一致。跨线程 JS 调用本来就不被允许，所以别的线程无法在某个 env 重绑它的
对象之前去观察那些对象；唯一真实的窗口是**接收 env 自己**——它已经接受了通知、但还没应用
重绑时，就执行了依赖那些未重绑对象的逻辑。

这个窗口是已知的后续问题，不在本任务范围：要在通知处加一条**中文 TODO**，写明「本环境
接受消息后、重绑生效前，可能仍执行触及未重绑对象的逻辑」，并指出后续要处理（推迟到安全
时机，或在入口处校验）。

## 方案要点

**跨线程传递的是脚本本身，不是实例。** 传递 `Ref<GodotJSScript>`，在消息在途期间持有引用，
处理完毕后 unreference。这样就不会出现脚本被提前释放的问题。

**归属查找。** `ScriptInstance::get_script_instance<GodotJSScriptInstance>(obj)` 之后取
`get_env()`（`jsb_script_instance.h:292`），并判空。不是 `GodotJSScriptInstance` 的实例
（placeholder、其它脚本语言）没有 env，不承载任何逻辑，不处理（见 R3）。

注意：`jsb_script_instance.h:88-93` 有两个 `if constexpr` 分支，条件完全相同、结论相反，
导致 `get_script_instance<GodotJSShadowScriptInstance>` **恒返回 `nullptr`**。目前潜伏未爆
（现有两个调用点都没用它），本任务避开它，统一用 `GodotJSScriptInstance` 重载。

**接收端不重跑 `load_module_immediately`。** `loaded_` 在 `:291` 已经是全局 false，但它本身
只是"下次进循环会重新加载"的信号，不是刷新指令。若让接收端自己走一遍
`ensure_module_loaded`，它会重复执行调用方已经做完的事（`base.unref()`、
`source_changed_cache`、`_update_exports` 这些全局副作用），并且和 `loaded_` 的时序纠缠。
接收端应当只执行刷新自己那份状态所需的动作。

**幂等。** 一次重载对每个 env 至多通知一次。幂等性来自"通知本身就是按（重载, env）投递"，
而不是在脚本上挂"已通知过"的标记——那种标记会误伤后续合法的重载。

**ShadowRealm——同步执行，但必须校验宿主。** shadow realm 与宿主线程同线程、但拥有独立
isolate，所以 isolate 亲和性和 worker 一样；不同的是它不需要投递。按用户决定，它走现有的
shadow 消息入口（`Environment::handle_message`，`jsb_environment.cpp:610`，由
`Environment::update` 在 `:546-559` 消费）并**同步执行**。

用户指出的关键前提：**只有调用者确实是该 realm 的宿主 env 时才能直接投递**。否则就会在同
一个线程上、用一个无关 env 去执行 guest JS——线程断言抓不到（线程相同），但那恰恰是本绑定
明令禁止的跨 env JS 执行。"是否是宿主"必须从 realm 自己记录的宿主来解析
（`Environment::wrap(host_isolate)`，即 `jsb_shadow_realm.cpp:538` 的写法），不能靠线程身份
判断，因为同一线程上可能存在多个 Environment。若某 realm 建在别的线程的 env 上，它就不走
同步路径，落到延迟路径。

**Web 后端——复用现有通道，无需特例。** 已核实：

- `jsb_worker.cpp:53-58` 直接 include `emscripten/threading.h` 与 `pthread.h`；`:389`
  `pthread_self()`；`:459` `emscripten_set_main_loop_arg(&WorkerImpl::_web_loop_tick, ...)`。
  web worker 是真 pthread，跑自己的 v8 isolate 和自己的循环，isolate 亲和性与 native 完全一致。
- 投递通道已存在且在用：native 走 `Worker::on_receive(worker->id_, WorkerMessage(...))`
  （`jsb_worker.cpp:1144`），web 走
  `jsbi_PostMessage(pthread_id, runtime, data_sp, transfer_id, transfer_sp)`（`:1109`）。
  两者都汇入接收 env 的队列，在 `Environment::update` 里消费。

所以通知复用现有 worker 通道即可；后端相关的差异只在编码/解码，且不能假设两端都是 JS 可见的值。

## 需求

- **R1** `load_module_immediately` 只重绑**属于调用方 `Environment`** 的实例，且不得对属于
  其他 env 的实例断言。
- **R2** 属于其他 env 的实例，最终必须**由那个 env 自己**完成重绑。跳过不是修复本身；跳过
  只是让拥有者能去做它自己那部分。
- **R3** `ScriptInstance` 不是 `GodotJSScriptInstance` 的实例（placeholder、其它脚本语言）
  不承载 env，不做重载，也不参与通知。
- **R4** 重载不得因此阻塞，也不得丢失属于 worker/shadow env 的实例。

## 约束

- **C1** `GodotJSScript` 与 `ScriptClassInfo` 是不区分 env 的、面向 Godot 侧的类型。**不要**
  给它们加 env，也不要假设存在 per-env 的脚本变体。
- **C2** 本绑定不支持跨线程 JS 访问。跨线程的工作必须发生在所属线程上。
- **C3** `Environment::rebind` 是 isolate 亲和的（`jsb_environment.cpp:1368-1373` 会压入所属
  isolate 的 `HandleScope` / `Context::Scope`）。它只能在所属线程执行——这是「在哪里做」的
  技术约束，**不是**推迟做的理由。
- **C4** `Environment::_rebind` 本来就容忍外来对象（`jsb_environment.cpp:1379-1382`：
  `JSB_LOG(Fatal, "bad instance"); return;`）。debug 与 release 必须一致，当前不一致。
- **C5** 不要放宽 `jsb_script.cpp:947` 的 `jsb_check(Variant(obj->get_script()) == Variant(this))`
  —— 它成立（脚本资源共享），且是真实的不变式。
- **C6** 跨线程通知携带**脚本**，不携带实例。传 `Ref<GodotJSScript>`，处理完后 unreference。
- **C7** worker env 从消息队列里取待处理工作，发生在 `Environment::update`
  （`jsb_environment.cpp:546-559`）。shadow realm 的消息同样在那里被消费。

## 待定的设计细节（已收敛）

- **通知范围：已定** —— 通知所有 `module_cache_` 含该模块的 env，不只是实例拥有者（方案 B）。
  env 的缓存一旦陈旧，后续每次查找都会走 reload 分支却拿到旧模块，只通知实例拥有者治标不治本。
- 接收端刷新函数的具体形态：**已定** —— 新增独立的 `refresh_in_env(Environment*)`，
  不复用 `load_module_immediately`（见 `design.md` §5）。
- `jsb_script_instance.h:88-93` 的潜伏 bug 是否顺手修：属于独立缺陷，可能另开任务；
  本任务统一用 `get_script_instance<GodotJSScriptInstance>` 绕开。

## 验收标准

- [x] **AC1** 复现测试从 `rc=0x80000003` + `FATAL ... verify_object` 变为 `rc=0`，并输出
      `GODOTJS_TEST_PROJECT_COMPLETED`，两条 `[worker-reload]` 日志都出现。
- [x] **AC2** worker 创建的实例确实换到了新 prototype，**从 worker 侧验证**（不能只证明"没崩"）。
- [x] **AC3** 主线程实例的重绑行为不变；`static-members` / `cross-environment` / 编辑器热重载
      无回归。
- [x] **AC4** `jsb_script.cpp:947` 的 `jsb_check` 仍在位。
- [x] **AC5** v8 与 quickjs-ng 两套本地门禁通过：doctest 全绿，集成测试输出
      `GODOTJS_TEST_PROJECT_COMPLETED`。
- [x] **AC6** 所有 prototype 切换都发生在拥有该实例的 env 上。
- [x] **AC7** 通知处有中文 TODO，说明「接受消息后、重绑生效前」的窗口及后续处理方向。
- [x] **AC8** 临时测试接口 `__jsb_test_reload_script__` 在该场景被 C++ 侧覆盖后移除
      （按用户指示）。

## 复现装置（已在工作区，未提交）

`project/tests/worker-reload/` 四个新文件 + `project/tests/start.ts` 注册，以及一个
`JSB_TESTS_ENABLED` 保护的全局 `__jsb_test_reload_script__`
（`jsb_builtins.h` / `jsb_builtins.cpp` / `jsb_environment.cpp:368`）。

测试改的是 tsc 产物 `res://.godot/godotjs_ext/**/worker-reload-target.js`，因为
`mark_as_reloading` 比对的就是这个文件的 hash。崩溃会导致 `finally` 来不及执行，该文件
无法就地复原；这是可接受的——该路径已被 gitignore（`.godot/`），且 CI 每次运行前都会
删除 `.tsbuildinfo` 并重跑 `tsc`。

## 范围

- 主要涉及 `src/runtime/weaver/jsb_script.cpp`；若最终采用"即时通知"则还包括
  `src/runtime/bridge/*` 的通知路径。
- **明确不做**：把 `instances_` 改成 per-env、给 `GodotJSScript` 加 env 字段、任何无关的绑定改动。

> 结项说明（2026-10-05）：AC1/AC2 在开发期已实测通过；AC5 复跑 v8 doctest 1224/1224 +
> 集成 `GODOTJS_TEST_PROJECT_COMPLETED`，quickjs-ng doctest 1236/1236；AC8 按用户指示连同
> `project/tests/worker-reload/` 一并移除，故 AC1/AC2 的复现装置不再留在仓库中。
