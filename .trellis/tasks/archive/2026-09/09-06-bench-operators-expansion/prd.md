# 操作符基准测试扩展

> 静态绑定收尾任务之一。

## Goal

扩展 benchmark 的 Operators 组：当前仅 3 个 Vector2 case，operator dispatch 覆盖不足，无法为「运算符/构造器 ptrcall thunk 是否恢复」提供裁决数据。

## Requirements

- Operators 组补充跨类型运算符 case：至少覆盖 Basis / Transform3D / Projection 的 struct 参数运算（当前 det==0 溢出 bug 的实证来源类型），以及 int/float 基础类型运算符对照
- case 生成走既有设施：~~`misc/build/generate_benchmark_cases.py`~~（2026-09-07 起移除，`cases.builtin.ts` 改为手维护）或 `project/tests/benchmark/` 既有组结构
- 保持与现有 bench 框架一致（`--only=<组>` / `--calls=<N>` 过滤语义不变）

## 依赖

- 无前置依赖
- 产出被 `static-ctor-thunks`（P4 降级项恢复决策）消费

## Acceptance Criteria

- [x] Operators 组新增 case 可通过 `--bench --only=Operators` 单独运行（23 case 全跑通）
- [x] 全量 bench 通过：107 cases、invalid=0、引擎 ERROR 行 0、Orphan=0、exit code == 0（两腿各验一轮）
- [x] 基准报告能区分 static/dynamic 两腿数据（**⚠ 本节下表数据已作废**——2026-09-06 复查发现 dll 身份错标 + GC 污染，修正数据见 `archive/2026-09/09-06-static-binding-bench-gc/report.md`）

## 实测两腿数据（2026-09-06，同引擎同 TS，release flavor）

> **⚠ 数据作废（2026-09-06 复查）**：①当次采数全程用的是 dynamic dll（两份日志文件均同 md5），"两腿"实为同一腿自比，dll 身份错标；②旧 static 构建的每调用 497 条全局二分查表（`find_operator_thunk`）放大了各腿差异，叠加 GC 污染使「static 全面慢 4-5x」成为假象。下表数据勿再引用。
>
> **修正后结论**（同 dll md5 核对 + `--gc` 隔离采数，三轮中位数）：static 腿 Operators 组 static/dynamic 中位 0.97x、比较运算符反快 ~20%、标量取值类快 2-5x；A2 每 (left,op) 局部 switch 表落地后全局表主路径已移除。完整数据与量化见 `archive/2026-09/09-06-static-binding-bench-gc/report.md`。

<details><summary>作废原始数据（仅留档，勿引用）</summary>

| 组别 | dynamic | static | d/s |
|---|---|---|---|
| 比较运算符（EQUAL/LESS） | 113-137 ns | 342-364 ns | ~0.35x |
| 算术运算（ADD/MULTIPLY，含 struct/scalar 右参） | 529-681 ns | 956-1146 ns | ~0.55x |
| Projection.MULTIPLY(Projection)（最大 64B 结构） | 658.0 ns | 1141.0 ns | 0.58x |

</details>

结论输入（已修正）：运算符走 `Variant::evaluate` 分发，static 腿的 dispatch thunk 反而更慢（fa599d8 实验已证）——**支持维持运算符不恢复 ptrcall thunk**。

## Notes

- 改 TS 后必须重编：`cd project && node_modules/.bin/tsc --noCheck`（产物进 `project/.godot/godotjs_ext/`，引擎加载这份）
- 验收命令与陷阱见 `.trellis/spec/godotjs-ext/build/scons-build.md`
