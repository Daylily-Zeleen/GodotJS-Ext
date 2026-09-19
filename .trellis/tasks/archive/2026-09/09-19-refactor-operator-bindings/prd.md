# 重构运算符的绑定与代码生成（静态函数 → 成员函数）

## Goal

把内置类型的 JS 运算符从**静态函数**形式 `Vector2.OP_ADD(a, b)` 改为**成员函数**形式 `a.OP_ADD(b)`（`this` 即左操作数），把**声明代码生成**与**两条绑定腿**一并改造，并把 `TypeDB::load_primitive_types` 的硬编码类型表换成与运行时同源的 `def.h` 单源。

**函数名保持不变**：`OP_ADD`/`OP_EQUAL`/`OP_NEGATE`…（`JSB_OPERATOR_NAME` 现状）。只改**接收者位置**（首参 → `this`）与**挂载面**（static → instance）。不引入任何新命名、不做重命名、不留 `OP_*` 静态别名。

用户价值：运算符调用不必重复写类型名（`vec.OP_ADD(other)` 而非 `Vector2.OP_ADD(vec, other)`）；同时消除类型清单双份维护（运行时 `jsb_primitive_types.def.h` vs 编辑器 `kPrimitiveTypes[]`）。

## Background（已核实事实）

### 三条绑定/生成路径（全量清点，无第四条）

| 路径 | 文件 | 现状 |
|---|---|---|
| 运行时·动态腿 | `src/runtime/bridge/jsb_primitive_bindings.cpp:60-74,111-176` | `BinaryOperator::invoke` / `UnaryOperator::invoke`，data = `int32_t` op code，走 `Variant::evaluate` |
| 运行时·静态腿 | 同上 `:45-59`；thunks 在 `src/static_binding/thunks/builtin_operators.h` | `operator_dispatch_binary<Variant::OP_x, CurrentType, &find_op_<Lit>_<Op>>` / `operator_unary_thunk<...>` |
| 编辑器 d.ts | `src/editor/codegen/jsb_codegen_writer.cpp:526-536`（数据源 `jsb_codegen_type_db.cpp:440-448`） | 输出 `static OP_ADD(left: Vector2, right: Vector2): Vector2` |

两腿共用同一份宏调用声明：`src/runtime/internal/jsb_primitive_operators.def.gen.h`（由 `misc/build/generate_primitive_operators.py` 从 godot-cpp api json 生成，经 `SConstruct:807-836` 无条件生成）。

宏消费点：`jsb_primitive_bindings.cpp:97-107`（`JSB_TYPE_BEGIN/END`）、`:183-185`（`#include` + `Number`→`double` 垫片）；静态腿二元表 `find_op_<Left>_<Op>` 由 `misc/build/static_binding_codegen.py:emit_operator_pair_tables`（`:1270`）发射，声明进 `builtin_operator_tables.gen.h`（`dispatch.h:52` 在 `jsb::static_binding` 内 include），定义进 `dispatch_builtin.gen.cpp`。

### 规模（生成器实跑产物实测，4.7 api json）

- `jsb_primitive_types.def.h` 声明 **32 个挂载类**（String 另走 `reflect_bind_utilities`，见 `jsb_primitive_bindings.cpp:965-971`）。
- **JS 可见运算符方法 = 236 个（类 × 运算符名）**：一元 50 + 二元 186。取证：`generate_primitive_operators.py` 产出的 def.gen.h 去 String 后 32 个 `JSB_TYPE_BEGIN` 块（原 33 块含 String 的 10 个不挂载方法）。
- 静态腿 `find_op_*` 表 **186** 张、表内 `case` 标签合计 **344** 个可选 (左,op,右) 组合（`static_binding_codegen.py` 去 String 后产物）。
- json 在 32 个挂载类上声明 394 条重载行；与 344 的差异来自生成器刻意跳过：`==`/`!=` 的 NIL 行（thunk 短路覆盖，不发表行）、`and`/`or`/`xor` 的 NIL 行（JS 用原生 `&&`/`||`/`^`）、其余 `Variant` 右参行。
- 一元运算符（`unary-`/`unary+`/`not`/`~`）不进表，注册期直挂。
- `%` 的 NIL 行（`"fmt" % null`）**只存在于 String/StringName**，两者均不挂载运算符 → 对已挂载类型无活代码；R6 去 String 后静态腿不再产出该 case。thunk 内的 `R = godot::Variant` 处理保留为防御性逻辑。
- 运算符名 `OP_*` 与既有实例方法/成员**无冲突**（`OP_` 前缀天然保证唯一性）。

