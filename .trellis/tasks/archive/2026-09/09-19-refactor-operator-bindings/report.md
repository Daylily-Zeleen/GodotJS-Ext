# 实施报告：重构内置类型运算符（静态函数 → 成员函数）

## 目标

把内置类型 JS 运算符从静态 `Vector2.OP_ADD(a, b)` 改为成员 `a.OP_ADD(b)`（`this` = 左操作数），同步改造动态腿 / 静态腿 / 编辑器 d.ts 三条面；`TypeDB::load_primitive_types` 改用 `jsb_primitive_types.def.h` 单源；去掉 String 的死代码运算符生成；新增覆盖全部已绑定运算符的 TS 集成测试。

**未提交、未推送**（按要求留人工审查）。

## 改动清单

`git diff --stat`（含 spec 与任务文档）：12 文件，134 insertions / 148 deletions。

| 文件 | 改动 |
|---|---|
| `misc/build/generate_primitive_operators.py` | skip 集合加入 `String` |
| `misc/build/static_binding_codegen.py` | `JS_NATIVE_LEFT` 加入 `"String"`（含交叉引用注释） |
| `src/runtime/bridge/jsb_primitive_bindings.cpp` | 两腿 `class_builder.Static()` → `Instance()`；`BinaryOperator::invoke` 改 `info.This()`/`info[0]`、arity 1；`UnaryOperator::invoke` 同 |
| `src/static_binding/thunks/builtin_operators.h` | `operator_thunk`/`operator_unary_thunk`/`operator_dispatch_binary`/`evaluate_dynamic_binary` 改 `info.This()`/`info[0]`；**nil 相等短路极性修正**（见下） |
| `src/editor/codegen/jsb_codegen_type_db.h` | `OperatorDecl` 加 `is_unary` |
| `src/editor/codegen/jsb_codegen_type_db.cpp` | `load_primitive_types` 改 `jsb_primitive_types.def.h` 单源 + `DEF` 宏 + String utilities 单行；operators 循环填 `is_unary` |
| `src/editor/codegen/jsb_codegen_writer.cpp` | `ClassWriter::operator_` 输出成员签名（`String(...)` 显式包裹，规避 MSVC C2593） |
| `project/tests/benchmark/cases.builtin.ts` | 23 条 operator case 改成员调用；头注释同步 |
| `misc/bench_matrix.py` | `assert_leg` docstring 改述为 `STATIC_BINDING_ENABLED` 常量来源 |
| `project/tests/start.ts` | 场景列表加入 `Operators.tscn` |
| `project/tests/operators/test-operators.ts`（新增 806 行） | 运算符专项测试 |
| `project/tests/operators/Operators.tscn`（新增） | 场景 |
| `.trellis/spec/godotjs-ext/cpp/architecture-constraints.md` | 运算符章节改成员形态 + 挂载排除集 + NIL 行更正 |
| `.trellis/spec/godotjs-ext/cpp/generated-files.md` | `def.gen.h` 行补"两腿共用 + 形态由消费者宏决定" |

生成器实跑（4.7 api json）：`def.gen.h` 32 块（原 33）、无 `find_op_String_*`、`operator_thunk` 实例化 335 → **280**、JS 方法 **236**（一元 50 / 二元 186）、静态表 **186**、可选组合 **344**。

## 超出原计划的两处修正（均为修既有缺陷，非新增功能）

### 1. 静态腿 nil 相等短路极性反了（本次新测试首次暴露）

`operator_thunk` 里 `==`/`!=` 对 null/undefined 的短路返回 `OpC == OP_EQUAL`，即 `X.OP_EQUAL(null) → true`。

- 引擎权威规则（`variant_op.cpp:537/548/659/670`）：`X == nil` → `OperatorEvaluatorAlwaysFalse`；`X != nil` → `AlwaysTrue`。即 `X.OP_EQUAL(null)` 必须为 `false`。
- 动态腿走 `Variant::evaluate`，行为正确；静态腿与引擎相反。
- 该分支在 HEAD 已存在（`git log -L` 追溯到 `8e84f04`），其原始注释明确写着"X == nil is always false, X != nil is always true"——代码与自己的文档相反，是既有笔误。

