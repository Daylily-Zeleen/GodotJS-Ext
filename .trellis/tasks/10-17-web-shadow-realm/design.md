# 设计：web 后端 ShadowRealm 支持

## 现状与阻塞

- `JSB_SHADOW_REALM_ENABLED` 对 `JSB_WITH_WEB` 强制 0（`src/jsb.config.h:233`），因此：
  - `jsb_shadow_realm.cpp`（2007 行，bridge 层、引擎无关）整体不参与 web 编译；
  - `Environment::Environment` 里 `ShadowRealm::register_(context, global)` 不执行
    （它给**每个** Environment 注册 `godot.shadowRealm` 模块加载器与跨包装类，
    所以 worker/主环境的 `require('godot.shadowRealm')` 都拿不到模块）。
- web shim（`jsbb.impl.js` 的 `_jsbb_`）是 **realm 级单例且只持一个 engine**：
  `NewEngine` 遇到已有 engine 直接报错返回 -1 → ShadowRealm 需要的"第二个 Environment/Isolate"无法创建。

## 方案

### 1) web shim 多 engine（`monolith.ts`）

- `_jsbb_.engines: Map<EngineID, jsbb_Engine>`；`NewEngine(opaque)` 用 **opaque（C++ `Isolate*` 的数值）** 作为
  engine id（每个 `v8::Isolate`（web shim）唯一且随 env 存活，与 `Environment::opaque` 同源）；
  `GetEngine(id)`/`FreeEngine(id)` 按 map 解析，未知 id 打 `invalid engine id` 并返回 undefined（不再静默命中单例）。
- 引擎级状态继续留在 `jsbb_Engine` 实例（stack/handles/registry/host_event_hooks 均为实例字段）；
  realm 级共享（`interop`/`wasmop`/worker 消息缓存/内存视图）不动。
- 跨线程句柄释放（2026-10-17 已落地的属主线程投递）payload 携带 engine id（opaque），
  thunk 在属主线程上按 id 解析 → 自动适配多 engine。

### 2) 打开 web 的 ShadowRealm 编译

- `src/jsb.config.h`：`JSB_SHADOW_REALM_ENABLED` 对 web 也置 1。
- 预期的编译/运行差异（迭代修）：
  - `jsb_shadow_realm.cpp` 参与 web 编译（bridge 层，走 v8 shim API，理论上兼容）；
  - `Environment` 增加 `shadow_env_list_` 与 `ShadowRealm::register_`：**每个** Environment 都会注册
    `godot.shadowRealm` 模块加载器与跨包装类 —— web 上主 env 与 shadow env 共享 realm，
    重复注册必须幂等（避免重复的全局类/加载器）；
  - 主/影子 env 共享同一个 JS realm（`globalThis`）：这与 v8 的 isolate 级隔离不同，
    但与 web 的线程/堆模型一致（realm 只能被属主线程访问）；隔离语义由 C++ 侧的
    shadow env（独立 isolate 状态、独立句柄空间）承担。

### 3) 消息与对象转移

- shadow env 与 master env 同线程、同 realm → `postMessage`/`evaluate` 的值转移走既有
  `cross_isolate`/`TransferData` 路径，但两端 isolate 相同（web）→ 不需要跨 isolate 拷贝即可用。
- `post_to_master_web` 的 `_web_sender_route` 目前只允许 worker 发起：shadow→master 的转发在
  web 上走同线程直通（`handle_message` 分支），不依赖该路由。

## 验证

- web 腿 cross-environment：shadow 后端启用，object-transfer ×4 与 error-reporting ×2 场景通过；
- C++ doctest 的 `test_jsb_shadow_realm.h` 用例在 CI 的 web/jsc/v8 腿上的表现不回归（如有该腿用例）；
- native（node）回归：构建 rc=0、doctest 98|98+1280|1280、host-node PASS。
