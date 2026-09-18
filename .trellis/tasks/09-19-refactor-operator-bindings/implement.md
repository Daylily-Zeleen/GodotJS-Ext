# Implement — 运算符成员形态重构（命名不变）

> 前置：`task.py start` 前确认 `src/runtime/bridge/jsb_primitive_bindings.cpp` 未被 `09-12-form-a-default-handling` 并行编辑（查 `git status` + 文件 mtime）。
> **验证环境**：本任务只用 quickjs-ng（见 §6）。不构建/不测试 v8 / node / quickjs / JavaScriptCore。
> 生成文件纪律：只改生成器，不手改 `*.gen.*`。两个生成器本次**有且仅有 R6 的 String 排除改动**；除该范围外出现任何 diff 即为越界。

## 0. 基线固定（先做，否则改后无法归因）

- [ ] 0.1 按 `test/codegen-baseline.md` 建编辑器 codegen 基线：`python misc/verify_codegen.py --godot <引擎路径> --update-baseline` → 再跑一次确认双轮全绿。基线不可得时退化为：改前 `godot --headless --editor --path ./project --generate-types`，快照 `project/gen`、`project/typings` 到 `.agent_tmp/baseline-before/`。
- [ ] 0.2 抽取运算符覆盖矩阵（脚本留 `.agent_tmp/`，不入库）：
  - 从 `.agent_tmp/ops.def.gen.h`（`generate_primitive_operators.py` 产物）抽每类 op 名；
  - 从 `.agent_tmp/sbgen/dispatch_builtin.gen.cpp`（`static_binding_codegen.py` 产物）抽每张 `find_op_<Class>_<Op>` 表的 `case` 标签。
  - **在 R6（去 String）完成后重新抽取**，得到最终覆盖集合；目标规模：226 JS 方法（一元 49 + 二元 177）、177 表、287 可选组合。
  - 产出 TS 数据字面量，供 §5 的测试文件嵌入。
- [ ] 0.3 记录部署位 md5（`build/scons-build.md` 的 dll 身份纪律）。

## 1. 动态腿（`src/runtime/bridge/jsb_primitive_bindings.cpp`）

- [ ] 1.1 `BinaryOperator::invoke`（约 `:114`）：`info.Length() != 1` 报错；`left ← info.This()`；`right ← info[0]`。
- [ ] 1.2 `UnaryOperator::invoke`（约 `:147`）：`info.Length() != 0` 报错；`left ← info.This()`。
- [ ] 1.3 注册宏 `:60-74`（`#else` 动态分支）：三处 `class_builder.Static()` → `class_builder.Instance()`；`JSB_OPERATOR_NAME` 与 `JSB_LOG` 行不变。
- [ ] 1.4 静态腿宏 `:45-59`：三处 `class_builder.Static()` → `class_builder.Instance()`；模板实参不变。

## 2. 静态腿 thunks（`src/static_binding/thunks/builtin_operators.h`）

- [ ] 2.1 `operator_thunk`（`:65`）：左 `left_backing_of<L>(info.This())`；短路判定 `info.Length() < 1 || info[0]->IsNullOrUndefined()`；全部右参读取 `info[1]` → `info[0]`（int64/double/bool/String/包装提取五分支）；`R = godot::Variant` 的 NIL 分支保持 `(void)info` 不读实参。
- [ ] 2.2 `operator_unary_thunk`（`:134`）：左改 `info.This()`，其余不动。
- [ ] 2.3 `operator_dispatch_binary`（`:197`）：`probe_vt<probe_prefer_object_types>(info.This())`、`probe_vt<probe_prefer_primitive_types>(info[0])`。
- [ ] 2.4 `evaluate_dynamic_binary`（`:169`）：两操作数改读 `info.This()` / `info[0]`。
- [ ] 2.5 不改：表函数签名 `ThunkFn f(godot::Variant::Type)`、`left_vt == GetTypeInfo<LeftT>::VARIANT_TYPE` 守卫、`left_backing_of` / `left_opaque_of`、`static_binding_codegen.py`。

## 3. 编辑器 d.ts

