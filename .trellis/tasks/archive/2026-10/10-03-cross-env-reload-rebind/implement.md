# 实施计划

## 前置：spec 上下文

实现前需读：

- `.trellis/spec/godotjs-ext/cpp/index.md`（开发前检查单）
- `.trellis/spec/godotjs-ext/cpp/coding-standards.md`
- `.trellis/spec/godotjs-ext/cpp/architecture-constraints.md`（涉及 Environment 生命周期、
  worker 线程）
- `.trellis/spec/godotjs-ext/cpp/godot-cpp-usage.md`
- `.trellis/spec/godotjs-ext/build/index.md`（构建命令）
- `.trellis/spec/godotjs-ext/test/index.md`（测试约定）

## 步骤

### 步骤 1：调用方分流（`src/runtime/weaver/jsb_script.cpp`）

改 `load_module_immediately` 的 `:944-958` 循环：

1. 保留 `:947` 的 `jsb_check(Variant(obj->get_script()) == Variant(this))`
2. 删掉 `:948` 的 `jsb_check(env->verify_object(obj))`
3. 取归属：`ScriptInstance::get_script_instance<GodotJSScriptInstance>(obj)` → `get_env()`，
   判空
4. `owner_env == env` → 走原逻辑（`:950-955`）
5. `owner_env != env` → 记入通知集合
6. `owner_env == nullptr` → 跳过，不通知（R3）

循环后按 design.md §7 收集通知目标：遍历 `Environment::get_all_environments()`，跳过调用方
自身，按 `module_cache_.find(module_id)` 过滤，命中者入列表，去重后投递。

验证：此时崩溃消失，但接收端还没实现刷新 → 测试应转为 `rc=0` 且 worker 实例仍是旧
prototype（AC1 先达成，AC2 待步骤 3）。

### 步骤 2：通知通道（`src/runtime/bridge/jsb_environment.h/.cpp`）

1. 定义内部通知载荷：`Ref<GodotJSScript>` + 目标 env 定位信息。用
   `internal::DoubleBuffered`（与 `inbox_` 同款，`jsb_environment.h:196`）承载，接收端消费
   时 unreference（design.md §4 / C6）
2. `Environment` 增加投递入口（thread safe，不碰 isolate）与消费入口（在 `update()` 中，
   位置对齐 `:544-563` 的现有 worker 消息处理，满足 C7）
3. 消费入口加中文 TODO：`【已知遗留】本环境接受重载通知后、重绑生效前，仍可能执行触及未重绑
   对象的逻辑；后续需推迟到安全时机或在入口处校验`（design.md §8、AC7）

验证：本地构建通过；此时消息能投递但无处理逻辑，日志可见通知到达。

### 步骤 3：接收端刷新（`src/runtime/weaver/jsb_script.cpp`）

新增 `GodotJSScript::refresh_in_env(jsb::Environment *p_env)`，按 design.md §5 的六步：

1. `p_env->mark_as_reloading(module_id)`；`NoChanges` 则直接返回（幂等）
2. `p_env->load(path, &module)`
3. 用 `p_env->get_script_class(...)` 判类匹配
4. 对本 env 拥有的实例 `p_env->rebind(obj, module->script_class_id)`；不匹配则
   `set_script(Ref<Script>())` + Warning
5. `p_env->load(base_script_module_id)`
6. 不碰 `loaded_` / `script_class_info_` / `base` / `_update_exports`

遍历 `instances_` 时加 `GodotJSScriptLanguage::mutex_`（与 `:943` 同锁）。

验证：**AC2** —— 测试改为从 worker 侧读取其实例的 prototype 相关状态，确认已换新（不能只
证明没崩）。

### 步骤 4：ShadowRealm 投递（`src/runtime/bridge/jsb_shadow_realm.cpp` 或环境侧）

按 design.md §6.2：调用者 == realm 的 host env（用 `Environment::wrap(host_isolate)` 判定，
**不靠线程身份**）→ 走 `Environment::handle_message`（`jsb_environment.cpp:610`）同步执行；
否则落延迟路径。

验证：shadow realm 场景下 reload 不崩溃，且同步路径确实被走到。

### 步骤 5：web 后端

