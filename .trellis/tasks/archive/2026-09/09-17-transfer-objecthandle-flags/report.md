# 对象转移与退出生命周期修复报告

> 下方旧版通过记录不代表用户 2026-09-18 调整后的状态；本轮结果见文末「用户语义调整后的跑测」。

## 目标

修复 Environment 转移对象丢失 JS-owned/persistent 语义、持久计数失配及退出崩溃。先复现，再在 V8 与 QuickJS-NG 本地编译并运行真实集成。未提交、未推送；改动留待人工审查。

## 原缺陷的直接证据

- `.agent_tmp/transfer-owned-before-fixed-driver.log`：旧 V8 DLL 连续往返后，接收 worker terminate，未转移 control 已释放但转移 Node 存活，明确报 `JS ownership lost`。进程虽然 exit 0，FAILED 哨兵仍判失败。
- `.agent_tmp/transfer-persistent-before.log`：旧 DLL 在 None 解绑 persistent 时触发 `!OBF_PERSIST` 致命断言，exit 2147483651。
- 旧 DLL 的 C++ 两套件全绿后仍 0xc0000005 退出，LLDB 未获得实际异常栈。
- `.agent_tmp/transfer-shadow-v8.log`：真实 shadow owned/native-owned 通过，随后 GC 在 TransferableShadowRealmImpl 析构触发 V8 `node->IsInUse()`；堆栈证实 guest Global 活得比 guest isolate 更久。

## 修复

- TransferData 保存 js_owned/persistent；发送前快照、接收绑定后 C2 合并，已有绑定同样处理。
- mark 幂等；persistent 原子有符号计数在移除句柄时注销，包含 None，None 仍不调用 native finalizer。
- 合法未访问 PackedScene 子节点先建立 engine-owned 源绑定；prepare 返回 bool，失败调用方不插入/finalize/发送。
- RefCounted 保留源引用在消息 Variant 保活下配对释放；测试用 native WeakRef，避免高位 ObjectID 经 JS number 丢精度造成伪失败。
- EnvironmentRef bool 检查实际 env；不再把已 reset 的控制块当活环境。
- shadow finish 在 guest isolate 存活时清空派生 Global，再 terminate/dispose/reset。
- C++ shadow fixture 的 Node 在 guest 存活时显式释放；原 Node/script/StringName 泄漏不再出现。
- worker 测试遵循既有 completeCallback 协议，避免场景提前销毁。

## 已运行验收

宿主：`D:/Dev/godot/godot/bin/Godot_v4.7.2-stable_win64_console.exe`。
构建：`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes verbose=yes -j6`；QuickJS-NG 叠加 `use_quickjs_ng=yes`。SCons 外层打印 unknown variable 警告，但实际输出选择相应引擎并编入 JSB_TESTS_ENABLED，两套件实际执行。
TS：`node node_modules/typescript/bin/tsc --noCheck`（cwd project）。

| 运行 | 结果 | 证据 |
|---|---|---|
| V8 C++ | runtime 50/50、581 assertions；editor 3/3、12 assertions；exit 0 | `.agent_tmp/transfer-v8/cpp.log` |
| V8 native worker 完整集成 | 四项 lifecycle 全 done，原三轮会话及完整项目 COMPLETED，exit 0 | `.agent_tmp/transfer-v8/integration.log` |
| V8 真实 shadow | 四项 lifecycle 全 done，COMPLETED，exit 0；原 Global fatal 消失 | `.agent_tmp/transfer-shadow-v8-after.log` |
| QuickJS-NG C++ | runtime 51/51、586 assertions；editor 3/3、12 assertions；exit 0 | `.agent_tmp/transfer-quickjs-ng/cpp.log` |
| QuickJS-NG native worker 完整集成 | 四项 lifecycle 全 done，COMPLETED，exit 0 | `.agent_tmp/transfer-quickjs-ng/integration.log` |

native 回归包括：JS-owned 接收 teardown 删除；engine-owned 父子节点保持存活、父 free 递归释放子；三轮往返与已有绑定复用；RefCounted native holder/WeakRef 回收；persistent 主环境迁移计数差值与重复登记。
V8 native 验证 DLL MD5：runtime f61277c4ec4a0cbba6c31feceab0de12，editor 371695b60e0fb184d2cb5bd07895b821。shadow 修复后另外增量构建并运行 shadow 验收，不用旧 MD5 冒充该轮身份。
QuickJS-NG 部署双端 MD5 已匹配：runtime 72ee48200ea302341bb0ca939bf9c583，editor 1fdcc195004da504d65ef4c8bdc84fdf。当前本地部署为 QuickJS-NG。

