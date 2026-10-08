# 技术设计：跨隔离区错误上报

## 1. 三种边界与它们的物理约束

| 边界 | 形态 | 能否"直接传对象" | 现有机制 |
|---|---|---|---|
| `JSShadowRealm`（`evaluate` / `importValueSync`） | **同线程、两个 isolate** | ❌ 跨 isolate 使用 `Local/Global` = UB | 同名线程跨 env 已有 `wrap_cross_env_value`（`jsb_shadow_realm.cpp:823`）：函数/对象 → `FunctionCrossWrapper` / `ObjectCrossWrapper`**活代理**（`:869`、`:880-882`），symbol → `SymbolCrossUtils`，原始值 → 序列化+反序列化 |
| `JSWorker` | 独立 isolate + 独立线程 | ❌ 另一线程的 isolate 在主线程不可访问 | 消息：`Message`（type + 序列化 buffer + `TransferData`），`jsb_message.h:44-63` |
| `TransferableJSShadowRealm` | 同 Worker（消息） | ❌ 同上 | 消息：`jsb_shadow_realm.cpp:1708-1717`、`:1746-1755` |

**结论**：同步边界理论上可以"活代理"保真；跨线程边界**只能拷贝**。

## 2. 为什么不能把异常对象直接抛过去（原理）

- `v8::Local/Global` 绑定 isolate。跨 isolate 使用不是"抛异常"，是未定义行为（崩）。
- 因此跨边界异常只有一条路：**先把异常降级成与 realm 无关的数据，再在目标 realm 重建**，然后按 API 形态抛出 / reject / 交给 `onerror`。
- 旁证：JS 规范对 ShadowRealm 的 WrappedFunction 规定"抛出的值是原始值就原样抛，是对象则改为在调用方 realm 抛 `TypeError`"——规范自己也不允许异常对象跨 realm；结构化克隆里 `Error` 的克隆内容就是 `{name, message, stack, cause}`。

## 3. 两个必须一起修的前置缺陷

1. **`ToLocalChecked()` 导致崩溃**：`jsb_shadow_realm.cpp:1251-1252`。异常时 `Script::Compile/Run` 返回空 `MaybeLocal`（jsc `jsb_jsc_object.cpp:418-422`、quickjs `jsb_quickjs_object.cpp:542-545`；真 v8 同），而 `impl::MaybeLocal::ToLocalChecked()` = `jsb_check(!IsEmpty())`（`impl/jsc/jsb_jsc_handle.h:118-121`、quickjs/web 同）→ dev 构建 `CRASH_COND`（`internal/jsb_macros.h:60`），release 下 `jsb_check` 被编掉 → 无效栈槽（UB）。改成 `ToLocal(&script)` / `ToLocal(&result)` 后，紧随的 `try_catch.has_caught()`（`:1254`）才真正可达。
   - 实测证据：`report.md` §30（临时探针 → SEH crash，栈顶 `jsb_shadow_realm.cpp:1252`，doctest 82→66 中断；删除探针后 82/82 SUCCESS）。
2. **异常状态没有被正确结束**：三个 shim 的 `impl::TryCatch::~TryCatch() = default`（`impl/jsc/jsb_jsc_catch.h:42`、quickjs/web 同）；`jsb_throw(guest_isolate, ...)`（`:1255`）会在 **guest** 留下 pending throw（v8 语义；jsc/quickjs 的异常是"每次调用显式传出"，行为不同，需逐腿确认）。错误分支要显式结束/清理，且**永远不要**把异常抛进"不是调用方"的 isolate。

## 4. 值传递方案对比（回应用户提议）

| 方案 | 保真度 | `instanceof Error` | 生命周期风险 | 适用边界 |
|---|---|---|---|---|
| **A. 活代理**（复用 `ObjectCrossWrapper`） | 最高（字段/方法都在，惰性读取） | ❌ 假（对象属于 guest realm） | 有：guest realm `terminate()`/析构后访问悬空；而 `CrossWrapper` 的"isolate 可能已 dispose"问题在本仓是已登记遗留（`.trellis/tasks/archive/2026-09/09-06-c1-dynamic-crash/prd.md:32` 的 P3） | 仅同步边界 |
| **B. 拷贝 + 白名单重建**（本任务采纳） | 核心字段全保真；额外字段 best-effort | ✅ 真 | 无 | 全部边界 |
| C. 只传字符串 | 低（无 stack、无字段） | ❌ | 无 | 现状，应淘汰 |