按 design.md §6.1：web 走 `jsbi_PostMessage(...)`（`jsb_worker.cpp:1109`），native 走
`Worker::on_receive(...)`（`:931`）。web 的编码/解码不能假设两端是 JS 可见的值。

验证：web 构建通过（本地无法运行，需在 CI 或说明限制）。

## 验证

每次 push 前跑本地门禁（v8 + quickjs-ng 两套）：

```
scons platform=windows target=editor dev_build=yes tests=yes -j2
scons platform=windows target=editor use_quickjs_ng=yes dev_build=yes tests=yes -j2
godot --headless --path ./project --jsb-run-tests            # 期待 Status: SUCCESS!
rm -f project/.godot/.tsbuildinfo && pnpm -C project exec tsc
godot --audio-driver Dummy --headless --path ./project --quit-after 240000
                                                          # 期待 GODOTJS_TEST_PROJECT_COMPLETED
```

注意：复现测试会改写 tsc 产物 `res://.godot/godotjs_ext/**/worker-reload-target.js`，
崩溃时无法复原，因此每次跑集成测试前必须先删 `.tsbuildinfo` 重跑 `tsc`（与 CI 一致）。

## 完成标准

- [ ] AC1 复现测试从崩溃转为 `rc=0` + `GODOTJS_TEST_PROJECT_COMPLETED`
- [ ] AC2 worker 实例确实换到新 prototype（worker 侧验证）
- [ ] AC3 无回归：`static-members` / `cross-environment` / 编辑器热重载
- [ ] AC4 `jsb_check(obj->get_script() == this)` 仍在位
- [ ] AC5 v8 与 quickjs-ng 两套门禁全绿
- [ ] AC6 所有 prototype 切换发生在拥有实例的 env 上
- [ ] AC7 通知处有中文 TODO
- [ ] AC8 移除 `__jsb_test_reload_script__`：`jsb_builtins.h`、`jsb_builtins.cpp`、
      `jsb_environment.cpp:368` 三处整体回退；移除后确认该场景已被 C++ 侧覆盖

## 范围外

- 把 `instances_` 改成 per-env
- 给 `GodotJSScript` 加 env 字段
- `jsb_script_instance.h:88-93` 的潜伏 bug（另开任务）
- 立即一致性（同步等待 worker 完成）——本任务只做最终一致
## 实施状态（2026-10-03）

### 已完成并验证

- 步骤 1~5 全部实现。
- **崩溃已消除**：复现测试从 `rc=0x80000003` + `FATAL: Condition "!(env->verify_object(obj))"` 变为
  `rc=0` 并输出 `GODOTJS_TEST_PROJECT_COMPLETED`。
- 门禁全绿：v8 doctest 82/82（1224 assertions）、quickjs-ng doctest 84/84（1236 assertions）、
  v8 集成测试 `rc=0`。

### 与原设计的一处偏差（已实测确认）

通知的发出点从 `load_module_immediately` 改到了 `_reload`。原因（探针实测）：

`loaded_` 是脚本级共享 bool。谁先走到 `ensure_module_loaded` 谁就消费它。若从
`load_module_immediately` 发通知，worker 往往会先一步进入该函数并把 `loaded_` 置回 true，
于是主线程的 `ensure_module_loaded` 直接早退、循环根本不进，**通知永远不会发出**。
`_reload` 才是「脚本确实变了」这个事实的权威来源。

### AC2 未达成（遗留）

探针实测链路是通的：

```
send caller=main(caller_worker=0) target=worker(target_worker=1)
notify: queued for other thread
consumed SCRIPT_RELOAD
refresh_in_env ENTER
refresh_in_env mark_as_reloading=2      (Requested)
visit ... owner=worker p_env=worker     (归属匹配)
[wr] rebinding obj=...                  (在 worker 线程上执行了 rebind)
refresh_in_env rebound=1
```

但 worker env 的模块 **parse 计数没有增加**（恒为 1），模块体没有在该 env 重新求值，
因此 `SetPrototype` 落到的是同一个 prototype，对象实际没有换。

`refresh_in_env` 已经：
- 用 `mark_as_reloading` 把 worker 自己那份 `JavaScriptModule` 标脏（返回 `Requested`，
  说明确实命中了 worker 的 module 且文件 hash 已变）；