## 自检判断

“尚未证明 ObjectHandle flag 丢失修复”与已有运行结果不符：旧 DLL owned 场景失败、修复后同场景在两引擎通过；真实 shadow 四场景也通过。无需再添加重复的原生 bit 断言来代替已运行的端到端生命周期证据。没有直接读取接收 OBF_JS_OWNED 位；观测的是其对最终 native 对象删除的实际影响。

## 遗留与边界

- C++ 两引擎各保留 8 次 `ScriptInstance::callp: env is null`。未静默、未称完全无错误；进程已不崩溃，fixture 资源泄漏已消除。
- 现有 C++ 负向语法用例输出 eval_source 错误；TS 既有 missing metadata 用例输出错误。它们不能与 lifecycle FAILED 混为一谈。
- web 无运行产物，未执行 web 验收；QuickJS-NG shadow host-object serializer 不在本轮已验范围。
- persistent monitor 仅主环境可观测，worker 内部计数没有直接读取验证。
- 留存真实回归与日志；一次性验证脚本删除。规范已记录迁移/失败传播/GC线程语义/guest Global 释放顺序。任务未归档，待人工审查。

## 用户语义调整后的跑测（2026-09-18）

本轮边界：保留用户 runtime 实现，只调整 `project/tests/worker/test-worker.ts` 与 C++ fixture 解释注释；不提交、不推送。当前 flags 完整恢复、prepare void/要求已有句柄、mark 不幂等、计数 int32_t；旧 PRD 中 bool prepare/atomic/OR 合并等结论已失效。

测试适配：删除重复 mark/已 persistent 接收幂等假设；保留首次登记、传出减计数、全新与已有非 persistent 接收、最终 native free 回到基线的断言。persistent fixture 改为叶节点以独立验证；默认 native-owned 仍保留未访问子节点，未预绑定、未跳过。

V8 已按规范构建完成，TS `tsc --noCheck` 通过。部署双方 MD5 一致：runtime `3f8221cbabdb175c9eb6e202878c95de`，editor `47d13f8a52af4145fb0db22aa049887d`。

| V8 场景 | 实测结果 |
|---|---|
| C++ 全套 | runtime 50/50、581 assertions；editor 3/3、12 assertions；exit 0；仍有 8 次 env is null |
| 完整 TS | owned done；native-owned 在 UnboundNativeChild 源句柄断言中止，exit 2147483651；无 COMPLETED |
| refcounted 隔离 | done、COMPLETED、exit 0 |
| persistent 隔离 | start 后 90 秒超时，进程已终止 |
| shadow 通道 | owned/native-owned/refcounted done；persistent start 后 90 秒超时 |

证据：`.agent_tmp/transfer-user-semantics/v8-*.log` 与同名 json，记录真实退出码、DLL 身份与完成标志。未将隔离场景通过当作完整集成通过。

源码审阅解释实测失败：
1. worker `append_node_descendants_for_transfer` 仍自动收集后代；`prepare_transfer_out` 却要求每个对象已在源 DB。日志明确指出 `UnboundNativeChild` 在 Godot DB 有效、在 jsb DB 不存在。
2. persistent 接收在 `transfer_in_bind:2084` 持 ObjectHandlePtr 写锁，`:2089` 调 mark，再次 `try_get_object` 取得同一非递归 shared_timed_mutex 写锁。两条通道均超时；自锁链由源码及复核确认，未抓调试器现场栈。
3. 另有未实测边界：非 persistent 源 flags 覆盖已 persistent 目标，会清位但未同步减计数；不把该静态风险称已复现。

### test_jsb_shadow_realm.h 改动究竟做什么

原测试只验证：guest 内 `new test_01` 得到的 Godot 脚本实例，其 Environment 是 guest 而非 main。传回主环境的是 instance ID 字符串，不是转移对象。
新增 `GuestInstanceCleanup` 不增加转移断言，只补测试资源清理：guest 全局保存 Node；作用域正常结束或 REQUIRE 提前退出时，析构器回 guest 调 free；随后较早构造的 initer 才销毁 Environment。避免测试 Node 泄漏及销毁后才清理。现已在定义与保存点加中文注释。

### QuickJS-NG 同轮结果

