# 技术设计

## 1. 根因与修复位置

`_reload(true)`（`jsb_script.cpp:257`）只把**调用方 env** 的 module cache 标脏
（`:286` → `Environment::mark_as_reloading` → `module_cache_.find`，`jsb_environment.cpp:1070`），
然后 `:291` 置全局 `loaded_ = false`。任何 env 下次走 `ensure_module_loaded()` 都会进入
`load_module_immediately()`，但该函数 `:906` 拿到的 `env` 是**调用线程自己**的 env，于是
`:944` 遍历进程级的 `instances_` 时，撞上属于其它 env 的实例，`:948`
`jsb_check(env->verify_object(obj))` 断言失败 → `CRASH_COND` → `GENERATE_TRAP()`。

修复分两半：

- **调用方**：`load_module_immediately()` 只对**属于本 env** 的实例做 rebind，其余分流。
- **拥有方**：收到通知后，在自己的线程、自己的 env 上刷新该模块并重绑自己的实例。

## 2. 为什么不需要给 `GodotJSScript` 加 per-env 状态

`loaded_` 是脚本级的单 bool，只表示"有没有 env 加载过"，这确实无法表达 per-env 状态。但
**per-env 状态已经存在**，位置就是 `Environment::module_cache_`（`jsb_environment.h:258`，
每个 env 一份）里的 `JavaScriptModule`：

```
JavaScriptModule（jsb_module.h:63-75）
  ├─ reload_requested   每个 env 的 module 各自一份
  ├─ time_modified
  └─ hash
```

`mark_as_reloading` 查的是**本 env** 的 module（`jsb_environment.cpp:1070`），`_load_module`
在 `:1168-1177` 按**本 env 那个 module** 的 `is_reloading()` 决定是否重新解析。

所以接收端不需要任何额外状态：主线程改了文件时间/hash，每个 env 的 module 各自记录了上次
看到的值，接收端一查就知道自己那份过期了。

**结论：不改 `GodotJSScript` 的字段，不新增查表，性能影响为零。**

`loaded_` 的共享性也不影响本方案：接收端**不调用** `load_module_immediately()`，因此不会被
`:894` 的 `if (loaded_) return` 挡住。

## 3. 调用方改造：`load_module_immediately` 的循环分流

`:944-958` 的循环改为按归属分流。归属判定：

```cpp
GodotJSScriptInstance *si = ScriptInstance::get_script_instance<GodotJSScriptInstance>(obj);
jsb::Environment *owner_env = si ? si->get_env() : nullptr;   // 判空，见 R3
```

分流：

| `owner_env` | 动作 |
|---|---|
| `== env`（本 env） | 原逻辑不变：`env->get_script_class(...)` 判类匹配 → `env->rebind` 或 `set_script(Ref<Script>())` |
| 其它 worker env | 收集到通知列表，不在此处理 |
| 其它 shadow realm env | 同上（投递方式不同，见 §6） |
| `nullptr`（非 `GodotJSScriptInstance`） | 跳过，不通知（R3） |

循环结束时，对收集到的通知列表去重后投递。

`jsb_check(Variant(obj->get_script()) == Variant(this))`（`:947`）保持在原位（C5）。

**注意**：`owner_env != env` 时 `:954` 的 `obj->set_script(Ref<Script>())` **也不能在调用方做**
—— 那是关于该对象绑定关系的决定，必须由拥有者 env 做（见 §5）。

## 4. 通知载荷：脚本，不是实例

传 `Ref<GodotJSScript>`，在途期间持有引用，处理完 unreference（PRD C6）。

**不能复用现有 `Message`/`WorkerMessage` 的 JS 值编码**：`Message::id_` 是 master env 里的
worker 对象 id（`jsb_message.h:78`），`Buffer` 走的是 JS 可见值序列化。这是一条**引擎内部**
通知，不是 JS 消息，不应占用 JS 通道。

**载荷内容**：`Ref<GodotJSScript>` + 该 env 的定位信息（`WorkerID`，或 shadow realm 的
标识）。用现有的 `internal::DoubleBuffered`（`jsb_environment.h:196` 的 `inbox_` 同款）
承载，避免在无锁队列里放非平凡对象。

去重：同一次 reload 对同一 env 至多一条。实现上以 (script, owner_env) 为键在本次
`load_module_immediately` 调用内去重——**不使用脚本级的"已通知"标记**，那会误伤后续合法重载。

## 5. 接收端刷新：新增独立函数，不复用 `load_module_immediately`

新增 `GodotJSScript::refresh_in_env(jsb::Environment *p_env)`（仅在 `p_env` 所属线程调用）：

