# 任务 B：uint64 参数不抛异常、两腿一致 — 执行记录

## 目标

uint64 参数在静态绑定下不再抛异常，且静态/动态两腿对同一输入写入**完全相同的字节**。
今天：同一逻辑操作，方法调用抛异常、属性赋值正常。

## 已完成的改动

| 文件 | 改动 |
|---|---|
| `src/runtime/bridge/jsb_type_convert_direct.h` | 新增 `template <> struct JSToGD<uint64_t>`（走 `Helper::to_uint64`）；从 `JSB_DIRECT_FIXED_INT` 移除 `uint64_t`；删除 `js_to_fixed_width_int` 的 `uint64_t` 分支与那句恒假 max 比较；注释说明窄整型为何仍保留范围检查 |
| `src/runtime/bridge/jsb_static_binding_util.h` | 补 `StaticBindingUtil<uint64_t>`（`get` → `to_uint64`，`set` → `new_unsigned_integer`） |
| `src/runtime/impl/jsb_primitive_conv.h` | **修 `to_uint64` 的 Number 分支 UB**（见下） |
| `src/runtime/bridge/jsb_type_convert.{h,cpp}` | `js_to_gd_var` 补上参数方向 meta 重载（`INT_IS_UINT64` → `to_uint64`） |
| `src/runtime/bridge/jsb_object_bindings.cpp` | `_godot_object_method` 参数、`_godot_object_set2` 补 meta |
| `src/runtime/tests/test_jsb_int64_conv.h` | 新增用例 `JSToGD<uint64_t> writes high-bit values instead of rejecting them`（含窄槽仍拒绝越界） |
| `project/tests/int64/test-int64.ts` | 新增 `u64 write` section |

## 实施中发现并修掉的真实缺陷

### 1. `to_uint64` 的 Number 分支有未定义行为（新测试直接抓出来的）

初版实现（A 任务落地时写的）是：

```cpp
r_val = (uint64_t)(int64_t)p_val.As<v8::Number>()->Value();
```

对 `[2^63, 2^64)` 区间的 double，`(int64_t)` 转换是 **UB**。x86-64 的 `cvttsd2si`
在该区间返回 INT64_MIN 哨兵，于是：

| 输入 | 期望 | 实测（修复前） |
|---|---|---|
| `put_u64(1e19)` | `0x8ac7230489e80000` | **`0x8000000000000000`** |

动态腿用的是直接的无符号转换，所以这条只在静态腿出现 —— 正好是「两腿不一致」的原始症状。
TS 新加的 `write 1e19 static bits` 断言第一次运行就 **FAILED**（`0x8000000000000000`），
即该守卫是活的。

修法：按区间分开处理，且区间外**拒绝**而不是产生 UB：

```cpp
if (v >= 0.0 && v < 18446744073709551616.0) r_val = (uint64_t)v;        // [0, 2^64)
else if (v < 0.0 && v >= -9223372036854775808.0) r_val = (uint64_t)(int64_t)v;  // 负数回绕
else return false;                                                       // 区间外无表示
```

### 2. 参数方向漏传 meta

初版只给返回方向补了 meta（`gd_var_to_js`），参数方向仍走无 meta 的 `js_to_gd_var`。
dynamic 腿因此把 `put_u64(1e19)` 写成 `0x8000000000000000` 而不是 `0x8ac7230489e80000`。

两个方向的 Variant INT 槽确实存同样的 64 位，**但取位方式不同**：从 `[2^63, 2^64)` 的 double
取位时 `(int64_t)` 是 UB，x86 返回 INT64_MIN 哨兵。修法：`js_to_gd_var` 新增带 meta 的重载，
`INT_IS_UINT64` 分支走 `Helper::to_uint64` 再写回有符号槽（位模式不变）。
调用点补 meta：`_godot_object_method`（参数）、`_godot_object_set2`（setter 参数）。

## 验证证据（三腿全绿，实测）

`tsc` 重编 + 三腿各自 `scons` 构建 + C++ 套件 + TS 套件：

| 腿 | C++ runtime | C++ editor | TS 套件 |
|---|---|---|---|
| static | 56 用例 / 730 断言 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`checks=49 expected=49` |
| shared | 56 / 730 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`checks=49 expected=49` |
| dynamic | 56 / 730 / SUCCESS | 3 / 12 / SUCCESS | `COMPLETED=1 FAILED=0`，`checks=49 expected=49` |

三腿 TS 断言数完全一致 → AC2.1 的「静态与动态写入完全相同」在字节层面成立
（`write <case> legs agree` 逐例比较）。

### 负向验证

- **C++ 侧**：无需人为削减 —— `to_uint64` 的 UB 缺陷让 `JSToGD<uint64_t>` 用例在**修复前真实失败**
  （`0x8000000000000000` vs 期望 `0x8ac7230489e80000`），比人为削减更强。
- **TS 侧**：`EXPECTED_CHECKS` 49 → 50 一次 → `COMPLETED=0 FAILED=1`，
  日志 `int64 coverage` + `49 checks ran, expected exactly 50`；还原后 `checks=49 expected=49` 恢复绿。

## 遗留（本轮不改，已记录）

- **窄槽在 dynamic 腿不做范围检查**：`put_8(300)` / `put_u16(-1)` 在 static 腿抛异常，
  在 dynamic 腿静默截断。这是**既有分歧**（`js_to_fixed_width_int` 只作用于静态直连路径；
  reflect 路径的 `js_to_gd_var` INT 分支从不按窄 meta 检查），非本轮引入。
  AC2.4 的「保持**现有**拒绝行为」在两腿本来就是两种行为，故 TS 侧不断言跨腿不变式；
  窄槽拒绝已由 C++ `JSToGD<int8_t> / <uint8_t> / <char32_t>` 用例权威覆盖（静态侧）。
- AC2.5：五引擎编译 —— 本机只有 windows+v8；其余由 CI 覆盖。写方向是引擎无关的 C++ 逻辑，
  五引擎共用同一份改动。
- AC2.6：父任务 AC8/AC9/AC10 —— A 任务已跑过 bench 门禁（24 轮 / invalid=0）；
  B 未改出口路径，但最终收尾时会重跑一次确认无回归。
