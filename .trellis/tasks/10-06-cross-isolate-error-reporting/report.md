# 进度：跨隔离区错误上报

## 触发

用户在讨论"下一个高性价比 TODO"时提出：错误上报在 ShadowRealm 上同样要做；并追问"跨隔离区抛异常到底该怎么处理"、"能不能尽量原样传给宿主隔离区（遍历字段 + 只支持基础类型 + 不能跨就抛转换错误），有没有性能问题"。

## 已完成：调研 + 实测（2026-10-06）

完整证据链在 `.trellis/tasks/09-29-todo-audit/report.md` §30，摘要：

1. **`JSShadowRealm::evaluate` 遇到异常会崩（实测）**：临时探针执行 `evaluate("(function(){throw new Error('boom')})()")`
   → `ERROR: test case CRASHED: Unhandled SEH exception caught`，C++ 栈顶 = `jsb::ShadowRealmImpl::evaluate (jsb_shadow_realm.cpp:1252)`；
   当轮 doctest 从 82 例中断在 **66 例**。探针已删除、源码 `git diff` 确认还原，重建复跑 **82/82 SUCCESS**。
   根因：`:1251-1252` 的 `ToLocalChecked()`（`impl::TryLocalChecked` = `jsb_check(!IsEmpty())`），而异常时 `Script::Run` 返回空。
2. **`importValue` / `importValueSync`**：错误被压成纯字符串（`:1140-1151`、`:1316`、`:1362`）。
3. **Worker**：三处 catch 只打日志（`jsb_worker.cpp:286-292`、`:566-572`、`:839-844`），`TYPE_ERROR` 无发送端；host 侧接收端已实现（`jsb_environment.cpp:761-767`）。
4. **TransferableJSShadowRealm**：`onerror` 已声明（`godot.shadowRealm.d.ts:62-63`）但同样无发送端。
5. **同步边界已有"活代理"能力**：`wrap_cross_env_value` 对对象走 `ObjectCrossWrapper`（`jsb_shadow_realm.cpp:606`、`:869`、`:880-882`）——即"尽量原样"在同线程场景早已可行，但本任务**不**把它用于错误（寿命/`instanceof` 问题，见 `design.md` §4）。

## 待用户决策（`prd.md` 末节）

1. 错误形态：统一为**目标 realm 重建的 Error**（`instanceof Error` 成立，`cause`=递归记录）？
2. 额外字段策略：best-effort（默认，附 `untransferred` 清单）vs "尽量全带"？是否需要 `Map`/`Set`（逐腿确认序列化器支持度）？
3. `untransferred` 清单的字段名。
4. 定时器异常（`jsb_environment.cpp:532-534`）是否纳入本任务。

## 状态

- 规划产物：`prd.md` / `design.md` / `implement.md` + `implement.jsonl`(3) / `check.jsonl`(2)，`task.py validate` 通过。
- **未 `task.py start`**：等用户 review 后进入执行。

## 决策落实（2026-10-06）

用户答复 → 已写入 `prd.md`「决策」节、`design.md` §5.2/§5.4、`implement.md` 步骤 2/5：

1. 统一**重建 Error**，不用 Proxy/活代理 ✅
2. 额外字段按 best-effort + 未携带清单 ✅
3. 清单字段名 = **`Symbol.for("jsb.untransferred")`**（用户要求用 Symbol 防冲突、名字别太长）——
   选 `Symbol.for` 而不是每 isolate 唯一的 `Symbol::New`：注册表 symbol 能在目标 realm 本地重算、用户无需我们额外导出
   （三腿 `Symbol::For` 均已实现：jsc `jsb_jsc_primitive.cpp:184`、quickjs `jsb_quickjs_primitive.cpp:181`、web `jsb_web_primitive.cpp:124`；仓内先例 `jsb_shadow_realm.cpp:382`）
4. 定时器：**只纳入 worker 环境**（与本任务同路：worker 由 `jsb_worker.cpp:365` 的 `p_env->update(delta)` 驱动定时器；
   异常现在只落到 `jsb_timer_action.cpp:73-75` 的日志）；主环境维持现状，新增全局钩子另立任务。

**顺带核实的过时描述**：`jsb_environment.cpp:532-533` 的 TODO 说定时器异常"被吞"，与 `jsb_timer_action.cpp:73-75`
（已 `try_catch.has_caught()` + `JSB_LOG(Error, "timer error …")`）不符——本任务实施时改写该注释。

状态：规划产物已按决策更新，等用户 review 后 `task.py start`。

## 实施：（步骤 0/1 + 步骤 3 前半）2026-10-06

### 已改（`src/runtime/bridge/jsb_shadow_realm.cpp`）

1. **`evaluate` 崩溃修复**：`Script::Compile`/`Run` 改用 `ToLocal(...)`（原来 `ToLocalChecked()` 在异常时 trap）；
   异常在 guest 里降级成文本，回到 **调用方（host）isolate** 再抛。
2. **新增两个 helper**：
   - `_make_realm_error(isolate, context, message)`：用**目标 realm 自己的 `Error` 构造器**建 Error
     （注意用 `CallAsConstructor`：`Function::NewInstance` 只有真 v8 有，quickjs/jsc shim 没有 → 首次 qjs 构建即报
     `error C2039: "NewInstance": 不是 "v8::Function" 的成员`）。
   - `_throw_realm_error(isolate, message, error)`：`#if !JSB_WITH_WEB` 用 `isolate->ThrowException(value)`；
     web 腿的 shim 没有 `ThrowException(Local<Value>)`（与 `jsb_environment.h:878` 的 `#if !JSB_WITH_WEB` 同理），退回 `jsb_throw(message)`。
3. **`importValue`**：失败改为**只 reject 一个 Error**（原来既 `jsb_throw` 又用字符串 reject —— 同步抛会让调用方拿不到 Promise，语义错）。
4. **`importValueSync`**：失败抛 Error（原来抛字符串）。

### 新增回归用例（`src/runtime/tests/test_jsb_shadow_realm.h`）

| 用例 | 断言 |
|---|---|
| `an exception from evaluate becomes a host-realm Error` | host 侧 `catch` 到 `instanceof Error`、message 含 `boom` |
| `importValueSync failure throws a host-realm Error` | `instanceof Error` |
| `importValue rejects a host-realm Error` | reject 的是 `instanceof Error`（微任务用 `isolate->PerformMicrotaskCheckpoint()` 驱动） |

### 过程中发现的两个坑（都已处理/记录）

1. **`has_caught()` 是一次性语义**：quickjs 的 `Isolate::try_catch()`（`impl/quickjs/jsb_quickjs_isolate.h:274`）会把异常从引擎搬进内部槽，
   **重复调用会 `jsb_checkf` 断言**（"stack.exception is dirty…"）。我第一版在 `executed` 表达式与错误分支里各调了一次 →
   qjs 上触发断言 → 错误打印路径又调 `_refill_debug_stack` → 再次断言 → **999 次错误风暴 + 最终崩溃**（265K 行日志、单轮 15 分钟）。
   已改为**只调一次**并存 `const bool caught`。教训与仓库既有写法一致：`if (has_caught()) { BridgeHelper::get_exception(...) }`。
2. **`Function::NewInstance` 不是跨腿 API**（见上），跨腿构造对象用 `CallAsConstructor`（仓内先例 `jsb_environment.cpp:1822`）。

### 验证状态

- v8（改前一轮）：doctest **85/85（1255 断言）SUCCESS**、smoke rc=0。
- qjs-ng：修复前 doctest 崩（上面第 1 条）；修复后正在重跑完整门禁（构建 + doctest + smoke × 两个后端）。
- 遗留噪声（与本次改动无关）：v8 doctest 跑完后进程退出阶段有 8 条 `Call ScriptInstance::callp() failed: env is null`
  （改动前 `probe_after.log` 已存在同样的 8 条）。

### 待办（后续步骤）

- 步骤 2：错误记录（`name`/`message`/`stack` + 额外字段 best-effort + `Symbol.for("jsb.untransferred")` 清单 + 有界遍历），
  并把 `evaluate`/`importValueSync`/`importValue` 从"文本 Error"升级为"记录重建的 Error"。
- 步骤 4：Worker / TransferableShadowRealm 的 `TYPE_ERROR` 发送端 + host 侧重建。
- 步骤 5：worker 环境的定时器异常复用同一通路；改写 `jsb_environment.cpp:532-533` 的过时 TODO。
- 步骤 6：全量门禁 + jsc 语法门 + CI。

## qjs 上的三个连环坑（2026-10-06，已全部解决）

qjs 腿的 doctest 最初不是失败而是**崩溃 + 错误风暴**（26.5 万行日志、单轮 20+ 分钟）。逐层挖出三个原因：

### 坑 1：`has_caught()` 是一次性语义（我引入）
quickjs `Isolate::try_catch()`（`impl/quickjs/jsb_quickjs_isolate.h:272-281`）会把异常从引擎 `JS_GetException` **搬进内部槽**；
槽已脏时再调 → `jsb_checkf` 断言 "stack.exception is dirty"。我第一版在条件表达式与错误分支各调一次 → 断言。
**已修**：只调一次存 `const bool caught`。

### 坑 2：`Isolate::ThrowException(value)` 会污染 TryCatch 的槽（我引入）
jsc/quickjs 的 `ThrowException` 实现是 `set_stack_steal(StackPos::Exception, ...)` + `JS_Throw(...)`
（`impl/quickjs/jsb_quickjs_isolate.h:365-372`、`impl/jsc/jsb_jsc_isolate.h:225-231`）——即**抛出的同时把值寄存到 TryCatch 用的槽**。
宿主 JS `catch` 掉之后没人清理这个槽 → 之后任何 `has_caught()` 断言/误判 → 错误打印路径（`_refill_debug_stack`）再断言 → 风暴。
**已修**：新增 `_throw_value_in_realm()`，改为**按 JS 语义抛出**（在目标 realm 编译并调用 `(function (e) { throw e; })`）：
只留下正常的 pending exception，槽保持干净（各腿 `Function::Call` 在异常时是 "intentionally keep the exception"，
不碰槽：`impl/quickjs/jsb_quickjs_function.cpp:40-45`）。

### 坑 3：`Script::Run` 在 quickjs 上会把异常丢掉（**既有行为**，非我引入）
`impl/quickjs/jsb_quickjs_object.cpp:542-545`：`JS_EvalFunction` 返回异常时调用 `MarkExceptionAsTrivial(ctx)`
（= `JS_GetException` + free，`impl/quickjs/jsb_quickjs_ext.h:59-67`）→ **pending exception 被消费掉** →
随后 `TryCatch::has_caught()` 永远 false。我第一版用 `Script::Compile`+`Run`，于是 qjs 上"没有异常"但结果空 →
走了我的兜底文案（实测探针打印 `PROBE-TEXT: [Error:failed to evaluate the source in the shadow realm]`）。
**已修**：改用各腿统一的 eval 入口 `impl::Helper::compile_function(...)`（quickjs/jsc 的实现都明确 "intentionally keep the exception"；
`impl/quickjs/jsb_quickjs_helper.h:241-253`、`impl/jsc/jsb_jsc_helper.h:195-213`、`impl/v8/jsb_v8_helper.h:172`），
这样 `has_caught()` 在所有腿都能看到 guest 的异常。

**验证（qjs 实测）**：doctest **87/87、1267 断言 SUCCESS**（改前：崩溃 / 20 分钟风暴）；v8 + smoke 正在重跑（bg_7）。
计数差异说明：qjs 比 v8 多 2 个用例（腿特定用例），断言数 1267 vs 1255。

## 步骤 2：错误记录（完成，qjs 实测）

### 新增
- `src/runtime/bridge/jsb_error_record.h/.cpp`：**记录 -> 复制 -> 重建**
  - **采集在 JS 侧**（`kCaptureSource`，`Object.getOwnPropertyDescriptor` 遍历）：只放行原始值 / 数组 / 普通对象，
    BigInt 转字符串；跳过 accessor（**不触发 getter**）、函数、环；上限 深度 8 / 节点 4096 / 字符串 64K；
    带不走的路径进 `untransferred`。**为什么放 JS 侧**：`getOwnPropertyDescriptor` 天然满足"不触发 getter"，
    且各腿 shim 的 C++ 原始值/属性 API 签名不一致，放 JS 侧最省事。
  - **复制**：`v8::ValueSerializer` / `ValueDeserializer`（与消息通道同一套），记录里只有纯数据，各腿都能过；
    缓冲按仓内既有约定用 `impl::Helper::free` 释放（`jsb_shadow_realm.cpp:947-951` 的先例）。
  - **重建在 JS 侧**（`kRebuildSource`）：`new Error(message)` + `name`/`stack`/`extra`/`cause`，
    未携带清单挂到 `Symbol.for("jsb.untransferred")`。
- `impl::TryCatch::get_exception_value()`：三腿 shim + v8 各加一行（jsc/quickjs 读 `StackPos::Exception`、
  web 读 `StackBase::Error`、v8 用 `try_catch_.Exception()`）；在 shim 的 catch 头里补了 `v8::Value`/`v8::Local` 前向声明。

### 接线（`jsb_shadow_realm.cpp` 的 `evaluate`）
guest 作用域内：`get_exception_value()` -> 采集 -> 序列化（**注意顺序**：`get_exception_value()` 必须在
`get_message()` 之前，后者会消费异常槽）；回到 host 作用域：反序列化 -> 重建 -> 抛出；
任一步失败退回"只带文本的 Error"。

### 实测（qjs，探针输出）
```
RECORD-PROBE: [{"isError":true,"name":"Error","message":"with-fields","hasStack":true,
                "code":42,"detail":{"a":"x","b":[1,2]},"lost":["fn"]}]
```
即：`instanceof Error` 成立、`name`/`message`/`stack` 都在、自定义字段 `code`/`detail`（含嵌套对象与数组）带过来了、
函数字段 `fn` 进了未携带清单（`Symbol.for("jsb.untransferred")` 可读）。qjs doctest **88/88、1283 断言 SUCCESS**。

### 编译期踩到的跨腿差异（都是 shim 缺 API）
- `MaybeLocal::FromMaybe` 无（各腿 shim 都没有）→ 用 `ToLocal(&v)`。
- `v8::Local` 无 `operator bool` → 判 `IsEmpty()`。

### 待办
- `importValue` / `importValueSync` 目前仍只带文本（`_importValue` 内部把异常压成了字符串，要拿到记录需要重构成把异常值带出来）——
  记为后续小步。
- 步骤 4（Worker / TransferableShadowRealm 的 `TYPE_ERROR` 发送端 + host 侧重建）、步骤 5（worker 定时器）、步骤 6（全量门禁 + jsc 语法门 + CI）。

## 步骤 4：Worker 的 `TYPE_ERROR` 发送端 + master 侧重建（代码完成，qjs 编译/doctest 通过）

### Worker 侧（`src/runtime/bridge/jsb_worker.cpp`）
- 新增（`#if !JSB_WITH_WEB`）两个 helper（放在 `class WorkerImpl` 之前，否则调用点看不到）：
  - `_post_error_value_to_master(worker_env, master, handle, error_value)`：采集 -> 序列化 -> `post_message(Message(TYPE_ERROR, handle, Buffer::steal(...)))`
  - `_post_error_text_to_master(...)`：只有文本时的退化（走同一采集脚本的非对象分支）
- `_on_message` 的 `has_caught()` 分支：**先** `get_exception_value()`（`get_message()` 会消费异常槽），再日志 + 发 `TYPE_ERROR`
  → 覆盖"worker 的 `onmessage` 抛错"（AC5）
- 入口脚本加载失败（`env->load(impl->path_) != OK`）：原来什么都不发（master 会一直等 `onready`），现在发 `TYPE_ERROR`
  → 覆盖"worker 启动失败"（AC4）
- **web 腿未接入**：web 的 worker 消息走 shim（`jsbi_PostMessage`），本轮保持"只打日志"；记为后续（web 本机不可测，需 CI）