**采纳 B 的理由**：错误对象通常会被保存、汇总、写日志（跨 tick 长期存活），活代理的悬空风险落在最不该出错的地方；且 `if (e instanceof Error)` 是用户最常见的判断。A 保留给"返回值"这类短生命周期场景（现状不动）。

## 5. 目标设计

### 5.1 统一记录（realm 无关）

```
ErrorRecord {
    name: String        // 缺失时用 "Error"
    message: String
    stack: String       // 源侧文本；目标侧写到重建 Error 的 stack
    cause: ErrorRecord? // 递归（有界）
    extra: Dictionary   // best-effort 复制来的自定义字段
    untransferred: PackedStringArray  // 未能携带的字段路径清单
}
```

- 源侧采集：`impl::TryCatch::get_message(&msg, &stack)`（jsc 已有该 API：`impl/jsc/jsb_jsc_catch.h:47`），再补 `name`（能从异常对象取则取，取不到就解析/默认 `Error`）。
- 目标侧重建：在目标 realm `new Error(message)` → 覆盖 `name`、`stack`（源文本）→ 挂 `extra` 的字段 → 把 `untransferred` 挂为一个字段（字段名待定）。

### 5.2 各入口的落法

| 入口 | 落法 |
|---|---|
| `evaluate` / `importValueSync` | 在**调用方（host）isolate** 重建 Error 并 `jsb_throw(host_isolate, err)`；同时保留 Godot 侧日志 |
| `importValue` | 同一个重建 Error 用于 `resolver->Reject(...)`（替换现在的字符串 reject，`jsb_shadow_realm.cpp:1316`） |
| Worker（启动失败 / 回调抛错） | 源侧建记录 → `post_message(Message(TYPE_ERROR, handle, <序列化的记录>))`；host 侧现有 `onerror` 通路（`jsb_environment.cpp:761-767`）反序列化 → 重建 Error → 交给用户回调 |
| `TransferableJSShadowRealm` | 与 Worker **同一个 helper**（保证形态一致） |
| 定时器 | **worker 环境**：复用同一条 `TYPE_ERROR` 通路（`jsb_worker.cpp:365` 的 `p_env->update(delta)` 驱动 worker 的定时器）；**主环境**：维持现状（`jsb_timer_action.cpp:73-75` 已捕获并日志），要送脚本侧需新增全局钩子 → 另立任务。顺带改写 `jsb_environment.cpp:532-533` 的过时描述 |

### 5.3 字段复制规则（有界、best-effort）

- 允许类型：`undefined`/`null`/`boolean`/`number`/`string`/`bigint`（按各腿序列化器支持度确认）、数组、普通对象（递归）。
- 我们各腿序列化器已有的额外类型（可纳入白名单，需逐腿确认）：`Date`、`RegExp`、TypedArray/ArrayBuffer（quickjs：`impl/quickjs/jsb_quickjs_serializer.cpp:506/525/555-572`；jsc：`impl/jsc/jsb_jsc_serializer.cpp:84`）。
- 不带：函数、类实例、`Proxy`、`Promise`、`Map`/`Set`（支持度不一）、Godot 对象（错误路径不做 transfer）、symbol 键。
- **绝不触发 getter/setter**：用 `GetOwnPropertyDescriptor`，`has_get()/has_set()` 为真则按"不携带"处理。
- 边界：深度上限（初值 8）、节点上限（初值 10k）、总字节上限（初值 256 KB）、`visited` 环检测——超限即停止并记入 `untransferred`。
- 转换本身包在 TryCatch 内；转换失败**不得**覆盖原始 `name`/`message`/`stack`。

### 5.4 未携带清单的字段名（已定）