- 再用**同一个 id** 调 `p_env->load(module_id, &module)`。

即标脏与加载指向同一个 module 条目，但 `_load_module` 没有重新执行模块体。这一环尚未定位，
怀疑在 `resolver->load(...)` 是否真的重跑 JS 模块体。**这是下一步要查的**。

测试里对应的断言已降级为带 `TODO(AC2 未达成)` 的观测记录，不再断言 prototype 变化——
不能留一个已知失败的断言。

### AC8 未执行（有意）

`__jsb_test_reload_script__` 保留。它是当前唯一能在 headless 下驱动 reload 的入口，
AC2 未达成意味着 C++ 侧还没有等价覆盖，移除会直接失去复现能力。

### 下一步

1. 查 `_load_module` 的 `resolver->load(...)` 为何没有重跑模块体（AC2 的最后一环）。
2. AC2 达成后，再决定 `__jsb_test_reload_script__` 的去留。

---

## 已解决：AC2 达成（2026-10-03）

### 根因不在引擎，在测试

`test-worker-reload.ts` 改写模块产物的方式是错的：

```ts
const writer = FileAccess.open(modulePath, FileAccess.ModeFlags.WRITE); // 打开即截断
writer.store_string(FileAccess.get_file_as_string(modulePath));        // 读到的已是空串
```

`ModeFlags.WRITE` 在 `open` 时就截断文件，所以 `get_file_as_string` 返回空串，
`worker-reload-target.js` 被写成只剩一条注释。重新求值一个空模块不产生新类、
不产生新 prototype，于是 tag 永远是 `gen1`、模块级计数永远是 1。

这条错误把「重载链路本身是好的」误导向了「模块没有重新求值」的假结论。

### 定位过程（关键一步）

探针一度全部"沉默"，原因是 **Godot 的 `String::sprintf` 不支持 `%p` / `%llu`** ——
这两类格式符会让整条 `JSB_LOG` 输出失效（只留下 `at: ...` 格式错误标注）。
换成 `%s` + `String::num_int64()` 后一次拿到决定性证据：

```
refresh_in_env env=<worker> tid=83 id=...target.js mark=2   ← worker 线程消费了通知
RELOAD-BRANCH  env=<worker> tid=83 module=...               ← 走了 reload 分支
EVALUATE       env=<worker> ...target.js                    ← 模块体重新求值
after-load     env=<worker> parses=1                        ← 但计数器没涨 → 源文件本身有问题
```

另有一个干扰项：**worker 线程的 `JSB_LOG` 是缓冲到进程结束才 flush 的**，
所以 `[wr]` 行在日志里的位置完全不能用来判断时序，早期据此得出的「时序太早」结论是错的。

### 修复

1. 测试先读后写；结束时把产物还原成 `tsc` 原样，避免跨轮残留脏文件。
2. `waitForNewTag`：等 worker 真正报出新 prototype，替换原来「等两帧」的写法
   （worker 消费通知 + 重新求值 + 重绑不受主线程帧数约束）。

### 验证（干净构建，无探针）

```
[worker-reload] worker module parse count: 1      ← 首轮读取
[worker-reload] worker module parse count: 2      ← worker 重新求值
[worker-reload] worker prototype after reload: gen2  ← worker 线程上完成重绑
GODOTJS_TEST_PROJECT_COMPLETED
```

- `rc=0x00000000`，无 `[wr]` 残留
- v8 doctest 82/82（1224 assertions）
- quickjs-ng doctest 84/84（1236 assertions）
- jsc 语法门：`jsb_environment.cpp` / `jsb_module_resolver.cpp` / `jsb_script.cpp` /
  `jsb_builtins.cpp` / `jsb_worker.cpp` / `jsb_shadow_realm.cpp` 全部 rc=0

### AC8 的处置

`__jsb_test_reload_script__` **保留**。C++ 侧没有等价覆盖能替代它：
本场景需要真实 JSWorker 线程 + 脚本重载，doctest 无法构造。
它是当前唯一能把这条链路跑起来的入口，移除等于删掉复现能力。

---

## 评审后重构（2026-10-03）

