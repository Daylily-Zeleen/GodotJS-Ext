# BigInt 开关语义收口（独立宏）

> 父任务：`09-06-lowprio-uint64-bigint-codegen`。共享技术方案见父任务 `design.md`。
> 本任务 = 父任务 R3。
> **依赖：`09-24-uint64-bigint-return`（读方向）与 `09-24-uint64-bigint-arg`（写方向）
> 都落地后才能开始** —— 本任务只做「把已有行为收进一个开关」，不实现转换逻辑本身。
> 与 `09-24-uint64-bigint-ctor-operators`（D）**无依赖关系**：D 是入口侧改动，不受本开关影响。

## Goal

新增一个**独立**的编译期开关，让使用者能选择 64 位整数是否以 BigInt 表示，同时保证：

- **开关只管「出来的值」**：开 = 超出 2^53-1 的返回值出口 BigInt；关 = 仍出 `Number`（接受丢位）。
- **「进去的值」两种模式都不抛异常** —— 传 number 永远能用，不因模式不同而报错。

用户诉求原话：BigInt 和 number 混用不方便，希望保留接口能力完整，不希望只用 number 时出现异常。

## Background

现状：`src/jsb.config.h:155` 有 `JSB_WITH_BIGINT`（当前 `1`），但它**不是可用的开关**：

1. 关掉之后出口退化成「一律 number」——即继续丢位（缺陷未修）；
2. 入口不认 BigInt；
3. **「uint64 参数抛异常」跟这个宏完全无关**，关不关都存在
   （根因是 `jsb_type_convert_direct.h:125-131` 的 `wide < 0` 早退，与 `JSB_WITH_BIGINT` 无关）。

所以用户决定：**新增独立宏**，与 `JSB_WITH_BIGINT` 解耦。

相关既有定义：
```c
// src/jsb.config.h:154-160
// use bigint if a value can not represented as Integer(Number)
#define JSB_WITH_BIGINT 1

// use `BigInt` if a value from godot greater than JSB_MAX_SAFE_INTEGER which can not represented as Integer(Number).
// used only if `JSB_WITH_BIGINT` is enabled.
// DO NOT CHANGE THIS VALUE.
#define JSB_MAX_SAFE_INTEGER (((int64_t)1 << 53) - 1) // 9007199254740991
```

## Requirements

- **R3.1** 在 `src/jsb.config.h` 新增独立宏（建议名 `JSB_BIGINT_FOR_64BIT`，最终名在本任务
  定稿），与 `JSB_WITH_BIGINT` 解耦。
- **R3.2** 语义定稿：
  - 开（默认，与现状兼容）：超出 2^53-1 的 64 位返回值出口 BigInt（uint64 走无符号）。
  - 关：超出 2^53-1 的返回值仍出 `Number`（接受丢位，与今天一致）。
  - **两种模式下**：入口（JS → Godot）都不抛异常。
- **R3.3** 梳理 `JSB_WITH_BIGINT` 的全部既有引用点，明确两个宏的关系与允许的组合，
  消除「四种组合里有未定义行为」的可能。至少明确：`JSB_BIGINT_FOR_64BIT=1` 依赖
  `JSB_WITH_BIGINT=1`。
- **R3.4** 在 `src/jsb.config.h` 注释里写明两个宏的区别、交互与默认值理由。
- **R3.5** 开关只作用于**出口表示**。不允许出现「关掉开关后传参又开始抛异常」的组合。
- **R3.6** 新增 TS 集成测试覆盖两种模式下的差异行为（见 AC3.7）。

## Acceptance Criteria

- [ ] **AC3.1** 宏开（默认）：父任务 AC1.1 / AC1.2 / AC1.4 仍全部成立。
- [ ] **AC3.2** 宏关：AC2.1 的「**不抛异常**」与「静态/动态写入字节相同」仍成立；
      AC1.1 允许失败（RefCounted ObjectID 往返不可用，与今天同），
      但**不得出现任何新异常**，且 `|v| <= 2^53-1` 的行为不变。
- [ ] **AC3.3** 宏关时，超出 2^53-1 的返回值仍出 `Number`（明确接受丢位，
      行为与本次改动前的今天一致）。
- [ ] **AC3.4** 两个宏的所有合法组合都能编译，无未定义行为；非法组合有编译期断言或
      配置期拒绝。
- [ ] **AC3.5** 两种模式各跑一次父任务 AC6（全量 TS 集成测试），均 exit 0 且含
      `GODOTJS_TEST_PROJECT_COMPLETED`。
- [ ] **AC3.6** `src/jsb.config.h` 的注释能让下一个人不查代码就答出「两个宏分别管什么」。
- [ ] **AC3.7** 新增 TS 集成测试场景（或扩展 `project/tests/int64/`）断言两种模式下的
      关键差异：开关关时超出 2^53-1 的返回值出 `Number`，但 `put_u64(2^63)` 仍不抛异常。
      测试需能**同时**在两种模式下通过（用编译期/运行期可见的模式标识分支），
      断言一律经 `reportTestFailure`。

## Out of Scope

- 转换逻辑本身（双边阈值、`Uint64Value`、`JSToGD<uint64_t>`、meta 传递）→ 父任务 A / B。
- 入口侧的数值槽接受面（`int`/`float`/`bool`）→ D。**本开关不得改变入口行为**
  （AC3.5 明确要求关掉开关后传参仍不抛异常）。
- 三个数值槽（`int`/`float`/`bool`）的接受面对齐引擎语义（`new Vector2i(2n, 3)`、
  `new Vector2(2n, 3n)`、`set_block_signals(1)` 今天被拒）→ `09-24-uint64-bigint-ctor-operators`。
  那是**入口**侧改动，本开关只管**出口**，两者互不影响。

## Technical Notes

- 用户已明确选择「新增独立宏」而非复用 `JSB_WITH_BIGINT`（2026-09-24 决策）。
- 本任务的验收**依赖** A/B 已完成；若 A/B 未落地，本任务只能写配置骨架，
  无法完成 AC3.1 / AC3.2 —— 不要在这种情况下声称完成。
- 改动面小但需要重编（`jsb.config.h` 是全局头）。两种模式各编一次，
  不要在中间为措辞反复编译。