### Master 侧（`src/runtime/bridge/jsb_environment.cpp`）
- `invoke_worker_callback_from_message(..., bool p_rebuild_error)`：`TYPE_ERROR` 时把 payload（错误记录）用
  `rebuild_error_from_record` 重建成 **Error** 再交给 `onerror`；`TYPE_MESSAGE`/`TYPE_READY` 传 `false`（行为不变）

### 状态
- qjs：构建 rc=0、doctest 88/88（1283 断言）SUCCESS。
- 全量门禁（qjs/v8 + doctest + smoke）重跑中（bg_9）——smoke 会跑项目的 worker 测试，可证"无回归"，但**尚未覆盖"错误确实到达 onerror"**（AC4/AC5 的断言用例待加：需要给 `cross-environment` 的 peer 加一个"抛错"消息类型）。
- TransferableShadowRealm 的 `onerror` 发送端仍未接（下一小步）。

## 步骤 4 收尾：worker 错误上报**已被自动化用例验证**（AC4/AC5）

### 新增测试（项目 smoke 套件）
- `project/tests/worker-error/WorkerError.tscn` + `worker-error.ts`（master 侧两个用例）、
  `peer-throws.ts`（worker 入口：收到消息即抛错）、`peer-throws-on-load.ts`（worker 入口：加载期抛错）
- `project/tests/start.ts` 注册新场景（15 → 16 个场景）
- 注意：项目 TS 需要 `npx tsc`（在 `project/` 下）编译到 `.godot/godotjs_ext/` 才生效——**改了 TS 但没 tsc，smoke 跑的还是旧代码**（本轮踩过）。

### 实测（v8，smoke）
```
[JS] WERR: case1 onerror ok
[JS] WERR: case2 onerror fired shape=Error: failed to load the worker script (see the worker log for the path) isError=true
[JS] WERR: case2 onerror ok
ERROR: [JSWorker][Error] failed to load the worker script: tests/worker-error/peer-throws-on-load
WARNING: [JS] GODOTJS_TEST_PROJECT_COMPLETED
```
即：① worker 的 `onmessage` 抛错 → master 的 `onerror` 收到 **Error**；② 入口脚本加载失败 → master 的 `onerror` 收到 **Error**（而不是一直等 `onready`）；smoke rc=0。
（用例里的 `WERR:` 插桩已删除，只留断言。）

### 本轮踩到的三个坑（都已在代码里注释说明）
1. **worker 环境在"入口脚本加载失败"后不能再跑 JS**：我原本在 worker 侧用 JS 采集错误记录 →
   `capture_error_record` 里 `compile_function` 直接崩（AV）。改为**发一个空 payload 的 `TYPE_ERROR`**，
   文案由 master 侧补（`failed to load the worker script …`）。
   （尝试过的替代方案：先清 pending exception —— 无效；说明不只是 pending exception 的问题。）
2. **句柄必须在 HandleScope 内创建**：`_post_error_*` 里把 `new_string` 放在作用域外 → fatal "Cannot create a handle without a HandleScope"。
3. **`NativeObjectID`（消息里的对象 handle）与 `WorkerID` 是两套索引**：不能用 `(WorkerID)` 直接转；
   因此 master 侧不反查脚本路径（路径在 worker 日志里）。

### 另外记一个既有隐患（不在本任务范围）
对**已经退出的 worker** 调 `worker.terminate()` 会崩（本轮的 case2 曾在 `finally` 里调用它，去掉后正常）；
`worker-error.ts` 的 case2 里留了 NOTE 说明为何不调用。

## 步骤 5：worker 环境的定时器异常（代码 + 实测完成）

### 改动
- `Environment::CreateParams` 增加 `master_token` / `worker_handle`；`Environment` 保存并在构造时写入
  （worker 环境创建处 `jsb_worker.cpp:451` 之后传入 `impl->get_token()` / `impl->get_handle()`）。
- 新增 `Environment::forward_error_to_master(error_value)`：worker 环境把异常采集/序列化后
  `post_message(TYPE_ERROR, worker_handle_)` 给 master（master 侧复用已有的 `onerror` 重建路径）。
- `JavaScriptTimerAction::operator()` 的 catch：**先** `get_exception_value()` 再 `get_message()`，
  然后 `env->forward_error_to_master(exception)`（主环境 `is_worker()` 为假 → 什么都不做，维持"只进日志"）。
- 改写 `jsb_environment.cpp` 里那条过时描述（原文说定时器异常"被吞"，实际 `jsb_timer_action.cpp:73-75` 早已捕获并日志）。

### 实测（v8 smoke，新增第三个用例）
worker 的 peer 在 `setTimeout` 回调里抛错：
```
ERROR: [jsb][Error] timer error Uncaught Error: worker-error-test: peer timer throw
    at .../peer-throws.ts:10:5
```
`worker-error.ts` 的 case2（定时器）断言 master 的 `onerror` 收到含 "peer timer throw" 的 **Error** → 通过
（无 `GODOTJS_TEST_PROJECT_FAILED`，`GODOTJS_TEST_PROJECT_COMPLETED` 正常打印）。

### 观察（一次，未复现）
本轮有一次 smoke 跑到 1200s 超时被强杀；随后连续两次同样命令都是 rc=0 + COMPLETED（其中一次 343 行日志正常跑完）。
没有抓到卡住时的日志（进程被强杀），暂记为"见过一次的偶发"，若再出现需按 worker/timer 方向查。

## 步骤 4 续：`TransferableJSShadowRealm` 的 `onerror`（完成并实测）

### 改动
- `src/runtime/bridge/jsb_shadow_realm.cpp`：realm 的 `_on_message` catch 里先 `get_exception_value()` 再 `get_message()`，
  然后调用新增的 `TransferableShadowRealmImpl::_post_error_to_host(...)`：
  采集错误记录 -> 序列化 -> `master->post_message(Message(TYPE_ERROR, handle, …))`。
- **宿主侧无需改动**：`Environment::handle_message` / `update` 的 inbox 最终都走 `_on_worker_message`，
  所以 `TYPE_ERROR` 分支（含空 payload 分支）对 realm 同样生效 —— realm 的 `onerror` 收到的是重建的 **Error**。

### 踩到的坑（已修，注释留在代码里）
**同步投递导致 re-entrancy 崩溃**：最初用 `master->handle_message(...)`（同步）→ 宿主的 `onerror` 被插在
realm 的 `onmessage` 帧里执行 → 用户在 `onerror` 里调 `realm.terminate()`（很自然的写法）就会在帧内销毁
自己的 isolate：实测 `Fatal error in v8::Isolate::Dispose()`（栈：`_post_error_to_host` -> … -> `terminate`）。
改为 `master->post_message(...)`（异步，下一帧由宿主 update 派发）后消失，语义也与 worker 一致。

### 实测（v8 smoke，第四个用例）
```
ERROR: [ShadowRealm][Error] Uncaught Error: worker-error-test: peer throw
WARNING: [JS] GODOTJS_TEST_PROJECT_COMPLETED      (rc=0，无 FAILED)
```
`worker-error.ts` 的 `realmThrowsInOnMessage` 断言宿主 `onerror` 收到含 "peer throw" 的 **Error** → 通过。
peer 脚本现在同时支持 worker（`JSWorkerParent`）与 realm（`ShadowRealmParent`）两种宿主。

## 步骤 6（本地部分）
- jsc 语法门：`jsb_jsc_object.cpp` / `jsb_jsc_isolate.cpp`（含 `TryCatch::get_exception_value` 改动）均 rc=0。
- 待办：推送 + CI（`Test (host-jsc, macos-latest)` 等）需要用户授权。

## AC8（有界性）+ AC10（规范）完成

### AC8：`test_jsb_shadow_realm.h` 新增"错误记录有界"用例
- 例 1：错误对象上挂 **accessor**（getter 里计数）、**环**、**64 层深链**、**10 万元素数组** →
  断言 `getterCalls == 0`（accessor 一律不读）、环/深链进未携带清单、整体仍是可用 Error。
- 例 2：**200 个函数字段** → 断言未携带清单被截断为 64 条 + 一个 `"..."` 标记。
- 过程中修掉一个真 bug：清单上限的 `"..."` 标记原本放在**遍历之前**（`const record = ...` 之前），
  永远不会被 push → 清单只到 64 条且无标记。改为在 `note()` 首次超限时补标记（结果 65 ✓ 确定性）。
  先在 `node` 里用**逐字提取的采集脚本**复现（`{"lostCount":64,"hasMarker":false}`）再修的——比"改完重编"省事。
- **v8 doctest：87/87、1289 断言 SUCCESS**。

### AC10：规范落文件
新增 `.trellis/spec/godotjs-ext/cpp/cross-isolate-errors.md`，并在 `cpp/index.md` 的开发前检查单里挂上入口。
内容：8 条硬约束（异常不可跨 isolate / 投递必须异步 / `TryCatch` 取值顺序 / `has_caught()` 一次性 /
不要用 `ThrowException(value)` / 环境损坏时别跑 JS / 句柄须在 HandleScope 内 / 跨腿 API 差异）、
错误记录的形态与上界、`onerror` 契约表、写新代码时的检查单。

## 本地 AC 汇总

| AC | 状态 |
|---|---|
| AC1 崩溃先复现 | ✅（`report.md` 记录：SEH crash @ `jsb_shadow_realm.cpp:1252`，doctest 82→66 中断） |
| AC2 `evaluate` → host Error、不崩 | ✅ doctest |
| AC3 `importValue` reject Error（非字符串） | ✅ doctest |
| AC4 worker 启动失败 → `onerror` | ✅ 项目用例 |
| AC5 worker `onmessage` 抛错 → `onerror` | ✅ 项目用例 |
| AC6 Worker 与 TransferableShadowRealm 同形态 | ✅ 项目用例（同一宿主重建路径；发送侧两处 helper 逻辑相同、可后续合并） |
| AC7 额外字段 best-effort + 未携带清单 | ✅ doctest |
| AC8 有界性（getter/环/深链/大数组/清单上限） | ✅ doctest（本轮补） |
| AC9 门禁 | 本地 ✅（v8/qjs doctest + smoke、jsc 语法门）；**CI 待推送** |
| AC10 规范 | ✅ 本轮补 |
| AC11 清单用 `Symbol.for("jsb.untransferred")` | ✅ doctest |
| AC12 worker 定时器异常 → `onerror`；主环境只日志 | ✅ 项目用例 |

## 仍欠（明确记录）

1. `importValue` / `importValueSync` 的错误只有文案（`_importValue` 内部把异常压成字符串）→ 要富记录需把异常值带出来。
2. web 腿的 worker 错误路径未接（web 只有 emscripten 路径；本机不可测）。
3. 发送侧两个 helper（worker / shadow realm）逻辑重复，可合并到一个共享函数。
4. 既有隐患：对已退出的 worker 调 `terminate()` 会崩（非本任务引入）。
5. 观察一次 smoke 卡到 1200s（未复现，连续两次 rc=0）。

## 最终本地门禁（bg_13，全部代码就位后）

```
build qjs-ng rc=0 → doctest 89/89（1301 断言）SUCCESS → smoke rc=0
build v8    rc=0 → doctest 87/87（1289 断言）SUCCESS → smoke rc=0
```
（qjs 比 v8 多 2 个腿特定用例，属既有的正常差异；smoke 含项目里新增的 4 个跨隔离区错误用例。）

jsc 语法门：`jsb_jsc_object.cpp` / `jsb_jsc_isolate.cpp` 均 rc=0。

**本地可做的部分到此为止。** 剩下的是提交/推送 + CI（`Test (host-jsc, macos-latest)` 等）——按仓库规则需用户逐次授权。


## 步骤 7：错误记录从 JS 移植到 C++（本轮）+ 四个后端坑（全部实测）

### 改动
- `jsb_error_record.h/.cpp`：`capture` / `serialize` / `deserialize` / `rebuild` 全部 C++ 实现，
  单一 `jsb::error_record` 命名空间；不再有 JS 侧脚本（`kCaptureSource` 删除）。
  白名单转换器 `_variant_to_js` / `_js_to_variant` 手写，**不走 `TypeConvert`**。
  未携带清单的 symbol 键常量 `kSymbolKey` 单一来源，并从 `godot-jsb` 模块导出 `untransferred`（`Symbol.for`）。
- 类型声明：`scripts/typings/godot.minimal.d.ts`（`untransferred: unique symbol` / `JsbThrownValue` /
  全局 `Error` 增强）、`godot.worker.d.ts` / `godot.shadowRealm.d.ts`（`onerror?: (error: JsbThrownValue) => void`，
  去掉 "TODO not implemented yet"）。
- 测试：`test_jsb_shadow_realm.h` 新增原始值异常用例（`throw "primitive-boom"`）；
  项目用例 `worker-error` 改为 peer 挂 `fn` 字段、宿主用类型化的 `error[untransferred]` 断言清单含 `"fn"`。

### 四个坑（都是本轮定位并修掉的）
1. **函数也是 object**（v8）：采集时 `IsFunction()` 必须先于对象分支，否则 `e.fn` 被当成普通对象收成 `{}`。
2. **异常槽是栈槽别名**（quickjs）：`get_exception_value()` 的 `Local` 在 `get_message()` 之后是野指针；
   而**在异常仍挂起时跑 JS（采集要读属性）也会崩**。修法：异常在**调用点**先采集为纯数据记录再打日志/投递，
   `_post_error_to_master` / `_post_error_to_host` / `forward_error_to_master` 一律改成收 `const ErrorRecord &`。
3. **`Symbol::For` 返回悬垂句柄**（quickjs shim 的局部 `HandleScope`）：`rebuild` 挂清单时崩在 `set_js(symbol, ...)`。
   已修 shim（返回值分配在调用方作用域）。
4. **`get_message()` 对非 Error 抛出值不填 message**（quickjs）：`evaluate` 原来用"文案非空"判断要不要抛，
   于是 `throw "boom"` 被静默吞掉（宿主侧看到 `NO-THROW`）。改成用 `has_caught()` 判断。

### 本轮实测
```
qjs-ng：build rc=0 → doctest 90/90（1313 断言）SUCCESS → smoke rc=0 COMPLETED
v8    ：build rc=0 → doctest 88/88（1301 断言）SUCCESS → smoke rc=0 COMPLETED
jsc 语法门：jsb_error_record / jsb_bridge_module_loader / jsb_environment / jsb_shadow_realm /
            jsb_timer_action / jsb_worker / jsb_jsc_catch / jsb_jsc_primitive /
            jsb_quickjs_catch / jsb_quickjs_primitive / test_jsb_shadow_realm.h —— 全部 rc=0
typings：`npx tsc`（project）rc=0；测试用 `error[untransferred]` 走类型检查
```
（qjs doctest 90 vs v8 88：腿特定用例的既有差异。）

### 补记：`Symbol::_get_well_known` 同源 bug（本轮随审阅提问一起修掉）

`jsb_quickjs_primitive.cpp` 的 `_get_well_known`（`Symbol::GetIterator` / `GetToPrimitive` / `GetToStringTag` …）
与 `Symbol::For` 是**同一处写法错误**，而且多一处：

1. 函数内开了局部 `HandleScope` → `push_steal` 的返回值槽在函数返回时被析构释放 → 调用方拿到悬垂句柄；
2. `const JSValue &symbol_obj = isolate->stack_val(StackPos::SymbolClass)` 是槽的**借用**引用，
   却直接 `JS_FreeValue(ctx, symbol_obj)` → 引用计数欠账，反复查询会把 Symbol 构造器提前释放。

调用点在 `jsb_shadow_realm.cpp` 的跨环境 symbol 包装（9 个 well-known symbol 逐个查询），
项目用例没覆盖这条路径，所以此前一直没暴露。