修法：`v8::Boolean::New(isolate, OpC == Variant::OP_NOT_EQUAL)`，并把注释改为引引擎求值器名。验收项"`v.OP_EQUAL(null)` → false、`v.OP_NOT_EQUAL(undefined)` → true"要求此修正，否则静态腿不达标。

### 2. 测试操作数构造形态（两腿互斥，取交集）

原计划测试用 `new PackedVector2Array([...])` 之类字面量实参构造操作数。实测两腿对该形态**各自**不兼容：

| 操作数构造 | 静态腿 | 动态腿 |
|---|---|---|
| `new PackedVector2Array([v])`（JS 数组字面量实参） | 💥 生成的 ctor 表无匹配、`new` 出不带 Variant 的包装；成员调用报 `no bound this`；再交给 operator thunk 会解空指针 SIGSEGV | ✅ |
| 逐元素 `push_back(Vector2)` | ✅ | 💥 SIGSEGV |
| **无参构造 `new PackedVector2Array()`** | ✅ | ✅ |

两条腿的构造缺陷都**不在本任务改动面内**（ctor 分发为 `type_compatible.h` + 生成的 `find_ctor_*`，二者与 HEAD 逐字节相同；`cases.builtin.ts` 既有的构造 case 也只用 `new X(new GArray())` 形态）。测试改为只使用两腿交集形态（无参构造）。空容器的运算符仍完整走调用路径与返回型别断言，语义正确性由 `checkBoundaries` 里的精确值断言保证。

## 验证证据

| 判据 | 结果 | 证据 |
|---|---|---|
| 静态腿 TS 集成（quickjs-ng，`static_binding=yes`，dll md5 `73150025`） | exit=0，`OPERATORS-DIAG methods=236 unary=50 binary=186 calls=344`，`GODOTJS_TEST_PROJECT_COMPLETED`，0 Orphan | `.agent_tmp/final-check.log` |
| 动态腿 TS 集成（quickjs-ng，`static_binding=no`，dll md5 `65a598c7`） | exit=0，同上 DIAG，0 Orphan | `.agent_tmp/it-dyn-noarg.log` |
| C++ 双套件（`tests=yes` + `dev_build=yes`，`--jsb-run-tests`） | 51/51 + 3/3 passed，586+12 assertions，exit=0 | `.agent_tmp/cpp-tests2.log` |
| 完整性守卫（负向） | 从期望集合删 `Vector2.OP_ADD` → `GODOTJS_TEST_PROJECT_FAILED: completeness methods`，`methods=235`；已还原 | `.agent_tmp/guard-neg2.log` |
| d.ts 单源 | 注释掉 `DEF(RID)` → 重编 → `class RID` 计数 1→0；还原 → 1。`static OP_` = 0，`OP_ADD(right:` = 19 | `.agent_tmp/chain-rid.log` / `chain-restore.log` |
| 生成器范围（HEAD vs 当前，逐文件） | `dispatch_builtin.gen.cpp` 差异**全部**为 `find_op_String_*` / `operator_thunk<...String...>`（166 行）；`builtin_operator_tables.gen.h` 9 行；**ctor 段逐字节相同**；`dispatch_class.gen.cpp`/`dispatch_utility.gen.cpp`/`manifest.gen.json` 完全相同 | 见 `## 归因` 节 |
| TS 编译 | `tsc --noCheck` exit=0；产物与源码确定性一致 | — |

## 未达标项：bench `invalid=2`（既有缺陷，非本任务回归）

静态腿全量 bench（`-- --bench`，234 cases）：`staticBinding=true`，**`invalid=2`**，两项均为 `Constructors.new Color(String)` 与 `Constructors.new Color(String,float)`，错误落在 `thunks_common.h:471 produce_value`（构造参数编组）。