```
1  p_env->mark_as_reloading(module_id)
     → 查【本 env】的 module；reload_requested 为真 → Requested
     → 若 NoChanges（文件其实没变）→ 直接返回，幂等
2  p_env->load(path, &module)
     → _load_module :1169 看到本 env 的 reload_requested
       → mark_as_reloaded() + 重新解析 + _parse_script_class
3  用【本 env】的 script_classes_ 判类匹配：
     ClassDB::is_parent_class(p_env->get_script_class(module->script_class_id)
                                  ->native_class_name, obj->get_class())
4  对【本 env 拥有】的实例 env->rebind(obj, module->script_class_id)
     不匹配 → obj->set_script(Ref<Script>()) + Warning
5  基类模块：p_env->load(base_script_module_id)（对应 :966）
6  不碰共享状态：loaded_ / script_class_info_ / base / _update_exports
     这些已由调用方在本次 reload 中完成，重复执行会互相覆盖
```

**第 6 点是关键**：接收端只刷新自己 env 的 module cache 和实例绑定，全局快照一律不碰。
这正是"接收端不重跑 `load_module_immediately`"的理由——否则会重复执行 `base.unref()`、
`source_changed_cache`（`:909-910`）这类全局副作用。

第 1 步的 `mark_as_reloading` 同时提供了**幂等性**：同一文件若已被刷新过，返回 `NoChanges`，
函数直接返回。

`refresh_in_env` 遍历 `instances_` 时仍需加锁（与 `:943` 同用
`GodotJSScriptLanguage::mutex_`），因为 `instances_` 仍是进程级共享的。

## 6. 投递

### 6.1 Worker

native 走 `Worker::on_receive(WorkerID, WorkerMessage)`（`jsb_worker.cpp:931`，`#else`
分支即非 web）；web 走 `jsbi_PostMessage(...)`（`jsb_worker.cpp:1109`）。两者都汇入接收 env
的队列，在 `Environment::update`（`jsb_environment.cpp:522`，worker 侧为
`_dispatch_inbox` `jsb_worker.cpp:324`）消费——满足 C7。

反向定位：从 `owner_env` 取 `thread_id_`，经 `get_workers()`（thread_id → WorkerID，
`jsb_worker.cpp:767`）得到 `WorkerID`。

`Environment::post_message`（`jsb_environment.h:626`）标注 `[thread safe]`，
`WorkerImpl::on_receive`（`jsb_worker.cpp:353`）只是 `inbox_.add`，两者都不碰 isolate。

web 无需特例：web worker 是真 pthread（`jsb_worker.cpp:53-58`、`:389` `pthread_self()`、
`:459` `emscripten_set_main_loop_arg`），isolate 亲和性与 native 一致；只是编码/解码不能
假设两端都是 JS 可见的值。

### 6.2 ShadowRealm

同线程、不同 isolate → isolate 亲和，但没有投递问题。

**必须校验宿主**（用户明确指出）：只有调用者确实是该 realm 的 host env 时才能同步执行。
否则会在同一线程上用一个无关 env 执行 guest JS —— 线程断言抓不到（线程相同），但那正是本
绑定禁止的跨 env JS 执行（C2）。

宿主判定用 realm 自己记录的 host：`Environment::wrap(host_isolate)`
（`jsb_shadow_realm.cpp:538` 的写法）。**不能靠线程身份**判断——同一线程上可能有多个
Environment（`jsb_environment.cpp:103-108` 的注释明确说明）。

- 调用者 == host env → 走 `Environment::handle_message`（`jsb_environment.cpp:610`）
  **同步**执行
- 否则（realm 建在别的线程的 env 上）→ 落到延迟路径

## 7. 通知范围：所有 `module_cache_` 含该模块的 env（已定）

通知对象是**所有** `module_cache_` 里含该模块的 env，不只是拥有实例的 env。

理由：env 的 `module_cache_` 一旦陈旧，后续每次查找都会走 reload 分支
（`_load_module` 检查 `is_reloading()`）却拿到旧模块——只通知实例拥有者治标不治本。

取全部 env 用现成接口 `Environment::get_all_environments()`（`jsb_environment.cpp:766`，
内部走 `EnvironmentStore::get_list()`）。遍历时按 `module_cache_` 过滤（每个 env 查一次
`find(module_id)`），命中的才入通知列表；调用方自身跳过（它已在本次调用中刷新完）。

## 8. 已知遗留（不在本任务）

**接受消息后、重绑生效前的窗口**：接收 env 在消费通知与应用重绑之间，可能执行触及未重绑
对象的逻辑。按用户指示，在通知处理处加一条**中文 TODO**，写明该窗口及后续处理方向
（推迟到安全时机，或在入口处校验）。

## 9. 测试接口的处置

`__jsb_test_reload_script__`（`JSB_TESTS_ENABLED` 保护）在该场景被 C++ 侧覆盖后移除（AC8）。
移除前，`jsb_builtins.h` / `jsb_builtins.cpp` / `jsb_environment.cpp:368` 三处改动整体回退。