**验证（按 fails-before / passes-after）**：
```
旧实现 + 新回归用例 → [runtime] [jsb] quickjs well-known symbols stay stable and owned
                      ERROR: test case CRASHED: Unhandled SEH exception caught（46 用例后中断）
修复后           → doctest 91/91（2348 断言）SUCCESS → smoke rc=0 COMPLETED
```
顺带补了 shim 缺失的卫兵（`JS_IsException` / `JS_IsSymbol`，与 jsc / web 两腿对齐；那两腿原本就是对的：
无局部 HandleScope + `push_copy`/返回栈位）。


## 步骤 8：错误记录只搬 JS 基础类型（按用户要求简化）+ realm 定时器异常也转发

### 1. 简化 `jsb_error_record.{h,cpp}`
只搬 **Error 自己的字段 + JS 基础类型**：
- `name` / `message` / `stack`；`cause`（是 Error 就递归其字段，否则只带基础类型）；
- 自定义字段只保留 string / number / boolean / null / undefined（新增 `primitive_is_undefined` 以区分 undefined 与 null）；
- 数组、普通对象、函数、symbol、BigInt、Godot 类型（`Object`/`Array`/`Dictionary`/...）**一律不搬**，
  只把路径登记进 `untransferred` —— 由用户在发送侧显式转换，JSB 不替用户决定怎么转。

实现上删掉了 `carry_object`、`_variant_to_js` / `_js_to_variant` 的数组与对象分支、
以及 `cause` 的 `Variant::Dictionary` 编解码（改成 `std::shared_ptr<ErrorRecord>` 直接递归）。

顺带修掉两个在简化中暴露的真 bug：
1. payload 读取必须用 `HasOwnProperty` 判断键存在性 —— `Get()` 对缺失键也返回 `undefined`，
   旧判据 `!value->IsUndefined()` 恰好掩盖了它；换成"存在即原始值"后，每个 payload 都被误判成
   `throw undefined`，宿主 `onerror` 收到 `undefined`（smoke 实测）。
2. `cause` 的"Error 形态"判定不能用 `read_own_data_property("stack")`：v8 的 `stack` 可能是 accessor，
   会被判成非 Error 而整条 cause 掉进未携带清单。改看自有键存在性（`stack` 或 `message`）。

### 2. `forward_error_to_master` 覆盖 shadow realm（上一问的落地）
- `Environment`：`worker_master_token_` / `worker_handle_` → `master_token_` / `master_handle_`，
  新增 `set_error_forward_target()`；`forward_error_to_master` 的判据从 `is_worker()` 改成"有没有登记转发目标"。
- `jsb_shadow_realm.cpp`：realm 的 handle 由宿主在环境创建**之后**才绑定，所以在 `create` 里补登记；
  只有 `TransferableJSShadowRealm`（唯一有 `onerror` 的形态）转发（`forwards_errors_to_host()`）。
- 新增项目用例 5：realm 里 `setTimeout(() => { throw ... })` → 宿主 `onerror`。
  修前：日志有 `timer error ... realm timer throw`，但 `onerror` 不触发、用例超时失败；
  修后：宿主收到重建的 Error，用例通过。

### 本轮实测
```
v8    build rc=0 → doctest 88/88（1306 断言）SUCCESS → smoke rc=0 COMPLETED（含用例 5）
qjs-ng build rc=0 → doctest 91/91（2353 断言）SUCCESS → smoke rc=0 COMPLETED（含用例 5）
tsc（project）rc=0；jsc 语法门 7 个 TU 全 rc=0
```


## 步骤 9：node / web 两条后端腿（进行中，node 有阻塞）

### node：实现完成，功能实测可用，但多场景运行出现非确定性访问违例
**实现**
- `Environment`：新增 `has_error_forward_target()`；`_forward_uncaught_error`（文件内 static）注册成全局
  `__jsb_forward_uncaught_error`（仅 `JSB_WITH_NODE`）：有转发目标（worker / transferable shadow realm）时
  `capture -> forward_error_to_master -> return true`（吞掉），否则返回 false。
- `NodeRuntime::bootstrap_script()`：装 `process.setUncaughtExceptionCaptureCallback`，**惰性**取上面的入口
  （bootstrap 早于 JSB 的 global 注册；装钩子时取不到），转发成功就吞掉，否则重抛保持 node 默认行为。
  （node 自带的 `setTimeout`/IO 回调在 node 下不经过 JSB 的 `timer_manager_`，所以只能在 node 这边挂钩子。）

**实测**
- 单独跑 worker-error 场景：5 个用例全部通过（含 worker 定时器、realm 定时器 → 宿主 `onerror`），
  转发链路有打点证据（`hook enter → captured msg=realm timer throw → forward-serialize size=85 → forwarded →
  master recv TYPE_ERROR → master onerror(record) called`）。
- 修前同一场景：node 直接把进程打挂（`Timeout._onTimeout ... Node.js v24.21.1-pre`，rc=1，没有任何 `onerror`）。

**阻塞（未定位）**：多场景 smoke 出现非确定性访问违例（rc=0xC0000005）。已排除/已确认：
- 去掉用例里的 `realm.terminate()` → 稳定通过（2/2）；
- 定时器回调不抛错 + 保留 `terminate()` → 稳定通过；
- 延迟一个 tick 再转发 → 更糟（场景内就崩）；只 `capture` 不转发 → 仍崩 → 不是"转发"这一步；
- 崩溃位置会变（有时在下一个场景开始时、有时提前到本场景），像内存被写坏；
- 符号化失败：Godot 崩溃处理器与 `llvm-symbolizer` 都解不出这一版 DLL 的符号（`??:0:0` / `__guard_memcpy_fptr`）。
→ 下一步需要真正的调试器会话（DAP / VS / WinDbg），或把 hook 的侵入面再缩小。

### web：先纠正假设，再接线（进行中）
scout 核实（推翻我之前的说法）：
- web **没有** `jsb::Message` 通道：worker 消息是"活 JS 值"通道（`jsbi_PostMessage` → `_jsbb_.PostMessage`，
  载荷是 StackPosition），所以 `Message::get_buffer()` 在 web 上根本不用；
- `_on_worker_message`（含 `TYPE_ERROR → onerror`）在 web **不编译**；真正的接收端是
  `Worker::on_web_message_from_pthread`（`jsb_worker.cpp:857-950`，master 侧：`transfer_id==0` → onready，否则 onmessage）；
- **shadow realm 在 web 整体禁用**（`JSB_SHADOW_REALM_ENABLED=0`），无需 web 对应实现；
- web 的 `JSB_WITH_ESSENTIALS=0` → **用浏览器原生定时器**（我之前说 web 走 `timer_manager_` 是错的）；
- 本机可用 `D:/Dev/emsdk`（4.0.11）构建 web 腿，但**只能构建，不能跑**（仓库里没有 web 运行时测试编排；CI 的 web 腿也只构建）。

已完成的第一步：把 payload 编解码升为公开 API（`jsb::error_record::record_to_js` / `record_from_js`），
并定义 web 通道区分"错误记录"与普通消息的包装键 `kErrorPayloadKey`（`"__jsbError"`）。
发送端（worker 三个 catch 点）、接收端（`on_web_message_from_pthread` 的错误分支）尚未接线。

### 本轮各腿状态
```
v8     build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
qjs-ng build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
node   build rc=0 → doctest 90/90 SUCCESS → smoke ✗（上述非确定性访问违例；单场景 worker-error 全过）
```
临时物已清理（TS 与 `start.ts` 还原、`[dbg-*]` 打点删除、拷到 addon 目录的 pdb 删除）。


## 步骤 10：node 崩溃——先解决"符号化失败"，再抓到带符号的真实栈

### 为什么之前所有回溯都没符号（找到了）
node 腿会额外构建一个启动器 exe（`bin/windows/godotjs-ext.exe`），而它和主库**共用同一个 PDB 路径**
（`bin/windows/godotjs-ext.pdb`）。exe 在主库之后链接 → 把主库的 PDB 覆盖掉 → 被 Godot 加载的主库
没有可用符号：Godot 崩溃处理器、`llvm-symbolizer`、lldb 断点全部失效（"couldn't map PC to fn name"）。

- 证据：`llvm-readobj --coff-debug-directory` 显示 exe 与 dll 都记录 `PDBFileName: ...godotjs-ext.pdb`；
  PDB 的 `.text` 只有 0x98D0D（≈650KB，等于 exe 的规模），而主库 `.text` 是 0x48881FC（≈76MB）。
- 修法（`SConstruct`，已改）：给启动器 exe 单独的 PDB 名（`godotjs-ext.exe.pdb`）。
  这类冲突只在 node 模式出现（只有 node 才构建该 exe），也解释了为什么早期会话能符号化、
  现在（node 腿）不行。

改完之后崩溃栈立刻可读（同一份场景、同一个失败）：

```
[0] v8::internal::Isolate::FindOrAllocatePerThreadDataForThisThread
[2] v8::Isolate::Scope::Scope          (third/libnode/include/v8-isolate.h:371)
[3] jsb::ObjectCrossWrapper::finalizer (src/runtime/bridge/jsb_shadow_realm.cpp:855)
[4] jsb::Environment::free_object      (jsb_environment.cpp:1099)
[5] jsb::Environment::exec_async_call  (jsb_environment.cpp:679)
[6] jsb::Environment::add_async_call   (jsb_environment.cpp:704)
[7] jsb::Environment::object_gc_callback_second_pass (jsb_environment.h:831)
[8] v8::internal::GlobalHandles::InvokeSecondPassPhantomCallbacks
[11] node::PerIsolatePlatformData::RunForegroundTask
[13] jsb::impl::NodeRuntime::PumpEventLoop (jsb_node_runtime.cpp:268)
[14] jsb::Environment::update          (jsb_environment.cpp:561)
```

### 根因（已定位，修法明确，尚未改完）
`ObjectCrossWrapper::finalizer` 里 `Environment::wrap(isolate)` 是**不安全**的：它读 isolate 的
embedder data（`jsb_environment.h:355`）。而当 shadow realm 被 `terminate()` 之后，node 的 per-isolate
foreground task 队列仍会派发 GC second-pass 回调 → `object_gc_callback_second_pass` 入队 async call →
`free_object` → 这个包装器的 finalizer 拿着**已 dispose 的 guest isolate** 反查环境 → 得到悬垂
`Environment*` → 在 `is_disposing()` 上踩野指针（`0xC0000005`）。

已试过但不够的两个守卫（保留在代码里，作为部分防护）：
1. 用 `free_object` 传进来的 `p_env` 判 `is_disposing()` —— 错，那个是**宿主**环境；
2. 用 `Environment::wrap(isolate)` 拿到"owner"再判 —— 依然崩，因为 `wrap()` 自身对已销毁 isolate 不安全
   （崩溃点从 finalizer 内部挪到了 `is_disposing()`，栈已有符号可证）。

**下一步（明确的修法）**：不要让 finalizer 依赖 isolate→env 的反查；在包装器创建时记下所属环境的
token/强引用（`EnvironmentRef` 或 store token），finalizer 里用 store 查询判活：环境已不在就只
`memdelete(self)`（不再进 isolate、不再 `remove_cache`——那个缓存本来就随 isolate 一起死）。

### 本轮门禁
```
node build rc=0（含 debug_symbols）→ 单场景 worker-error 通过；多场景 smoke 仍崩（上述根因，未修完）
v8 / qjs-ng：本轮未重跑（SConstruct 改动只影响 node 模式的启动器 PDB 命名）
```
临时物：`[dbg-*]` 打点已清、TS/`start.ts` 未留残留、拷到 addon 的 pdb 已删。


## 步骤 11：node 崩溃已修完（多场景 smoke 连续 3 次绿）

### 修法（两处，都在 `src/runtime/bridge/jsb_shadow_realm.cpp`）
1. `CrossWrapper` 在构造时记下**所属环境**的指针 `guest_env_key_`（只当 `EnvironmentStore` 的键用，
   绝不直接解引用）。
2. `ObjectCrossWrapper::finalizer` 改成用 `Environment::_access(guest_env_key_)` **查活**（销毁会从表里摘掉）：
   - 环境还在 → 老路径（`JSB_ISOLATE_SCOPE` + `HandleScope` + `remove_cache` + `memdelete`）；
   - 环境已不在（realm 被 `terminate()` 之后宿主 GC 才回收这个包装器）→ **故意不 `memdelete`**：
     包装器的 `TStrongRef` 持有 guest isolate 的 `v8::Global`，而那个 isolate 已 dispose，释放它会进
     v8 的 GlobalHandles 并 fatal（实测 `V8_Fatal @ GlobalHandles::NodeSpace::Release`）。
     句柄空间随 isolate 一起消失，所以这里只"泄漏"包装器自身的 C++ 内存（每包装器约百字节，有界）。
     代码里留了 `TODO(长期)`：realm 析构时应在 guest isolate 还活着时主动回收它名下的包装器，
     那样既不用泄漏、也不会让 finalizer 面对已销毁的 isolate。

### 顺带修掉的构建问题（`SConstruct`）
node 腿的启动器 exe 与主库共用 PDB 路径 `bin/windows/godotjs-ext.pdb`，exe 后链接会把主库符号覆盖掉
（这是本轮"所有崩溃回溯都 couldn't map PC"的原因）。给启动器单独用 `godotjs-ext.exe.pdb`。

### 本轮门禁（全部实跑）
```
node  build rc=0 → doctest SUCCESS → smoke 连续 3 次 rc=0 COMPLETED（无崩溃）
v8    build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
qjs-ng build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
jsc 语法门：jsb_shadow_realm / jsb_error_record / jsb_environment / jsb_worker / jsb_timer_action /
           jsb_bridge_module_loader / jsb_jsc_catch / test_jsb_shadow_realm.h 全部 rc=0
```


## 步骤 12：审阅意见处理 + `jsb_shadow_realm.cpp` 损坏与恢复

### 审阅点落地
1. **`Environment::master_token_` / `master_handle_` 改成 private**：它们的全部使用点都在 `Environment` 自己
   （构造函数 + `forward_error_to_master`），外部只需要 `has_error_forward_target()` / `set_error_forward_target()`
   （改 `jsb_environment.h`）。
2. **`SConstruct` 的启动器 PDB 改动**：见 §11；本轮把注释与实现收紧（从 exe 目标名派生、并加"与主库同名时改名"
   的兜底），并写明 ① 非 MSVC 走不到这里（与 `make_target_env` 同一个 toolchain 判定，MinGW 没有 PDB 概念）；
   ② 与主库一致，这个判定不看 target：**release 下同样会产出 PDB**（主库本来就这样；要收敛得单独按
   `debug_symbols`/target 处理）。
3. **node 侧为什么改 bootstrap（而不是 C++ hook）**：vendored libnode 头里**没有**能装"带值 uncaught handler"的
   C++ API（只有 `node::FatalException`、`SetIsolateUpForNode`（装 node 自己的 listener）、`SetAbortHandler`、
   `SetProcessExitHandler` 与 v8 的 abort 回调）。`process.setUncaughtExceptionCaptureCallback` 是 node 官方
   且唯一带值的钩子，且每个 JSB 环境各有自己的 `node::Environment`，天然按环境生效 —— 所以放在该环境已有的
   bootstrap 脚本里（3 行 + 一次惰性取 global），C++ 只提供入口函数。C++ 侧替代方案要么替换 node 的未捕获
   机制、要么包 `setTimeout`（改变用户可见的函数身份），都不合适。
4. **跨环境包装器清理（用户建议的"realm 析构时清缓存"）**：试过在 `ShadowRealmImpl::finish()` 里
   `release_isolate()`（释放每个包装器持有的 guest `Global` + 清缓存），**实测 abort**：
   `v8::Isolate::Deinitialize() Deinitializing the isolate that is entered by a thread`
   （`finish` → `env_.reset()` → `~NodeRuntime` → `MultiIsolatePlatform::DisposeIsolate`），
   由 `onerror` 里调 `terminate()` 触发 —— 说明 terminate 路径上 guest isolate 仍处于 entered 状态，
   是**预存**的 isolate 生命周期问题。所以本轮保留"记下所属环境指针 + store 查活 + 环境已消失则不析构"
   的兜底（`TODO(长期)` 指明正确做法）；缓存清理（`purge_cache_isolate`）保留为工具函数但暂不调用。

