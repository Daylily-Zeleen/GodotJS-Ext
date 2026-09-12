# 静态绑定 utility 与 vararg 族 thunk 清扫（形态 shared：签名共享）

> 父任务：09-11-static-binding-size-reduction（子任务 3，P3）。依赖关系：在子任务 1/2 的共享模式定型后收尾清扫。

## Goal

在 `binding_mode=shared` 形态下对剩余小头族套用同一共用化模式（字符串感知解析器复核计数）：utility_function_thunk 102 → 32、class_vararg_method_thunk 15 → 10、builtin_vararg_method_thunk 6 → 5、utility_vararg_function_thunk 12 → 3。**形态 A 零改动**。

## Background（已核实事实）

- utility 函数**不携带默认参数**（`src/static_binding/thunks/utility_functions.h:35` 注释；唯一 (T,Lit) 对计数 = 0）——无默认值负担，同 class 族模式；32 唯一签名本就纯类型
- vararg 族结构：固定前缀展开 + 尾部循环（`class_methods.h:131-133`、`builtin_methods.h:184-186`），共享化按前缀签名维度；vararg gen 无带默认值 Arg（DefArg 出现 = 0）
- utility 挂载走全局函数表（`jsb_primitive_bindings.cpp:936-945`），无 class/self 接线负担；挂载点 `_static_method`/`_utility_method` 已用 `info.Data()` 传 collection_index（`:552-570`）——data 通道现成
- 勘误：此前漏计 utility_vararg 族（12 引用 / 3 唯一），本任务纳入

## Requirements

- utility 共享 thunk 按 `(RetT, ArgsT...)` 特化；每函数 data：{eager 解析的 `GDExtensionPtrUtilityFunction`, 函数名}
- vararg 族按 `(VTC, IsStaticC, RetT, 前缀 ArgsT...)` 同模式共用；per-method data 与子任务 1/2 同构
- codegen 发射端与挂载端与子任务 1/2 同构改造（shared 形态条件发射）

## Acceptance Criteria

- [ ] shared 形态实例化声明数：utility ≤ 35、class_vararg ≤ 10、builtin_vararg ≤ 5、utility_vararg ≤ 3
- [ ] C++ 双套件全绿、TS 集成 `GODOTJS_TEST_PROJECT_COMPLETED`、bench invalid=0
- [ ] dll 体积最终读数记录（三腿 static/shared/dynamic 对比，相对形态 A 基线的总降幅）

## Out of Scope

- ctor（133 无冗余）、operator、member getter/setter 族
- 任何运行时行为变化；形态 A 改动

## Notes

- 本任务收尾后父任务 09-11 可归档：AC = 三个子任务全部归档 + 三腿体积/性能报告
