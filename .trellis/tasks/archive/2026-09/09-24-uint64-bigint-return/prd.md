# int64/uint64 返回值不丢位（BigInt 出口）

> 父任务：`09-06-lowprio-uint64-bigint-codegen`。共享技术方案见父任务 `design.md`。
> 本任务 = 父任务 R1，覆盖读方向（Godot → JS）。
> **依赖：无**。B 与 D 都依赖本任务（消费 `jsb_primitive_conv.h` 的转换原语）。
> 但 B 与 D 之间**不可并行**（共享 `jsb_type_convert_direct.h` 与 `jsb_static_binding_util.h`），
> 实际顺序为 **A → (B → D)** 或 **A → (D → B)**。

## Goal

64 位整数从 Godot 返回给 JS 时**不再丢信息**。能被 JS `Number` 精确表示的值保持出 `Number`
（不破坏现有性能与兼容），超出时按 meta 出口 BigInt —— uint64 用**无符号**方式生成。

用户可见效果：`object.get_instance_id()` 得到的 id 可以直接喂给 `instance_from_id()` 拿回原对象。
今天这对 RefCounted 对象**必然失败**（实测）。

## Background（2026-09-24 实机实测）

四份 `src/runtime/impl/{v8,quickjs,jsc,web}/jsb_*_helper.h` 的 `new_integer` **内容完全相同**，
阈值判断是单边的：

```cpp
if (const int32_t downscale = (int32_t)p_val; (int64_t)downscale == p_val) return v8::Int32::New(...);
#if JSB_WITH_BIGINT
if (p_val > JSB_MAX_SAFE_INTEGER) return v8::BigInt::New(isolate, p_val);   // ← 只判正向
#endif
return v8::Number::New(isolate, (double)p_val);                             // ← 负值全部落这里
```

实测（dll md5 `a78c33e6e8ae2f3ca5aad3f5665a5d00`，`binding_mode=shared`）：

| 引擎真值 | JS 拿到 | 位模式 | 丢信息 |
|---|---|---|---|
| `get_u64()` = 2^53 | `9007199254740992`（bigint） | `0x0020000000000000` | 否 |
| `get_u64()` = 2^63 | `-9223372036854776000`（number） | `0x8000000000000000` | 位对、值语义错 |
| `get_u64()` = 2^63+1 | `-9223372036854776000`（number） | `0x8000000000000000` | **是** |
| `get_u64()` = 2^64-1 | `-1`（number） | `0xffffffffffffffff` | 否（巧合） |
| `get_64()` = INT64_MIN+1 | `-9223372036854776000`（number） | `0x8000000000000000` | **是（int64 同害）** |

**不是 uint64 专属**：所有「负且幅值 > 2^53」的值都被舍入，int64 同样受害
（api json：int64 返回 132 个 / 参数 366 个）。

用户可见后果（实测）：
```
Resource.get_instance_id(): typeof=number bits=0x8000000689000400
真值（to_string 的 itos）      :       0x80000006890005ec
instance_from_id(roundtrip) same=false  valid=false
```
根因：RefCounted 的 ObjectID bit63 承载 `is_ref_counted`
（`core/object/object.h:882-886` `OBJECTDB_REFERENCE_BIT = 1 << 63`），有符号视角必然为负。

现有规避：`project/tests/cross-environment/test-cross-environment.ts:434-441` 已写明
「RefCounted ObjectIDs set bit 63 ... never use that number as an ObjectDB key」并改用 `weakref()`。

## Requirements

- **R1.1** 新增引擎无关转换实现 `src/runtime/impl/jsb_primitive_conv.h`，提供
  `to_int64` / `to_uint64` / `to_double` / `to_bool` / `new_integer` / `new_unsigned_integer`
  （签名见父 `design.md` §3.1 A-1）。
  四个 `impl/*/jsb_*_helper.h` 的 `to_int64` / `new_integer` 改为委托，删除四份重复体。
  **本任务负责固定该头的完整签名**（含 `to_double` / `to_bool`），因为 B 与 D 都要消费它：
  B 用 `to_uint64`，D 用 `to_int64` / `to_double` / `to_bool`。D **不得**另起一份。
- **R1.2** 出口判据改为**双边**：`v > JSB_MAX_SAFE_INTEGER || v < -JSB_MAX_SAFE_INTEGER`。
  `JSB_MAX_SAFE_INTEGER` 的值不动（`src/jsb.config.h:160` 标注 DO NOT CHANGE）。
- **R1.3** 每引擎补 `uint64_t BigInt::Uint64Value(bool*)`。v8/node 无需改动
  （v8 直接吃官方头，node 复用 v8 helper）；需补的是 quickjs-ng / jsc / web。
- **R1.4** 静态腿返回值按 meta 出口：`src/static_binding/thunks/thunks_common.h` 的 `Ret<T>`
  在 `std::is_same_v<type, uint64_t>` 时走 `internal::translate_uint64_return` → `new_unsigned_integer`。
  **不改 codegen** —— `Ret<uint64_t>` 与 `Ret<int64_t>` 已是不同模板实例。