### 事故与恢复（如实记录）
审阅过程中我用按字符切片的脚本改这个文件，切错了两处，`jsb_shadow_realm.cpp` 被切坏（重复/缺块）。
处理：`git checkout --` 该文件回 HEAD（该文件改动全是本任务的、未提交），把损坏版本留档到
`.agent_tmp/shadow_realm_mangled.cpp` 作为对照，再用**按花括号配对的提取器**从留档里取出完整片段重新落地。
恢复过程中发现该文件里 `_post_error_to_host` 调用的是 **`handle_message`（同步）**，而它自己的 NOTE 写明必须用
`post_message`（异步）——同步投递会把宿主 `onerror` 插进 realm 的 onmessage 帧里，`terminate()` 在帧内销毁
guest isolate → 正是上面那条 abort。改回 `post_message` 后 node 恢复绿灯。

### 本轮门禁（全部实跑）
```
node  build rc=0 → doctest 90/90 SUCCESS → smoke 连续 3 次 rc=0 COMPLETED
v8    build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
qjs-ng build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
jsc 语法门：jsb_shadow_realm / jsb_error_record / jsb_environment 全部 rc=0
```


## 步骤 13：node 钩子改成 C++ 实现（不动 bootstrap、不暴露 global）+ 缓存清理按用户方案

### node 未捕获异常钩子（C++ / v8 实现）
新增 `src/runtime/impl/node/jsb_node_uncaught_hook.{h,cpp}`（照 `jsb_node_console_hook` 的结构）：
- 用 C++ 取 `process.setUncaughtExceptionCaptureCallback`，传入一个 **`v8::Function::New` 创建的 C++ 回调**
  （`_on_uncaught_exception`）：有转发目标就把异常采集为记录并 `forward_error_to_master`（吞掉）；
  否则把异常抛回引擎，交给 node 自己的未捕获路径。
- `jsb_node_runtime.cpp` 的 **bootstrap 脚本恢复原样**（不再有 `setUncaughtExceptionCaptureCallback` 片段），
  `jsb_environment.cpp` 里也不再往 globalThis 上挂 `__jsb_forward_uncaught_error`：C++ 实现不需要暴露任何东西。
- 安装时机：只给"有转发目标"的环境装（主环境不装、行为不变）——worker 在构造时（`CreateParams` 带目标）、
  transferable shadow realm 在 `set_error_forward_target()` 里补装。
- **踩到的坑**：一开始按 isolate 指针做"已安装"去重，结果 shadow realm 环境被静默漏装 —— 前一个环境的
  isolate 销毁后，新环境的 isolate 很可能落在**同一地址**，去重表命中就跳过了。去掉去重（重复安装只是替换
  回调，无副作用）后 node 连续 4 次 smoke 全绿。

### 跨环境包装器：缓存清理按用户方案，剩下的那一处说明
- 按用户的顺序做了：`finish()` 里**先取 isolate**，`env_->dispose()` -> `env_.reset()`（环境/isolate 销毁）
  **之后**再 `CrossWrapper::purge_cache_isolate(isolate)` —— purge 只动缓存表（键是裸值：isolate 指针 + 对象
  的 identity hash），不碰 isolate，所以放在 reset 之后是安全的。实测不再 abort（smoke 4/4 绿）。
- 但"finalizer 发现缓存里没有 isolate_ 就跳过清理自己"**不能**当作"所属环境是否还活着"的判据：
  缓存键 `WrapperIdentity` 需要 guest 对象的 identity hash，而取它要 `value_.object_.Get(isolate_)` ——
  在 isolate 已销毁时就是野指针。所以 finalizer 仍用创建时记下的 `guest_env_key_` 指针去 `EnvironmentStore`
  查活（store 只比较指针、不解引用，安全）。
- 另外，即使跳过缓存清理，**也不能 `memdelete` 这个包装器**：它内部的 `TStrongRef` 持有 guest isolate 的
  `v8::Global`，isolate 已销毁时释放会 fatal（实测 `V8_Fatal @ GlobalHandles::NodeSpace::Release`）。
  所以环境已消失时是"放弃析构"（每包装器约百字节、有界），`TODO(长期)` 指明：要根治得先修 terminate 路径上
  "guest isolate 仍被线程 entered 就 dispose"那个预存问题（`v8::Isolate::Deinitialize() ... entered by a
  thread`），修好后才能在 guest 还活着时回收它们、既不用泄漏也不用放弃析构。

### 本轮门禁（全部实跑）
```
node   build rc=0 → doctest 90/90 SUCCESS → smoke 连续 4 次 rc=0 COMPLETED
v8     build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
qjs-ng build rc=0 → doctest SUCCESS → smoke rc=0 COMPLETED
jsc 语法门：jsb_shadow_realm / jsb_environment / jsb_error_record → 全 rc=0
```


## 步骤 14：node 钩子收进 `jsb_node_runtime.cpp`（不再单文件/不经环境），并解释包装器与缓存的数据流

### 改动
- 删掉 `impl/node/jsb_node_uncaught_hook.{h,cpp}`；回调与安装都放进 `jsb_node_runtime.cpp`（文件内 static）。
- 安装点改到 **`NodeRuntime` 的构造函数**（紧跟 console hook 之后）：`NodeRuntime` 本来就是 per-Environment 的，
  "每个环境一份"这件事它在构造时就成立，不需要 `Environment` 参与。
- `jsb_environment.{h,cpp}` 里相关引用全部撤掉（`set_error_forward_target` 回到 inline 的纯字段赋值），
  也不再往 globalThis 上挂任何东西（该 global 在上一轮已删）。
- 回调里没有转发目标时用 `isolate->ThrowException(info[0])` 把异常抛回引擎（node 按自己的未捕获路径处理）；
  本想用 `node::FatalException`，但它要 `const v8::TryCatch &`，拿不到。

### 包装器 / 缓存的数据流（回答"isolate 都没了怎么还有 TStrongRef"）
- `ObjectCrossWrapper::create()` 里是 `memnew(ObjectCrossWrapper(p_guest_isolate, p_guest_obj))` 之后
  `p_host_env->bind_js_owned_pointer(...)` —— **C++ 包装器对象与它对应的 JS 对象都属于宿主环境**，
  finalizer 由宿主的 GC 触发；所以**guest 环境/isolate 销毁后，包装器依然活着**。
- `CrossWrapper` 的 `TStrongRef<v8::Object> value_` 持有的是 **guest isolate 上**的 `v8::Global`（包住的 guest 对象）。
  guest isolate 销毁不会带走包装器，只会让这个 Global 的句柄空间消失 —— 之后 `~Global` → `Reset()` 就是
  `V8_Fatal @ GlobalHandles::NodeSpace::Release`（实测），所以环境已消失时不能析构包装器。
- `CrossWrapper::wrapper_cache_` 是 **static** 表：键 `WrapperIdentity{ uintptr_t guest_isolate; int guest_value_hash }`
  （纯裸值），值是 **`TWeakRef<v8::Object>`**，指向**宿主侧**的 proxy。所以在 `env_.reset()`（guest isolate 已销毁）
  之后清缓存是安全的 —— purge 只做 map erase + 释放宿主 isolate 上的弱引用，**完全不碰 guest isolate**
  （本轮就是按这个顺序做的，实测 4/4 绿）。
- 但"缓存里没有这个 isolate 了"**不能**当 finalizer 的存活判据：构造查找键需要
  `guest_value->GetIdentityHash()`，那要经 `value_.object_.Get(isolate_)` 读 guest 对象 —— isolate 已销毁就是
  野指针。所以 finalizer 仍用创建时登记的 `guest_env_key_` 去 `EnvironmentStore` 查活（store 只比较指针）。


## 步骤 15：复测用户手写的 `jsb_shadow_realm.cpp`（缓存按 isolate 索引 + finalizer 直接析构）

### 复测结论
1. **原样编译不过**：`jsb_shadow_realm.cpp:414` 的
   `wrapper_cache_.emplace(p_isolate, IsolateWrappers{ { hash, TWeakRef<v8::Object>(...) } })` ——
   `TWeakRef` 不可拷贝，而 `unordered_map` 的 initializer_list 构造要求拷贝元素。
   最小修法（本地已打上、仅为复测；逻辑未动）：先 `emplace(p_isolate, IsolateWrappers{})`，
   再 `it->second.try_emplace(hash, p_host_isolate, p_wrapper)` 就地构造。
   （顺带：`is_isolate_alive` 需 `const`，否则 `throw_error_if_dead() const` 编不过。）
2. 修完能编过，**node 仍崩**（smoke 3/3 失败：两次 0xC0000005、一次 0x80000003），栈：
   `ObjectCrossWrapper::finalizer` -> `memdelete` -> `~ObjectCrossWrapper` -> `~CrossWrapper`
   -> `~TStrongRef<v8::Object>` -> `~Global` -> `PersistentBase<Object>::Reset`
   -> `GlobalHandles::NodeSpace<Node>::Release` -> `V8_Fatal` / AV。
   —— 即"包装器内部的 `TStrongRef` 持有 **guest isolate** 的 `v8::Global`，guest isolate 已销毁后再释放它"。
   `remove_cache()` 的提前返回（缓存已被 purge）挡不住这个：崩在 `~TStrongRef`，不在缓存。
3. **v8 / qjs 两条腿：doctest 通过、smoke COMPLETED（不崩）**。也就是说这个崩溃是 node 的环境/isolate
   销毁时序暴露出来的。
4. 另外，缓存按**裸 `v8::Isolate*`** 索引来判"isolate 还活着"有地址复用风险：前一个环境的 isolate 销毁后，
   新环境的 isolate 可能落在同一地址 —— 本轮 node 钩子的 `s_installed` 去重就实测踩过一次（同一个地址被复用，
   新环境的安装被静默跳过）。判活需要不可复用的信号（代次/令牌），或者干脆把包装器生命周期绑到 realm 上，
   在 guest 还活着时回收。

### 对照：先前本地绿灯的状态
"环境已消失就不析构该包装器（放弃析构，~百字节有界）+ 用创建时登记的指针去 `EnvironmentStore` 查活 +
`env_.reset()` 之后再 purge 缓存"：node smoke 连续 4 次 COMPLETED、v8/qjs 也全绿。
两条路的分歧点只有一个：**guest isolate 死后能不能安全释放包装器持有的那个 `Global`** —— 不能。

## 步骤 16：包装器注册表改存裸指针（去掉 internal field 反查）+ 缓存清理配对修正，三腿全绿

### 改动（全在 `src/runtime/bridge/jsb_shadow_realm.cpp`）
1. **注册表元素 `v8::Global<v8::Object>` → `CrossWrapper *`**（`get_wrappers()` 变成
   `internal::SArray<CrossWrapper *>`，仍为函数内 static）。`SArray` 要求元素可拷贝/可移动，
   而 `v8::Global` / `TStrongRef` / `TWeakRef` 都是 move-only（`jsb_ref.h:31-36,74-79`），
   之前那版只能靠 `std::move` 绕，一旦走拷贝构造路径就 `C2280`。裸指针是 POD，顺带把
   `add_cache` 的签名从 `const v8::Local<v8::Object> &` 换成 `CrossWrapper *`。
2. **`try_get_cache` 不再直接从 `Global` 取 Local**，改为
   `get_wrappers().get_value(idx)->get_obj(p_host_isolate)`：走 `CrossWrapper` 自己的
   "死了就抛 dead wrapper" 判定（`get_obj` 在 `value_.Get(isolate_)` 为空时 `jsb_throw`），
   语义比裸 `Global::Get` 更明确。
3. **`purge_cache_isolate` 删掉全部 `GetAlignedPointerFromInternalField`**。原因：`ObjectCrossWrapper`
   缓存的是 `v8::Proxy`，`GetAlignedPointerFromInternalField` 对 Proxy 直接 `FATAL ERROR:
   v8::Object::GetAlignedPointerFromInternalField() Internal field out of bounds`；而且用户明确要求
   不通过内部字段反查指针。现在注册表里存的就是指针本身，purge 只需
   `get_wrappers().get_value(idx)->reset(true)` 再 `remove_at(idx)`。`reset(true)` 的
   `p_reset_only` 语义即"只清自身的 v8 引用、不回头动注册表"，避免 `remove_cache` 与 purge 双删同一槽。
4. **第二段遍历的语法与语义修正**：`while (auto it = map2.find(...))` 在 C++ 里非法（声明语句不能
   作条件），改成 `for (auto it = map2.find(p_isolate); it != map2.end(); it = map2.find(p_isolate))`；
   同时补上循环体内的 `map2.erase(it)`（原来只摘 `SArray` 槽、不清索引，会留下指向空槽的条目）。
5. **`remove_cache` 两处真实缺陷**：
   - `jsb_check(p_host_isolate = isolate);` 是**赋值**不是比较（原文件就有的手误，审查中抓到），改成 `==`；
   - `map2.erase(it3)` 之后才读 `it3->second` 取 `WrapperIdx` —— **erase 后迭代器失效**，是悬垂读。
     改成先 `const WrapperIdx idx = it3->second;` 再 erase。原来能"跑通"只是运气：`erase` 通常不
     立刻复用节点内存，且该槽随后本来就要 `remove_at`。
6. **`add_cache` 的调用点从 `proxy` 改为 `ptr`**：`ObjectCrossWrapper::create` 里缓存键仍是
   `p_guest_obj`，但存进注册表的是 C++ 包装器指针，因此传 `ptr`（`FunctionCrossWrapper` 同理）。

### 本轮门禁（全部实跑）
```
node  build rc=0 → doctest 90/90 SUCCESS → 全场景 smoke 连续 40 次 rc=0 COMPLETED
      （日志确认覆盖 res://tests/worker-error/WorkerError.tscn）
v8    build rc=0 → doctest SUCCESS → smoke 连续 3 次 rc=0 COMPLETED（另 24 次全绿）
qjs   build rc=0 → doctest SUCCESS → smoke 连续 3 次 rc=0 COMPLETED（use_quickjs_ng=yes）
jsc   语法门：clang++ -fsyntax-only 覆盖 jsb_shadow_realm.cpp / jsb_environment.cpp /
      jsb_error_record.cpp / jsb_cross_isolate_util.cpp / jsb_worker.cpp → 全部 rc=0
      （注：曾出现 `'node.h' file not found`，原因是 jsc 腿需 `JSB_WITH_NODE 0` 的
      `src/jsb.gen.h`，而工作区里那份是 node 构建产物；把 gen.h 临时切到 JSB_WITH_NODE 0
      再跑即通过，测毕已还原）
```

### `node::AsyncHooks::FailWithCorruptedAsyncStack`：**未定性**（2026-10-08 重测，推翻先前的"假警报"判断）
先前我把它写成"假警报 / 与本次改动无关 / 非本任务引入"，**那个结论的证据是不成立的**，此处更正。

#### 干净环境下的实测（无重编、无其它进程、部署 DLL 与构建 md5 一致）
```
node 全场景 smoke 连续 40 次（构建后不再重编）→ 37 绿，3 次 rc=1 + FailWithCorruptedAsyncStack
        （7.5%；其中 COMPLETED 已打印，崩溃发生在全部场景跑完之后）
```
即：**它不是"边跑边编"的假象**，在干净环境下稳定复现约 7.5%。

#### 崩溃栈（每次都落在同一个 worker 上）
```
node::AsyncHooks::FailWithCorruptedAsyncStack   (env.cc:1915)
node::AsyncHooks::pop_async_context             (env.cc:167)
node::InternalCallbackScope::Close              (callback.cc:140)
node::InternalMakeCallback → Environment::CheckImmediate (env.cc:1626)
uv_run → jsb::impl::NodeRuntime::PumpEventLoop  (jsb_node_runtime.cpp)
     → jsb::Environment::update                 (jsb_environment.cpp:528)
     → jsb::WorkerImpl::_update_environment / _run   ← 某个 worker 线程
```
发生时机在**全部场景跑完之后**（`GODOTJS_TEST_PROJECT_COMPLETED` 已打印），即 worker 环境的回收阶段。

