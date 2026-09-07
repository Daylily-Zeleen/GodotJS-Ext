# static-binding 调用层去查表化与 bench GC 隔离

> 静态绑定收尾任务之一。来源：2026-09-06 双腿基准数据复查中确认的三处调用层冗余与 bench 框架计量缺陷。

## 背景与问题

双腿 bench（107 cases）暴露 static 腿部分类别慢于 dynamic 腿 4-5x。A/B 实测排除了解析缓存缺失（resolve_* 全部为 magic static），定位出两类问题：

1. **调用层冗余查表**（用户审查发现）：
   - `operator_dispatch_unary/binary`（`builtin_operators.h`）**每次调用**都 probe 操作数类型 + 二分查 `find_operator_thunk`（497 条表），而方法/成员绑定在注册期一次性定 thunk——风格不一致且引入每调用开销
   - 一元运算符没有"右参重载"的概念，运行时查表完全多余
   - 二元运算符的重载集合（(op, left) → 若干 right）编译期已知，却把完整查找推到每次调用
2. **bench 框架无组间隔离**：static 腿 builtin 组产生大量短命 wrapper Variant，同进程内后续 Object 组 case 计时被 GC 压力污染 ~5x（`--only=Node` 单跑 80ns vs 全量 385ns，同 dll）；`--only` 还只过滤 BUILTIN_CASES 不过滤 OBJECT_CASES，单组验证一直静默跑全量

## Requirements

### A. 运算符调用层去查表化（codegen 期定形）

- **A1 一元运算符**：`operator_dispatch_unary` 废弃。注册期（`jsb_primitive_operators.def.gen.h` 宏展开处）直接挂 `operator_unary_thunk`——左类型在 `OperatorRegister<InType>` 编译期已知，无重载选择问题
- **A2 二元运算符**：JS 静态方法 `ADD(a, b)` 的实参类型只有运行时可知，**probe 不可消除**；可消除的是查表。`static_binding_codegen.py` 按 `(left_type, op)` 预展开该左类型下全部 right 重载，生成**每左类型一份的 thunk 表**（紧凑 switch 或函数指针数组，key=right 的 Variant::Type 小整数），注册期挂到对应静态方法；`operator_dispatch_binary` 退化为"probe right_vt → 直接 switch 取 thunk"，无二分、无 497 条全局表
- **A3 全局 `find_operator_thunk` 二分表**：仅保留给跨类动态探测场景（若有），主路径不再依赖

### B. bench 框架 GC 隔离与 --only 修正

- **B1** `benchmark.ts` 新增启动参数 `--gc`（user arg，`--` 之后）：启用后**每个 case 计时开始前**请求一次 GC。实现：引擎侧需暴露 JS 可调的 GC 入口（`Environment::_on_gc_request` 的 `LowMemoryNotification` / `JSB_EXPOSE_GC_FOR_TESTING` 的 `RequestGarbageCollectionForTesting` 已有 C++ 实现，缺 JS 绑定）——在 JS 全局或 `Engine`/`Performance` 合适入口暴露 `gc()`（挂 `JSB_EXPOSE_GC_FOR_TESTING` 宏门控，默认 release 构建不可用时不报错、`--gc` 自动降级为 no-op 并打 WARNING）
- **B2** 不用 `sleep`/定时器等待：GC 请求是同步语义（`RequestGarbageCollectionForTesting` 同步完成全量收集；`LowMemoryNotification` 亦是同步触发）。case 间不引入异步等待，保持计时确定性
- **B3** 修 `--only`：同时过滤 `BUILTIN_CASES` 与 `OBJECT_CASES`（各按 group 名匹配）
- **B4** 两腿最终对比数据必须带 `--gc` 采（两组数据都采：带/不带，量化 GC 污染本身）

### C. resolve 双重间接评估（A 项顺带）

- **C1** 现状：`resolve_builtin_method` 等 magic static 已把"查表"降到"一次静态局部变量读取 + 返回"，调用点无赋值开销，代码最小。**不改动**，仅在 spec 记录设计依据；若反汇编显示编译器未内联可再议

## Acceptance Criteria

- [ ] A1/A2 落地后：JS 侧一元/二元运算符调用不经过全局二分表（代码审查确认），运算符 case 性能不劣于改造前
- [ ] B1：`--gc` 参数生效（case 计时前 GC 完成），未暴露 gc 入口的构建上 `--gc` 降级 no-op + WARNING
- [ ] B3：`--only=<组>` 只跑指定组（builtin + object 均生效），日志可证
- [ ] 全量 bench 两腿 ×3 轮中位数，`--gc` 下：Object 组 case 在 static 腿恢复到单组水平（±20% 内，如 Node.get_child_count ≈ 80ns 量级而非 385ns）
- [ ] 产出最终双腿对比报告（含 GC 污染量化：--gc on/off 差异表），更新 `bench-operators-expansion` 归档 PRD 中被污染的结论
- [ ] 机制性结论沉淀 spec（运算符双层分发的定形依据、bench GC 隔离规程）

## Notes

- 改 C++ 后需重编两 flavor；改 TS 后 `tsc --noCheck`（产物在 `project/.godot/godotjs_ext/`）
- bench 命令形式：`godot --headless --bench --path ./project [-- --gc --only=<组>]`
- 相关：`.trellis/tasks/archive/2026-09/09-06-bench-operators-expansion/`（被污染的两腿数据）、`fa599d8`（运算符 thunk 实验，当时因 thunk 链慢于 evaluate 回退——本任务 A2 的直连 thunk 与其区别在于去掉中间 dispatch 层）
