# 数值槽对齐引擎语义（BigInt / number / null-undefined 接入构造器与运算符）

> 父任务：`09-06-lowprio-uint64-bigint-codegen`。共享技术方案见父任务 `design.md` §3.4。
> 本任务 = 父任务 R4。
> **依赖**：需要 A（`09-24-uint64-bigint-return`）提供的 `Helper::to_int64`（`jsb_primitive_conv.h`）。
> ⚠ **与 A 在 `thunks_common.h` 上重叠**（A 改 `Ret<T>`，本任务改 `probe_vt`）——
> 两者对该文件的修改必须**串行**，或由同一人一次改完。

## Goal

三个数值槽（`int` / `float` / `bool`）的转换器接受面对齐引擎 `Variant::can_convert_strict` 的语义，
使 BigInt 与 number（以及 bool 槽的 null/undefined）都能用于内置类型构造器与运算符，
不再出现「引擎认为可以、本项目拒绝」或「筛选通过但 marshal 拒绝」。

用户可见效果：

```js
new Vector2i(2n, 3)          // 今天被拒 → 成功，等价 new Vector2i(2, 3)
new Vector2(2n, 3n)          // 今天被拒 → 成功，等价 new Vector2(2, 3)
v.OP_MULTIPLY(2n)            // 结果正确（今天靠动态回退）
obj.set_block_signals(1)     // 今天被拒 → 成功，等价 set_block_signals(true)
obj.set_block_signals(null)  // 今天被拒 → 成功，等价 set_block_signals(false)
// 读回：obj.is_blocking_signals()
```

> **范围修正记录（2026-09-24）**：本任务初稿只覆盖 `int` 槽，把 `float` 槽写成
> 「不承诺支持」、`bool` 槽写成「保持现状」。这是**划错范围** —— `int`/`float`/`bool`
> 都是无 meta 的数值槽，同属「构造器/运算符参数」。正确口径是「逐档核对转换器接受面」。
> 修正后三槽一并处理。

## Background（2026-09-24 实机实测）

### B-1. 真实分歧是「本项目 vs 引擎」

引擎 `Variant::can_convert_strict`（`core/variant/variant.cpp:575-604`）：
`BOOL` 接受 `INT`/`FLOAT`/`NIL`；`INT` 接受 `BOOL`/`FLOAT`/`NIL`；`FLOAT` 接受 `BOOL`/`INT`/`NIL`。
（三处的 `STRING` 都被注释掉。）

实测（**无默认值**的 `Projection.create_depth_correction(flip_y:bool)` 隔离真实转换）：

| 传入 | 本项目静态腿 | 引擎语义 |
|---|---|---|
| `true` / `false` | OK | OK |
| `undefined` / `null` | **THREW** | 应接受（NIL） |
| `1` / `0` | **THREW** | 应接受（INT） |
| `1n` / `0n` | **THREW** | 应接受（INT） |
| `""` | THREW | 拒绝 ✓ 一致 |

> ⚠ **测 bool 参数必须选无默认值的位置**：有默认值时传 `undefined` 会被替换成默认值，
> 结果与「undefined 转布尔」无法区分（实测 `Rect2.intersects(b, include_borders=false)`
> 传 `undefined` 返回 `false`，那是默认值，不是转换结果）。

### B-2. 三个槽的规模（api json 实测）

| 槽 | 内置构造器参数 | 运算符右操作数 | class 方法参数 |
|---|---|---|---|
| `int` | 19 | 69 | 441（有 meta）+ 大量无 meta |
| `float` | 39 | 60 | 大量 |
| `bool` | 0（注册类内） | 20 | **1908** |

### B-3. `probe_vt` 缺 BigInt 分支

`probe_vt`（`src/static_binding/thunks/thunks_common.h:342-371`）**没有 BigInt 分支**：
```cpp
if (val->IsInt32()) return Variant::INT;
if (val->IsNumber()) return Variant::FLOAT;
if (val->IsBoolean()) return Variant::BOOL;
if (val->IsString()) return Variant::STRING;
// ... 没有 IsBigInt
return Variant::VARIANT_MAX;
```
BigInt 落到 `VARIANT_MAX` → `can_be_converted_from<INT>(VARIANT_MAX)` 为 false →
构造器重载筛选失败。

实测：

| 你写 | 结果 |
|---|---|
| `new Vector2i(2n, 3)`（int 槽） | **拒绝**：`no suitable constructor for Vector2i (received 2 arg(s): (unknown), int)` |
| `new Vector2(2n, 3n)`（float 槽） | **拒绝**（同上） |
| `new Vector2i(2, 3)` / `new Vector2(2, 3)` | 成功 |
| `new Vector2(1,2).OP_MULTIPLY(2n)` | 成功，结果正确（x=2 y=4）—— 走的是动态回退 |
| `new Vector2(1,2).OP_MULTIPLY(2)` | 成功，x=2 y=4 |