### 现状缺陷（item 2）

`jsb_codegen_type_db.cpp:285-344` 的 `load_primitive_types`：
- 定义 `JSB_CODEGEN_DEF` / `JSB_CODEGEN_DEF_UTIL` 两个宏，但**从未使用**（直接 `#undef`），实际数据来自硬编码 `kPrimitiveTypes[]`——33 条目手工维护名字 + `Variant::Type` + `utilities_mode` 三列。
- 运行时同源清单 `src/runtime/bridge/jsb_primitive_types.def.h`（32 个 `DEF(TypeName)`，String 单独以 `reflect_bind_utilities` 注册，见 `jsb_primitive_bindings.cpp:965-971`）内容等价却各写一份。

## Requirements

### R1 成员函数形态（两腿 + 代码生成一致）

R1.1 JS 可见形态：`left.OP_XXX(right)` 替代静态调用 `Vector2.OP_XXX(left, right)`；一元为无参成员 `value.OP_NEGATE()`。`this` 为左操作数，右操作数为实参 `info[0]`。
R1.2 **命名不变**：沿用 `JSB_OPERATOR_NAME(op_code)` = `"OP_" #op_code`（`src/internal/jsb_macros.h:38`）。不新增命名映射、不改宏、不引入 `api_tool` 侧的任何运算符名函数。
R1.3 旧静态形态**彻底移除**（无别名、无 shim）：`Vector2.OP_ADD`（静态）为 `undefined`。
R1.4 语义与现状完全一致：probe 右侧实参类型 → 选重载 → miss 回退 `Variant::evaluate`；`==`/`!=` 的 null/undefined 短路保留；`%` 的 NIL 行保留；注册期零查表不变。

### R2 三条改造面（动态腿 / 静态腿 / 编辑器 d.ts）

R2.1 动态腿：`BinaryOperator::invoke`/`UnaryOperator::invoke` 改从 `info.This()` 取左操作数，右操作数改取 `info[0]`；注册宏改 `class_builder.Instance()`。
R2.2 静态腿：`operator_thunk` 改从 `info.This()` 取左操作数（右操作数取 `info[0]`）；`operator_unary_thunk` 同；`operator_dispatch_binary` 改 probe `info.This()` 与 `info[0]`；`evaluate_dynamic_binary` 同步；`JSB_DEFINE_*` 宏改挂 `Instance()`。表函数签名 `ThunkFn f(godot::Variant::Type)` **不变**，`static_binding_codegen.py` 不动。
R2.3 编辑器 d.ts：`ClassWriter::operator_` 输出成员签名——一元 `OP_NEGATE(): Vector2`，二元 `OP_ADD(right: Vector2): Vector2`；不再输出 `static`，不再输出 `left` 形参。
R2.4 清理：`project/tests/benchmark/cases.builtin.ts` 的 23 条 operator case 改成员调用形态；`misc/bench_matrix.py` 的 `Vector2.OP_IN` 身份说明（本就与代码不符——代码用 `STATIC_BINDING_ENABLED` 常量）改述；spec `architecture-constraints.md` 的运算符双层分发章节同步。

### R3 TypeDB 原始类型清单单源化

`TypeDB::load_primitive_types` 删除 `kPrimitiveTypes[]` 及相关循环，改为与 `jsb_primitive_bindings.cpp:965-971` **同形**的形式：

```cpp
#pragma push_macro("DEF")
#undef DEF
#define DEF(TypeName) { ... _load_primitive_type(Variant::get_type_name(type_id), type_id, false); }
#include "jsb_primitive_types.def.h"
#pragma pop_macro("DEF")

// String 与 runtime 侧一致：单独以 utilities 模式注册
_load_primitive_type(Variant::get_type_name(Variant::STRING), Variant::STRING, true /* utilities_mode */);
```

- 直接用 `DEF` 宏名（形式对齐 runtime 侧 `register_primitive_bindings`），不保留 `JSB_CODEGEN_DEF` / `JSB_CODEGEN_DEF_UTIL` 这两个名字。
- 用 `#pragma push_macro("DEF")` / `pop_macro` 包裹，避免污染 TU 内其他内容。
- 运行时新增/删除 def.h 条目时编辑器侧自动跟随，无需双改。

### R4 构建与测试只用 quickjs-ng（本次任务约束）

R4.1 **本任务的构建与测试只使用 quickjs-ng**（`scons ... use_quickjs_ng=yes`），不构建、不测试 v8 / node / quickjs / JavaScriptCore 等其他 JS 运行时。
R4.2 "两腿"仍指绑定腿 `static_binding=yes` / `static_binding=no`；两腿**都在 quickjs-ng 下**验证。
R4.3 该约束写入本任务文档，作为实施与验收的适用范围；不修改 `SConstruct` 的引擎选择机制、不改动其他引擎的代码路径。

