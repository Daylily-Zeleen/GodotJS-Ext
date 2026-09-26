# 静态绑定数值转换与 64 位返回契约收敛

> 父任务 `09-06-lowprio-uint64-bigint-codegen` 的子任务。
> 来源：2026-09-26 对本任务已完成部分（子任务 A/B/C/D + 窄槽对齐）的代码复审，逐条答疑后收敛出的改造单。

## Goal

消除上一轮 64 位 / 数值槽改造留下的三类欠账：

1. **冗余与不必要的抽象**：CI 的 `binding_mode` 多了一个与 scons 默认同义的 `default` 哨兵；
   `jsb_primitive_conv.h` 的 `ValueT` 模板参数没有实义（调用点恒为 `v8::Local<v8::Value>`）；
   该头的 `inline` 与四个 shim 的 `_FORCE_INLINE_` + `to_string` 风格不一致。
2. **静态绑定的静态性未吃满**：返回路径 `Ret<T>::translate_return` 为 `uint64_t` 留了一个
   `std::is_same_v` 特判（第三套机制），且**非 Variant 缓冲**也要绕一次 `Variant` 构造；
   入口已有 `JSToGD<T>`，出口却没有对称的 `GDToJS<T>`。
3. **64 位返回的类型契约未定义**：当前出口随值大小在 `number` / `bigint` 之间切换
   （`type uint64 = number /* || bigint */`），调用者必须先判类型；需要一个可选的
   "固定 bigint" 契约，并让 typings 别名随构建开关生成。

另有两处收尾：负 `Number` 进 `uint64` 槽时静默按位回绕（应加 debug 告警，release 零开销），
以及 `design.md` 里把该行为描述为"回绕扩表示范围"的措辞不实，需更正为"位模式回传 / 引擎对齐"。

## Background

上一轮的结论见父任务 `report.md` / `design.md` 与 `research/64bit-api-surface.md`。
与本任务相关的既有事实：

- `Ret<T>::encoded_type = PtrToArg<T>::EncodeT`；`MAKE_PTRARG(uint64_t)` 的 `EncodeT` 就是 `uint64_t`。
- 三处 thunk 的返回缓冲不同：`class_methods.h` 恒为 `godot::Variant`（`object_method_bind_call` 产出）；
  `builtin_methods.h` / `utility_functions.h` 为 `RetT::encoded_type`。
- 因此 `ReturnBufT == Variant` 分支**只**在 class 方法触发；对 `uint64_t` 而言
  模板里那个 `else`（`Variant(PtrToArg<type>::convert(...))`）永不命中。
- 64 位 `meta` **只**出现在 class methods（`class.property` / `signal` / `builtin` / `utility` 全为 0），
  实测见 `research/64bit-api-surface.md`。
- 引擎自身对数值槽只做 `static_cast` 截断，不校验宽度（窄槽对齐那轮的实测结论）。

## Requirements

### R1 机械清理批（行为不变）

- **R1.1 CI**：`workflow_dispatch` 的 `binding_mode` 去掉 `default` 选项，input 默认值改为 `shared`，
  守卫简化为 `inputs.binding_mode != ''`（含义：input 直接就是 scons 的取值）。
- **R1.2** `jsb_primitive_conv.h` 去掉 `template <typename ValueT>`，四个读取器改回定值签名
  `const v8::Local<v8::Value>`（调用点已证实恒为该类型）。
- **R1.3** `jsb_type_convert_direct.h` 里的 `(void)p_isolate; (void)p_context;` 改为项目惯例
  `jsb_unused(...)`（该头已可达 `jsb_macros.h`）。
- **R1.4** 本任务新增的文件不声明 `Contributors of GodotJS` 版权节；现存的两个新文件
  （`jsb_primitive_conv.h`、`test_jsb_int64_conv.h`）移除该节。**该规则写进 spec。**
