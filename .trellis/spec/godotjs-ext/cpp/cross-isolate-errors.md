# 跨隔离区错误上报（worker / ShadowRealm / 定时器）

> 适用：任何要把 JS 异常从"一个环境/隔离区"送到"另一个"的改动。
> 实现：`src/runtime/bridge/jsb_error_record.h/.cpp`（记录 ↔ 重建），接线在
> `jsb_shadow_realm.cpp` / `jsb_worker.cpp` / `jsb_environment.cpp` / `jsb_timer_action.cpp`。

## 硬约束

1. **异常对象不能跨 isolate 传**。跨 isolate 使用 `Local`/`Global` 是未定义行为（崩，不是抛）。
   任何跨边界的错误都必须：**降级成纯数据（记录）→ 复制 → 在目标 realm 用自己的 `Error` 构造器重建**。
   `v8::Symbol` / symbol 键属性 / 函数 / 类实例 / Proxy 都不参与复制。
2. **投递必须异步**。同步投递（`handle_message`）会把宿主的 `onerror` 插进来源侧的 JS 帧里，
   用户在回调里 `terminate()` 就会在帧内销毁自己的 isolate：实测
   `Fatal error in v8::Isolate::Dispose()`。跨环境统一走 `post_message`（下一帧由宿主 update 派发）。
3. **`TryCatch` 的取值顺序**：`has_caught()` → `get_exception_value()` → `get_message()`。
   `get_message()` 会消费异常槽（quickjs 会把它从引擎搬进内部槽），之后取不到。
4. **`has_caught()` 是一次性语义**（jsc/quickjs/web）：只能调一次，结果存下来复用；
   重复调用在 quickjs 上直接 `jsb_checkf` 断言（"stack.exception is dirty"），并会触发错误打印风暴。
5. **不要用 `Isolate::ThrowException(value)` 抛跨边界错误**（jsc/quickjs 会把值"寄存"进 TryCatch 的槽，
   之后任何 `has_caught()` 都会读到脏状态）。要抛值就按 JS 语义抛（见 `_throw_value_in_realm`）。
6. **来源环境已损坏时不要在它里面跑 JS**：worker 的入口脚本加载失败后，环境不能再用（实测直接崩）。
   这种场景只发"空 payload 的 `TYPE_ERROR`"，文案由接收侧补。
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
| `JSShadowRealm.evaluate` / `importValueSync` | 同步抛（重建的 Error） |
| `JSShadowRealm.importValue` | Promise reject（重建的 Error） |
| `JSWorker.onerror` | 异步：worker 侧发 `TYPE_ERROR`，宿主重建 |
| `TransferableJSShadowRealm.onerror` | 同上（与 Worker 同形态） |
| worker / transferable shadow realm 里的定时器回调异常 | 同上（`Environment::forward_error_to_master`） |
| 主环境、普通 shadow realm 的定时器回调异常 | 只进日志（前者的 realm 没有 `onerror` 接收者，后者没有全局错误钩子） |

## 写新代码时的检查单

- [ ] 跨边界错误走"记录 → 复制 → 重建"，没有把 `Local/Global` 带过界
- [ ] 投递是异步的（`post_message`），回调里允许 `terminate()`
- [ ] `TryCatch` 取值顺序正确，且 `has_caught()` 只调一次
- [ ] 新增的 JS 侧采集/重建脚本有界（深度/节点/字符串/清单上限）
- [ ] 目标 realm 拿到的是 `Error`（`instanceof` 成立），未携带字段能从 `Symbol.for("jsb.untransferred")` 读到
- [ ] 记录里只有 Error 字段 + JS 基础类型，没有把 Godot 类型/数组/对象偷偷搬过去
- [ ] 有对应用例：错误确实到达回调/抛出点（不是"只打了日志"）