- [ ] 3.1 `jsb_codegen_type_db.h:135` `OperatorDecl` 增 `bool is_unary = false;`（`op_name` 保留）。
- [ ] 3.2 `jsb_codegen_type_db.cpp` operators 循环（约 `:440`）：填 `is_unary = (op.op == Variant::OP_NEGATE || op.op == Variant::OP_POSITIVE || op.op == Variant::OP_NOT || op.op == Variant::OP_BIT_NEGATE)`；`op_name` 一行不改。
- [ ] 3.3 `jsb_codegen_writer.cpp` `ClassWriter::operator_`（`:526`）重写：一元 `name(): R`；二元 `name(right: U): R`，`right_type == Variant::NIL` 时 `U = "Variant | null"`；删 `static` 前缀与 `left` 形参；`separator_line_ = true` 保留。
- [ ] 3.4 确认 `p_info.left_type` 无其他消费点（若有则保留字段、仅不再打印）。

## 4. TypeDB 单源（`jsb_codegen_type_db.cpp:285-344`）

- [ ] 4.1 重写为 `#pragma push_macro("DEF")` / `#undef DEF` / `#define DEF(TypeName) {...}` / `#include "jsb_primitive_types.def.h"` / `#pragma pop_macro("DEF")` + String utilities 显式一行（形式对齐 `jsb_primitive_bindings.cpp:964-972`）。
- [ ] 4.2 删除 `kPrimitiveTypes[]`、其循环、死宏 `JSB_CODEGEN_DEF` 与 `JSB_CODEGEN_DEF_UTIL`。
- [ ] 4.3 裸 include 若失败（`runtime/bridge` 不在 editor CPPPATH），改用相对路径并记录原因；**不得**新建重复清单文件。

## 5. 运算符专项 TS 测试（R5）

- [ ] 5.1 新建 `project/tests/operators/test-operators.ts` + `Operators.tscn`，形态对齐 `project/tests/default-args/`（`section()`/`check()`/`expectPasses`，失败一律走 `reportTestFailure`，异常不得逃出 `_ready`）。
- [ ] 5.2 挂进 `project/tests/start.ts` 的 `scenes` 数组（非 bench 分支）。
- [ ] 5.3 嵌入 §0.2 产出的覆盖矩阵，实现 design §6 的六层断言：
  1. 形态：全部方法 `typeof value.OP_X === "function"` + `(Class as any).OP_X === undefined`（目标 226，以重抽值为准）；
  2. 契约：全部可选组合逐个调用不抛 + 返回型别与声明一致（目标 287）；
  3. 精确值：代表性子集（含 bench 已覆盖的 23 条）断言具体取值；
  4. 不变量：全部类型通用的 `a.OP_EQUAL(a) === true` / `a.OP_NOT_EQUAL(a) === false` / 比较反对称 / 结果型别；
  5. 边界：`OP_EQUAL(null)`→false、`OP_NOT_EQUAL(undefined)`→true（String 的 `%`-NIL 行不测——String 不挂载运算符）；
  6. 完整性守卫：期望集合与实际发现集合双向比对，差异即失败。
- [ ] 5.4 实现「按 Variant 类型造合法操作数」工厂（数值/字符串/`Object*`→`Node` 实例/空容器/其余内置无参构造），并给工厂加自检（构造失败不得被误报为运算符缺陷）。
- [ ] 5.5 测试须双腿通用：只用 static / dynamic 两腿都成立的行为，不依赖 `STATIC_BINDING_ENABLED` 分支而跳过断言。

## 6. String 运算符生成移除（R6）+ 清理（R7）

- [ ] 6.0 **去 String 运算符生成**（R6，在 §0.2 重抽**之前**做）：
  - `misc/build/generate_primitive_operators.py`：`generate()` 的 skip 集合加入 `"String"`（与既有 `bool/int/float/StringName` 同处，注释说明其走 utilities 注册、不挂运算符）；
  - `misc/build/static_binding_codegen.py`：`emit_operator_pair_tables` 的 `JS_NATIVE_LEFT` 加入 `"String"`；
  - 两处互加交叉引用注释（既有注释已声明"keep aligned"，补上 String 后判据同源）；
  - **不改** `jsb_primitive_types.def.h`；`TypeDB` 侧本来就对（String 走 utilities 模式），无需改动；
  - 验证：重跑两生成器后 `def.gen.h` 无 `JSB_TYPE_BEGIN(String)`、`dispatch_builtin.gen.cpp` 与 `builtin_operator_tables.gen.h` 无 `find_op_String_*`、`operator_thunk` 实例化总数 335 → ≤280。
