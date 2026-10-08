# 跨隔离区错误上报（Worker / ShadowRealm / 定时器）

## Goal

让 JS 错误能**跨隔离区**送达用户代码：ShadowRealm 的同步入口不再崩溃、消息入口（Worker / TransferableShadowRealm）有可用的 `onerror`、错误形态在各入口一致。

## 背景（已核实的现状）

| 入口 | 现状 | 证据 |
|---|---|---|
| `JSShadowRealm.evaluate` | **遇到异常会崩**（dev 构建 trap；release 下取到无效栈槽 = UB） | `src/runtime/bridge/jsb_shadow_realm.cpp:1251-1252` 用 `ToLocalChecked()`；异常时 `Script::Run` 返回空（jsc `jsb_jsc_object.cpp:418-422`、quickjs `jsb_quickjs_object.cpp:542-545`），三 shim 的 `ToLocalChecked()` = `jsb_check(!IsEmpty())`（`impl/*/jsb_*_handle.h:118-121`）。**实测**：`evaluate("(function(){throw new Error('boom')})()")` → `test case CRASHED: Unhandled SEH exception caught`，栈顶 `jsb_shadow_realm.cpp:1252`，doctest 从 82 例中断在 66 例 |
| `JSShadowRealm.evaluate` 的错误分支 | 即使走到也无效：`jsb_throw(guest_isolate, <String>)` 抛进 guest，调用方（host）只会拿到 `undefined` | `jsb_shadow_realm.cpp:1254-1256`；返回路径 `:1262-1264` |
| `importValue` / `importValueSync` | 错误被压成**纯字符串**：Promise 用字符串 reject、同步入口抛字符串（无 stack、`instanceof Error` 假） | `jsb_shadow_realm.cpp:1140-1151`、`:1316`、`:1362` |
| `JSWorker.onerror` | **发送端不存在**：三处回调 catch 只打日志；host 侧接收端已实现 | 只打日志：`jsb_worker.cpp:286-292`、`:566-572`、`:839-844`；接收端：`jsb_environment.cpp:761-767`；typings 已声明：`scripts/typings/godot.worker.d.ts:42-43` |
| Worker 启动期失败 | 一条消息都不发（只在 worker 线程打日志） | 成功才发 `TYPE_READY`：`jsb_worker.cpp:611` |
| `TransferableJSShadowRealm.onerror` | 同 Worker：声明了、无发送端 | `scripts/typings/godot.shadowRealm.d.ts:62-63`；消息通路 `jsb_shadow_realm.cpp:1708-1717`、`:1746-1755` |
| 定时器回调异常 | 被吞（注释写明"需要转发给 onerror"） | `jsb_environment.cpp:532-534`，`timer->action(ctx)`：`src/runtime/internal/jsb_timer_manager.h:290` |
| **node 腿**：node 自己管的异步回调（`setTimeout` 等）抛错 | 进不了本任务通路：node 下 `Environment::update` 只 `node_runtime_->PumpEventLoop()`，JSB 的 `timer_manager_` 不被驱动（`setTimeout` 也不会被 essentials 接管，因为 node 全局已有） | `jsb_environment.cpp:525-528`（`#if JSB_WITH_NODE` 分支）、`jsb_essentials.cpp:268`（`bind_timer_if_missing`）。worker/realm 的 `onmessage`、加载失败在 node 下**是**通的（那些分支守卫是 `#if !JSB_WITH_WEB`，node 走这一支） |
| **web 腿**：整条"发送侧"不存在 | `post_message`/`inbox_` 本身被排除；三处发送端同样被排除；**接收端**（`_on_worker_message` 的 `TYPE_ERROR → onerror`）没被排除，web 走 `jsbi_*` 胶水 | `jsb_environment.h:650-656`、`jsb_worker.cpp:121-155`、`jsb_environment.cpp:803+`、`jsb_shadow_realm.cpp:1769+`（`#if !JSB_WITH_WEB`）；接收：`jsb_environment.cpp:747`；通道：`src/runtime/impl/web/js/library_godotjs_jsbi.js:190` |

## 需求

