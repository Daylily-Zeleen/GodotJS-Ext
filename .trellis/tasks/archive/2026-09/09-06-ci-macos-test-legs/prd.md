# 补 macos 测试 leg

## Goal

test job 补 macos 的 host-v8(arm64) 与 host-jsc 测试 leg：构建产物已产出且 macos runner 可跑编辑器测试，当前完全无测试覆盖

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.

---

# 结论：明确不做（2026-09-28）

用户决定**不再实施**本任务。归档性质是 closed-with-reason，**不是**已过时、
也**不是**已完成 —— 缺口仍然存在，只是决定不去补。

## 处置时的实际状态（核对 .github/workflows/ci.yml）

本任务创建时的描述是「test job 补 macos 的 host-v8(arm64) 与 host-jsc 测试 leg，
当前完全无测试覆盖」。到归档时：

| 目标 | 状态 |
|---|---|
| macos + **host-v8(arm64)** 测试腿 | ❌ 仍不存在。test job 矩阵（ci.yml:754-780）共 5 条：host-v8 / host-qjs 跑 ubuntu-22.04，host-node 跑 windows / ubuntu / macos |
| macos + **host-jsc** 测试腿 | ❌ 仍不存在。测试矩阵里连 host-jsc 这个 runtime 都没有（构建矩阵有 macos/jsc，ci.yml:551-555） |
| macos 上"完全无测试覆盖" | 部分改善：已有 `Test (host-node, macos-latest)`（ci.yml:777-780），即 macos 覆盖从 0 条变为 1 条（node 引擎） |

**注意区分**：`Test (host-node, macos-latest)` 的存在**不等于**本任务达成 ——
它补的是 node 引擎的 macos 覆盖，而本任务要的是 v8(arm64) 与 jsc 两条腿。

## 不做的理由

- 本轮主线是依赖源切换与构建链路修复；该缺口属 P3 backlog，无排期压力。
- macos 上 v8 覆盖面窄：构建矩阵中 macos/v8 为 arm64 only（ci.yml:452-458，
  v8 预编译无 universal 变体），加测试腿会额外拉长 CI 时间，收益有限。
- jsc 引擎在 macos 上无对应测试需求被提出。

## 若未来重开（触发条件）

出现下列任一情形时应重新评估：

1. 需要在 macos 上验证 v8 或 jsc 引擎的**运行时**行为（而非仅构建成功）。
2. 出现 macos 特有的 v8/jsc 回归，需要测试腿来复现与防回归。

重开时的落地方式（缺口仅在测试矩阵，产物已具备）：

- `macos-editor-arm64-v8` 产物构建矩阵中已存在（ci.yml:454-458，`macos-latest`）。
- 在 test job 的 `matrix.include`（ci.yml:754-780）补两条 entry 即可：
  - `{runtime: host-v8, os: macos-latest, artifact: macos-editor-arm64-v8,
     godot_asset: Godot_v4.7.1-stable_macos.universal.zip}`
  - `{runtime: host-jsc, os: macos-latest, artifact: macos-editor-arm64-jsc,
     godot_asset: Godot_v4.7.1-stable_macos.universal.zip}`
- macos 腿需注意引擎的原生 arm64 启动（见 ci.yml:862-880 的 dylib 别名处理与
  `arch -arm64` 相关说明）：Godot 对 universal 引擎在两种架构下都给 `universal`
  feature，但只有真 arm64 进程才匹配 `arm64` key。

本任务归档为 won't-do，非实现完成、非过时。