### B-4. `StaticBindingUtil` 缺特化

只有 5 个特化（`Object *` / `float` / `double` / `int64_t` / `int32_t`），
缺 `bool` / `uint64_t` / `uint8_t` 等窄整型 / `char32_t`。缺的走主模板
（`jsb_static_binding_util.h:38-49`）→ `js_to_gd_var` 带类型 → 接受面可能与 `JSToGD<T>` 不一致。
本任务只补 `bool`。

### 为什么必须三处同批改

1. **`probe_vt`** 加 BigInt 分支后，BigInt 会被归为 `Variant::INT`；
2. `operator_dispatch_binary`（`builtin_operators.h:204-215`）拿到 `INT` 就会选中
   `operator_thunk<..., int64_t, ...>`；
3. 该 thunk 的 `R == int64_t` 分支是**裸 `As<>`**：
   ```cpp
   right_slot = (int64_t)info[0].As<v8::Int32>()->Value();
   ```
   各引擎 shim 的 `As<S>()` 是**纯类型重解释**（v8 是 `Local<S>::Cast`，quickjs/jsc/web 是
   `Local<S>(data_)`），**不做运行时类型检查**。BigInt 的 handle 被按 `Int32` 读
   → **错误解释（读越界/垃圾值）**。

所以：只加 `probe_vt` 分支 = 引入新缺陷。三处必须同批。

## Requirements

**int 槽**

- **R4.1** `probe_vt` 增加 `IsBigInt() → Variant::INT` 分支，位置与 `IsInt32` 同组
  （`IsNumber()` 之后、`IsNullOrUndefined()` 之前），受 `JSB_WITH_BIGINT` 门控。
  两个 `probe_prefer_*` 模式都要经过这个位置。

**float / double 槽**

- **R4.2** `JSToGD<float>` / `JSToGD<double>` 加 BigInt 分支，走各引擎「转数字」原语
  （v8 `Value::NumberValue` / quickjs `Value::NumberValue` / jsc `JSValueToNumber` /
  web `jsbi_NumberValue`），**按 `Number()` 语义转 double**（用户已定）。
  `> 2^53` 的 BigInt 转 double 丢低位是 double 槽固有精度，不额外报错。
- **R4.3** `StaticBindingUtil<float>` / `<double>` 同步（动态腿 `ReflectConstructorCall` 走这条）。

**bool 槽**

- **R4.4** 对齐引擎语义，放行 **number + bigint + null/undefined**（即引擎的
  `INT`/`FLOAT`/`NIL`），字符串仍拒。真值映射：`0`/`0n`/`null`/`undefined` → false，
  其余 number/bigint → true。改动四处：
  `JSToGD<bool>`、`can_convert_strict<BOOL>`（`jsb_type_convert.cpp:565-567`）、
  typed `js_to_gd_var` 的 `BOOL` 分支（`:200-206`）、`StaticBindingUtil<bool>`（补特化）。
- **R4.5** **默认值优先级不能破坏**：`undefined` 在**有默认值**的位置必须仍走默认值替换；
  只有**无默认值**的位置才走「转布尔」。

**运算符与审计表（必须同批）**

- **R4.6** `builtin_operators.h` 的 `R == int64_t` / `R == double` / `R == bool` 三个分支
  都改为走转换原语（`to_int64` / `to_double` / `to_bool`），**不再裸 `As<>`**。
  裸 `As<S>()` 是纯类型重解释（v8 `Local<S>::Cast`），不做运行时检查；
  R4.1 / R4.2 / R4.4 放行后这三个槽都会收到新类型，必须同批改。
- **R4.7** 同步更新 `type_compatible.h:38-66` 的审计表三行（`BOOL`/`INT`/`FLOAT`），
  补 `NIL` 与 `BIGINT` 源，并逐行核对编组器接受面。
  契约：「**谓词的接受面必须 ⊆ 对应 C++ 形参编组器的接受面**」。
- **R4.8** 新增 C++ doctest 与 TS 集成测试（见 Acceptance）。

## Acceptance Criteria

- [ ] **AC4.1** `new Vector2i(2n, 3)`（int 槽）在 `binding_mode ∈ {static, shared, dynamic}`
      三腿下都成功，结果与 `new Vector2i(2, 3)` 相同（逐成员比较）。
