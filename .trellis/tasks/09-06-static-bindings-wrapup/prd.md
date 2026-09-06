# 静态绑定分支收尾（父任务）

> 分支 `feature/static-bindings` 主体（P0-P5）完成后的收尾任务集。

## Goal

完成 `feature/static-bindings` 分支的收尾项：基准数据补齐、P4 降级项恢复、dynamic 路径崩溃专项、CI 双腿对比、缓冲区修复审计。父任务持有任务地图与跨子任务约束，不做直接实现。

## 分支现状基线（2026-09-06）

- HEAD：`5af231d`（已推送）
- **static dll 路径**：全部验证通过——doctest exit=0；TS 集成测试 exit=0 + COMPLETED；bench 89 cases invalid=0、零非预期错误、Orphan StringName=0
- **dynamic 路径**：api_tool ptrcall 参数缓冲区溢出已修复（5af231d），det==0 flood 485→0；**修复后仍有 SEGV 139（Windows）/挂死（linux CI）** → C1 专项（子任务 `c1-dynamic-crash`）
- api_tool 溢出 bug 在 main 分支也存在（17 处参数存储分配点同源），已在 main 复现并验证修复有效

## 任务地图与依赖

| 子任务 | 内容 | 优先级 | 依赖 |
|---|---|---|---|
| `bench-operators-expansion` | bench Operators 组扩展 | 待做 | 无 |
| `static-ctor-thunks` | P4 降级项恢复 | 待做 | 依赖 `bench-operators-expansion` 的基准数据裁决 |
| `c1-dynamic-crash` | dynamic 崩溃/挂死专项 | **专项** | 无（既有问题，71f7545 基线同样崩） |
| `ci-benchmark-both-legs` | CI benchmark 双腿回填 | 阻塞 | **阻塞于 `c1-dynamic-crash`** |
| `buffer-fix-static-audit` | 缓冲区修复 static 路径审计 | 低 | 无 |

## 相关文档

- 设计文档：`design.md`（本任务目录）
- ptrcall 编码规范：`.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`

## Acceptance Criteria

- [ ] 5 个子任务全部完成并归档
- [ ] P4 降级项（运算符 ptrcall thunks、构造器 thunks）基于基准数据做出明确决策（恢复或永久关闭并记录理由）
- [ ] CI benchmark job 恢复双腿（static + dynamic）对比数据
- [ ] 父任务整合验收：bench 89+ cases 双腿 invalid=0、零非预期错误、Orphan StringName=0；C++/TS 测试 exit=0
