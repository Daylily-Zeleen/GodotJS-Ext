# CI 改进收尾

## Goal

CI 缺口归组父任务：测试矩阵补齐（ci-macos-test-legs）、发布打包收尾（ci-release-packaging）、iOS Info.plist 核查（ci-ios-infoplist）、libnode typeinfo 符号解阻（libnode-typeinfo-symbols）；均低优先级（P3），基于 2026-09-06 对 .github/workflows 的现状核查

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.

## 子任务状态

- ~~`ci-macos-test-legs`~~ —— **明确不做（2026-09-28）**，已归档。该任务要补的
  macos + host-v8(arm64) / host-jsc 测试腿至今仍未实现（test job 矩阵
  ci.yml:754-780 只有 5 条，macos 上仅 host-node 一条）。缺口真实存在，只是决定
  不去补；归档性质为 won't-do，非完成、非过时。详见归档目录下该任务的 prd.md。
- `ci-release-packaging`、`ci-ios-infoplist` —— 仍为 planning。
- `libnode-typeinfo-symbols` —— 已归档（2026-09-26，依赖切换工作中解决）。