#### 日志相关性（343 份 OK + 21 份崩溃）
| 特征 | 崩溃样本(21) | 干净样本(343) |
|---|---|---|
| node 未捕获钩子被触发（`_node_uncaught_exception`） | **21/21（必然）** | 156 份也有 |
| 覆盖 worker-error / cross-environment 场景 | 全部 | 全部 |

→ 钩子被触发是**必要但不充分**条件；光有钩子触发不足以解释，说明还有别的时序因素。

#### 单场景隔离：复现不出来
`WorkerError.tscn` 单独 12 次、`CrossEnvironment.tscn` 单独 12 次、`NodeRuntime.tscn` 单独 12 次
→ **全部 rc=0，无一次崩溃**。即崩溃需要**整条多场景套件**（环境反复创建/销毁的累积）。

#### 先前那些"对照"为什么都不能用（如实记录）
- "把 `src/` 全 stash 后 32 次全绿"：那次是与用户并行重编同批文件期间跑的，**样本被污染**。
- "shadow_realm 单文件换回 HEAD 仍复现 2/16"：**反而说明它不是 shadow_realm 引入的**；但样本同样偏小、且在污染窗口内。
- "禁用 node 钩子后 40/40 全绿" / "钩子装但 body no-op 40/40 全绿"：**这两个对照是无效的** ——
  钩子被禁用后，worker 的未捕获异常无人处理 → node 默认行为杀掉 worker → 套件**卡住不完成**
  （实测 40/40 都是 `rc=1` 且没有 `COMPLETED`），根本没跑到崩溃点。**凡是"让套件卡住"的对照都没有证明力。**

#### 已修复（2026-10-09，node 40/40 干净全绿）——**修法已更正**

**第一次的修法不够**：只把钩子限制到"有转发目标的环境"后，仍然 2/40 复现（而且形态从
"干净退出 rc=1" 变成"卡住"），说明**真正的根因不只是范围**。

**真根因**：`process.setUncaughtExceptionCaptureCallback` 的回调是在 **node 自己的异步上下文
作用域内**被调用的，而 node 的 async 栈由"调用它的那一帧"负责收尾。在回调里跑 JS
（`capture` 要读属性、`forward` 要跨环境投递）会打乱该栈；此后
`InternalCallbackScope::Close` 撞上 `AsyncHooks::pop_async_context` 的一致性检查
（`async hook stack has become corrupted (actual: N, expected: 0)`，
node 24 `src/env.cc:165` / `:1908`）→ `Environment::Exit` 结束进程。

**最终修法（两处，合起来才成立）**：
1. **回调里只暂存、不跑 JS**：`Environment::stash_uncaught_exception()` 把异常值放进
   persistent 槽（双缓冲 + mutex，因为回调可能来自 uv 线程池），立即返回；
   真正的 `capture` + `forward_error_to_master` 放到**下一帧**
   `Environment::update()` → `Environment::flush_uncaught_exception()` 里做 —— 那里已经离开
   node 的 capture 作用域。
2. 钩子**只装在有转发目标的环境**（worker / transferable shadow realm），主环境完全不装，
   完整保留 node 默认的"打印 + 退出"。

**实测（干净环境、构建后不再重编）**：
```
修前（当前 main 形态）        node smoke 40 次 → 38 绿 + 2 次 async-stack 退出（5%）
只加"范围限定"                → 仍 2/40（形态变为卡住）—— 单独不够
只加"回调只暂存"              → 40/40 全绿
两者都加（最终提交）          → 40/40 全绿，doctest/三腿 smoke 全绿
```

#### 早期记录（当时的修法，已被上面取代）
根因是**我自己的 node 钩子安装范围错了**：`process.setUncaughtExceptionCaptureCallback` 的语义是
"认领"异常，而我给**每一个**环境（含主环境）都装了它 —— 主环境没有 `onerror` 接收者，异常被吞掉，
node 的异步上下文栈随之失衡，之后 `InternalCallbackScope::Close` 撞上
`AsyncHooks::pop_async_context` 的一致性检查（`src/env.cc:165`），`FailWithCorruptedAsyncStack`
直接 `Environment::Exit` 结束进程（所以是"打印了 COMPLETED 之后 rc=1"，不是段错误）。

修法：
1. 钩子**只装在"有错误转发目标"的环境**上（worker 在 `Environment` ctor 里按 `CreateParams` 判断，
   realm 在 `set_error_forward_target()` 时补装），主环境完全不装，保留 node 默认的打印+退出。
   安装入口收敛成 `Environment::_ensure_node_uncaught_hook()`，`NodeRuntime` 不再自作主张安装。
2. 顺带修掉同一批的两个真缺陷：
   - `CrossWrapper` 系列：`reset()` 原先拿 `caller_isolate_` 当宿主 isolate 去操作注册表，
     而 `wrap_cross_env_value` 的"传送到其他环境"分支会用**第三方 isolate** reset 别人的包装器；
     改为显式记录 `host_isolate_`。同时 `remove_cache` 不再在 finalizer 里
     `get_obj(host)->GetIdentityHash()`（宿主 isolate/上下文可能已不可用 → quickjs 腿实测
     `Global::Get → push_copy` 报错 → 崩溃处理器重入 → **死循环刷日志**），改为创建时记录
     `object_hash_`。
   - `remove_cache` 里对已摘除的 `SArray` 索引先做 `is_valid_index()` 判定（purge 可能已摘掉该槽）。
3. 频率变化：修前干净环境 **3/40**；只修钩子范围后 1/40；补上 CrossWrapper 的两个缺陷后 **0/30**。

#### 复现/取证方法上的两条教训（写下来避免重犯）
- **让套件"卡住"的对照没有证明力**：把钩子禁用、或装成 no-op，都会让 worker 的未捕获异常没人处理
  → 套件卡死不完成，根本没跑到崩溃点（我误把这种"40/40 无崩溃"当成了对照结果）。
- **"边跑引擎边 scons 重编"会污染样本**：那次"全 src stash 后 32 次全绿"就是在这种窗口里跑的。

#### 原先的结论（保留，已作废）
**未定性**。可支持的事实：干净环境下 7.5% 复现、总是某个 worker 的 node 事件循环、
需要整条套件、与我的 node 钩子被触发强相关但非充分。
要真正定性，需要**两个完整构建（本分支 vs HEAD）× 各 N≥40 次、在同样干净条件下**对比 ——
那需要临时把多个 `src/` 文件换成 HEAD 版本（**改动工作区状态，按新规范必须先经用户同意**）。

### 步骤 17：补上 `try_get_cache` 复用路径的针对性测试（2026-10-08）

新增用例：`test_jsb_shadow_realm.h` → `ShadowRealm: a guest object is wrapped once per host isolate (cache reuse)`。

- **断言**：guest 侧把同一个对象返回两次 → 宿主侧两个包装器 `===` 为 true（`"same":true`）；
  不同 guest 对象 → 不为 true（`"diff":false`）；guest 侧自读 `globalThis.__shared.marker` 正常。
- **有牙（teeth check）**：把 `try_get_cache` 首行改成 `return {}`（禁用复用，每次新建包装器）后，
  该用例的 `"same":true` 断言**确实失败**（`test_jsb_shadow_realm.h(467): ERROR: ... values: false`），
  证明它能区分"命中缓存"与"每次都新建"。已按单文件备份还原（未动工作区状态）。
- **最终**：node doctest **91/91（1192 断言）SUCCESS**。

#### 追查中发现：宿主经 Proxy 读 guest 属性返回 `undefined`（既有缺陷，已定性、未修）
测试过程中我原本还想断言"复用出来的宿主包装器仍能读到 guest 侧字段（Proxy 转发没坏）"，
结果它**挂了**。逐层定位（每步都有实测日志，插桩已全部移除）：

| 观察 | 结论 |
|---|---|
| `guestRead:"shared-obj"`（guest 内部读 `globalThis.__shared.marker`） | guest 对象**本身是好的** |
| `hostReadCached` / `hostReadFresh` 都是 `"undefined"`，`Object.keys(proxy)` 为 `[]` | 宿主侧经 Proxy 读也是空的 |
| `proxy_get` 里打点：`[PROXY-GET-ENTER] key=marker` **触发 2 次** | Proxy trap **确实被调用** |
| 同一个 trap 内：用 guest 本地新建的 `"marker"` 字符串读 `guest_object->Get(...)` → `localMarkerUndef=0`（取到值）；用 `transfer_key(...)` 得到的键读 → `valueIsUndef=1`（undefined） | **`transferred_key` 是坏的**：`_transfer_string` 的 `(p_from_str, p_to_isolate)` 用法违反了它自己的约定 —— 绝大多数调用点传 `(from, local_handle, to)`，而它内部用 `v8::String::WriteUtf8(p_from_isolate, ...)` 写的是**传进来的那个句柄所属的 isolate**；`transfer_key` 传的是 guest 侧句柄却声明在 host isolate 上，写成了跨 isolate 的 `WriteUtf8`。 |
| `git diff HEAD` 显示 `proxy_get` / `_transfer_string` / `transfer_key` **都不在我的改动里** | **既有缺陷，非本次引入** |

- 未修：属于本任务范围之外（本任务只做错误上报），且修法牵涉 `_transfer_string` 的调用约定统一，
  风险与收益要单独评估。
- 因此该用例**只断言缓存复用契约**（identity），不把"Proxy 属性转发"写进断言；缺陷本身记在此处。

### 步骤 18：修掉 `_transfer_string` 的尾随 NUL（宿主经 Proxy 读 guest 属性恒为 undefined）（2026-10-08）

**根因（实测，非推断）**：`_transfer_string` 把 `String::WriteUtf8` 的返回值当作 `NewFromUtf8` 的
**显式长度**，而两腿的返回值语义不同 —— 真 V8 返回"写入字节数**含**结尾 NUL"，quickjs/jsc shim
返回不含 NUL 的字节数。于是 v8 腿把 `"marker"` 转移成 `"marker\0"`，guest 里按该键取属性永远 miss。

探针取证（在 guest isolate + guest context 下测量，不测量则读数本身就是跨 isolate 垃圾）：
```
修复前： [TKEY] isStr=1 guestLen=7 literalLen=6 sameAsLiteral=0     // 7 = "marker" + NUL
修复后： [TKEY] isStr=1 guestLen=6 literalLen=6 sameAsLiteral=1
```
**修法（更正，2026-10-08）**：先前我在**调用点**剥一个 NUL —— 那只是把问题按住了，而且是在
"shim 不符合 V8 契约"的前提下继续将就。正确做法是**统一 shim 到 V8 语义**：

- `impl/{quickjs,jsc,web}/jsb_*_primitive.cpp` 的 `String::WriteUtf8` 改为与真 V8 一致 ——
  写入**至多 `length` 字节**、**总是**补结尾 NUL、返回"已写入字节数**含** NUL"；
- 调用点（`_transfer_string`）改回"直接按 V8 契约使用"：用含 NUL 的返回值当
  `NewFromUtf8` 的长度（等价于 `strlen(buffer)`），不再有任何按腿的补丁。

仓库里 `WriteUtf8` 只有这一个调用点，因此两边对齐后语义自洽。
**有牙（在 shim 那条腿 = qjs 上验证）**：把 quickjs 的返回值改回"不含 NUL"（旧语义）后，
cache-reuse 用例直接 **CRASH**（`test case CRASHED: Unhandled SEH exception caught`，
`len - 1` 与旧语义错配）——比断言失败更能说明耦合有多紧。

**回归哨兵**：`test_jsb_shadow_realm.h` 的 cache-reuse 用例新增
`hostRead: String(realm.evaluate(\`globalThis.__shared\`).marker)` 断言（宿主经 Proxy 读 guest 字段）。
**有牙**：把两处剥 NUL 去掉后，该断言**确实失败**
（`test_jsb_shadow_realm.h(475): ERROR: text.contains("\"hostRead\":\"shared-obj\"") values: false`）。

**影响面更正**：先前我把这条记为"与本任务无关的既有缺陷"。它确实早于本任务存在，但**不是无关** ——
它是"跨环境值传递"的正确性问题，任何依赖宿主侧读 guest 属性的用户代码都会静默拿到 `undefined`。

### 缺口明确记录：`try_get_cache` 复用路径没有针对性测试（2026-10-08）
本轮修掉了一个真语义错 —— 注册表存的是 `CrossWrapper *`，但 `try_get_cache` 写成返回
`get_obj(p_host_isolate)`（**guest 对象**），而复用语义要求返回**宿主侧包装器**（`FunctionCrossWrapper`
的函数包装 / `ObjectCrossWrapper` 的 `v8::Proxy`）。已加 `host_obj_`（宿主侧弱句柄）修正，`add_cache`
的调用点补传宿主侧对象。

**但这个修正目前没有任何直接断言**：`try_get_cache` 的命中路径（同一 guest 对象在同一个 host isolate
里二次包装）只被"全场景 smoke 不新增失败"间接覆盖 —— 那种覆盖不能区分"返回了正确的宿主包装器"
与"返回了 guest 对象"。已列入本轮待补（见步骤 17）。

### 仍未解决的（不变，前置问题）：guest isolate 死后释放包装器
包装器内部持有 **guest isolate** 的 v8 引用（`v8::Global<v8::Object> value_`，`ObjectCrossWrapper`
的宿主对象还是 `v8::Proxy`）；guest isolate 一旦销毁，再析构该包装器就会在
`Global::Reset → GlobalHandles::NodeSpace::Release` 处 `V8_Fatal`/AV（步骤 15 实测栈）。
当前的规避点只有一个：`Realm::finish()` 在 `env_->dispose()` 之后、`env_.reset()` **之前**调
`purge_cache_isolate(isolate)`，趁 guest isolate 仍存活把每个包装器的 `value_.Reset()` 清掉并把
`isolate_` 置 null；此后 finalizer 里的 `remove_cache` 因 `get_isolate()` 为 null 直接早退，
`memdelete` 也只析构空的 `Global`（`Reset()` 对空句柄是 no-op），因此不再触碰已销毁的 isolate。
`JSB_ISOLATE_SCOPE(p_isolate)` 用来压住
`v8::Isolate::Deinitialize() Deinitializing the isolate that is entered by a thread`。
要真正在 guest 存活期内主动回收包装器（而不是等到 purge 兜底），得先解决 terminate 路径上
guest isolate 仍被线程 entered 的问题。

## 步骤 19：用户重构——宿主信息路径统一（2026-10-09）

用户重新调整了实现，我复核后提交为 `4a560e5`（独立提交，只动 6 个文件）。

### 统一前后的差异
| 方面 | 之前 | 之后 |
|---|---|---|
| 宿主信息写入 | worker 走 `CreateParams.master_token/master_handle`；realm 走 `set_error_forward_target()`（宿主绑定 handle 之后补登记） | 两边都走 **`Environment::set_master_env_info(master_token, handle_in_master_env)`**，并在其中 `jsb_checkf` "只能设置一次" |
| realm 是否转发 | 虚函数 `forwards_errors_to_host()`（只有 Transferable 覆写为 true） | `if constexpr (can_forward_error = is_same_v<ShadowRealmType, TransferableShadowRealmImpl>)` —— 编译期即可判定 |
| 钩子安装 | `Environment::_ensure_node_uncaught_hook()` + 自记 `node_uncaught_hook_installed_` | 并入 `set_master_env_info()`：登记宿主信息时顺带装钩子 |
| 钩子回调 | `NodeRuntime` 硬编码指向 `_jsb_uncaught_exception`（C++ static 里再取 `Environment::wrap`） | `NodeRuntime::install_uncaught_exception_callback(v8::FunctionCallback)` 收回调；`Environment::stash_uncaught_exception` 改为 **static**，直接作为回调传入 |
| 命名 | `master_handle_` / `master_token_` | `handle_in_master_env_` / `master_token_` |