规范构建成功（叠加 `use_quickjs_ng=yes`）；部署 MD5 双方一致：runtime `d716b9ab2e6f9793c1dc5feb0b05faa2`、editor `eaf7c3b77ee189452e8f457608883a3b`。
- C++ runtime 51/51、586 assertions；editor 3/3、12 assertions；exit 0，仍有 8 次 env is null。
- 完整 TS owned done，native-owned 中止，exit 2147483651，无 COMPLETED。
- refcounted 隔离 done、COMPLETED、exit 0。
- persistent 隔离 start 后 90 秒超时，进程已终止。
- 证据 `.agent_tmp/transfer-user-semantics/quickjs-ng-*.log` 与 json；当前部署为该 QuickJS-NG 构建。

本轮交付为测试适配、源码审阅与真实跑测结果，不擅自重写用户运行时。后续源修复建议：统一递归后代与源绑定前置条件；避免持 DB 句柄锁时再调用会取同一锁的 mark。没有新增临时脚本文件；保留日志和回归。代理复核未发现需再改的测试问题。

## 跨环境通信测试重命名

统一名称为 cross-environment（跨环境通信）；对象转移测试使用 ObjectTransfer，不再使用含糊的 lifecycle。目录变为 `project/tests/cross-environment/`，主场景/驱动为 `CrossEnvironment.tscn` / `test-cross-environment.ts`，共享对端为 `cross-environment-peer.ts`，原生对象夹具为 `native-object.tscn`。UID 随文件迁移，消息协议、日志、主场景入口和当前规范引用同步更新；实际 JSWorker 专属 API 与三轮 Worker 会话名称保留。

新参数：`--object-transfer-case=owned|native-owned|refcounted|persistent`，`--object-transfer-backend=shadow`。旧参数不保留兼容别名。

运行范围未改变：无参数时四项对象转移使用 JSWorker，完成后运行三轮 JSWorker 基础消息测试；shadow 参数只将四项对象转移切到 TransferableJSShadowRealm（当前有 V8 检查），基础消息测试仍是 JSWorker。尚未实现同一套断言自动双后端遍历，不可宣称默认双覆盖。

验证：
- TS `tsc --noCheck` 成功；真实宿主通过新场景和新 peer 路径分别执行 owned/refcounted，两次都有新名称 done、项目 COMPLETED、exit 0。日志 `.agent_tmp/cross-environment-owned.log`、`cross-environment-refcounted.log`。
- `--generate-types` 输出 Type generation complete，新场景生成声明已引用新驱动，旧场景模块缺失的 TS2307 消失；但生成进程退出 3221225477，不能声称生成命令成功。
- `npx tsc --noEmit` 仍有 12 项类型错误：Node get_name/set_name、Object/Node free 声明缺失及 ES2022 下 Promise.withResolvers。未通过类型断言掩盖，也未为改名扩展公共声明/tsconfig 范围。
- 本轮未重编 C++，未重跑已知 native-owned/persistent 阻断；没有将两项 smoke 当完整集成通过。临时重命名工具已删除，保留日志；未提交、未推送。

## 用户修复 runtime 后的双后端跑测（2026-09-18）

用户已修改 runtime（工作树未提交）：`jsb_message.h` 给 `TransferData` 加 `flags` 字段；`jsb_environment.cpp` 的 `mark_as_persistent_object` 增加 `p_count_only`、`free_object` 对 persistent 统一减计数、`transfer_in_bind` 完整恢复 flags 并 `mark_as_persistent_object(instance, true)`；`jsb_shadow_realm.cpp` 增加 `dispose_environment()` 虚方法清空 guest Global。`42063a4` 已移除 worker 自动收集后代，native-owned 用例不再因未绑定子节点断言中止。

测试调度已实现为：无参 `['worker','shadow']` 顺序完整跑（四项对象转移 + 三轮基础会话），`--object-transfer-backend=worker|shadow` 只跑指定后端，`--object-transfer-case=<场景>` 限制对象用例但仍遍历后端，非法后端 FAILED。

构建（`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes verbose=yes -j6`）与 V8 完整双后端、QuickJS-NG 完整双后端、两引擎显式单后端、非法后端、`--jsb-run-tests` 均实跑。证据 `.agent_tmp/dual-backend/v8-*.log` 与 `.agent_tmp/dual-backend-qjs/*.log` 及同名 json。

