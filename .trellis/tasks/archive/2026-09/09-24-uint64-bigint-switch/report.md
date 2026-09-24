# 任务 C：BigInt 开关语义收口（独立宏）— 执行记录

## 目标

新增一个**独立**的编译期开关 `JSB_BIGINT_FOR_64BIT`，让使用者能选择 64 位整数
**离开 Godot** 时是否用 BigInt 表示，同时保证「进去的值」两种模式下都不抛异常。

## 已完成的改动

| 文件 | 改动 |
|---|---|
| `src/jsb.config.h` | 新增 `JSB_BIGINT_FOR_64BIT`（默认 `1`）；注释写明与 `JSB_WITH_BIGINT` 的分工；非法组合（新宏开而 `JSB_WITH_BIGINT` 关）用 `#error` 拒绝 |
| `src/runtime/impl/jsb_primitive_conv.h` | `new_integer` / `new_unsigned_integer` 的 BigInt 出口分支由 `JSB_WITH_BIGINT` 改门控为 `JSB_BIGINT_FOR_64BIT`（关掉时落到 `Number::New((double)v)`） |
| `src/runtime/bridge/jsb_bridge_module_loader.cpp` | 向 `godot-jsb` 模块导出 `BIGINT_FOR_64BIT`（与 `BINDING_MODE` 同一用途） |
| `scripts/typings/godot.minimal.d.ts`、`project/typings/godot.minimal.d.ts` | 声明 `BIGINT_FOR_64BIT: boolean` |
| `project/tests/int64/test-int64.ts` | 改为**模式感知**：`EXPECTED_CHECKS` 与 readback 断言按 `BIGINT_FOR_64BIT` 分支；write 段两模式相同（断言入口不随开关变化） |
| `src/runtime/tests/test_jsb_int64_conv.h` | 3 个涉及**出口**的用例改为按 `JSB_BIGINT_FOR_64BIT` 分支（阈值、位保留、`StaticBindingUtil<uint64_t>::set`、`gd_var_to_js`） |

## 语义（R3.2 / R3.5）

| 方向 | 开关开（默认） | 开关关 |
|---|---|---|
| 出口：`|v| > 2^53-1` | `BigInt`（uint64 无符号） | `Number`（接受丢位，= 改动前行为） |
| 出口：`|v| <= 2^53-1` | `Number`（能塞进 int32 先出 `Int32`） | 同左（未变） |
| 入口：`put_u64(v)` / 传 BigInt | 不抛异常 | **同样不抛异常**（门控在 `JSB_WITH_BIGINT`） |

## 验证证据（两种模式各三腿，实测）

| 模式 | 腿 | C++ 套件 | TS 套件 |
|---|---|---|---|
| 开（默认） | static | 58 用例 SUCCESS | `C=1 F=0`，`INT64-DIAG checks=49 expected=49 bigint=true` |
| 开 | shared | 58 用例 SUCCESS | `C=1 F=0`，`checks=49 expected=49 bigint=true` |
| 开 | dynamic | 57 用例 SUCCESS | `C=1 F=0`，`checks=49 expected=49 bigint=true` |
| 关 | static | 58 用例 SUCCESS | `C=1 F=0`，`INT64-DIAG checks=44 expected=44 bigint=false` |
| 关 | shared | 58 用例 SUCCESS | `C=1 F=0`，`checks=44 expected=44 bigint=false` |
| 关 | dynamic | 57 用例 SUCCESS | `C=1 F=0`，`checks=44 expected=44 bigint=false` |

两模式的目标数不同（49 / 44）是设计使然：开关关时 RefCounted ObjectID 的 5 条
无损断言（typeof / positive / bit63 / valid / round-trip / exact bits）不成立，
按 AC3.2 只断言表示形态（`typeof resId === "number"`）。
`NUMERIC-DIAG checks=21` 在两模式下**相同**（数值槽是入口侧，不受本开关影响）。

### 非法组合的负向验证（AC3.4）

用预处理器直接验证 `#error` 条件表达式：

- `JSB_BIGINT_FOR_64BIT=1` + `JSB_WITH_BIGINT=0` → 预处理器打印 `GUARD_FIRED_AS_EXPECTED`
- `JSB_BIGINT_FOR_64BIT=1` + `JSB_WITH_BIGINT=1` → 不触发（合法）

### 实施中发现并修掉的真实缺陷（开关关时暴露）

首轮 OFF 模式三腿 C++ 各 **2 个用例 FAILED** —— 是 A/B 时期写的 3 个用例
在出口断言上写死了 `IsBigInt()`（`new_integer` 阈值、位保留、`StaticBindingUtil<uint64_t>::set`、
`gd_var_to_js` 的双 meta 对照）。开关关时出口本就是 `Number`，这些断言失去依据。
已按 `JSB_BIGINT_FOR_64BIT` 分支重写：关时断言「是非负 `Number`」（即无符号视图仍在）。

这轮同时证明守卫是活的：**改动前面向 OFF 的断言会红**。

## 遗留 / 待办

- AC3.1：父任务 AC1.1 / AC1.2 / AC1.4 —— 由 ON 模式的三腿运行覆盖（`bigint=true` 且 `checks=49`）。
- AC3.5：两模式各跑一次全量 TS 集成测试（`exit 0` + `COMPLETED`）—— 上表 6 行即该验收。
- AC3.7：测试同时覆盖两模式 —— 由 `BIGINT_FOR_64BIT` 运行期分支实现，**同一份场景两次构建均绿**。
- AC3.6：`jsb.config.h` 注释已能独立回答「两个宏分别管什么」；同时沉淀到
  `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`「出口表示开关」一节。