- [ ] 6.1 `project/tests/benchmark/cases.builtin.ts:299-342` 23 条 operator case 改成员调用（`Vector2.OP_ADD(t.vector2, t.vector2)` → `t.vector2.OP_ADD(t.vector2)`）；组名 `Operators` 与 case 名字符串**保持不变**；`:8-9` 头注释同步。
- [ ] 6.2 `misc/bench_matrix.py:114-118` docstring：删掉 `Vector2.OP_IN !== undefined` 的失效说法（代码用 `STATIC_BINDING_ENABLED`），改述真实来源；逻辑不动。
- [ ] 6.3 spec：`cpp/architecture-constraints.md` 运算符双层分发节改成员形态（保留仍生效约束）；`cpp/generated-files.md:16` 补"两腿共用 + 成员形态"。
- [ ] 6.4 全局搜索 `\.OP_[A-Z]`：除 `cases.builtin.ts` 的成员调用、新运算符测试、`Variant::OP_*` 引擎枚举、`scripts/typings/godot.generated.d.ts` 的 `Operator` 枚举外无残留；生成器输出中不得再有 `static OP_`。
- [ ] 6.5 `09-08-member-form-operators`（planning, P3）目标与本任务相同但命名方案不采纳；归档/取代**需用户确认后执行**。

## 7. 验证（全部在 quickjs-ng 下）

构建命令（两腿各一次）：

```
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes use_quickjs_ng=yes -j5                      # static 腿（默认 static_binding=yes）
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes use_quickjs_ng=yes static_binding=no -j5   # dynamic 腿
```

- [ ] 7.1 两腿构建通过 + 部署 + md5 双查。
- [ ] 7.2 行为冒烟（两腿各一次）：`OP_ADD` 取值 / 静态为 undefined / `OP_NEGATE` / `OP_NOT` / `==` 短路。
- [ ] 7.3 C++ 双套件：`scons ... use_quickjs_ng=yes tests=yes` → `godot --path ./project --jsb-run-tests`（**必须 dev 构建**，`jsb_check` 只在 dev 生效；quickjs 的 `SetWeak` 断言见 `test/index.md`）。
- [ ] 7.4 TS 集成：生成 api 数据 → `cd project && node_modules/.bin/tsc --noCheck` → `godot --path ./project --verbose`；判据 `GODOTJS_TEST_PROJECT_COMPLETED`、exit 0、无新增 Orphan StringName；日志须含新运算符场景的执行记录。
- [ ] 7.5 运算符专项：确认覆盖计数（目标 226 方法 / 287 组合）由测试自身打印并核对；完整性守卫**做一次负向验证**（临时从期望集合删一项 → 测试必须报失败 → 还原）。
- [ ] 7.6 d.ts 校验：`python misc/verify_codegen.py --godot <引擎路径>`；逐条归因 diff（预期：primitive 类 `static OP_*` 全量变为成员签名且 `left` 形参消失；`project/gen` 无变化）。
- [ ] 7.7 单源验证：临时注释 `jsb_primitive_types.def.h` 一项 → 重编 → d.ts 对应类消失 → 还原 → 重编复原。
- [ ] 7.8 bench：`python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix`（quickjs-ng）；判据 `invalid=0`。
- [ ] 7.9 两个生成器重跑无 diff（`--check` 或字节对比）。

## 回滚点

- 分离提交：① 两腿运行时 ② 编辑器 d.ts ③ TypeDB 单源 ④ 运算符专项测试 ⑤ 清理（bench/spec）。
- `jsb_primitive_operators.def.gen.h` 与 `src/static_binding/gen/*` 内容本次不应变化。

## 风险文件（改动前先看 git status / mtime）

- `src/runtime/bridge/jsb_primitive_bindings.cpp`（与 `09-12-form-a-default-handling` 潜在竞争）
- `src/static_binding/thunks/thunks_common.h`（只读参考 `probe_vt`，本任务不改）
- `project/tests/start.ts`（新增场景接线；与 bench 分支互不干扰）