**根因已定位**（审查阶段读未改动产物得出，非本任务引入）：

1. `type_compatible.h:111` 声明 `Variant::COLOR` 可接受 `STRING`（`COLOR: return p_source_type == STRING || p_source_type == INT`）。
2. 生成的 `dispatch_builtin.gen.cpp:4509-4517` 里 `Color` 的 1 参重载**先判 `Color(Color)` 再判 `Color(String)`**——`argts[0] = STRING` 同时满足前者的 `can_be_converted_from<COLOR>`（因第 1 条），于是选中 `builtin_ctor_thunk<COLOR, 1, Args<godot::Color>>`，其 `marshal_one<Color>` 再把 JS string 判为非法 → `bad argument 0` → 计入 invalid。2 参形态同理（`:4520` vs `:4524`）。
3. 该 ctor 分发与 `type_compatible.h` 均**未被本任务改动**（ctor 段与 HEAD 生成器输出逐字节相同）。

归因为**既有**，证据三重：

1. **不在改动面**：失败 case 在 `cases.builtin.ts` 中未被本任务编辑（本任务只改 operator case 行）；失败路径（ctor 分发 + `type_compatible.h` + `thunks_common.h`）与 HEAD **逐字节相同**——生成产物 `find_ctor_*` 段 HEAD 与当前完全相同，`type_compatible.h`/`thunks_common.h`/`builtin_constructors.h` 均不在 `git diff` 中；`jsb_primitive_bindings.cpp` 的 3 个改动 hunk 无一涉及 ctor 注册。
2. **同用例在动态腿 `invalid=0`**：同一测试文件、同一 `Constructors` 组，动态腿 121/121 通过。缺陷是静态腿 ctor 分发特有。
3. **仓库既有记录**：归档任务 `09-17-static-binding-comment-sweep/report.md` 已记载同一 `invalid=2`（同样两项 `Color(String)`），早于本任务。

本报告不将其改写为"通过"。此外，`bench_matrix.py --build` 隐含按 v8 构建（未传 `use_quickjs_ng`），违反 R4，故 bench 未走该入口，而是手工按 quickjs-ng 构建后直接跑 `-- --bench`。

## 审查结论（trellis-check 子代理，独立复核）

8 项审查项：**6 PASS / 1 FAIL（即上节 bench 项）/ 1 部分未验证**；无本任务引入的缺陷。逐条要点：

- 成员形态、`info.This()`/`info[0]` 取用、`Instance()` 挂载、无残留 `info[1]`/`Static()`、nil 短路极性、String 排除范围（结构性证明：HEAD 产物删去 String 块后与当前产物**逐字节相同**）、TypeDB 单源、d.ts writer、测试完整性、验收项逐条——均 PASS。
- 修复的两处审查发现：
  - **W1**：`calls=344` 原先只是硬编码常量插进日志，**未断言**——新增 `OP_CALLS.length !== EXPECTED_CALLS` 失败判定，并负向验证（删一行 → `GODOTJS_TEST_PROJECT_FAILED: completeness calls`；还原 → 绿）。参照 `.agent_tmp/w1-check.log` / `w1-neg.log` / `restore-check.log`。
  - `project/tests/start.ts:26` 的 START-DIAG 场景计数仍是 8（列表已 9 条）→ 改 9。
- spec 三处行号/表述勘误已改：`X==nil` 求值器注册行（533-569/655-691，原引镜像行）、`register_string_modulo_op` 引用点（variant_op.cpp:408，原引头文件声明行）、静态腿左操作数不匹配的行为（**抛错**，非"走动态兜底"）。
- 已知未由审查独立复现（采信日志）：两腿构建与 C++/TS 套件未重跑；def.h toggle 实验仅有日志。审查未发现 report 与仓库状态的实质矛盾。

### 更正的既有风险（审查提示，非本任务引入）

