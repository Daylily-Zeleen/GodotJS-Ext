# 跨隔离区错误上报（worker / ShadowRealm / 定时器）

> 适用：任何要把 JS 异常从"一个环境/隔离区"送到"另一个"的改动。
> 实现：`src/runtime/bridge/jsb_error_record.h/.cpp`（记录 ↔ 重建），接线在
> `jsb_shadow_realm.cpp` / `jsb_worker.cpp` / `jsb_environment.cpp` / `jsb_timer_action.cpp`。

## 硬约束

1. **异常对象不能跨 isolate 传**。跨 isolate 使用 `Local`/`Global` 是未定义行为（崩，不是抛）。
   任何跨边界的错误都必须：**降级成纯数据（记录）→ 复制 → 在目标 realm 用自己的 `Error` 构造器重建**。
   `v8::Symbol` / symbol 键属性 / 函数 / 类实例 / Proxy 都不参与复制。
2. **投递分同步/异步两档，取决于"来源侧是不是真的同帧同线程"**（统一入口
   `Environment::forward_error_to_master(exception|record, ErrorForwardMode)`）：
   - **异步**（`post_message`，宿主下一帧由 `update()` 派发）：来源是**另一线程**（worker）或
     "捕完异常就返回、栈随即消失"的回调（定时器）。worker 与 master 不同线程，同步投递是跨线程调 JS，非法。
   - **同步**（`handle_message`）：来源与宿主**同线程同栈**时（shadow realm 的 `onmessage`/同步 `evaluate`）。
     同步投递会把宿主的 `onerror` 插进来源侧的 JS 帧里执行——所以**必须**由来源侧的帧内引用计数兜住：
     `ShadowRealmImpl::ins_refcount_ > 0` 时 `terminate()` 只做标记、退帧时才真正销毁
     （见硬约束 12）。没有这层保护时，用户在 `onerror` 里 `terminate()` 会实测
     `FATAL ERROR: v8::Isolate::Deinitialize() Deinitializing the isolate that is entered by a thread`。
3. **`TryCatch` 的取值顺序**：`has_caught()` → `get_exception_value()` → `get_message()`。
   `get_message()` 会消费异常槽（quickjs 会把它从引擎搬进内部槽），之后取不到。
4. **`has_caught()` 是一次性语义**（jsc/quickjs/web）：只能调一次，结果存下来复用；
   重复调用在 quickjs 上直接 `jsb_checkf` 断言（"stack.exception is dirty"），并会触发错误打印风暴。
5. **不要用 `Isolate::ThrowException(value)` 抛跨边界错误**（jsc/quickjs 会把值"寄存"进 TryCatch 的槽，
   之后任何 `has_caught()` 都会读到脏状态）。要抛值就按 JS 语义抛（见 `jsb::cross_isolate::throw_error`）。
6. **`Environment::load` 自己转发加载失败**：它是 `Environment` 成员，本身就持有宿主信息，所以失败时
   直接 `forward_error_to_master(exception)`（入口脚本加载失败即此路）：`load` 在异常槽还热时把脚本真实
   Error 发给宿主 `onerror`。
   NOTE 早先认为"加载失败后 worker 环境已损坏、不能在其中跑 JS、只能发空 payload"，实测该前提不成立：
   `capture` 是纯 v8 API、`serialize` 只用异常值建对象，都能在失败后的环境里正常跑
   （前提是转发入口自带 `v8::Context::Scope`，见硬约束 14）。
   NOTE `load` **不再**回传错误记录给调用方：`importValue(Sync)` 的失败信息走 `String` 出参、在调用方
   realm 用 `make_error` 重建成 Error（那是一次跨 realm 的本地投递，与 `load` 的"转发给宿主"是两回事）。
7. **所有 v8 句柄必须在 `HandleScope` 内创建**（在作用域外 `new_string` 会 fatal：
   "Cannot create a handle without a HandleScope"）。