- **R1.5** 动态腿把 meta 接进 `TypeConvert`：新增带
  `GDExtensionClassMethodArgumentMetadata` 的重载，并在
  `jsb_object_bindings.cpp` 的 `:483` / `:511` 调用点传入
  `get_argument_metadata(index)` / `get_return_metadata()`。
- **R1.6** 窄整数（int8/16/32、uint8/16/32、char32）的出口行为不变。
- **R1.7** 新增 **C++ doctest** 与 **TS 集成测试**覆盖以上行为（见 AC1.7 / AC1.8）。

## Acceptance Criteria

- [ ] **AC1.1** `Resource.get_instance_id()` 与 `Node.get_instance_id()` 在
      `binding_mode ∈ {static, shared, dynamic}` 三腿下，
      `instance_from_id(get_instance_id()) === obj` 成立，`is_instance_id_valid()` 为 true。
- [ ] **AC1.2** `StreamPeer.get_64()` / `get_u64()` 回读 `INT64_MIN+1`、`2^63+1` 等
      「负且幅值 > 2^53」的值时，`typeof` 为 `bigint` 且位模式精确。
- [ ] **AC1.3** `|v| <= 2^53-1` 的返回值仍出 `Number`（不因本次改动变成 BigInt）；
      `Int32` 快路径（能塞进 int32 时出 `Int32`）不变。
- [ ] **AC1.4** uint64 返回的高位值以**无符号**形式出口：`get_instance_id()` 得
      `9223372064923059180n` 这类正数 BigInt，而不是负数。
- [ ] **AC1.5** 五个引擎编译通过；node 复用 v8 路径无需额外代码。
- [ ] **AC1.6** 父任务 AC8（全量 TS 集成测试）、AC9（C++ 测试）、AC10（bench 门禁）通过。
- [ ] **AC1.7** 新增 C++ 用例覆盖转换原语（`to_int64` / `new_integer` /
      `new_unsigned_integer` 的阈值与位模式），放在 `src/runtime/tests/test_jsb_int64_conv.h`，
      在 `src/runtime/tests/jsb_test_main.cpp` 登记；`--jsb-run-tests` exit 0 且无泄漏。
      形态参考 `src/runtime/tests/test_jsb_any_runtime.h`（`GodotJSScriptLanguageIniter` +
      `JSB_TESTS_EXECUTION_SCOPE`）。
- [ ] **AC1.8** 新增 TS 集成测试场景 `project/tests/int64/`（登记进 `project/tests/start.ts`
      的 `scenes` 列表），覆盖 AC1.1 / AC1.2 / AC1.3；断言一律经 `reportTestFailure`，
      守卫要断言数量，并做**负向验证**（人为削减一次确认 FAILED，再还原）。
      **不进 bench**（`misc/bench_matrix.py:96-97` 的 `invalid != 0` 会 FATAL）。

## Out of Scope

- 写方向（uint64 参数不抛异常、两腿一致）→ `09-24-uint64-bigint-arg`。
- 独立开关宏 → `09-24-uint64-bigint-switch`。
- 三个数值槽（`int` / `float` / `bool`）的接受面对齐引擎语义
  （`new Vector2i(2n, 3)`、`new Vector2(2n, 3n)`、`set_block_signals(1)` 今天被拒）
  → `09-24-uint64-bigint-ctor-operators`。本任务只提供它需要的转换原语。

## 交点（与其他子任务）

- `src/runtime/impl/jsb_primitive_conv.h`：本任务**创建**并固定签名，B / D 只调用。
- `src/static_binding/thunks/thunks_common.h`：本任务改 `Ret<T>`，D 改 `probe_vt` → **串行**。
- `src/runtime/tests/test_jsb_int64_conv.h`：本任务建头 + 登记，B / D 追加用例 → **串行**。
- `src/runtime/bridge/jsb_type_convert_direct.h`：B 改 `JSToGD<uint64_t>`，D 改
  `JSToGD<float/double/bool>` → **串行**。
- `src/runtime/bridge/jsb_static_binding_util.h`：B 补 `StaticBindingUtil<uint64_t>`，
  D 补 `StaticBindingUtil<bool>` → **串行**。
- 结论：**A → (B → D)** 或 **A → (D → B)**，B 与 D 不可并行。

## Technical Notes

- 无符号出口没有现成先例可参照（`src/runtime/js_type_extension/` 是临时目录，用户将移除，
  不作为参照）。
- 三腿都要验证：`SConstruct:37` 默认 `binding_mode=shared`；`:858-860` 由该值决定
  `JSB_WITH_STATIC_BINDINGS` / `JSB_WITH_SHARED_THUNKS`。
- 改 `impl/*/jsb_*_helper.h` 会触发全量重编（单次 100~190s）——一次列全变体，每步只编一次。
- `instance_from_id` / `is_instance_id_valid` 在 api json 中**无 meta**（type 仅 `int`），
  引擎侧是 `int64_t` 参数（`core/variant/variant_utility.cpp:1124`），只能靠位模式正确。