- 字段名用 **Symbol**，键 `jsb.untransferred`：`error[Symbol.for("jsb.untransferred")] = PackedStringArray`（元素形如 `"detail.x"`）。
- 为什么用 `Symbol.for` 而不是每 isolate 唯一的 `v8::Symbol::New`（`Environment::symbols_`，`jsb_environment.h:186`）：注册表 symbol 可以**在目标 realm 本地重算**，用户也能用同一个键读回，无需我们额外导出 API。
- 各腿支持：`v8::Symbol::For` 三个 shim 都已实现（jsc `impl/jsc/jsb_jsc_primitive.cpp:184`、quickjs `impl/quickjs/jsb_quickjs_primitive.cpp:181`、web `impl/web/jsb_web_primitive.cpp:124`），仓内已有先例（symbol 跨环境传递：`jsb_shadow_realm.cpp:382`）。
- 注意：symbol 本身**不能**随消息序列化过去，且 symbol 键的属性不会被序列化——所以清单是「目标 realm 重建 Error 时本地挂上去」，与 payload 无关。

### 5.5 `onerror` 参数（Worker 与 TransferableShadowRealm 一致）

- 形态：**重建的 `Error`**（`instanceof Error` 成立），额外字段直接挂在该对象上，`untransferred` 清单挂在其上（字段名待定）。
- 说明：不做"裸记录"（`e instanceof Error` 假、`String(e)` 变 `[object Object]`、库里常见分支失效）；不把原始错误对象挂 `cause`（跨 realm 传不了），`cause` 只做**递归记录**（即重新建一个 Error）。

## 6. 性能分析（回应用户提问）

- **只在错误路径上跑**：抓栈本身的成本比"遍历几十个属性"高 1~2 个数量级 → 正常使用下不是性能问题。
- **风险是病态输入**，必须靠 §5.3 的边界（深链、环、大数组、getter、巨型字符串/二进制）。
- **双遍历**：我们遍历一次建记录，序列化器再遍历一次 → 都在错误路径，可接受。
- **同线程活代理（方案 A）**：每次属性访问都跨 isolate，打印/枚举大对象时成本不可控——这也是不把 A 用于错误对象的原因之一。

## 7. 验证计划

| 面 | 手段 |
|---|---|
| ShadowRealm 崩溃/异常 | `src/runtime/tests/test_jsb_shadow_realm.h` 加回归用例（写法即 `report.md` §30 的探针：`evaluate` 里 `throw`），修前必须复现崩溃、修后断言 `instanceof Error` + message/stack |
| `importValue`/`importValueSync` | 同上文件，断言 reject/抛出的对象形态 |
| Worker | 真实 worker（本机 v8/qjs 可跑）+ 启动失败/回调抛错两个用例；jsc 走 CI |
| TransferableShadowRealm | 与 Worker 同一断言（字段集一致） |
| 有界性 | 构造病态对象图（环/深链/大数组/getter 计数器）断言不超限、getter 未被触发 |
| 定时器（若做） | 复用 `src/runtime/tests/test_jsb_any_runtime.h:113/119/130` 的 `tm.invoke_timers(&ctx)` 驱动方式 |

## 8. 回滚与风险

- 改动集中在：`jsb_shadow_realm.cpp`（错误分支 + 记录/重建 helper）、`jsb_worker.cpp`（三处 catch 发送端）、`jsb_environment.cpp`（host 侧重建）、三 shim（`ToLocalChecked` 使用点不动，改调用点；TryCatch 结束语义若需改则在 `impl/*/jsb_*_catch.h`）。
- 回滚点 1：只做 §3 的两个前置修复（崩溃消失、错误分支可达）——独立可交付、独立可回滚。
- 回滚点 2：消息通道启用 `TYPE_ERROR` 发送端（Worker/Transferable）——不影响既有 `TYPE_MESSAGE`/`TYPE_READY`。
- 风险：跨线程消息 payload 格式兼容（新旧版本混用场景：GodotJS 是自包含扩展，无跨版本混用，风险低）；`TryCatch` 语义改动可能影响其他调用点（需逐个核对其"捕获后不抛出"的既有用法）。