### R5 TS 集成测试新增运算符专项，覆盖全部已绑定运算符

R5.1 新增一个运算符专项 TS 集成测试场景（`project/tests/operators/`），挂进 `project/tests/start.ts` 的场景列表，随主测试流程跑（判据仍是 `GODOTJS_TEST_PROJECT_COMPLETED`）。
R5.2 **覆盖全部已绑定运算符**，不抽样。下表是**去 String 之前**的实测基线（生成器实跑产物：`generate_primitive_operators.py` 的 def.gen.h + `static_binding_codegen.py` 的 `dispatch_builtin.gen.cpp`，4.7 api json）；R6 落地后的覆盖目标见 R5.9：

| 维度 | 数量 | 取证 |
|---|---:|---|
| 挂载类（`jsb_primitive_types.def.h`） | 32 | def.h 32 条 `DEF`，String 另走 utilities |
| **JS 可见运算符方法（类 × 运算符名）** | **236**（一元 50 / 二元 186） | def.gen.h 去 String 后 32 块（原 33 块含 String 的 10 个不挂载方法） |
| 静态腿 `find_op_*` 表 | 186 | `dispatch_builtin.gen.cpp` 去 String 后 186 表 |
| **静态腿可选 (左,op,右) 组合** | **344** | 去 String 后 344 个 `case` 标签 |
| json 声明的重载行（32 类） | 394 | api json；含被生成器刻意跳过的行（见下） |

两类数字差异必须理解，否则会把"覆盖率不足"误判为缺陷：
- 394（json 行）> 344（静态腿 case）：`==`/`!=` 的 NIL 行由 thunk 短路覆盖不发表行、`and`/`or`/`xor` 的 NIL 行刻意跳过（JS 用原生 `&&`/`||`/`^`）、`Variant` 右参行不发表行（% 的 NIL 除外）。
- 动态腿无表，任何 (左,op,右) 组合都走 `Variant::evaluate` 兜底，故 344 是**静态腿**的可选组合数，不是全量可调用组合。

R5.3 每个绑定方法至少断言三件事：
1. **成员形态存在**：`typeof value.OP_X === "function"`；
2. **静态形态已移除**：`(Class as any).OP_X === undefined`；
3. **调用可用**：以一个合法操作数调用，不抛异常，且返回值的 Variant 型别与 api json 声明的 `return_type` 一致。

R5.4 二元运算符按**右参类型**逐个覆盖静态腿全部可选组合（每个表内每个 `case` 标签各一个代表操作数，去 String 后目标 344），而非只测每方法首个重载；覆盖表由 `dispatch_builtin.gen.cpp` 机械抽取（R6 落地后重抽）。
R5.5 语义正确性**双重取证**：
- **精确值断言**：对当前 bench 已覆盖的 23 条 + 各类型若干代表，断言具体取值（如 `new Vector2(1,2).OP_ADD(new Vector2(3,4))` 等于 `(4,6)`）；
- **不变量断言**：对其余全部方法，断言与引擎取值器无关的自洽不变量——`a.OP_EQUAL(a) === true`、`a.OP_NOT_EQUAL(a) === false`、比较类反对称（`a.OP_LESS(b)` 与 `b.OP_GREATER(a)` 同真值）、`a.OP_ADD(a)` 返回型别正确等。

R5.6 **边界与短路**：`OP_EQUAL(null)` / `OP_NOT_EQUAL(undefined)` 的短路语义。（注：原拟的 `"%s".OP_MODULE(null)` **不成立**——String 不挂载运算符，见 R6；已从验收项移除。）
R5.7 **完整性守卫**：测试内声明期望的覆盖集合（按类列出该类的运算符名集合与总数），运行时把"实际发现的成员方法集合"与之比对，任何一侧增减都报失败——防止 api json 升级或绑定面变化后测试静默漏测。
R5.8 测试表由**生成器产物**机械推导生成（一次性脚本产出数据表嵌入测试文件，脚本留在 `.agent_tmp/`，不入库），**不手写**以免人工遗漏。
R5.9 覆盖集合（去 String 后实测，下文 R6.6）：**236 个 JS 可见方法**（一元 50 + 二元 186）、**186 张静态腿表**、**344 个可选组合**。实施时以重新抽取的实际值为准。

### R6 去掉 String 的运算符生成（绑定与声明代码生成都用不上）

