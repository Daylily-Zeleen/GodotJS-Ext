# 修复静态腿内置类型构造函数分发

## Goal

修掉静态腿 `new X(...)` 分发的两类缺陷（均为既有，非某项改动引入）：

1. **错选重载后编组失败**：`new Color("abc")` / `new Color("abc", 1.5)` 选中 `Color(Color)` 重载，`marshal_one` 再拒绝 JS string → `bad argument 0`。静态腿 bench `Constructors` 组 `invalid=2`，动态腿同组 `invalid=0`（动态腿成功）。
2. **无匹配时返回空包装，下游崩**：生成的 `find_ctor_*` 在 arity 链后无兜底抛出，`throw_no_suitable_ctor` 定义了但从未发射；返回的包装 `IF_Pointer` 为 null，进入运算符接收者位即 SIGSEGV。

**技术方案见 [design.md](./design.md)**（含根因分层、三个候选方向的裁决、P1/P2 划分、错误矩阵）。

## Requirements

- R1 谓词与 marshaller 对齐（`type_compatible.h`）：去掉 `COLOR` 对 `STRING`/`INT` 的放行、`RID` 对 `OBJECT` 的放行；文件内写明「谓词 ⊆ 对应 `JSToGD<CppT>` 接受面」契约，并附审计表。不改变已一致的项（`ARRAY ↔ PACKED_*`、数值互转、`TRANSFORM3D` 家族、`STRING_NAME`/`NODE_PATH`）。
- R2 补兜底抛出（`misc/build/static_binding_codegen.py` `emit_ctor_dispatch`）：每个 `find_ctor_*` 的 arity 链后发射 `thunks::internal::throw_no_suitable_ctor(<VT>, info);`。仅改生成器，不手改 `*.gen.*`。
- R3 **不变量：Variant 包装的 `IF_Pointer` 恒非空——不加空指针守卫**（2026-09-19 修正）。曾计划在 `probe_vt`（`thunks_common.h`）与 `extract_variant_backed`（`jsb_type_convert_direct.h`）加判空，经用户指出后撤销：`IF_VariantFieldCount` 的包装由 `bind_valuetype` 写入 `IF_Pointer`，不存在"已判定为 variant 但指针为空"的合法状态。空包装是 R2 缺陷的**症状**，正解是让分发永不出产它（R2 兜底抛出），不在消费端打补丁。`probe_vt` 保留用户增强的 `TypeConvert::is_variant`/`is_object` 判定（语义等价于原 `InternalFieldCount()` 比较，另覆盖 NODE 下 Promise 特例）。
- R4 `throw_no_suitable_ctor` 由 `static` 改 `inline`（卫生性）。
- R5 构建/测试只用 quickjs-ng；两腿（`static_binding=yes/no`）都验证。
- R6 `emit_ctor_dispatch` 之外零 diff；`find_ctor_*` 的 arity 分支体逐字节不变（只多末尾兜底行）。

## Acceptance Criteria

- [x] 静态腿 bench `Constructors` 组 `invalid=0`；`new Color(String)` 与 `new Color(String,float)` 两条 case `sample != "invalid"`，且与动态腿结论一致。（`.agent_tmp/final-bench-static.log` / `final-bench-dyn.log`，均 `invalid=0`）
- [x] `new Color("abc")` 选中 `Color(String)` 并成功；`new Color(new Color())` 保持成功。（两条均 `sample=obj:Color`）
- [x] `new PackedVector2Array([new Vector2()])` 以 `no suitable constructor` 异常返回，**进程不崩**。（`.agent_tmp/probe-ctor.log`：`array-literal threw=true`，崩溃标记 0）
- [x] 空包装不再可能产生：R2 兜底抛出后，任何 `find_ctor_*` 全不匹配的调用都抛 `no suitable constructor`（探针实测），故 §2.4 的下游解引用路径不可达。（原"守卫负向验证"已作废：消费端守卫不属本修复，见 R3。）
- [x] 动态腿行为不变（`Constructors` 组 `invalid` 仍为 0）。（dll `1735e897`）
- [x] `git diff misc/build/static_binding_codegen.py` 仅含 `emit_ctor_dispatch` 的兜底发射；`find_ctor_*` 既有分支体逐字节不变。
- [x] C++ 双套件（`tests=yes dev_build=yes`，quickjs-ng）全绿：静态腿 51/51 + 3/3、动态腿 51/51 + 3/3，exit=0。

## Out of Scope

- **P2 根治**（用与 `marshal_one` 同源的非抛出探针 `can_marshal<CppT>` 取代手工谓词表）：**经用户决定不做**（2026-09-19）。`type_compatible.h` 本就是为静态 ctor 参数筛选专设的表，P1 令其健全后已观测缺陷全部闭合，无性能损失；漂移风险由 P1.1 的契约注释 + 审计表承担。方案存档于 design.md §4 P2，不再实施。
- `throw_no_suitable_ctor` 的文案/签名改造（除 `static`→`inline`）。
- 动态腿 ctor 路径的既有行为。`bench` 用例名与组名（`cases.builtin.ts`）保持不变。
- 其他 JS 运行时（v8 / node / quickjs / JSC）的构建与验证。

## Notes / 依赖

- 来源：`09-19-refactor-operator-bindings` 的审查阶段定位（`.agent_tmp/ab-bench-static.log` 静态腿 `invalid=2`；`.agent_tmp/bench-ctors-dyn.log` 动态腿 `invalid=0`）。归档任务 `09-17-static-binding-comment-sweep/report.md` 亦记载同一 `invalid=2`——早于本次，属既有。
- 该缺陷**不在** `09-19-refactor-operator-bindings` 的改动面内（ctor 分发与 `type_compatible.h` 与 HEAD 逐字节相同）；本任务独立承接。
- 生成文件纪律：`*.gen.*` 一律改生成器。本任务的生成器改动**仅限** `emit_ctor_dispatch` 的兜底发射。
