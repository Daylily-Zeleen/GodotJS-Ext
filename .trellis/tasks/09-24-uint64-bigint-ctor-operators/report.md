# 任务 D：数值槽对齐引擎语义（BigInt / number / null-undefined 接入构造器与运算符）— 执行记录

## 目标

三个无 meta 的数值槽（`int` / `float` / `bool`）的**转换器接受面**对齐引擎
`Variant::can_convert_strict`，使 BigInt 与 number（以及 bool 槽的 null/undefined）
能用于内置类型构造器与运算符，不再出现「引擎认为可以、本项目拒绝」或
「重载筛选通过但 marshal 拒绝」。

## 已完成的改动

| 文件 | 改动 |
|---|---|
| `src/static_binding/thunks/thunks_common.h` | `probe_vt` 增加 `IsBigInt() → Variant::INT` 分支（`JSB_WITH_BIGINT` 门控，与 `IsInt32` 同组；两个 `probe_prefer_*` 模式都经过该位置） |
| `src/runtime/bridge/jsb_type_convert_direct.h` | `JSToGD<bool>` 走 `Helper::to_bool`；`JSToGD<double>` / `<float>` 走 `Helper::to_double`；三者 + `JSToGD<int64_t>` 都经 `js_bool_as_number` 接受 boolean；删除已无用的 `JSB_DIRECT_SCALAR` 宏 |
| `src/runtime/bridge/jsb_type_convert.cpp` | typed `js_to_gd_var` 的 `BOOL` 分支改走 `to_bool`；`can_convert_strict<BOOL>` 放行 number / bigint / null / undefined |
| `src/runtime/bridge/jsb_static_binding_util.h` | `StaticBindingUtil<float>` / `<double>` / `<int32_t>` 改为委托 `JSToGD<T>`；补 `StaticBindingUtil<bool>` |
| `src/static_binding/thunks/builtin_operators.h` | `R == int64_t` / `double` / `bool` 三个分支改走 `to_int64` / `to_double` / `to_bool`，不再裸 `As<>` |
| `src/static_binding/thunks/type_compatible.h` | 审计表 `BOOL` / `INT` / `FLOAT` 三行重写（补 `NIL`，说明 `BIGINT` 经 `probe_vt` 归入 `INT`），并写明三行与编组器接受面的一致性依据 |
| `src/runtime/tests/test_jsb_int64_conv.h` | 追加两个用例：`probe_vt` 映射 BigInt；`JSToGD<float/double/bool/int64_t>` 的接受面 + `StaticBindingUtil` + `can_convert_strict<BOOL>` |
| `project/tests/numeric/`（新建） | TS 场景 `test-numeric.ts` + `Numeric.tscn` |
| `project/tests/start.ts` | 登记 `Numeric.tscn` |

## 实施中发现并修掉的真实缺陷

### 1. reflect 构造器路径的 `StaticBindingUtil<int32_t>` 仍是裸 `As<>`（dynamic 腿抓出）

只改 `probe_vt` + `JSToGD` 不够：`Vector2i` 的 `int64` 分量在 **dynamic 腿**走的是
`ReflectConstructorCall` → `StaticBindingUtil<int32_t>`，而它当时是

```cpp
if (p_input->IsNumber()) { r_value = (int32_t)p_input.As<v8::Int32>()->Value(); ... }
```

`As<v8::Int32>()` 是**纯 handle 重解释**，且 `IsNumber()` 把 BigInt 挡在门外 ——
实测 dynamic 腿 `new Vector2i(2n, 3)` 报 `bad param at 0`
（`jsb_reflect_binding_util.h:478`）。改为委托 `JSToGD<int32_t>` 后两腿一致。

### 2. `Vector2 × bool` 不是引擎运算符（我的测试假设错误，非代码缺陷）

初版 TS 断言了 `OP_MULTIPLY(true)`，实测抛
`bad operation between Vector2 and bool`。这**不是**转换缺陷：引擎没有注册
`Vector2 × bool` 的求值器，`probe_vt(true)` 得 `BOOL` 后查表未命中 → 动态回退 → 按引擎语义拒绝。
已从测试中移除该假设并注明原因（AC4.4 要断言的「不错误解释」由 `OP_MULTIPLY(2n)` 覆盖）。

## 验证证据（三腿全绿，实测）

`tsc` 重编 + 三腿各自 `scons` 构建 + C++ 套件 + TS 套件：

| 腿 | C++ runtime | C++ editor | TS 套件 |
|---|---|---|---|
| static | 58 用例 / 802 断言 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`NUMERIC-DIAG checks=21 expected=21`，`INT64-DIAG checks=49 expected=49` |
| shared | 58 / 802 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`checks=21 expected=21`，`49 expected=49` |
| dynamic | 57 / 790 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`checks=21 expected=21`，`49 expected=49` |

dynamic 的 C++ 用例数少 1（57 vs 58）是**预期**：`probe_vt` 用例受
`JSB_WITH_STATIC_BINDINGS` 门控，dynamic 腿不编入该层。

### 逐条核对 PRD 的期望值表