**已核实**：String 在两条生成器路径上都产出了**死代码**。

| 生成位置 | String 产物 | 为何死 |
|---|---|---|
| `generate_primitive_operators.py` → `jsb_primitive_operators.def.gen.h` | `JSB_TYPE_BEGIN(String)` 块：9 个二元 `JSB_DEFINE_OVERLOADED_BINARY_BEGIN` + 1 个 `JSB_DEFINE_UNARY(NOT)` | 该块展开出 `OperatorRegister<String>` 特化，而**只有 `reflect_bind` 调 `OperatorRegister<T>::generate()`**（`jsb_primitive_bindings.cpp:821`）；String 用的是 `reflect_bind_utilities`（`:971`），从不调用 |
| `static_binding_codegen.py` → `dispatch_builtin.gen.cpp` + `builtin_operator_tables.gen.h` | 9 张 `find_op_String_*` 表（57 个 `case` 标签），连带 **55 个 String 独有 `operator_thunk` 实例化**（全量 335 个中的 55 个） | 唯一引用者是上面那个从不展开的宏 `&find_op_String_MODULE`；外部链接符号仍占二进制体积 |
| `TypeDB`（d.ts） | 无 | 这侧**本来就正确**——String 走 `p_utilities_mode`，`_load_primitive_type` 提前 return 跳过 operators |

根因：两个生成器各自维护排除集且互相对齐（`static_binding_codegen.py:1289` 注释 "Keep this exclusion set aligned with generate_primitive_operators.py"），但**两边都漏了 String**——`JS_NATIVE_LEFT` 含 `bool/int/float/StringName`，`generate_primitive_operators.py:196` 的 skip 集合同样是这 4 个。

R6.1 `generate_primitive_operators.py`：skip 集合加入 `String`（其走 utilities 注册，不挂运算符）。
R6.2 `static_binding_codegen.py`：`JS_NATIVE_LEFT` 加入 `"String"`（与 R6.1 同步，保持两生成器排除集一致）。
R6.3 **两生成器的排除集合必须一致**：两处各加指向对方的交叉引用注释，后续引擎增删时同步。
R6.4 不修改 `jsb_primitive_types.def.h`（String 仍参与 primitive 绑定，只是不参与运算符生成）。
R6.5 `TypeDB` 侧无改动（本来就对）；但 R3 的单源化完成后顺带确认 String 仍走 utilities 模式。

R6.6 实测收益（生成器实跑，实施时已核对）：

| 维度 | 去 String 前 | 去 String 后 |
|---|---:|---:|
| `def.gen.h` 类型块 | 33 | **32** |
| JS 可见运算符方法 | 246（一元 51 / 二元 195） | **236（一元 50 / 二元 186）** |
| 静态腿 `find_op_*` 表 | 195 | **186** |
| 静态腿可选 `case` 组合 | 401 | **344** |
| `operator_thunk` 不同实例化 | 335 | **280** |

> **勘误**：本节原写"236 → 226 / 186 → 177 / 344 → 287"，是把 String 减了两次（R5 基线表的 236/186/344 **本就已剔除 String**）。上表为实跑实测值。

### R7 `cases.builtin.ts` 运算符相关调用形态同步

R7.1 `project/tests/benchmark/cases.builtin.ts` 中全部 operator case 的调用形态改为成员形式（原静态形态的 `left` 形参消失，接收者即左操作数）：`Vector2.OP_ADD(t.vector2, t.vector2)` → `t.vector2.OP_ADD(t.vector2)`。
R7.2 组名 `Operators` 与 case 名字符串**保持不变**（报告结构稳定，便于 diff）。
R7.3 文件头注释中"调用静态方法"的表述同步更新。

## Acceptance Criteria