- 静态腿 `left_backing_of`/`probe_vt`（`thunks_common.h:347/364`、`builtin_operators.h:50/114`）对 `IF_Pointer` 内部字段**无空指针检查**。可经静态腿未匹配 ctor 产生的空内部包装到达（生成的 `find_ctor_*` **没有**兜底抛出——`throw_no_suitable_ctor` 已定义于 `builtin_constructors.h:42` 但从未发射）。此路径是既有缺陷（ctor 分发），本任务只改了接收者位置；但在成员形态下它落在 `this` 位，值得显式记录。最小复现：静态腿 `new PackedVector2Array([new Vector2()]).OP_ADD(new PackedVector2Array())`。
- 审查指正：report 前文把这处 `no bound this`/SIGSEGV 叙事归到 operator thunk；实际首崩点在 `operator_dispatch_binary` 调用的 `probe_vt`（`thunks_common.h:347`），早于 `left_backing_of`。缺陷类别相同，行号更正于此。

## 遗留

- **静态腿 ctor 分发缺陷**（`Color(String)` 构造失败 + 空包装 SIGSEGV）：根因已定位，**已立独立任务** `.trellis/tasks/09-19-static-ctor-dispatch-fix/`（含 design.md 修复方案）——见下节第 3 项。
- **两腿 ctor 接受形态不一致**（静态腿拒 JS 数组字面量实参、动态腿 `push_back` 路径会崩）：同属 ctor 分发面，已并入上述任务。
- **`09-08-member-form-operators`**：已归档取代——见下节第 4 项。

## 后续四项处理（用户 2026-09-19 追加）

### 1. 子代理 145 次调用 / 21 分钟 —— 根因是**派发时无预算**，已机制化修复

取证（`history://CheckOperatorRefactor` 全文解析）：**145 次工具调用 / 81 轮次 / 21m50s**；发「wrap up NOW」后**3 次调用**即交付，且结论完整。调用分布：`bash` 87（其中 **38 次实为 grep**）、`read` 41、`grep` 直调 14。话题分布：**51/145 次耗在与本轮改动无关的既有 ctor 分发缺陷**上。无重复命令（去重仅省 1 次）——不是空转，是**开放式任务被穷尽执行**。

修复（4 文件）：
- `.omp/agents/trellis-check.md`：新增 **Investigation Budget** 硬上限（每项 ~8 次、总 ~60 次、触顶标 UNVERIFIED 收敛）；独立读取须同轮并发；直调 `grep`/`glob` 不包 bash；已在 `report.md`/`.agent_tmp/*.log` 有证据的不复证；**既有缺陷确认后 1~2 次取证即停**。`tools:` 字段由不存在的 `find, search, ast_grep` 改为真实的 `glob, grep`。
- `.omp/agents/trellis-implement.md`：新增 **Iteration Budget**（批量改一次验、变体一次列全、外部失败 ≤3 次/≤2min、达标即停）。
- `.omp/agents/trellis-research.md`：`tools:` 同步改真实工具名。
- `.trellis/spec/guides/workflow-rules.md`：新增「调用预算（细则）」章节，含上述实测数据。

### 2. `start.ts` 场景计数硬编码 —— 已改为派生

原 `String(benchOnly ? 1 : 9)` 是硬编码字面量（我先写 8 后改 9，仍是硬编码）。现修正为**先建 `scenes` 数组、再取 `scenes.length`**（`project/tests/start.ts:41`），注释说明"硬编码会在增删场景时静默失同步——而 DIAG 的意义恰恰在此"。实测：`START-DIAG ... scenes=9`，`Loading scene` 9 行，`COMPLETED`，0 Orphan（`.agent_tmp/start-count.log`）。

### 3. 静态腿 ctor 分发修复方案 —— 已出，见独立任务

新建任务 `.trellis/tasks/09-19-static-ctor-dispatch-fix/`（`prd.md` + `design.md`，context 已 curation 并通过 `task.py validate`）。