### V8 结果
- C++ runtime 50/editor 3 全通过，`running tests result: 0`；完整 TS 双后端全套 done，exit 0，COMPLETED。
- 显式 worker / shadow 单后端各自全套 done 且无另一后端 start；`--object-transfer-case=owned` 双后端该用例 done；`backend=invalid` FAILED 并给出 expected worker or shadow。
- 仅剩既有 `The object does not have any 'meta' values with the key 'missing'`（TS 已知负向用例）。
- DLL：runtime f555c938…、editor eb24647d…。

### QuickJS-NG 结果
- C++ runtime 51/editor 3 全通过，`running tests result: 0`；完整 TS worker 全套 done、exit 0、COMPLETED。
- **shadow 通道在 owned 首用例即失败**：`thunks_common.h:471 produce_value bad argument 0: got`，peerError 后 FAILED。根因：`jsb_shadow_realm.cpp:1595-1602/1656-1662` 的 Variant serializer/deserializer delegate 仍由 `#if JSB_WITH_V8` 门控，QuickJS-NG 走无 delegate 的序列化器，对象转移载荷无法还原——运行时不支持，测试如实失败，未静默跳过。已把该限制写入 `test/index.md`。
- 显式 shadow 单后端同样在 owned 失败；显式 worker 全绿。
- DLL：runtime 0aa0970d…、editor 872e27ac…。

### 当前结论
调度契约（默认双后端、显式单后端、case 筛选、非法拒绝）两引擎均已验证。QuickJS-NG 的 shadow 对象转移是运行时缺口，不是测试调度问题；V8 双后端全绿。web worker 无本地构建产物未执行。未提交、未推送。

## Shadow/Worker 传输序列化对齐（2026-09-18）

将 `TransferableShadowRealmImpl` 的消息发送/接收与 `Worker` 对齐，消除 QuickJS-NG shadow 的对象转移缺口：

- `handle_post_message` 改用共享 `Worker::parse_transfer_list`（其从 private 提升为 public，附复用说明），统一走 `Serialization::VariantSerializerDelegate` / `VariantDeserializerDelegate`（原 `#if JSB_WITH_V8` 分支删除，与 `_on_message` 的 delegate 条件改为 `JSB_WITH_V8 || JSB_WITH_JAVASCRIPTCORE || JSB_WITH_QUICKJS`）。
- 两个发送点（`post_message_to_host`、master `post_message`）改用 `transfers[transfer_index]` 定序，与 Worker 一致。
- guest `_on_message` 补 `ThreadSafeForNodesScope`。新增 include：`jsb_worker.h`、`jsb_thread_safe_for_nodes_scope.h`。

构建（QuickJS-NG，同规范命令）成功后：
- shadow 单后端全套 done、COMPLETED、exit 0（owned / native-owned / refcounted / persistent + 三轮会话全通过）。
- 默认双后端全套 done、COMPLETED、exit 0。
- C++ runtime 51/editor 3 全通过、`running tests result: 0`。
- DLL：runtime ffd2487f…、editor 872e27ac…。证据 `.agent_tmp/dual-backend-qjs/shadow.log`、`default.log`、`cpp.log` 及同名 json。
- V8 双后端此前已全绿，本轮对齐不改 V8 发送/接收语义（仅收敛到同一共享实现）。测试 spec 的「QuickJS-NG shadow 传输限制」更新为「传输支持」（全绿）。web worker 无本地构建产物。未提交、未推送。

## 用户 runtime 复核 + 测试类型修复 + 验收核对（2026-09-18）

### 用户已改的 shadow/worker 对齐（我只复核，未再改）
用户把 `jsb_shadow_realm.cpp` 的 delegate 门控从 `#if JSB_WITH_V8` 改为 `#if !JSB_WITH_WEB`（send 侧 `handle_post_message` 与 receive 侧 `_on_message` 各一处），复用 `Worker::parse_transfer_list`（已提升 public，`jsb_worker.h`），两个发送点按 `transfers[transfer_index]` 定序，并撤掉了我在 guest `_on_message` 加的 `ThreadSafeForNodesScope`（该类当前为空实现，撤掉合理）。我未再改动 runtime 源码——用户版即为最终形态。

### 为什么之前「本地跑测绿」但 IDE 大量报错（诚实说明）
我此前验证用 `node node_modules/typescript/bin/tsc --noCheck`——它**只转译、不做类型检查**，产出引擎实际加载的 `.js`，所以运行全绿。但它掩盖了类型错误，IDE 用 `tsc --noEmit`（严格检查）就报出一堆。这是验证口径不正确：运行绿 ≠ 类型干净。修正：跑测前同时跑 `tsc --noEmit` 严格为准。