按评审意见改了两处设计：

### 1. 去掉 `host` 字段与 shadow realm 特判

`Environment::CreateParams::host` / `Environment::host_` / `get_host()` /
`jsb_shadow_realm.cpp` 的 `params.host = p_master;` 全部删除
（`jsb_shadow_realm.cpp` 现在与改动前逐字节相同）。

判定改为只看线程归属：

```cpp
if (!is_caller_thread()) { async_calls_.add(AsyncCall(TYPE_SCRIPT_RELOAD, p_script)); return; }
script->refresh_in_env(this);   // 本线程直接处理
```

说明：这里用的是 `AsyncCall` 队列，不是 `Environment::handle_message`/`post_message`
—— 后者承载 `Message`（需要 `NativeObjectID` + 序列化 `Buffer`），用于 worker /
shadow realm 的跨 env 消息，塞一个脚本指针进去要序列化脚本，不成立。
`AsyncCall` 正是本进程内同类通知的既有通道（`TYPE_REF` / `TYPE_DEREF` /
`TYPE_GC_REQUEST` 都走它）。若评审本意是字面使用 `Message` 链路，请指出。

### 2. `AsyncCall` 与传参恢复原状，改走裸指针 + reference/unreference

- `AsyncCall` 去掉了 `Ref<RefCounted> script_` 字段和那个多出来的构造函数，
  恢复成只有 `(Type, void *user_data_)`。
- `exec_async_call` 签名恢复为 `(AsyncCall::Type, void *p_user_data)`。
- 发送侧（`_notify_script_reloaded_in_envs`）`self->reference()` 后传 `self.ptr()`；
  接收侧（`notify_script_reloaded` 本线程分支 / `exec_async_call` 的
  `TYPE_SCRIPT_RELOAD` 分支）`unreference()` 归还。
- `notify_script_reloaded` 收 `void *p_script`，头文件不再需要 `GodotJSScript` 前置声明。

### 3. dispose 确实需要清两趟（已确认）

`exec_async_calls()` 是 `async_calls_.swap()` 一次 —— 只 swap 并清空它拿到的那一个槽，
另一个槽里的调用不会被这一趟处理，最终被 `~Environment` 静默丢弃
（`DoubleBuffered` 析构只打一条 `discarding unhandled buffers` warning），
入队时借的引用就永远还不回去。

顺带确认：`EnvironmentStore::remove(this)` 在 `dispose()` **末尾**（:519），
清空期间本环境仍在表里、仍可能被并发通知。因此在 `notify_script_reloaded` 开头加了
`is_disposing()` 守卫：正在销毁则直接 `unreference()` 返回，不入队。
`dispose()` 里改为连续两次 `exec_async_calls()`，把置位之前排进另一个槽的调用也清掉。

### 验证（重构后重跑）

- 构建 v8 rc=0、quickjs-ng rc=0
- 集成测试 rc=0 + `GODOTJS_TEST_PROJECT_COMPLETED`；
  `worker prototype after reload: gen2`（重绑仍成立）
- v8 doctest 82/82（1224 assertions）、quickjs-ng doctest 84/84（1236 assertions）
- jsc 语法门 6 个 TU 全 rc=0
- `jsb_environment.h` 净增 14 行、`jsb_environment.cpp` 净增 48 行（无签名改动）

---

## 结项（2026-10-05）

最终设计：通知载荷是**脚本裸指针**，由 `Environment::notify_script_reloaded()` 替队列
`reference()` 一票、消费端 `unreference()` 归还（参数收 `const Ref<GodotJSScript>&`，
保证调用点计数 ≥ 1）；`dispose()` 清两趟 + `is_disposing()` 守卫兜住丢弃路径。
传输走 `AsyncCall`（唯一在 web 也存在的通道），不传 ObjectID —— 后者在 wasm32 会被
截断，且 `get_instance` 到构造 `Ref` 之间有个应用层关不掉的窗口。

验收：v8 构建 + doctest 1224/1224 + 集成 COMPLETED；quickjs-ng doctest 1236/1236；
web `threads=yes/no` 两档编译 rc=0。

复现装置（`__jsb_test_reload_script__` + `project/tests/worker-reload/`）按用户指示移除。