- **R1** ShadowRealm 同步入口（`evaluate`、`importValueSync`）遇到 guest 异常：**不崩**，并在**调用方 isolate**抛出一个可用的错误对象。
- **R2** `importValue`（Promise）失败：用同一形态的错误对象 reject，不再用纯字符串。
- **R3** Worker：`onerror` 真正可用，覆盖 ① worker 启动/加载失败 ② worker 侧 `onmessage`/内部回调抛错；`onready` 语义不变。
- **R4** `TransferableJSShadowRealm`：`onerror` 与 Worker **参数形态完全一致**（同一个构造路径）。
- **R5** 错误形态：目标侧拿到的是**目标 realm 重建的 `Error`**（`instanceof Error` 成立；`name`/`message`/`stack` 来自源侧），额外字段按 R6 尽力携带。
- **R6** 额外字段策略：**核心字段永不失败**（`name`/`message`/`stack`/`cause`）；用户自定义字段逐个 best-effort 复制，转不过去的**不得导致整体失败**，而是以占位标记或丢弃处理，并附一份「未能携带的字段路径」清单。
- **R7** 转换必须**有界**：深度上限、节点数上限、总字节上限、环检测；只遍历自有可枚举属性；**不得触发 getter/setter**（用属性描述符判断）。
- **R8** 转换与投递本身抛错时不得二次崩溃（必须包在 TryCatch 内）。
- **R9** **worker 环境**的定时器（`setTimeout` 等）回调异常进入同一错误通路（发 `TYPE_ERROR` 给 master 的 `onerror`）；
  主环境的定时器异常维持现状（已被捕获并日志，`jsb_timer_action.cpp:73-75`），不新增全局钩子。
- **R11** **node 腿**：node 自分发的回调（定时器等）抛出的异常，若当前环境是 worker / transferable shadow realm，同样要走同一错误通路送到宿主的 `onerror`；主环境维持"只进日志"。
- **R12** **web 腿**：web（emscripten）下补齐发送侧——worker / transferable shadow realm 的错误同样以 `TYPE_ERROR` 送达宿主 `onerror`（载荷形态按 web 通道能力决定，接收端已存在）；web 上的定时器走 JSB 自己的 `timer_manager_`，因此随发送侧一起覆盖。
- **R10** Godot 侧日志保留（现有 `JSB_LOG`/`JSB_WORKER_LOG` 行为不回退），脚本侧没人接 `onerror` 时仍可查日志。

## 非目标

- 不保留错误对象的身份/原型（跨线程物理上不可能；跨 realm 也做不到 `instanceof` 原 realm 的构造器）。
- 不改变 `wrap_cross_env_value` 的现有语义（活代理继续用于"返回值"这类场景）。
- 不自动 transfer 错误里携带的 Godot 对象（transfer 是移动语义，会破坏发送方）。
- 不改 ShadowRealm / Worker 的既有消息格式（只新增/启用 `TYPE_ERROR` 的发送端）。

## 范围

必做：R1-R8、R10-R12。R9（定时器异常转发）与 R11/R12 同属"跨隔离区错误通路覆盖到各腿"，均已纳入。

## 验收标准