8. **跨腿 API 差异**（写 shim 无关代码时要避开）：`Function::NewInstance` 只有真 v8 有（用 `CallAsConstructor`）；
   `MaybeLocal::FromMaybe` 各腿都没有（用 `ToLocal(&v)`）；`Local` 没有 `operator bool`（判 `IsEmpty()`）；
   `Isolate::ThrowException(value)` 只有 jsc/quickjs/v8 有（web 腿要走字符串路径）；
   quickjs 的 `Maybe` 没有 `FromJust`（用 `To()` / `FromMaybe()`），`GetOwnPropertyNames` 的结果要用
   `IsEmpty()`/`ToLocalChecked()` 取（`ToLocal(&local)` 在该腿直接编译不过）。
9. **读取 payload 用 `HasOwnProperty` 判断键是否存在**：`Get()` 对不存在的键也会成功返回 `undefined`，
   只看 `Get()` 会把每个 payload 都当成"原始值 `undefined`"（实测宿主 `onerror` 收到 `undefined`）。
   反过来"原始值异常就是 `undefined`"这个合法情形也要靠同一个存在性判断才分得出来。
10. **quickjs shim 里"返回句柄"的函数不能自带 `HandleScope`**：返回值槽会随局部作用域析构被释放，
   调用方拿到的是悬垂句柄。另外**槽里的值是借用引用**，不能直接 `JS_FreeValue`（槽自己持有那一份，
   少记一次引用 → 反复调用会把 Symbol 构造器等提前释放）。
   已修 `Symbol::For` 与 `Symbol::_get_well_known`（`Symbol::GetIterator` / `GetToPrimitive` 等）；
   `test_jsb_quickjs_runtime.h` 有回归用例（旧实现下直接崩，修复后连查 256 次身份恒定）。

11. **不要用 `GetAlignedPointerFromInternalField` 反查原生包装器**（`CrossWrapper` 这类）。
    宿主侧对象可能是 `v8::Proxy`（`ObjectCrossWrapper` 缓存的就是 Proxy），Proxy 没有对应内部字段，
    取字段直接 `FATAL ERROR: Internal field out of bounds`。
    正确做法：**注册表直接存 `CrossWrapper *`**（POD 裸指针）；`SArray` 要求元素可拷贝/可移动，
    而 `v8::Global` / `TStrongRef` / `TWeakRef` 都是 move-only（`jsb_ref.h:31-36,74-79`），
    存句柄会被迫走拷贝路径（`C2280`）。指针的有效区间 = `add_cache` 到 `remove_cache/purge`，
    与包装器对象存活区间重合。
    purge 时对每个指针调 `reset(true)`（只清自身 v8 引用，不动注册表），**注册表条目由 purge 统一摘除**，
    避免与 `remove_cache` 双删同一 `SArray` 槽。注意 `unordered_map::erase(it)` 之后不能再读 `it->second`
    （迭代器失效，实测为悬垂读），要先把 `WrapperIdx` 取出来再 erase。
12. **realm 的 JS 帧必须用 `ShadowRealmImpl::FrameScope` 兜住销毁**。帧内引用计数 `ins_refcount_ > 0`
    时 `_terminate()` **只置 `terminated_in_frame_` 标记、不就地销毁**；退帧时 `FrameScope` 析构补真正的
    `_destroy(id)`。否则帧内（宿主 `onerror` 里）`terminate()` 会 `env_->dispose()` + `env_.reset()` 掉
    本帧正在跑的 isolate（硬约束 2 的 fatal）。`_destroy` 要用**传入的 id** 摘除，因为 `finish()` 会清空 `id_`。
    **每个会执行 guest 代码、且可能同步转发给宿主的入口都要挂 `FrameScope`**：`_on_message` / `evaluate` /
    `importValue` / `importValueSync`（用户可以在任何一处捕获异常后立刻 `terminate()`）。
    NOTE `FrameScope` 析构时会销毁 realm，所以**析构点之后不得再使用 `realm`**；`evaluate` 里把它放在
    guest 执行的那个块作用域内（块比函数体短，块外的 host 重建不再碰 realm）。