| 断言 | 改动前 | 改动后（实测） |
|---|---|---|
| `new Vector2i(2n, 3)`（int 槽） | 拒绝 `no suitable constructor` | 成功，x=2 y=3 ✓ |
| `new Vector2(2n, 3n)`（float 槽） | 拒绝 | 成功，x=2 y=3 ✓ |
| `OP_MULTIPLY(2n)` | 成功（动态回退） | 成功，x=2 y=4（静态路径）✓ |
| `set_block_signals(1)` / `(0)` / `(1n)` / `(0n)` / `(null)` / `(undefined)` | THREW | 全部成功，`is_blocking_signals()` 读回 true/false/false/... ✓ |
| `set_block_signals("")` | THREW | THREW（字符串仍拒，未变）✓ |
| `String.strip_edges("  x", undefined)`（有默认值） | `"x"`（默认值） | `"x"`（**未变**）✓ |
| `Projection.create_depth_correction(undefined)`（无默认值） | THREW | 成功（undefined → false）✓ |
| `new Vector2i(2, 3)` / `OP_MULTIPLY(2)` | 成功 | 成功（未回退）✓ |

## 负向验证（TS 守卫）

`EXPECTED_CHECKS` 21 → 22：`COMPLETED=0 FAILED=1`，
日志 `GODOTJS_TEST_PROJECT_FAILED: numeric coverage` + `21 checks ran, expected exactly 22`；
还原后 `checks=21 expected=21` 恢复绿。

C++ 侧无需人为削减：dynamic 腿的 `bad param at 0`（`StaticBindingUtil<int32_t>` 裸 `As<>`）
是**真实失败**，比人为削减更强。

## 遗留 / 待办

- AC4.9：父任务 AC8 / AC9 / AC10 —— 收尾时跑 bench 门禁确认无回归。
- `StaticBindingUtil` 缺的其余特化（`uint64_t` / 窄整型 / `char32_t`）：按 design §7
  记录到 spec，不在本轮改。本轮已顺带修 `int32_t`（它是 dynamic 腿 BigInt 构造器的必经路径）。

## 补充（窄槽对齐引擎：拒绝 → 截断）

### 判定依据

用户规则是「引擎有检查就以动态腿为准，没有就以静态腿为准」。此前静态腿对
`int8/uint8/...` 越界是**拒绝**。用纯 GDScript 探针（`.agent_tmp/gdprobe/probe*.gd`）
实测引擎行为：

| 调用 | 引擎结果 |
|---|---|
| `PackedByteArray.put_8(300)` | OK，`data=[44]` |
| `put_8(-129)` | OK，`data=[127]` |
| `put_8(2^40)` | OK，`data=[0]` |
| `put_u16(70000)` | OK，`data=[112,17]` |
| `put_8(1e300)` | OK，`data=[0]` |
| `Vector2i(3000000000, -3000000000)` | `(-1294967296, 1294967296)` |
| `put_8("abc")` / `put_8(null)` | **报错**（类型类别不匹配） |

源码结论（`core/variant/binder_common.h:58-72`）：DEBUG 下只走
`VariantCasterAndValidate<T>::cast` → `Variant::can_convert_strict`，而它只看
**Variant 类型类别**（拿不到宽度）；release 下直接 `VariantCaster<T>::cast` →
`Variant::_to_int<int8_t>()` → `T(_data._int)`，即 `static_cast`。
**结论：引擎从不校验宽度，只截断。**

### 改动

| 文件 | 改动 |
|---|---|
| `src/runtime/bridge/jsb_type_convert_direct.h` | `js_to_fixed_width_int` 去掉范围拒绝，改为截断；DEBUG 下越界告警 |
| `src/runtime/bridge/jsb_type_convert.cpp` | 新增 DEBUG-only `verify_narrow_int_slot(meta, val)`，`js_to_gd_var` 的 INT 分支调用（一处覆盖 class 方法 / utility / setter-ctor 三条路径） |
| `src/runtime/bridge/jsb_static_binding_util.h` | 新增 `JSB_STATIC_BINDING_FIXED_INT` 宏 + `int8_t/int16_t/uint8_t/uint16_t/uint32_t/char32_t` 六个特化，均委托 `JSToGD<T>` |
| `src/static_binding/thunks/type_compatible.h` | `INT` 行改为「窄槽收窄为截断」并附实测数据 |
| `src/runtime/tests/test_jsb_int64_conv.h` | 窄槽断言改为截断（`uint8_t(256)→0`、`uint8_t(-1)→255`、`int8_t(128)→-128`、`int8_t(-129)→127`、`int8_t(300)→44`、`char32_t(-1)→UINT32_MAX`）+ BigInt 截断 + 非数值仍失败 |
| `project/tests/int64/test-int64.ts` | 新增 `narrow slots truncate like the engine` section（6 条 check），`EXPECTED_CHECKS` → `BIGINT_MODE ? 55 : 50` |

### 告警策略

告警用 `#if JSB_DEBUG` 显式门控 + 条件 `if`，**release 零开销**。实测：

```
strings bin/windows/godotjs-ext.windows.template_release.x86_64.dll | grep -c 'narrow slot'  ->  0
strings bin/windows/godotjs-ext.windows.editor.x86_64.dll           | grep -c 'narrow slot'  ->  2
```

editor（`JSB_DEBUG=1`）三腿各触发 5 次截断告警；dynamic 腿措辞来自统一入口
`js_to_gd_var`（`narrow slot argument: ... does not fit the declared N-bit slot`）。

### 坑：`String::sprintf` 不支持 `%lld`

Godot 的 `String::sprintf` **没有 `%lld`**，用了会让整行日志变空（极难排查）。
64 位值必须走 `%d` + `(int)` 转换（项目既有惯例）。两处告警已改。