- **R1.5** `StaticBindingUtil<...>::get/set` 加 `_FORCE_INLINE_`。
- **R1.6** 补回 `new_integer` 的 `JSB_LOG(VeryVerbose, "represented as bigint %d", ...)`。
  原文用的是 `%lld`，而 Godot 的 `String::sprintf` **不支持 `%lld`**（会让整行日志变空），
  必须写成 `%d` + `(int)`。

### R2 结构改造批（需重编 + 全腿验证）

- **R2.1** `jsb_primitive_conv.h` 移入 `jsb::impl::internal` 命名空间，改用 `_FORCE_INLINE_`，
  四个 shim 转发到 `jsb::impl::Helper`（与 `to_string` / `new_string` 保持同一风格）。
  前提：四个 shim 必须把该头的 `#include` **移到各自 pch 之后**
  （现在 v8 是 `31` vs pch `33`，quickjs/jsc/web 是 `29` vs pch `34`；pch 之前 `_FORCE_INLINE_` 未定义，已实测）。
- **R2.2** 新增 `GDToJS<T>`，与 `JSToGD<T>` 对称，**覆盖所有 Godot 自持类型**：主模板回退
  `gd_var_to_js`，对能在编码槽上直取的类型（标量、手写值类型）加特化以省掉 `Variant` 构造。
- **R2.3** `Ret<T>::translate_return` 收敛：删掉 `std::is_same_v<type, uint64_t>` 特判与
  `internal::translate_uint64_return`，统一走 `GDToJS<type>::convert(...)`；
  **非 Variant 缓冲路径不得再构造 `Variant`**。
- **R2.4** 出口三态宏（命名已定）：

  | `JSB_BIGINT_FOR_64BIT` | `JSB_64BIT_RETURN_FIXED_BIGINT` | 64 位返回值 |
  |---|---|---|
  | 0 | 无关 | 恒 `number`（有损，逃生口） |
  | 1 | 0（**默认**） | 值相关：≤`2^53-1` → `number`，否则 `bigint` |
  | 1 | 1 | 恒 `bigint` |

  非法组合（`FIXED_BIGINT=1 && FOR_64BIT=0`）用 `#error` 拒绝。`int64` 与 `uint64` 同步适用。
  **固定模式下连 `Int32` 快路径一并去掉**（已定）：否则小值仍出 `number`，该宏就失去意义。
- **R2.5** typings 类型别名**随构建开关生成**（不是运行时再判）。`kPredefinedLines` 是 C++ 字面量，
  可在生成时读宏。别名分工（已定）：
  - **入参**：`int64` / `uint64` 恒为 `number | bigint`（运行时本就两者都收）。
  - **返回值**：默认复用入参别名；`FIXED_BIGINT=1` 时改用 `int64_ret` / `uint64_ret`（= `bigint`）。

### R3 收尾

- **R3.1** 负 `Number` 进 `uint64` 槽：保留"按位回绕"语义（对齐引擎，拒绝会造成 static/dynamic 分叉），
  但在 `JSB_DEBUG` 下告警，release 零开销（与窄槽那套一致）。
- **R3.2** 更正 `design.md` §1.4 与相关注释：该行为的定位是"位模式回传 / 引擎对齐"，
  不是"用 53 位负值扩大 64 位表示范围"（后者在数学上不成立）。

## Acceptance Criteria

- [ ] **AC1**：CI `binding_mode` 无 `default` 选项，input 默认 `shared`，守卫为单条件；
      `gh workflow run ci.yml -f binding_mode=<static|shared|dynamic>` 三模式均能跑通且
      `Build (windows, …, v8)` 的 scons 命令行含对应 `binding_mode=<mode>`，web 腿不含。
- [ ] **AC2**：`jsb_primitive_conv.h` 无 `ValueT`；四个读取器签名为
      `(const v8::Local<v8::Value> p_val, ...)`（编译通过即证）。
- [ ] **AC3**：`jsb_type_convert_direct.h` 中不再出现 `(void)p_isolate` / `(void)p_context`。
- [ ] **AC4**：本任务新增文件不含 `Contributors of GodotJS`；`jsb_primitive_conv.h` 与
      `test_jsb_int64_conv.h` 已移除该节；spec 中存在该规则条目。