13. **统一转发入口内部才做"判接收者 + capture"**（`forward_error_to_master(const v8::Local<v8::Value>&)`）。
    调用方只把 `TryCatch::get_exception_value()` 交出去，不要各自 `has_error_receiver()`/`capture()`。
    调用方仍要在调用**之后**用 `get_exception()` 打日志：quickjs 的异常槽靠 `get_message()` 清空，
    不消费会让下一次 `has_caught()` 命中 `jsb_checkf("stack.exception is dirty")`。
    **跨环境"采集→重建→抛出"封在 `jsb_cross_isolate`**（`jsb_cross_isolate_util.{h,cpp}`，命名空间
    `jsb::cross_isolate`）。对外只有三个入口，调用方**看不到记录/载体类型**：
    - `throw_cross_isolate_error(target_isolate, target_context, exception_value, fallback)`：
      在**源作用域内**调用，内部完成 `capture(源) → rebuild(目标) → throw(目标)`；
    - `make_error(isolate, context, message)`：造一个 `Error`（如 `importValue` 的 reject）；
    - `throw_error(isolate, context, message, error)`：按 JS 语义抛出（`(function(e){throw e;})`，
      不用 `Isolate::ThrowException`，以免污染 TryCatch 槽）。
    载体 `internal::CrossIsolateException`、`internal::rebuild_error` 是**内部实现**。
    NOTE `capture` 必须用 `Isolate::TryGetCurrent()` 取源 isolate（各腿 shim 只有 `TryGetCurrent`，
    没有 `GetCurrent`）。shadow realm 的 `evaluate`/`importValue` **只留干净调用点**（各一句），
    `jsb_shadow_realm.cpp` 不再 include `jsb_error_record.h`；本地 `_throw_value_in_realm`/`_throw_realm_error`
    已删除。`internal::rebuild_error` **不能**自带 `HandleScope`：返回值要交给调用方，开在函数内会悬垂
    （实测崩在 `v8::internal::LookupIterator::GetRootForNonJSReceiver`）。
14. **转发入口里必须自带 `v8::Context::Scope`**。`forward_error_to_master` 会进入本环境的 isolate，
    而 `capture`/`serialize` 内部要 `Object::New`/`Set`/`new_string`——这些需要**当前 context**。
    只在 `Isolate::Scope` 下、没有 context scope 时实测崩在 `record_to_js` 的 `payload->Set`（SIGSEGV）。
    （定时器路径之所以掩盖了这个问题：调用它的 `flush_uncaught_exception` 已经建好了 context scope。）

## 错误记录的形态

```
ErrorRecord = { name, message, stack, extra, untransferred, cause }
```

- 采集在 **C++ 侧**（`jsb::error_record::capture`，`GetOwnPropertyDescriptor` 遍历，**不触发 getter**）。
- **只搬 Error 自己的字段 + JS 基础类型**：`name` / `message` / `stack`、`cause`（是 Error 就递归其字段）、
  以及 string / number / boolean / null / undefined。数组、普通对象、函数、symbol、BigInt、
  Godot 类型（`Object` / `Array` / `Dictionary` / ...）**一律不搬**，只把路径登记进 `untransferred`——
  这些数据得由用户在发送侧自己显式转出去（`String(x)` / `JSON.stringify(x)` / 摊平成基础类型），
  JSB 不替用户决定怎么转。
  C++ 侧不借道 `TypeConvert`：后者会触达惰性原生化类暴露，在 evaluate 的嵌套 isolate 作用域里会崩（实测 SIGSEGV）。
  注意 v8 里**函数也是 object**，`IsFunction()` 必须在对象分支之前拦。
- **有界**：深度 8 / 节点 4096 / 字符串 64K；未携带清单本身也有上限（64 条 + 一个 `"..."` 标记），
  否则一个 10 万元素的数组会把 payload 反过来撑爆。