**结论（三候选方向裁决）**：不是改 `builtin_ctor_thunk` 参数处理（错层——它会把"不匹配"变成"静默构造垃圾值"），也不是只调重载优先级（治症不治本：`new Color(1)` 仍失败、空包装仍 SIGSEGV、且把 JSON 顺序变隐式契约）。**正解是修 `marshal_one` 整条链路的不完备**。

**根因（取证纠正了一处此前的误判）**：两腿用的根本**不是同一个谓词**——
- 动态腿：`TypeConvert::can_convert_strict(值, 目标)` —— **看 JS 值形态**（`IsString`/`IsNumber`/`IsObject && is_variant`）
- 静态腿：`can_be_converted_from<目标>(probed_type)` —— **看 Variant 类型对**（镜像引擎 `Variant::can_convert_strict`）

对 `new Color("abc")` 两者给出**相反**判定：动态腿 `can_convert_strict(jsString, COLOR)` → false（跳过 `Color(Color)`，选中 `Color(String)`，**成功**）；静态腿 `can_be_converted_from<COLOR>(STRING)` → true（错选 `Color(Color)` → `extract_variant_backed` 拒绝 JS string → `bad argument 0`）。动态腿实测成功（`bench-ctors-dyn.log`: `sample=obj:Color`），故契约是"应可用"，静态腿背离。

**结构性根因**：动态腿只有**一个**函数同时承担"筛选"与"转换"，天然一致；静态腿把该决策**拆成两个实现**（生成期谓词表 + 运行期 marshaller），且 `marshal_one` 链**没有非抛出的可接受性探针**，生成器只能自造平行的预测表——必然漂移。

**程序化审计的失配全集**（32 个 `find_ctor_*` 逐条比对谓词面 vs `JSToGD<T>` 接受面）：仅 3 条——`COLOR→godot::Color` 接受 `STRING` 与 `INT`、`RID→godot::RID` 接受 `OBJECT`。其余 34 个目标一致。

**方案**：P1 止血（谓词对齐 marshaller / 补 `throw_no_suitable_ctor` 兜底发射 / 加空指针守卫 / `static`→`inline`），P2 根治（用与 `marshal_one` 同源的 `can_marshal<CppT>` 探针取代手工谓词表，结构上不可能漂移）。含错误矩阵与 5 条断言点（含守卫负向验证）。

### 4. `09-08-member-form-operators` —— 已确认取代并执行

该任务 `prd.md` 仅有 Goal 段落（Requirements / Acceptance Criteria 均为 `TBD`），其唯一实质内容 `vec.add(other)` 命名方案本次明确不采纳；未被实施过。已：
- `task.py archive 09-08-member-form-operators --no-commit --skip-branch-validation` → `.trellis/tasks/archive/2026-09/09-08-member-form-operators/`（status: `completed`）
- 从父任务 `09-08-lowprio-static-improvements` 的 `children` 移除（进度 `[1/3 done]` → `[0/2 done]`，未因归档倒退为 `[2/3]`）
- `prd.md` 的 Notes 段落更新为"已取代"

**未 commit**（`--no-commit`）：`git log` 顶端仍为 `3146422`。


- `project/icon.svg.import`：引擎 headless 运行重写了导入参数（删了 `compress/high_quality_mode` 等键）。
- `third/quickjs-ng`：子模块工作区出现 12 个未跟踪编译产物（`.obj`），无跟踪文件改动。
- 以上均未 `git checkout` 还原（`AGENTS.md`：不自行还原文件）。

## 复现命令

```
# 静态腿
taskkill //F //IM godot* ; scons platform=windows target=editor static_binding=yes compiledb=no debug_symbols=no dev_build=no verbose=no use_quickjs_ng=yes -j6
cd project && rm -f .godot/.tsbuildinfo && node node_modules/typescript/bin/tsc --noCheck
"D:/Dev/godot/godot/bin/Godot_v4.7.2-stable_win64_console.exe" --audio-driver Dummy --headless --path ./project --verbose
# 判据：exit 0 + OPERATORS-DIAG ... calls=344 + GODOTJS_TEST_PROJECT_COMPLETED + 无 Orphan
```