- [ ] **AC5**：`StaticBindingUtil` 的 `get/set` 带 `_FORCE_INLINE_`（含宏生成的 6 个窄整型特化）。
- [ ] **AC6**：`VeryVerbose` 日志回归，且**实测**在 debug 构建下打出该行（用一次 `new_integer`
      越阈调用触发）；日志行非空（证明未误用 `%lld`）。
- [ ] **AC7**：所有调用点走统一入口后，`Ret<T>::translate_return` 中不再有
      `std::is_same_v<type, uint64_t>` 与 `internal::translate_uint64_return`；
      非 Variant 缓冲路径无 `Variant` 临时对象（代码审查 + 编译期断言/审查确认）。
- [ ] **AC8**：`GDToJS<T>` 覆盖与 `JSToGD<T>` 对称的类型集合；未特化的类型经主模板回退且行为不变
      （全腿测试通过即证）。
- [ ] **AC9**：出口三态可构建且语义正确：
      `FOR_64BIT=0` → 64 位返回恒 `number`；
      `FOR_64BIT=1, FIXED_BIGINT=0` → 值相关（`≤INT32_MAX` 出 Int32，`≤2^53-1` 出 Number，否则 BigInt）；
      `FOR_64BIT=1, FIXED_BIGINT=1` → **恒 `bigint`，连 `Int32` 快路径也去掉**
      （实测 `get_process_frames()`/`get_instance_id()` 均得到 `typeof === "bigint"`）。
      非法组合报 `#error`。三种合法组合各有实测证据。
- [ ] **AC10**：typings 产物与当次构建开关一致（用生成物 grep 验证）：
      入参别名 `int64` / `uint64` **恒**为 `number | bigint`；
      `FIXED_BIGINT=1` 时存在 `int64_ret` / `uint64_ret` 且为 `bigint`，
      返回值位置使用 `*_ret`；`=0` 时不存在 `*_ret`。
- [ ] **AC11**：负 `Number` 进 `uint64` 槽在 `JSB_DEBUG` 构建下告警、在 release 下零开销
      （`strings <release dll> | grep -c <告警文案>` == 0，editor dll > 0）。
- [ ] **AC12**：`design.md` 措辞已更正，且不再出现"负值扩大表示范围"的说法。
- [ ] **AC13（总门禁）**：本地 5 腿全绿：v8 `static/shared/dynamic` + quickjs-ng `static/dynamic`
      （C++ 套件 SUCCESS、TS `GODOTJS_TEST_PROJECT_COMPLETED` 且 `INT64-DIAG checks=55`、无 FAILED）。
      三态宏的每种合法组合至少在一腿上有实测。
- [ ] **AC14**：父任务既有门禁不回归：`python misc/bench_matrix.py --report` 仍 `BENCH_RC=0`、`invalid=0`；
      CI `benchmark` job 仍全绿。

## Out of Scope

- `src/runtime/js_type_extension/string_ext.cpp` —— 用户明确该目录临时、将移除，不得参照或依赖。
- JS 侧行为（`JSON.stringify` 对 BigInt 抛错、`Map` 键 `1` 与 `1n` 不等、JS 算术混合抛错）：
  属用户自己的事，**不列为代价、不改**。
- `BigInt` 池化：已论证语义上不可实现（原始值不可变、生命周期不可观测、无 use_count），**不做**。
- 移动端 / web 的运行时行为：template-only，不跑运行时测试。

## Notes

- 数据源与逐条清单见 `research/64bit-api-surface.md`。
- 硬约束见 `AGENTS.md`：不改 `*.gen.*` / `*.def.*`（改生成逻辑）；临时文件放 `./.agent_tmp/`；
  禁止 `scons --clean`；文档中文、commit message 英文；不自行 commit/push（逐轮授权）。
- 验证成本提醒：改 `jsb_primitive_conv.h` 与四个 shim 的 include 顺序会触发**四引擎全量重编**
  （单次 100~190s）。R2 的每个变体只编一次，一次列全。