- [ ] `new Vector2(1,2).OP_ADD(new Vector2(3,4))` 得 `(4,6)`；静态 `Vector2.OP_ADD === undefined`。
- [ ] 一元：`new Vector2(1,-2).OP_NEGATE()` 得 `(-1,2)`；`v.OP_NOT()` 返回 boolean。
- [ ] `==`/`!=` 对 `null`/`undefined` 短路不变（`v.OP_EQUAL(null)` → false、`v.OP_NOT_EQUAL(undefined)` → true）。
- [ ] **String 运算符生成已移除**：`def.gen.h` 无 `JSB_TYPE_BEGIN(String)` 块；`dispatch_builtin.gen.cpp` / `builtin_operator_tables.gen.h` 无 `find_op_String_*`；`operator_thunk` 实例化总数从 335 降至 ≤280（去掉 55 个 String 独有）。String 的 primitive 绑定/utilities 注册**不受影响**。
- [ ] 两腿（`static_binding=yes` / `no`）**均在 quickjs-ng 下**构建通过且上述行为一致。
- [ ] 编辑器 d.ts 重新生成后：`Vector2` 含成员签名 `OP_ADD(right: Vector2): Vector2`、`OP_NEGATE(): Vector2`；全文 **零** `static OP_*`；`right=NIL` 的二元重载仍带右参（`right: GAny`；2026-09-19 修正，原为 `Variant | null`）。
- [ ] 类型清单单源验证：`jsb_primitive_types.def.h` 增删一项 → 编辑器 d.ts 输出随之变化；`kPrimitiveTypes` 与 `JSB_CODEGEN_DEF_UTIL` 已不存在。
- [ ] **运算符专项测试覆盖去 String 后的全部已绑定运算符**（236 方法：一元 50 + 二元 186）与**全部静态腿可选组合**（344 个），实际值以重新抽取为准；完整性守卫在覆盖集合被削减时失败。
- [ ] C++ 双套件全绿（`godot --path ./project --jsb-run-tests`，dev 构建，quickjs-ng）。
- [ ] TS 集成测试 `GODOTJS_TEST_PROJECT_COMPLETED`（含新增运算符专项场景）；bench case 全量 `invalid=0`。
- [ ] 两个生成器改动仅限 String 排除（R6）；除该范围外输出字节不变。

## Out of Scope

- **运算符重命名**（`OP_ADD` → `add` 等）：用户明确要求命名不变。
- 运算符的 JS 语法糖（`a + b` 重载、`Symbol.toPrimitive`、Proxy 拦截）：JS 无运算符重载，本次只做方法形态。
- `Object` 派生类运算符；`Nil`/`bool`/`int`/`float`/`StringName`/`String` 左操作数。其中 `bool`/`int`/`float`/`StringName` 已由既有 skip 排除；**`String` 的排除由本任务 R6 补齐**。String 走 utilities 注册（不挂运算符），故 `"x".OP_*` 从不存在，不在覆盖范围。
- **v8 / node / quickjs / JavaScriptCore 的构建与测试**：本任务只用 quickjs-ng（R4）。其他运行时若有差异化问题，不在本任务验证范围。
- 静态绑定 `binding_mode=shared` 的 thunk 共用化（父任务 09-11 系列）；本任务在形态 A 与 dynamic 两腿内改形态，不引入新 thunk 模板参数。
- `Variant::Operator` 枚举本身（`scripts/typings/godot.generated.d.ts:444-471` 是引擎枚举，不动）。
- 为 `cases.builtin.ts` 或运算符测试引入代码生成器：两处均为手维护文件（`cases.builtin.ts` 的生成器已按决策移除），抽取脚本一次性使用后丢弃。

## Notes / 依赖

- 既存任务 `09-08-member-form-operators`（planning, P3）目标相同，但**其描述中的 `vec.add(other)` 命名方案本次不采纳**（用户要求命名不变）。经用户确认，本任务**已取代**它：该任务已归档至 `.trellis/tasks/archive/2026-09/09-08-member-form-operators/`（status: completed），并从父任务 `09-08-lowprio-static-improvements` 的 children 移除。
- `09-12-form-a-default-handling` 正在 `in_progress`，改动落在 `thunks_common.h` / `builtin_methods.h`。与 `builtin_operators.h` 无文件重叠，但两任务均改 `jsb_primitive_bindings.cpp`——实施前先确认该文件未被并行编辑。
- 生成文件纪律：`*.gen.*` 一律改生成器，禁止手改；本任务对生成器的改动**仅限 R6 的 String 排除**，其他任何 diff 即为越界。
- 覆盖矩阵数字（去 String 后：236 JS 方法 / 186 表 / 344 组合 / 280 thunk 实例化）由生成器实跑产物机械抽取得出，实施时**重新抽取核对**，不引用本文档的冻结值。抽取命令：
  - `python misc/build/generate_primitive_operators.py --input third/godot-cpp/gdextension/extension_api-4-7.json --interface third/godot-cpp/gdextension/gdextension_interface.json --out <tmp>`
  - `python misc/build/static_binding_codegen.py --input third/godot-cpp/gdextension/extension_api-4-7.json --interface third/godot-cpp/gdextension/gdextension_interface.json --out <tmp>`
- 本 worktree 为全新检出（无 `bin/`、无 obj、无 `.sconsign`），首次 scons 是**全量构建 + 下载 v8 预编译包**，耗时远超增量。实施前先评估；quickjs-ng 腿可避免 v8 下载。