- [ ] **AC1** 回归用例先复现崩溃：`evaluate` 抛错用例在修前崩溃（已在 `report.md` §30 记录实测），修后通过。
- [ ] **AC2** `evaluate` / `importValueSync` 抛错时宿主侧 `catch` 到 `instanceof Error === true` 的对象，`message` 正确、`stack` 含 guest 栈文本；宿主进程不崩。
- [ ] **AC3** `importValue` 失败 → Promise reject 同一形态错误（不再是字符串）。
- [ ] **AC4** Worker 启动失败 → 主线程 `onerror` 收到该错误；启动成功仍走 `onready`。
- [ ] **AC5** Worker 侧回调抛错 → 主线程 `onerror`（不再只有日志）。
- [ ] **AC6** `TransferableJSShadowRealm.onerror` 与 Worker 的参数构造路径相同（同一 helper；测试断言字段集一致）。
- [ ] **AC7** 自定义字段：能转的带上（如 `e.code`/`e.detail` 的基础类型），不能转的被标记/丢弃且**附带未携带清单**；整体仍返回可用 Error。
- [ ] **AC8** 有界性：对构造出的病态用例（深链/环/大数组/getter）不卡死、不超时、不触发 getter（可用计数器断言）。
- [ ] **AC9** 门禁：本地 v8 + quickjs-ng 构建 rc=0、doctest 全绿、smoke `GODOTJS_TEST_PROJECT_COMPLETED`；node（`use_node=yes`）本地构建+运行；web（`platform=web`）本地构建（emsdk）；jsc 走语法门 + CI 的 `Test (host-jsc, macos-latest)`。
- [ ] **AC10** 文档/规范：新增的跨边界错误约定写进 `.trellis/spec/`（若判定为新约定）。
- [ ] **AC11** 未携带清单挂在重建 Error 的 `Symbol.for("jsb.untransferred")` 上（不是字符串键），用户可用该 symbol 读取；不能转的字段不导致整体失败。
- [ ] **AC12** worker 环境的定时器回调异常 → 主线程 `onerror`（与 AC5 同一形态）；主环境定时器异常仍只进日志（不新增 API）。
- [x] **AC13**（node）`use_node=yes` 构建 rc=0；node 下 worker / transferable shadow realm 的**回调异常**（含 node 自管定时器）能到宿主 `onerror`（本地跑项目用例 5 场景 + CI `host-node` 腿）；主环境仍只进日志。
- [ ] **AC14**（web）`platform=web` 构建 rc=0（本地 emsdk `D:/Dev/emsdk` + CI `platform: web` 腿）；web 下 worker / realm 的错误（回调异常、加载失败、定时器）走同一通路——运行时验证方式按实现时能力确定（web 腿在 CI 只构建；本机用 emsdk 跑浏览器验证或明确标注未跑）。

## 当前阻塞（2026-10-07，跟踪中）

### ~~B1（node）多场景运行出现非确定性访问违例，node 腿无法交付~~ 【已关闭 2026-10-07】
- **现象**：node（`use_node=yes`）下带 worker/realm 定时器异常用例的场景，进程以 `0xC0000005` 结束；
  位置会漂移（有时在本场景内、有时在下一场景开始），像内存被写坏；Godot 崩溃处理器与 `llvm-symbolizer`
  都解不出这一版 DLL 的符号（`??:0:0` / `__guard_memcpy_fptr`）。
- **已实现、且功能可用**：bootstrap 装进程级 uncaught 钩子 → C++ `__jsb_forward_uncaught_error` →
  worker/realm 环境 `capture → forward_error_to_master`（单场景 worker-error 的 5 个用例全过，含 worker 定时器、
  realm 定时器 → 宿主 `onerror`）。
- **已排除**：延迟一个 tick 再转发（更糟）；只 `capture` 不转发（仍崩）；定时器不抛错 + 保留 `terminate()`
 （稳定通过）；去掉用例里的 `realm.terminate()`（稳定通过）。
- **已用调试器抓到真实栈（2026-10-07）**：根因是 `ObjectCrossWrapper::finalizer` 用
  `Environment::wrap(已 dispose 的 guest isolate)` 反查环境 → 拿到悬垂 `Environment*` → AV；
  触发链是 node 的 per-isolate foreground task 在 realm `terminate()` 之后仍派发 GC second-pass 回调。
  （前置障碍已排除：node 的启动器 exe 与主库共用 PDB 路径导致主库符号被覆盖 —— 已在 `SConstruct` 修掉，
  改后崩溃栈立即带符号。）
- **已修复（2026-10-07，验证通过）**：`CrossWrapper` 记录所属环境指针，finalizer 用 `Environment::_access()`
  查活；环境已不在时不释放包装器（其 v8 `Global` 属于已 dispose 的 isolate，释放会 fatal），代码里留了
  `TODO(长期)` 让 realm 析构时在 guest 还活着时主动回收包装器。