### 测试类型修复（本批改动，已跑绿 + 类型干净）
`tsc --noEmit` 曾对 `test-cross-environment.ts` 报 7 处：
- `Promise.withResolvers`：tsconfig target=es2022 无该 lib → 改为手写 resolve/reject。
- `Node#get_name()/set_name()`：生成的静态绑定类型只暴露 `name` 属性（StringName），无 `get_name/set_name` → 本以为是类型缺失，改为 `.name`。**但这引入运行时回归**：场景加载（native-owned）节点与经 transfer 重绑定的节点，其 `.name` 取不到 / 返回 undefined（非 JS 字符串 strict-equal）。原始 worker 测试用 `get_name()` 是运行时真方法。finally：新增 `nodeName()/nodeSetName()` 本地 cast 辅助，走运行时 `get_name()/set_name()`，类型与运行一致。peer 侧同类问题一样处理（`name` 在 transfer 重绑后 undefined，改用 get/set_name）。
- `Object#free()`：生成的类型未声明（只有 `queue_free`/`cancel_free`）→ 新增 `freeObject()` 本地 cast 辅助。

修完：`tsc --noEmit` 干净、`tsc --noCheck` 产出、QuickJS-NG 双后端全套（worker+shadow 各 4 用例 + 3 轮会话）done/COMPLETED/exit 0，C++ runtime 51/editor 3 全过。证据 `.agent_tmp/verify-user-alignment/default3.log`、`cpp.log` 及同名 json。

### PRD 验收记录已按最新状态核对更新
native-owned/persistent/完整 TS/shadow 均已解除为通过；仅 web worker 无本地产物与 worker 内部 persistent 计数直读两项仍 `[ ]`。任务整体判定：**核心修复已由用户提交（528f3b5 等 10 提交已推送至 origin/feature/static-bindings = `17f8d05`，PR #4 已更新正文）并本地双引擎跑测全绿**，待人工审查后归档。本批测试修复（`project/tests/cross-environment/` 两个 ts）仍在工作区未提交，与用户已推送的三处 runtime 变更解耦。

## 代码生成对齐后的测试类型修复(2026-09-18,本节取代上一节 cast 辅助方案)

### 根因(用户定位)
代码生成器未过滤 `name` 属性:typings 误发 `get name()/set name()` 属性,而静态绑定 dispatch 实际按 `set_name()/get_name()` 方法处理(`dispatch_class.gen.cpp` Node 行)。用户修复(commit `b5c0846`,`is_ignored()` 迁移至公共 `NamingUtil`、确保代码生成不将 name 映射为属性)后,typings 与实际绑定对上号。

### 测试修复(撤全部 cast,只用契约内 API)
- 删除 `freeObject()/nodeName()/nodeSetName()` 三个 `as unknown as` cast 辅助——不再绕过类型检查、不再调用未入生成契约的 API。
- name:直调类型化 `get_name()/set_name()`(`godot4.gen.d.ts:8824-8825`,`StringName = string`)。
- free:`free()` 仍不在生成契约(dispatch 表无 `"free"` thunk、typings 无声明)→ 全部释放点(对象均为 Node)改用类型内 `queue_free()` + `instanceof Node` 收窄;queue_free 帧末删除,runObjectTransfer 子释放断言前补两次 `process_frame` 等待,persistent 计数断言由 `persistentCount` 自带帧等待覆盖。
- peer:`get_name()/set_name()` 直调,删除过时的「typings 只暴露 name 属性」注释。

### 验证
- `tsc --noEmit` exit 0;`as unknown as`/cast 辅助残留 grep 为零。
- `tsc --noCheck` 产出(引擎加载编译产物)。
- QuickJS-NG 双后端完整集成:worker+shadow 各 4 对象用例 + 3 轮会话全 done/COMPLETED/exit 0;**「static binding not found」回退警告 0 次**(cast 方案调未绑定 API 会走动态回退,现归零);0 FAILED。证据 `.agent_tmp/typed-test-default.log`。
- C++ 双套件(当前部署 DLL,runtime 10:30 / editor 10:41 构建,含 `b5c0846` runtime 变更):runtime 51/editor 3、doctest SUCCESS、`running tests result: 0`、exit 0。证据 `.agent_tmp/typed-test-cpp.log`。
- 部署位 md5 双查一致:runtime `2f8d098d477fbba79d94dd241019d0ee`、editor `5aeb420e8413ec5255abd6e2849098b8`。

工作区未提交、未推送。任务待人工审查后归档。
