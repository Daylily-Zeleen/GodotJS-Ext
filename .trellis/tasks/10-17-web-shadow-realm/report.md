# 10-17 web shadow realm 移植进度（本轮）

## 已完成

### 四个缺失的 v8 shim（web）
- `jsb_web_data.h/.cpp`：`Data::Equals`（走既有 `jsbi_stack_eq`）、`Data::ToPrimitive`（新 `jsbi_ToPrimitive`，对象走 valueOf，结果压新栈槽）。
- `jsb_web_primitive.h`：`Value::Equals(ctx, that)` / `Value::ToPrimitive(ctx)`（`Maybe<bool>`/`MaybeLocal<Value>`）。
- `jsb_web_object.h/.cpp`：`v8::Proxy`（`class Proxy : public Object`），`Proxy::New`/`GetTarget`；JS 侧 `_jsbb_proxy_targets`（WeakMap）弥补 JS 无法读取 Proxy target。实现放 .cpp（object.h → isolate.h → array_buffer.h → object.h 有包含环）。
- `jsb_web_maybe.h`：补 `Just`/`Nothing` 自由函数。
- `jsb_web_serializer.h/.cpp`（新）：`ValueSerializer`/`ValueDeserializer` + 各自 `Delegate` 基类。
  - op 流：0x01 stash 引用（原始值/函数等同 realm 可共享值）；0x03 宿主对象（delegate 写 tag+transfer 索引）；0x04/0x06 对象/数组递归；0x05 结束；raw chunk 0x02+len+bytes。
  - 宿主对象必须走 delegate：内部字段（class_id）按 env 各自一份，引用直通会把发送方 class_id 带进接收方注册表导致 SArray 越界（实测 FATAL `jsb_sarray.h:580`）。
  - 深遍历原语 9 个：`jsbi_SerKind/ArrayGet/ArraySet/ArrayLen/OwnKeys/GetProp/SetProp/NewPlainObject/SerNewArray`（注意：`jsbi_NewArray` 是既有原语，勿重名）。
  - 实测修复：`Release()` 不得重复写 magic（`WriteHeader` 已写入流，否则双 magic 解析必败）。
- 库文件 `library_godotjs_jsbi.js` + `jsb_web_interop.h` 同步新增；glue 生成器校验两侧符号对齐。

### web 消息通道（shadow realm 投递依赖）
- `jsb_environment.h/.cpp`：`inbox_` 成员、`post_message`/`handle_message`、`_on_worker_message`、`invoke_worker_callback_from_message` 原 `#if !JSB_WITH_WEB` 全部放开到 web；`exec_calls` 里 web 分支改为「`jsbi_FlushPendingWorkerMessages()` + inbox 排空」双通道。
- `Serialization::VariantSerializerDelegate/VariantDeserializerDelegate`（environment.h）去掉 `#if !JSB_WITH_WEB`；三处 `#if JSB_WITH_V8||JSC||QJS` 分支补 `|| JSB_WITH_WEB`（environment.cpp:717、shadow_realm.cpp:1718/1786）。`ThrowDataCloneError` web 分叉走 `Helper::throw_error`（web shim 无 `Isolate::ThrowException`）。
- `forward_error_to_master` 重构：按「master 是否同线程」分叉——同线程（shadow realm）走 `Message`/inbox；跨 pthread worker 保持纯数据 `{__jsbError}` 浏览器通道。行为对非 web 腿等价。

## 实测结果（web-browser 矩阵，多次运行）
- **worker 后端（本任务原始范围）全绿**：object-transfer owned/native-owned/refcounted/persistent ×3 轮、error-reporting onmessage-throw/timer-throw、session 1/2/3 全部 done。
- **shadow 后端**：object-transfer 4 场景全绿；error-reporting 未通过（见遗留）；persistent 偶发 `bind_godot_object "Bind again?"` + 死值 inc-ref（GC 竞争，抖动）。
- web 腿整体出现抖动：`RuntimeError: unreachable` / `function signature mismatch` 在不同轮次早期出现（run14/16），此前 shadow 通道未启用时的三次 PASS 无此现象，疑与新启用的 shadow/消息面有关，待隔离。
- native 三项全绿：node 构建 rc=0、doctest `98|98 passed`+`1280|1280 passed`、host-node `GODOTJS_TEST_PROJECT_COMPLETED` rc=0。

## 遗留（下轮接手）
1. **shadow error-reporting**：宿主 onerror 收到的是 fallback `Error`（proto=Error.prototype，`same_proto=0` 实测），疑似 `make_cross_env_error` 里 `find_native_class(CrossEnvError)` 在同步投递路径失败后走了 `make_error` 兜底；需查 `jsb_check` 在该构建下是否被剥离（掩盖了 class_info 为空）。TEMP-DIAG 已全部撤除，可按此假设直接查。
2. **persistent shadow 抖动**：`bind_godot_object "Bind again?"`（jsb_environment.cpp:995）与 `UNEXPECTED inc reference on dead value`，GC 弱回调与重绑定竞争；transfer-in 前应先查既有绑定（对齐 worker 路径的去重逻辑）。
3. **早期 wasm trap 抖动**（unreachable / function signature mismatch）：先确认是否仅出现在 shadow 通道启用后的构建；可用 `--runtimes web-browser` 多跑几轮统计频率，再决定是否单独建任务。
4. 目标（web ×2 连续 PASS）未达成，goal 保持 active；worker 范围已绿，剩余全在 shadow 面。

## 本轮改动文件
- 新增：`src/runtime/impl/web/jsb_web_serializer.h/.cpp`
- web shim：`jsb_web_data.h/.cpp`、`jsb_web_primitive.h`、`jsb_web_object.h/.cpp`、`jsb_web_maybe.h`、`jsb_web_interop.h`、`js/library_godotjs_jsbi.js`
- bridge：`jsb_environment.h/.cpp`、`jsb_shadow_realm.cpp`、`jsb_cross_isolate_util.cpp`（仅诊断已撤）
- bridge（TS）：`src/runtime/impl/web/bridge/src/monolith.ts`（NewInstance 诊断已撤；wasmop 模块作用域 const 为上一轮 Q3 改动，本轮首次运行时验证通过）