**效果**：`NodeRuntime` 回归"纯 v8/node 助手"，不再知道错误上报的存在；worker 与 realm 的宿主信息只有一条路径；realm 的转发资格由类型推导，不再需要虚函数。

### 复核中顺手清掉的两处重构残留（我改的，随同一提交入档）
1. `jsb_node_runtime.cpp` ctor 里留着一个**空的花括号块** + 指向已删除符号 `Environment::_ensure_node_uncaught_hook()` 的注释 → 改成说明"钩子在 `set_master_env_info()` 里装"。
2. `jsb_environment.h` 的 `forward_error_to_master` 文档仍写旧成员名 `master_handle_` → 改为 `handle_in_master_env_`。

### 复核中被确认**不是**问题的两点（避免误判）
- `call().ToLocalChecked()`：仓库约定允许（仅 `impl::` 命名空间禁用 `MaybeLocal::FromMaybe`），如 `jsb_shadow_realm.cpp:587` 先例。
- `#if JSB_WITH_NODE` 内的成员在非 node 腿不声明：`set_master_env_info` 里那行也被 `#if JSB_WITH_NODE` 包着，一致。

### 本轮门禁（按用户要求只跑 node）
```
node build rc=0 → doctest SUCCESS → 项目 smoke 连续 30 次 全 COMPLETED（无 async-stack、无卡住）
逐提交（7 个）单独 node 构建：全部 build=0
error-reporting 用例：6 个 start 全部跑到（worker + shadow 双后端）
```

## 步骤 20：宿主句柄改由构造函数持有（用户改动 + 我修的时序缺陷）2026-10-09

### 改动
`CrossWrapper` 的宿主侧句柄从"在 `add_cache` 里补登"改为"**构造函数**就持有"：
`CrossWrapper(guest_isolate, source, host_isolate, host_value)`，`FunctionCrossWrapper` /
`ObjectCrossWrapper` 的 ctor 同步加参；`add_cache` 因此**不再需要** `p_host_value` 参数。

### 复核中发现的时序缺陷（已修，我的部分）
`ObjectCrossWrapper::create` 里，构造函数收到的是 **proxy 的 target**（`wrapper`），而
`create` **返回给宿主**的是 **Proxy**。这不是等价替换：

- `wrap_cross_env_value` 判断"这个宿主侧对象是不是已有的跨环境包装器"用的是
  `obj->HasOwnProperty(FlagSymbol)`；
- 对 `ObjectCrossWrapper`，该 symbol 由 **Proxy 的 `has`/`get` trap** 应答
  （`proxy_has` 对 symbol 直接返回 true，**不查 target**）；
- 于是把 target 当成宿主侧对象存进去后，缓存复用**永远查不中** → 同一个 guest 对象
  每次导出都新建一个 Proxy。

**实测**：改完当下 doctest 挂在回归用例的 `hostRead` 断言上
（`test_jsb_shadow_realm.h(475): text.contains("hostRead":"shared-obj") values: false`）——
"宿主经 Proxy 读 guest 字段"拿不到值，正是"没命中缓存、包装器不是同一个"的表现。

**修法**：`ObjectCrossWrapper::create` 里**先建 Proxy，再用它构造包装器**；finalizer 仍绑在
target 上（Proxy 持有 target，两者同生共死）。这样构造函数拿到的就是"要交给宿主的那个对象"，
`add_cache` 也不必再传宿主对象。

**验证**：doctest **91/91 SUCCESS**；30 次全场景 smoke + 15 次 cross-environment 单场景
全绿；注册表两条断言（"duplicate wrapper" / "registry entry belongs to another wrapper"）未触发。

## §21 转发接口重构（本轮）

### 现状（证据）
| 路径 | 现状 | 问题 |
|---|---|---|
| worker `onmessage` 抛错 | `jsb_worker.cpp:95` 的 static `_post_error_to_master` | 与 `forward_error_to_master` 重复 |
| realm `onmessage` 抛错 | `jsb_shadow_realm.cpp:1841` 的 `_post_error_to_host` | 又一份重复；且同步语义被做成异步 |
| 定时器抛错 | `jsb_timer_action.cpp:73-80`：先 `capture` 才 `forward_error_to_master` | 顺序反了（点1） |
| 入口脚本 load 失败 | worker 手发空 payload；realm 什么都不发；`load` 只 `JSB_LOG(Warning)` + `return ERR_COMPILATION_FAILED`（TODO 在 `:1619`） | 各走各的（点5） |
| `importValueSync` | `jsb_shadow_realm.cpp:1512` 只 `jsb_throw(isolate, err_msg)`（字符串） | 应抛重建的 Error（点4e） |

### 关键事实
- worker / transferable realm 环境**都已**调 `set_master_env_info`（`jsb_worker.cpp:406`、`jsb_shadow_realm.cpp:1053`），
  所以那两份 helper 是纯粹的重复。
- realm 跑在**调用方线程**（`params.thread_id = ThreadEx::get_caller_id()`，`jsb_shadow_realm.cpp:1078`）；
  realm 的普通 `postMessage`→host 走的是**同步** `handle_message`（`:1890`）。
- 同步投递有源码级警告（`jsb_shadow_realm.cpp:1855`）：同步 `handle_message` 会把宿主 `onerror`
  插进 realm 的 `onmessage` 帧里，用户在回调里 `terminate()` 会在帧内销毁自己的 isolate。
  现有跨环境测试**正是**在 `onerror` 里调 `peer.terminate()`（`test-cross-environment.ts:447`）。
- `Environment::dispose()` 只是置 `EF_PreDispose` + 拆 context，**析构（free isolate）才 fatal**
  （`jsb_environment.cpp:461`）。
- `ExecutionDeferredScope` 只 defer"类 post-bind"，**不阻止 JS 重入**（`jsb_environment.h:333`）。

### 决策
1. 统一入口放 `Environment` 上：`forward_error_to_master_sync / _async`，共享一个
   `_forward_error_to_master(record, mode)`（点2）。
2. `Environment::load` 增 `ErrorRecord *r_error` 出参：**capture 仍由调用方在 `has_caught()` 后做**
   （不做跨腿定制），`load` 只负责把"失败/成功"和 TryCatch 交给调用方（点5 的可实现内核）。
3. 点3 用 **`ShadowRealmImpl::ins_refcount_`**：帧内（`_enter_frame`/`_exit_frame`）`terminate()`
   只标记、不就地销毁；帧退出时若已在帧内 terminate 过就立即销毁。理由：不借助 V8 不支持的
   "同步抛异常再让用户 terminate"的时序，也不依赖 `ExecutionDeferredScope`。

## §22 转发接口重构（本轮·实做完成）

### 改了什么
- `Environment`：`forward_error_to_master` 拆成**异常值**与**记录**两个重载（`jsb_environment.{h,cpp}`）。
  值重载内部才 `has_master_env_info()`（点1：无接收者连 capture 都不做）+ `capture` + `Context::Scope`，
  再委托记录重载按 `ErrorForwardMode::{Sync,Async}` 投递（Sync=`handle_message`，Async=`post_message`）。
  > 用户要求：三个调用点不得各自判接收者/ capture —— 现在它们只把 `try_catch.get_exception_value()` 交出来。
- 三个调用点全部改走统一入口，删掉重复 helper：
  - 删 `jsb_worker.cpp` 的 static `_post_error_to_master`；定时器、worker、realm 一律一行转发。
  - 删 `jsb_shadow_realm.cpp` 的 `_post_error_to_host`（realm 的 `onmessage` 改**同步档**）。
- 日志仍在调用点：quickjs 的异常槽靠 `get_message()` 清空，不消费会让下一次 `has_caught()` 断言。
- 点3：`ShadowRealmImpl` 加 `ins_refcount_` / `terminated_in_frame_` + `FrameScope`；`_terminate` 帧内只标记，
  退帧时 `_destroy(id)` 补销毁；`_on_message` 与 `evaluate` 进帧时挂 `FrameScope`。
- 点5：`Environment::load(name, r_module, r_error)` 增 `ErrorRecord*` 出参（在异常槽还热时 capture）；
  worker 入口脚本加载失败改为转发**脚本真实 Error**（记录拿不到才退回空 payload）。
- 点4(e)：`_importValue` 改为回传 `ErrorRecord`；`importValueSync` 抛**重建的 Error**（不再是字符串）；
  `importValue` 保持 reject，但拒绝原因同形态（重建的 Error）。
- 更新 `project/tests/cross-environment/test-cross-environment.ts`：入口脚本加载失败断言从
  "合成文案 failed to load the worker script" 改为脚本真实错误 "peer load throw"（点5 的契约随之变化）。

### 实测
- node 构建 rc=0；doctest **91/91 passed**。
- CrossEnvironment 场景 `rc=0`，5 个 error-reporting 场景全 `:done`（onmessage-throw worker/shadow、
  timer-throw worker/shadow、startup-load-failure），**无 Fatal、无断言失败**。

### 过程中修掉的两个真问题（都是本轮引入后实测暴露的）
1. **`forward_error_to_master` 缺 `v8::Context::Scope`**：capture/serialize 要 `Object::New`/`Set`/`new_string`，
   只有 `Isolate::Scope` 时崩在 `record_to_js` 的 `payload->Set`（SIGSEGV）。定时器路径掩盖了它
   （`flush_uncaught_exception` 已建好 context scope），worker 入口加载失败这条新路径才暴露。
2. **同步转发 + 帧内 `terminate()` 的销毁 fatal**（用户点3 的症状）已复现：
   `FATAL ERROR: v8::Isolate::Deinitialize() Deinitializing the isolate that is entered by a thread`
   （栈：`_on_message` → 宿主 `onerror` → `terminate` → `_terminate` → `finish` → `env_->dispose()`）。
   `FrameScope` 延迟销毁修复后场景不再崩。

### 遗留 / 注意
- 转译产物 `project/.godot/godotjs_ext/` 是 `tsc`(outDir) 产物，改 `.ts` 后需 `cd project && tsc` 重新生成
  （headless 跑 editor 插件缺失，不会自动转译）。本轮误删过一次该目录，已用 `tsc` 重建。
- 规范 `.trellis/spec/godotjs-ext/cpp/cross-isolate-errors.md` 已更新：硬约束 2（同步/异步两档）、
  12（FrameScope）、13（统一入口判接收者+capture）、14（context scope）、`onerror` 契约表、检查单。


## §23 纠正：load 不该把记录交回调用方再转一手

- 问题：`Environment::load` 把异常采集进 `r_error` 交回，worker 再调 `env->forward_error_to_master(record)` —— 
  同一个 env、绕一圈，毫无意义（用户指出）。
- 改法：`load` 自己判——`r_error != nullptr` 才采集交回（唯一用例：realm 的 `importValue(Sync)` 要在
  **调用方 realm** 重建 Error 再抛/拒，属本地投递）；否则由 `load` 自己 `forward_error_to_master(exception)`。
  worker 侧因此回到 `env->load(path)`，并删掉转发调用与空 payload 兜底。
- 顺带推翻 spec 旧硬约束 6 的前提（"加载失败后 worker 环境已损坏、只能发空 payload"）：实测不成立，
  已改为记录真实错误；前提是转发入口自带 `v8::Context::Scope`（硬约束 14）。
- 实测：node 构建 rc=0、doctest 91/91、CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、无 Fatal。

## §24 回退：去掉 load 的错误出参，importValue 系列回到字符串

- 用户要求：`Environment::load` 的第 3 个出参去掉；`_importValue` 改回原来的**字符串**错误信息出参。
- 结果：`load(name, r_module)`——失败时**自己** `forward_error_to_master(exception)`（不再回传记录）；
  `_importValue(..., String &r_error_msg)` 回到 HEAD 形态；`importValue` reject 用 `make_error(err_msg)`、
  `importValueSync` 用 `jsb_throw(isolate, err_msg)`——两者都回到 HEAD。
- realm 的 `jsb::error_record::` 只剩 HEAD 原样的两处角色：`evaluate` 的跨 realm 搬运（1435/1451/1453/1459）、
  `importValue` reject 的 `make_error`（1514）。**未新增依赖**（`jsb_error_record.h` 本就 include，HEAD 也在用）。
- 实测澄清（推翻我先前的读法）：`jsb_throw` 在这条 node/v8 腿**抛的是 `Error` 对象**
  （`typeof=object isErr=true ctor=Error`），不是字符串；故已提交的 `importValueSync` 断言
  （`e instanceof Error` + `begins_with("Error:")`）在回退后仍成立，无需改测试。
- 实测：node 构建 rc=0；doctest **91/91**；CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、无 Fatal。

## §25 复查用户对 Environment::load 的改动 —— 发现并修掉 2 个问题（未提交，等确认）

用户改法：`if (has_master_env_info()) forward_error_to_master(exc, is_worker() ? Async : Sync);`

判为**有问题**，实测复现（transferable realm 探针）：
1. **双报**：非 worker 且登记了宿主的只有 **transferable realm**，而 realm 的 `load` 也会被 `importValue(Sync)` 触发。
   于是 `importValueSync` 失败**既**同步抛（正确）、**又**把宿主 `onerror` 触发（多余）。
   实测 `{"onerrorFired":true,"threw":true}`。
2. **崩溃**：同步转发把 `onerror` 插入 `importValueSync` 帧内，而该帧**没有** `ins_refcount_`（只有 `_on_message`/`evaluate` 有）。
   用户在 `onerror` 里 `terminate()` 就就地销毁正在跑的 isolate：
   `FATAL ERROR: v8::Isolate::Deinitialize() Deinitializing the isolate that is entered by a thread`
   （栈：`onerror` → `terminate` → `_terminate` → `_destroy` → `finish` → `env_.reset()`）。
   实测 rc=134。

**修法**：把守卫从 `has_master_env_info()` 改成 `is_worker()`（并去掉 `Sync` 档，worker 一律 Async）——
只有 worker 入口脚本加载失败才在这里转发。realm 的 `load` 失败由调用方处理：
`importValue(Sync)` 同步抛/拒、startup script 在构造函数抛 "Create ShadowRealm failed"。

**实测（修后）**：IVSYNC 探针 `{onerrorFired:false, threw:true}`、IVTERM 探针 `{...,terminated:false,threw:true}` 不崩；
删探针后 node 构建 rc=0、doctest **91/91**、CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、无 Fatal。

> 按「条件提交授权」：复查发现问题并已修，**停下等确认**，本轮不提交。

## §26 用户去掉 load 转发、worker 直接发字符串 —— 复查 + IsInUse 溯源

### 复查结论
**问题 A（已修）**：worker 侧 `new_string(vformat(...))` 在 `load()` 返回后建句柄，而 `load()` 的作用域已析构 →
`Fatal error in v8::HandleScope::CreateHandle(): Cannot create a handle without a HandleScope`（硬约束 7）。
修法：该 `forward_error_to_master(...)` 外面补 `JSB_ISOLATE_SCOPE` + `v8::HandleScope`。
**问题 B（改测试，用户定）**：转发字符串 → 宿主 `onerror` 收到字符串（原始值按 `error_record` 原样送达）。
`JsbThrownValue = Error | string | number | boolean | bigint | null | undefined` 本就含 string，故合法；
把 startup-load-failure 的断言从 `instanceof Error`（`c30270b`）改成 `typeof === 'string'` 且含
`failed to load the worker script`。

### IsInUse 溯源
- 现象：`Fatal error in , line 0 / Check failed: node->IsInUse().`，**总在 doctest 摘要之后**（进程退出阶段），
  前面恒定跟着 8 条 `Call ScriptInstance::callp() failed: env is null`（`jsb_script_instance.cpp:791`）。