- [ ] **AC4.2** `new Vector2(2n, 3n)`（float 槽）三腿下都成功，结果与 `new Vector2(2, 3)` 相同。
- [ ] **AC4.3** `new Vector2(1,2).OP_MULTIPLY(2n)` 结果正确（x=2 y=4），与传 `2` 时相同，
      三腿一致。
- [ ] **AC4.4** 运算符的三个分支（int / double / bool 槽）**不得**出现错误解释：
      测试要断言结果的具体数值，而不是「没抛异常」。
- [ ] **AC4.5** bool 槽按引擎语义放行：`set_block_signals(1)` / `(0)` / `(1n)` / `(0n)` /
      `(null)` / `(undefined)` 都不再抛异常，且用 `is_blocking_signals()` 读回验证真值映射
      （`0`/`0n`/`null`/`undefined` → false，`1`/`1n` → true）。字符串仍拒。
- [ ] **AC4.5b** **有默认值**的 bool 位置传 `undefined` 仍走默认值替换；**无默认值**的位置
      传 `undefined` 走转布尔（不再抛异常）。两条通道都测。

      ⚠ **对照组必须用默认为 `true` 的 bool 参数**，否则无法区分「默认值替换」与「真值转换」：
      `false` 既是默认值又是 `undefined` 的真值，两条假设给出相同结果。
      - **有默认值（判别性对照组）**：`String.strip_edges(s, left=true, right=true)`
        —— `strip_edges("  x", undefined)` 得 `"x"` = 走了默认值 `true`；
        若是真值转换会得 `"  x"`（false）。**实测已确认得 `"x"`**。
      - **无默认值（转换组）**：`Object.set_block_signals(enable)` ——
        `set_block_signals(undefined)` 今天 THREW，改动后应成功且 `is_blocking_signals()`
        为 false（undefined 的真值）。也可用 `Projection.create_depth_correction(flip_y)`
        （内置类型，不受 profile 裁剪）。
- [ ] **AC4.6** `type_compatible.h` 审计表三行（`BOOL`/`INT`/`FLOAT`）已更新，
      含 `NIL` 与 `BIGINT` 源，且与编组器接受面逐行核对通过。
- [ ] **AC4.7** 新增 C++ doctest 用例覆盖 `probe_vt(BigInt) == Variant::INT`、
      `JSToGD<float/double/bool>` 的接受面（含 null/undefined），
      在 `src/runtime/tests/jsb_test_main.cpp` 登记，`--jsb-run-tests` exit 0。
- [ ] **AC4.8** 新增 TS 集成测试场景覆盖 AC4.1 - AC4.6，登记进 `start.ts`，
      经 `reportTestFailure` 报错，并做负向验证。
- [ ] **AC4.9** 父任务 AC8（全量 TS 集成测试）、AC9（C++ 测试）、AC10（bench 门禁）通过。

## Out of Scope

- 读方向返回值（→ A）、uint64 参数（→ B）、独立宏（→ C）。
- `StaticBindingUtil` 缺的**其余**特化（`uint64_t` / `uint8_t` 等窄整型 / `char32_t`）：
  接受面可能与 `JSToGD<T>` 不一致，记录到 spec，不在本轮改。
- 字符串槽收数字：引擎的 `BOOL`/`INT`/`FLOAT` 接受面里 `STRING` 被注释掉，本项目一致拒绝。
- 运算符的 `Variant` 右操作数行（`NIL` 派发）—— 不受本次改动影响。

## Technical Notes

- `probe_vt` 有 `probe_prefer_primitive_types` / `probe_prefer_object_types` 两个模式，
  BigInt 分支应放在**两个模式都经过**的位置（`IsInt32`/`IsNumber` 那一组）。
- `find_ctor_*` 的筛选顺序由 codegen 决定（`misc/build/static_binding_codegen.py`）；
  本任务**不改 codegen**，只让 `probe_vt` 的结果变正确。
  生成物实证：`dispatch_builtin.gen.cpp:5532` 的 `probe_vt<probe_prefer_primitive_types>(info[i])`。
- 运算符静态表：`find_op_Vector2_MULTIPLY`（`dispatch_builtin.gen.cpp:3852-3864`）
  对 `Variant::INT` 返回 `operator_thunk<OP_MULTIPLY, godot::Vector2, int64_t, godot::Vector2>`。
- 探针用法与已知坑见 `../09-06-lowprio-uint64-bigint-codegen/research/README.md`
  （含「测 bool 必须选无默认值位置」「构建 profile 裁剪会让类不可用」两条）。
- 本任务涉及的文件与 A 在 `thunks_common.h` 上重叠（A 改 `Ret<T>`，本任务改 `probe_vt`），
  必须串行；与 A/B 在 C++ 测试头上也重叠（都往 `test_jsb_int64_conv.h` 加用例）。