- **顺带修掉**：node 启动器 exe 与主库共用 PDB 路径导致主库失去符号（`SConstruct` 已分离）。
- **相关 AC**：AC13（node）= **达成**（build rc=0、doctest SUCCESS、多场景 smoke 连续 3 次 COMPLETED）。
- **复核（2026-10-08）**：缓存注册表改存 `CrossWrapper *` 裸指针、purge 去掉
  `GetAlignedPointerFromInternalField` 反查、`remove_cache` 修掉 erase 后读迭代器（悬垂读）与
  `jsb_check` 误用赋值；node smoke 连续 4 次 COMPLETED（日志确认覆盖
  `res://tests/worker-error/WorkerError.tscn`），v8/qjs 各 2 次 COMPLETED，jsc 语法门 rc=0。
  详见 `report.md` §16。

### B2（web）实现未接线（按用户要求暂缓，先处理 node）
- 已核实：web 无 `jsb::Message` 通道（活 JS 值通道）、`_on_worker_message` 在 web 不编译、
  真正的接收端是 `Worker::on_web_message_from_pthread`、shadow realm 在 web 整体禁用、
  web 用浏览器原生定时器（`JSB_WITH_ESSENTIALS=0`）、本机 emsdk 只能构建不能跑。
- 已完成第一步：暴露 payload 编解码（`record_to_js` / `record_from_js`）+ 定义 `kErrorPayloadKey = "__jsbError"`。
- **相关 AC**：AC14（web）当前状态 = **未开始接线**。

## 决策（2026-10-06，用户已定）

1. **形态**：统一为**目标 realm 重建的 `Error`**；**不要 Proxy/活代理**（`ObjectCrossWrapper` 继续只用于返回值这类短命对象）。
2. **额外字段**：采纳本文 R6 的 best-effort 方案（核心字段永不失败 + 逐字段尽力 + 未携带清单）。
3. **清单字段名**：用 **Symbol**，键 `jsb.untransferred`（即 `Symbol.for("jsb.untransferred")`），避免与用户自定义字段冲突；名称已定，见 `design.md` §5.4。
4. **定时器**：只纳入 **worker 环境**的定时器异常（与 worker 错误上报同一通路）；主环境维持"捕获 + 日志"（新增全局错误钩子属新 API，另立任务）。

5. **后端覆盖（2026-10-06，用户追加）**：本任务覆盖到 **node 与 web 两条后端腿**，不另开任务。
   - node：定时器/异步走 node 自己的事件循环，因此需要在 **node 运行时**挂 uncaught-exception 钩子（JSB 的 `timer_manager_` 在 node 下不被驱动），回调里按当前 `JSRuntime → Environment` 定位环境，若是 worker/realm 就走现成的 `forward_error_to_master`；主环境维持只进日志。这是"新增钩子"，但它只服务于本任务既定的"worker/realm 错误上报"，不是面向用户的全局错误 API。
   - web：接收端（`_on_worker_message` 的 `TYPE_ERROR` 分支）已在编译范围，缺的是发送端；web 通道的载荷是栈位上的 JS 值而不是 `Buffer`，因此 `TYPE_ERROR` 在 web 上走"payload 值"形态（暴露 error record 的 payload 编解码），接收端补一条"无 buffer 但有 payload 值"的读取路径。

### 决策 4 的核实依据（"是否同源"）

- 定时器异常**现在并没有被吞**：`JavaScriptTimerAction::operator()` 已经 `try_catch.has_caught()` + `JSB_LOG(Error, "timer error …")`
  （`src/runtime/bridge/jsb_timer_action.cpp:73-75`）——`jsb_environment.cpp:532-533` 那条 TODO 的描述与现状不符（过时描述，本任务顺带改写）。
- 定时器只有一种 action（`internal::TTimerManager<JavaScriptTimerAction>`，`jsb_environment.h:245`），`invoke_timers` 只在 `Environment::update` 里被调（`jsb_environment.cpp:534`）。
- worker 环境同样驱动 `update(delta)`（`jsb_worker.cpp:365`）→ worker 里 `setTimeout` 抛错同样只落到日志 → 与本任务的 worker 错误上报**同路**，因此纳入。