- **非本轮引入**：基线日志时间戳均早于本轮源码改动（本轮 14:04；`fin2_node_dt` 01:32、`ur_dt` 05:09、`die` 10:34 都有）。
- **非本模块**：排查最早的错误上报提交（`d3d6ddb`/`9c633b2`）时它就已出现（当时的 `fin2_node_dt.err` 就带它）。
- 复现率 **1/5**（rc 恒为 0，测试恒 91/91）——竞态，与 doctest 用例无关。
- gdb 抓不到：gdb 下进程正常退出（`[Inferior 1 exited normally]`），确认是时序竞态。
- 定位结论：属**脚本语言/通知队列的 shutdown 阶段**（`env is null` 与 IsInUse 同阶段），与本轮跨环境错误上报无关。
  建议单开任务追（给 `.agent_tmp/*_dt.err` 这种既存噪声加一次基线扫描）。

### 实测
node 构建 rc=0；doctest **91/91**；CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、无 Fatal；
worker 已无 `error_record` 引用（无死代码）。

## §27 提交 + FrameScope 全覆盖 + error_record 彻底移出 realm

### 提交
`3980975 refactor(jsb): route cross-isolate errors through one environment entry point`（10 文件）。
`third/quickjs-ng` 是 submodule（构建产物导致脏），不入库。
新建任务 `10-09-isinuse-exit-fatal`（含复现步骤、已排除项）并随该提交入库。

### FrameScope 全覆盖（用户点2）
`evaluate`（guest 块）/ `importValue` / `importValueSync` 各挂 `FrameScope`——用户可在 importValue 抛同步异常后
立刻 `terminate()`，不挂就会在帧内即时销毁 isolate（硬约束 12 的 fatal）。
约束：`FrameScope` 析构即可能销毁 realm，**析构点后不得再用 `realm`**——`evaluate` 把它收进 guest 执行块
（块比函数体短），`importValue(Sync)` 收进 `_importValue` 调用那一小段。

### error_record 彻底移出 realm（用户点3）
`Environment` 新增：`class CrossRealmError`（move-only，内部 `uint8_t*`/`size`，析构 `impl::Helper::free`）、
`capture_cross_realm_error(exception)`、`rebuild_cross_realm_error(carrier, fallback_msg)`；
记录版 `forward_error_to_master(record, mode)` 降为 **private**。
- `jsb_shadow_realm.cpp` 现在**零** `jsb::error_record` 引用，`#include "jsb_error_record.h"` 已删。
- `evaluate`：guest `capture_cross_realm_error` → host `rebuild_cross_realm_error`；
  `importValue` reject：`rebuild_cross_realm_error(CrossRealmError(), err_msg)`。

### 过程中修掉的一个真 bug（我引入的）
`rebuild_cross_realm_error` 起先自带 `HandleScope` → 返回的 `Local` 随函数返回悬垂，
实测崩在 `v8::internal::LookupIterator::GetRootForNonJSReceiver`（doctest 68/67 fail 1）。
去掉函数内 `HandleScope`（由调用方提供）后修复。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；CrossEnvironment 场景 rc=0、两后端 object-transfer 与
5/5 error-reporting 场景全 done、无 Fatal；`jsb_shadow_realm.cpp` 无 error_record 引用。

## §28 跨隔离区抛出封装进 jsb_cross_isolate（用户定案：甲）

用户定：`evaluate`/`importValue(Sync)` 的错误按 design 是**抛给调用方**（非转发），但功能要**封装在
`jsb_cross_isolate` 里**，shadowRealm 只留干净调用点——不要"捕获什么重建什么"都写在 realm 里。

- 新增（`jsb_cross_isolate_util.{h,cpp}`，命名空间 `jsb::cross_isolate`）：
  - `class CrossIsolateException`：move-only 载体（内部缓冲，析构 `impl::Helper::free`）+ 静态 `capture()`。
  - `rebuild_error(isolate, context, carrier, fallback_msg)`：在目标 realm 重建（记录优先，否则 make_error）。
  - `throw_error(isolate, context, message, error)` / `throw_cross_isolate_error(target, carrier, msg)`：
    按 JS 语义抛（`(function(e){throw e;})`，不用 `Isolate::ThrowException` 以免污染 TryCatch 槽）。
- 删除上一轮放进 `Environment` 的 `CrossRealmError` / `capture_cross_realm_error` / `rebuild_cross_realm_error`。
- 删除 realm 的本地 `_throw_value_in_realm` / `_throw_realm_error`（并入 cross_isolate）。
- realm 现在只剩干净调用点：
  - `evaluate`：`guest_record = CrossIsolateException::capture(guest...)`；失败时
    `throw_cross_isolate_error(host_isolate, host_context, guest_record, guest_error)`。
  - `importValue` reject：`rebuild_error(isolate, context, CrossIsolateException(), err_msg)`。
  - `jsb_shadow_realm.cpp` 零 `jsb::error_record`、零 include。
- 过程中两次编译修正：`capture` 改静态成员（friend 未生效）；`rebuild_error` 需 friend（读载体私有成员）。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、无 Fatal。

### 未完成（待定）
用户先前指出 **node 的 `stash_uncaught_exception` 只处理了异步、未处理同步**（`flush_uncaught_exception`
恒用 `Async`；而 transferable realm 与宿主机同线程，应可同步）。此处尚未改，等确认口径后再动。

## §29 cross_isolate 收成"直接传异常值"（用户复查：realm 仍在处理）

用户复查 §28：realm 里还留着 `CrossIsolateException guest_record` 声明 + `capture()` 调用 +
`guest_failed` 编排，不算干净——**直接把 exception value 传过去**，别在 shadow realm 里处理。

- `jsb_cross_isolate` 对外只留三个入口（调用方看不到记录/载体类型）：
  - `throw_cross_isolate_error(target_isolate, target_context, exception_value, fallback)`——在源作用域内调用，
    内部 `capture(源) → rebuild(目标) → throw(目标)`；
  - `make_error(isolate, context, message)`；
  - `throw_error(isolate, context, message, error)`。
- 载体 `CrossIsolateException`、`rebuild_error` 降为 `cross_isolate::internal`；
  `capture` 用 `Isolate::TryGetCurrent()`（各腿 shim 无 `GetCurrent`）。
- realm 现在只剩两句：`cross_isolate::throw_cross_isolate_error(host_isolate, host_context, exception, guest_error)`
  与 `cross_isolate::make_error(isolate, context, err_msg)`（reject）。删掉 `guest_record`/`guest_failed`。

### 实测
node 构建 rc=0（首次遇 `LNK1102 内存不足`，重跑通过）；doctest **91/91 SUCCESS**；
CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、0 Fatal/FAILED。

## §30 error_record 收归 jsb_cross_isolate（Environment 不再直接使用）

用户要求：`error_record` 的功能封装进 `jsb_cross_isolate`，**Environment 也不直接使用**。

- `jsb_cross_isolate` 对外扩为 4 个纯函数 + 1 个常量访问器（都不暴露记录类型）：
  - `serialize_exception(isolate, context, exception) -> {uint8_t*, size_t}`（采集+序列化，所有权交调用方）
  - `rebuild_error_from_bytes(isolate, context, data, size)`（本地重建；空/无效记录返回空）
  - `make_error(isolate, context, message)`
  - `throw_error(isolate, context, message, error)`
  - `throw_cross_isolate_error(target_isolate, target_context, exception, fallback)`
  - `untransferred_symbol_key()`（给 `jsb_bridge_module_loader.cpp` 暴露 symbol 键用）
- `Environment`：删除私有的"记录版" `forward_error_to_master`（与值版合并，内部改调
  `cross_isolate::serialize_exception`）；接收侧 `_on_worker_message` 改调 `rebuild_error_from_bytes`
  与 `make_error`；删 `#include "jsb_error_record.h"` 与头文件里的 `ErrorRecord` 前向声明。
- `jsb_bridge_module_loader.cpp` 改调 `cross_isolate::untransferred_symbol_key()`。

**结果**：`grep -rln jsb_error_record.h src/` 现在只剩 **owner(`jsb_cross_isolate_util.cpp`) + 自身(`jsb_error_record.cpp`)**；
`jsb_environment.{h,cpp}` 与 `jsb_shadow_realm.cpp` 均零 `error_record` 引用。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、0 Fatal/FAILED。

### 仍未做
node `stash_uncaught_exception` 的"同步档"（用户此前点出，等口径）。

## §31 CrossEnvError 包装类（真类，注册进 globalThis）

用户定案：真类、`globalThis`、由 `jsb_cross_isolate` 注册；message 用第二种形式；`untransferred` 特例携带。

### 形状
- `class CrossEnvError extends Error`，由 `jsb_cross_isolate::ensure_cross_env_error()` 在**目标 realm** 里
  按需注册到 `globalThis`（C++ 侧 eval 一段 snippet，不碰 TS bundle/codegen）。
  取不到（用户删了）→ `rebuild_error_from_bytes` 回退返回源异常本体。
- wrapper：`message = "<realm>: <cause.message>"`、`cause` = 重建后的源异常、`sourceRealm` =
  `"worker" | "shadowRealm"`、`untransferred` = 纯字段（symbol 已删除）。
- `extends Error` ⇒ `instanceof Error` 仍成立。
- **原始值异常不包装**（`throw "boom"` 按原样送达；它没有字段/清单可带），这是保留的既有契约。
- 源异常自身的字段（name/message/stack/extra/内层 cause）都在 `cause` 上。

### 改动面
- `jsb_error_record.{h,cpp}`：`ErrorRecord` 加 `source_realm`；payload 加 `sourceRealm` 键；
  `capture_error_fields` 特例：own `untransferred` 是字符串数组时**并进记录**（跨多层时不丢）；
  `rebuild` 去掉 `Symbol.for(kSymbolKey)`；删除 `kSymbolKey`。
- `jsb_cross_isolate_util.{h,cpp}`：`serialize_exception` 记录来源环境类型；`rebuild_error_from_bytes`
  改为构建 wrapper；新增 `ensure_cross_env_error`；删除 `untransferred_symbol_key`。
- `jsb_bridge_module_loader.cpp`：删除 `untransferred` symbol 暴露。
- `scripts/typings/godot.minimal.d.ts`：删 symbol，加全局 `declare class CrossEnvError`，
  `JsbThrownValue` 加入 `CrossEnvError`。
- 测试：`test_jsb_shadow_realm.h`（record 两个用例改读 `e.cause` / `e.untransferred`）、
  `test-cross-environment.ts`（`error instanceof CrossEnvError` + `error.untransferred`）。

### 过程中修掉的两个问题
1. `ensure_cross_env_error` 的 IIFE 少 `return`，`script->Run` 返回 `undefined` → 不是函数 → 一直回退成普通 Error
   （doctest 一度 89/91）。补 `return globalThis.CrossEnvError;` 后修复。
2. `tsc` 报 `Cannot find name 'CrossEnvError'`：`project/typings/` 是**未跟踪的生成产物**（由内嵌预设安装，
   headless 不跑安装步骤）；同步 `scripts/typings/*.d.ts` 过去后编译通过。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；CrossEnvironment 场景 rc=0、5/5 error-reporting 场景 done、0 Fatal/FAILED。

### 仍未做
- node `stash_uncaught_exception` 的"同步档"（等口径）。
- 未提交（HEAD 仍是 `4a29d1e`；用户只授权提交那一次）。

## §32 用户纠错：`ensure_cross_env_error` 从 `globalThis` 反取构造函数 = 错

用户指出（对的）：`globalThis.CrossEnvError` 是**暴露给用户**用的（`instanceof` / 使用），
C++ **不能**从那里反取构造函数——用户改了/删了就会坏，而且那等于让用户数据当权威。
另外用户定案：跨隔离区**一律**用这个类型包起来（原始值也一样），`JsbThrownValue` 该删。

### 改法
1. `Environment` 新增 `v8::Global<v8::Function> cross_env_error_ctor_`（+ 访问器；dispose 里 `Reset`）。
2. `ensure_cross_env_error(Environment*)`：**类定义写在 C++ 里**，构造出来的构造函数 **C++ 自己持有**
   （缓存进 Environment）；同时 `globalThis.CrossEnvError = ...` 只作**用户暴露**。不再读 `globalThis`。
3. 新增 `make_cross_env_error(env, message, cause, sourceRealm, untransferred)`，`rebuild_error_from_bytes`
   与 worker 空 payload 分支都用它 —— **一律包装**。
4. 原始值异常：去掉 `if (record.is_primitive) return cause;`，本体放进 `cause`，`message = "<realm>: " + String(cause)`。
5. `throw_cross_isolate_error` 的退化分支（序列化都失败）也包成 `CrossEnvError`（建不出类才退回普通 `Error`）。
6. 类型：删 `JsbThrownValue`；`JSWorker.onerror` / `TransferableJSShadowRealm.onerror` 参数改 `CrossEnvError`。
   `scripts/typings/*` 与 `project/typings/*` 同步。

### 测试更新
- doctest：`ShadowRealm: a primitive thrown value is wrapped with the primitive in cause`
  （断言 `isCrossEnv:true` / `sourceRealm:"shadowRealm"` / `message:"shadowRealm: primitive-boom"` / `causeType:"string"`）。
- 场景：`error-reporting:startup-load-failure` 断言改为 `error instanceof CrossEnvError` + `error.cause` 含文案。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；`tsc --noEmit` rc=0；CrossEnvironment 场景 rc=0、0 FAIL、
`startup-load-failure:worker:done`。
NOTE 场景首次失败：改了 `.ts` 但没 emit（`project/.godot/godotjs_ext/` 才是运行产物），跑 `npx tsc` 后通过。

### 仍未做
- node `stash_uncaught_exception` 的"同步档"（等口径）。
- 未提交（HEAD 仍是 `4a29d1e`）。

## §33 删掉 `CrossEnvError.sourceRealm`（用户选 A）

理由（用户问"必要性"，我答：路由上零必要性）：接收方与来源 **1:1** ——`set_master_env_info(master_token,
handle_in_master_env)` 给每个子环境只登记一个接收对象，`forward_error_to_master` 只发给它；所以
`worker.onerror` / `realm.onerror` 由"哪个回调在跑"就确定来源，`sourceRealm` 是把同一信息存了第二遍。

保留 `message` 前缀（`"worker: <cause>"`），所以**源环境标记仍要过 payload**：
- 删：wrapper 上的 `sourceRealm` 属性、`make_cross_env_error` 的 `p_source_realm` 参数、两处 typings 的
  `readonly sourceRealm`、两处 doctest 探针/断言里的 `sourceRealm`。
- 留：`ErrorRecord::source_realm`（**内部字段**，只用来拼 message 前缀）；payload 键改名
  `kKeySourceRealm` → `kKeyRealm`，避免和已删的用户属性同名混淆。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**；`npx tsc`（emit）无错；CrossEnvironment 场景 rc=0、41 done、0 FAIL。

### 仍未做
- node `stash_uncaught_exception` 的"同步档"。
- 未提交（HEAD 仍是 `4a29d1e`）。

## §34 用户纠错：`CrossEnvError` 必须是**原生类**（ClassBuilder），不许在内嵌 JS 里定义

用户指出（对的）：仓库有 `impl::ClassBuilder`，正式代码里不该嵌 JS 源码；内嵌 JS 只该出现在测试里。

### 改法
- `Environment::cross_env_error_ctor_`（`v8::Global<v8::Function>`）→ **`impl::Class cross_env_error_class_`**
  （持类模板，避免只留函数、模板 Global 被释放的隐患）；访问器改 `get_cross_env_error_class()`。
- `ensure_cross_env_error(Environment*)`：`impl::ClassBuilder::New<0>(isolate, "CrossEnvError", &_cross_env_error_constructor, 0)`
  → `Build()`；构造函数是 **C++ 回调**，只落 `name = "CrossEnvError"` 与 `message`（`class_payload` 传 0：
  不注册进环境类表，没有实例绑定/析构需求）。
- `extends Error`：`CrossEnvError.prototype` 的 proto 接到目标 realm 的 `Error.prototype`
  （`Object::SetPrototype`，四条腿都实现了；等价于 JS `class X extends Error {}` 对原型链做的事）。
