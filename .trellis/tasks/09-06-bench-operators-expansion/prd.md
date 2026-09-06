# 操作符基准测试扩展

> 静态绑定收尾任务之一。

## Goal

扩展 benchmark 的 Operators 组：当前仅 3 个 Vector2 case，operator dispatch 覆盖不足，无法为「运算符/构造器 ptrcall thunk 是否恢复」提供裁决数据。

## Requirements

- Operators 组补充跨类型运算符 case：至少覆盖 Basis / Transform3D / Projection 的 struct 参数运算（当前 det==0 溢出 bug 的实证来源类型），以及 int/float 基础类型运算符对照
- case 生成走既有设施：`misc/build/generate_benchmark_cases.py`（如适用）或 `project/tests/benchmark/` 既有组结构
- 保持与现有 bench 框架一致（`--only=<组>` / `--calls=<N>` 过滤语义不变）

## 依赖

- 无前置依赖
- 产出被 `static-ctor-thunks`（P4 降级项恢复决策）消费

## Acceptance Criteria

- [ ] Operators 组新增 case 可通过 `--bench --only=Operators` 单独运行
- [ ] 全量 bench 通过：invalid=0、零非预期错误、Orphan StringName=0、exit code == 0
- [ ] 基准报告能区分 static/dynamic 两腿数据（为降级决策提供依据）

## Notes

- 改 TS 后必须重编：`cd project && node_modules/.bin/tsc --noCheck`（产物进 `project/.godot/godotjs_ext/`，引擎加载这份）
- 验收命令与陷阱见 `.trellis/spec/godotjs-ext/build/scons-build.md`