- `cause` 只在"Error 形态"（有自有 `stack` 或 `message`）时才递归；普通对象 cause 不搬（同上，用户自己转）。
- 带不走的字段路径写进 `untransferred`，在重建的 Error 上挂到 **`Symbol.for("jsb.untransferred")`**
  （用注册表 symbol：目标 realm 能本地重算、用户也能用同一个键读回，且不与用户字段冲突）。
- 接收侧是**目标 realm 重建的 `Error`**（`instanceof Error` 成立），`name`/`message`/`stack`/`extra`/`cause` 照抄；
  但 `throw "boom"` / `throw 42` 这类**原始值异常按原样送达**（不包装成 `Error`），`cause` 为 `Dictionary` 时递归重建。
- **不能用"文案是否为空"判断有没有出错**：quickjs 的 `get_message()` 对非 `Error` 抛出值不填 message，
  照它判断会把原始值异常整个吞掉、`evaluate()` 静默返回（实测 `NO-THROW`）。判断依据只能是 `has_caught()`。

## `onerror` 的契约

| 入口 | 错误怎么送达 |
|---|---|
| `JSShadowRealm.evaluate` / `importValueSync` | 同步抛（重建的 Error，带 name/stack/额外字段） |
| `JSShadowRealm.importValue` | Promise reject（重建的 Error；未处理的 reject 由 `PromiseRejectCallback_` 记日志，不会继续抛） |
| `JSWorker.onerror` | 异步：worker 侧发 `TYPE_ERROR`，宿主重建 |
| `TransferableJSShadowRealm.onerror` | **同步**（realm 与宿主同线程同栈）：`forward_error_to_master(..., Sync)`，回调在本帧内跑完 |
| worker / transferable shadow realm 里的定时器回调异常 | **异步**（定时器回调捕完异常就返回、栈随即消失）：worker 与 realm 都走 `forward_error_to_master(..., Async)`，下一帧由宿主 `update()` 派发 |
| worker 入口脚本加载失败 | 异步：`Environment::load` 在异常槽还热时采集记录，经统一入口发给宿主 `onerror`（拿到的是脚本真实 Error，失败时才退回空 payload） |
| transferable realm 的 `importValue(Sync)` 加载失败 | 用 `load` 回传的记录在调用方 realm 重建 Error（同步抛 / reject），不再是字符串 |
| 主环境、普通 shadow realm 的定时器回调异常 | 只进日志（前者的 realm 没有 `onerror` 接收者，后者没有全局错误钩子） |

## 写新代码时的检查单

- [ ] 跨边界错误走"记录 → 复制 → 重建"，没有把 `Local/Global` 带过界
- [ ] 调用统一入口 `forward_error_to_master(exception, mode)`：`mode` 与来源侧线程/栈语义相符（跨线程=Async）；
      不要在调用点自己 `has_error_receiver()`/`capture()`
- [ ] 转发入口内部有 `v8::Context::Scope`（capture/serialize 要建对象）；用异常值入口时**在入 isolate 作用域后再 capture**
- [ ] 来源侧是同步帧时，帧内 `terminate()` 被 `ShadowRealmImpl::FrameScope` 兜住（延迟销毁）
- [ ] `TryCatch` 取值顺序正确，且 `has_caught()` 只调一次；调用转发后仍用 `get_exception()` 清异常槽（否则 quickjs 脏槽）
- [ ] 新增的 JS 侧采集/重建脚本有界（深度/节点/字符串/清单上限）
- [ ] 目标 realm 拿到的是 `Error`（`instanceof` 成立），未携带字段能从 `Symbol.for("jsb.untransferred")` 读到
- [ ] 记录里只有 Error 字段 + JS 基础类型，没有把 Godot 类型/数组/对象偷偷搬过去
- [ ] 有对应用例：错误确实到达回调/抛出点（不是"只打了日志"）