- `stack`：原生构造函数不走 `Error` 的构造语义，包装错误自带不了栈 → `make_cross_env_error` 把
  **源异常的 `stack`** 复制到包装错误上（指向用户代码，比在 runtime 内部现抓一条更有用，且零额外分配）。
- 保留：类仍挂到 `globalThis.CrossEnvError`（**只给用户**）；C++ 用自己的引用，不从 globalThis 反取。

### 实测
node 构建 rc=0；doctest **91/91 SUCCESS**（`instanceof Error` / `name === "CrossEnvError"` / `message` 前缀 /
`hasStack` / `untransferred` 全过）；CrossEnvironment 场景 rc=0、41 done、0 FAIL。

### 遗留（本轮未动，需用户定）
`jsb_cross_isolate_util.cpp` 里的 `throw_value_in_context()` 仍用内嵌 JS（`"(function (e) { throw e; })"`）。
原因：各腿 shim 的 `Isolate::ThrowException(value)` 在 quickjs（`set_stack_steal(StackPos::Exception, ...)`
+ `jsb_checkf(IsNotErrorThrown(...))`）会污染 `TryCatch` 槽并触发断言/错误风暴；jsc 的 `_ThrowError` 也写同一个槽。
正解是给四条腿各加一个干净的 `Helper::throw_value`（v8 `ThrowException` / quickjs `JS_Throw` /
jsc `_ThrowError` / web 对应接口），但**只有 v8 能本轮实测**，其余三腿改 throw 路径我无法验证 → 等用户决定。

## §35 A 落地：原生类 + 记录直传 + `cause` 懒物化（用户选 A）

### 用户纠错（都对）
1. 「CrossEnvError 没有内部字段你怎么拿它调用 C++ 功能」——对：要做 C++ 侧功能就得有 per-instance 状态。
2. 「为什么 bind_js_owned_pointer 会崩」——**实测会崩**（见下），且我先前的"不会崩"分析是错的。
3. 「抄 ObjectCrossWrapper 的作业」——主体照抄（`add_native_class(Custom)` + `ClassBuilder::New<IF_ObjectFieldCount>`
   + `bind_js_owned_pointer` + `class_info->finalizer`），但**有一处它没踩、我们必踩**：`ObjectCrossWrapper` 是
   **Proxy**（代理自身 0 个内部字段），所以逃过了 `BridgeHelper::stringify` 的 `is_object` 分支。

### 实测出来的崩溃（先前的落雷猜测被证实）
新增 doctest「logging a CrossEnvError is safe」，里面一句 `console.log(e)`：
```
ERROR: FATAL: Condition "!(class_info->type == NativeClassType::GodotObject)" is true.
   at: jsb::BridgeHelper::stringify (src\runtime\bridge\jsb_bridge_helper.cpp:52)
CrashHandlerException: Program crashed    ← SEH
```
即：**2 个内部字段 → `TypeConvert::is_object()` 为真 → `find_object_class()` 命中 → `jsb_check(type == GodotObject)` 崩。**
`console.log(err)` 是用户最基本的操作，所以这条必须修。

### 改动
1. `Message` 加 **C++ 侧信道** `set_error_record/has_error_record/get_error_record`（`ErrorRecord` 直接随消息搬）。
   删掉 `error_record::serialize/deserialize/record_to_js/record_from_js` + `kErrorPayloadKey` + `kKeyRealm`（全成死码）。
2. `forward_error_to_master`：`capture_exception()` 后 `message.set_error_record(...)`，不再走 `ValueSerializer`
   （原先 transfers 恒空，序列化器在本路径上只"产字节"）。
3. `_on_worker_message`（改收 `Message&`）：直接从消息取记录物化；顺带**去掉了 `invoke_worker_callback_from_message`
   里那次"反序列化成 JS 对象随即被覆盖"的浪费**（并删掉多余的 `p_rebuild_error` 参数）。
4. `cross_isolate::register_cross_env_error(env)` 在 `Environment::init()` 里注册原生类（类 id 存
   `Environment::cross_env_error_class_id_`）；构造函数 + `cause` 原型访问器都是 C++；`globalThis.CrossEnvError`
   仍只作**用户暴露**，C++ 走类表拿自己的那份。
5. **部分懒**：`name/message/stack/untransferred` 直接落；`cause` 是原型访问器，首访才 `rebuild` 并
   `DefineOwnProperty` 缓存（**不能用 `Set`**：无 setter 的访问器会让 `[[Set]]` 静默失败，实测
   `hasOwnProperty("cause")` 仍为 false）。
6. `stack` 采集的唯一例外：v8 里 `stack` 是实例上的 **accessor**，只读描述符采不到 → 兜底在 `TryCatch` 里真读一次。
   （这同时修了 `record.stack` 在 v8 上**一直为空**的老问题；现在带的是**源栈**，`console.log` 打出来就是源帧。）

### 顺带修的两处既有 bug（都在 `CrossEnvError` 的必经之路上）
- `BridgeHelper::stringify`（`jsb_bridge_helper.cpp:52`）：非 `GodotObject` 类不再断言，落到 JS `ToString`。
  （`Worker` / `Shadow` 实例被 `console.log` 本来也会崩。）
- `js_to_gd_var(Variant::OBJECT)`（`jsb_type_convert.cpp`）：加 `IF_ClassType == GodotObject` 闸（与 `js_to_gd_obj` 一致）。
  不加的话 `bind_js_owned_pointer` 登记的指针会让 `verify_object()` 为真 → 把内部记录指针当 `Object*` 交出去 = **野指针**。

### 实测
node 构建 rc=0；doctest **92/92、1196 断言 SUCCESS**；`console.log(err)` 打的是**源栈**且不再崩；CrossEnvironment
场景 **rc=0、41 done、0 fail**。

### 仍未做 / 未验
- `throw_value_in_context()` 的内嵌 JS 仍在（各腿 shim `Isolate::ThrowException(value)` 有毛病，正解是四条腿各加
  `Helper::throw_value`；本轮只能实测 v8）。
- 只有 v8 实测；`register_cross_env_error` / finalizer / `Property` 访问器在 jsc/quickjs/web 上未验。
- 未提交（HEAD 仍是 `4a29d1e`）。

## §36 用户纠错：修错地方了 —— 不该在 `stringify` 里补分支，该把"判定"改成类型感知

用户指出（对的）：`CrossEnvError` 不是 Godot 类型，凭什么走给 Godot 类型准备的 `BridgeHelper::stringify` 分支？
而且我既然在里面 `return impl::Helper::to_string(...)`，那函数**末尾本来就有一句**同样的 return —— 多补一个分支是废话。
根因是**判定太宽**：`TypeConvert::is_object(obj)` 只看 `InternalFieldCount()`，于是 2 个内部字段的
`CrossEnvError` 被当成 Godot 绑定对象。同一个文件里就有类型感知的重载 `is_object(obj, NativeClassType::Type)`。

### 改法（内部字段保留：用户明确说"就是用内部字段"）
- `BridgeHelper::stringify`：`is_object(self)` → **`is_object(self, NativeClassType::GodotObject)`**；
  删掉我上一轮加的重复 `return impl::Helper::to_string(...)`。非 GodotObject 自然落到末尾的 return。
- `js_to_gd_var(Variant::OBJECT)`：`is_object(self)` → **`is_object(self, NativeClassType::GodotObject)`**，
  不满足就 `break`（与"传普通 JS 对象给 Object 参数"同等对待：转换失败），**不**返回 `null`。

### 实测
node 构建 rc=0；doctest **92/92、1196 断言 SUCCESS**；`console.log(err)` 输出源栈 + name/message 且不崩；
CrossEnvironment 场景 rc=0、41 done、0 fail。

## §37 用户自查后改 `BridgeHelper::stringify` → 实测不崩 → 单文件提交

用户自己把 `stringify` 的判定改成类型感知（`is_object(self)` → `is_object(self, NativeClassType::GodotObject)`），
要求"不崩就单独提交这个单文件修复"。

### 实测（关键：把当初崩的三个场景全测了）
| 场景 | 修复前 | 修复后 |
|---|---|---|
| `console.log(new JSShadowRealm())` | FATAL `!(class_info->type == GodotObject)` → SEH 崩 | `JSShadowRealm {}` ✔ |
| `console.log(new JSWorker(...))` | 同上 → 崩 | `JSWorker {}` ✔ |
| `console.log(new JSShadowRealm(), new JSWorker(...))` 数组 | — | `[ JSShadowRealm {}, JSWorker {} ]` ✔ |
| `console.log(CrossEnvError)` | 同上 → 崩 | 源栈 + `{ name:'CrossEnvError', message:'shadowRealm: log-me' }` ✔ |

doctest **92/92、1196 断言 SUCCESS**；CrossEnvironment 场景 **rc=0、41 done、0 fail**；探针已全删（`[PROBE]` 残留 0）。

### 提交
`5e48552 fix(jsb): only route Godot objects into the bound-object path in stringify`
—— **单文件、1 行**（`src/runtime/bridge/jsb_bridge_helper.cpp`），未 push。其余改动照旧留在工作区。

### 仍未决
`jsb_type_convert.cpp` 的 `Variant::OBJECT` 分支仍是 HEAD 的宽松判定（`is_object(self)` + `verify_object()`）：
`CrossEnvError` 的指针在 `object_db_` 里 → 若用户把它传给一个 `Object` 类型的 Godot 参数，会拿到
`(Object*)record_ptr`（野指针）。**仅静态推演，未复现**；对策三选一（改那一行 / 给 `CrossEnvError` 用 3 个内部字段 /
先搁置）——等用户定。另：`throw_value_in_context()` 的内嵌 JS 仍在；非 v8 腿未验。

## §38 `throw_value_in_context()` 去掉内嵌 JS → 各腿 `Helper::throw_value`

### 改动
- 删掉 bridge 里的 `internal::throw_value_in_context`（那段 `"(function (e) { throw e; })"`）。
  `cross_isolate::throw_error()` 改调 `impl::Helper::throw_value(isolate, context, value)`。
  **bridge 里已无 `Script::Compile`**（内嵌 JS 清零）。
- 各腿实现（依据都写在注释里）：
  - **v8**：`isolate->ThrowException(value)` —— 原生 pending exception，不碰任何 JSB 槽。
  - **jsc**：`isolate->ThrowException(value)`。**这是等价替换**（硬证据：`jsb_jsc_function.cpp` 的
    `Function::Call` 失败路径就是 `isolate_->_ThrowError(error)`，即内嵌 JS 跑出的异常也落到同一个
    `_ThrowError`；而 `TryCatch::has_caught()`/`get_exception_value()` 读的就是 `StackPos::Exception`）。
  - **quickjs**：`JS_Throw(ctx, JS_DupValue(ctx, value))` —— 只设引擎 pending exception，**不写**
    `StackPos::Exception` 槽。不能借道 `Isolate::ThrowException`：它多一次 `set_stack_steal`，会让之后任何
    `TryCatch::try_catch()` 撞 `jsb_checkf("stack.exception is dirty")`（该断言在
    `jsb_quickjs_isolate.h:274`）。
  - **web**：`jsbi_*` 互操作没有"抛任意值"的原语（只有 `jsbi_ThrowError(message)`）。按用户要求**带上实际值的文本**：
    `to_string(isolate, value)` 拼进文案（另加 dev 断言让"被调用"暴露）。该路径在 web 上不可达
    （`JSB_SHADOW_REALM_ENABLED == 0`）。

### 实测（node / v8 腿）
node 构建 rc=0；doctest **92/92、1196 断言 SUCCESS**；CrossEnvironment 场景 **rc=0、41 done、0 fail**。
覆盖了 `evaluate` 同步抛（本改动的核心路径）、`importValueSync`、原始值包装、`console.log(err)`、
worker/realm 的 onerror 投递。

### ⚠️ 验证受阻：用户提交 `43078a7` 的新断言在 dev 下恒假
`jsb_type_convert.cpp:558`：`jsb_check(TypeConvert::is_object(r_jval, NativeClassType::GodotObject));`
在 `clazz.NewInstance(context)` **之后**、`bind_godot_object(...)` **之前** —— 而 `IF_ClassType` 正是
`bind_godot_object` → `bind_pointer` 才写进内部字段的；`NewInstance` 出来的新对象该字段为 0，
所以这个断言在 dev build 下**必然失败**：
- doctest：`test cases: 14 | 1 failed`，在 "RefCounted objects" 崩（`gd_obj_to_js`）；
- CrossEnvironment 场景：**rc=3、0 个场景**，启动即崩。
修法（二选一）：断言改成查 `class_info->type == NativeClassType::GodotObject`（在 `NewInstance` 之前就能查），
或把这行移到 `bind_godot_object` 之后。
本轮为验证自己的改动，**临时**把那行换回改动前的 `jsb_check(TypeConvert::is_object(r_jval));` 跑完测试后
**已逐字还原**（`git diff src/runtime/bridge/jsb_type_convert.cpp` 相对 HEAD 无差异），并重编。

## §39 按用户要求重做：Message 按指针持 payload + 记录并进 cross_isolate + 注释瘦身

### 改动
- `jsb_message.h`：`MessageRawData` 基类（非纯虚、`= default` 析构）+ `unique_ptr<MessageRawData> raw_data_`；
  **任意消息不再背 64B 记录**（只有错误路径才分配）。去掉 `jsb_error_record.h` 这个 include。
- **`jsb_error_record.{h,cpp}` 删除**，全部并进 `jsb_cross_isolate_util.cpp`：`ErrorRecord`（含 `capture`/`rebuild`/各 helper）
  与 `CrossEnvErrorImpl` 都在**匿名 namespace** 里；头文件只剩 6 个函数（`register_` / `capture_error` /
  `take_error` / `make_error` / `throw_error` / `throw_cross_isolate_error`）+ 原有的 `parse_transfer_list`。
  `ErrorRecord` 类型不再对外出现。
- `Environment` 侧不再需要记录层：`forward_error_to_master` 一句
  `Message message(Message::TYPE_ERROR, handle, cross_isolate::capture_error(this, p_exception));`；
  `_on_worker_message` 的 `TYPE_ERROR` 分支一句 `cross_isolate::take_error(this, p_message, kFallback)`。
- 注释瘦身：实现里只留必要的一行；把"调查过程"的长篇说明删掉（细节留在本 spec）。

### 过程中修掉的两个问题
1. **分配/释放不配对**（我合并时引入）：`capture()` 用 `std::make_unique`（CRT `new`）而 finalizer 用 `memdelete`
   → doctest 在 "an exception from evaluate" 直接静默死（rc=116）。统一成 `new`/`delete` 后恢复。
2. **`source_realm` 丢失**：我把它从 `capture()` 挪到了 `capture_error()`，同步路径（`throw_cross_isolate_error`
   直接调 `capture()`）就没前缀了 → doctest 两处 `message` 断言失败。挪回 `capture()` 后恢复。

### 实测（node / v8）
doctest **92/92、1196 断言 SUCCESS**；CrossEnvironment 场景 **rc=0、41 done、0 fail**。

### ⚠️ 你提交里 `jsb_type_convert.cpp:558` 的断言现在是 **SIGSEGV**（dev）
```cpp
r_jval = class_info.escape()->clazz.NewInstance(context);
jsb_check(TypeConvert::is_object(r_jval) && class_info->type == NativeClassType::GodotObject);   // ← class_info 已失效
```
上一行注释自己写着 "class_info ptr will be invalid after escape()"，所以这里 `class_info->type` 是解悬垂指针
→ 实测 doctest 在 `RefCounted objects`(test_jsb_any_runtime.h:538)、场景在启动时 SIGSEGV。修法：在 `escape()`
**之前**取 `const NativeClassType::Type class_type = class_info->type;`，或 `escape()` 后用 `get_native_class(class_id)` 重取。
（本轮为验证自己的改动，仍临时把该行换成旧形式跑完测试，随后已逐字还原：`git diff jsb_type_convert.cpp` 无差异。）
