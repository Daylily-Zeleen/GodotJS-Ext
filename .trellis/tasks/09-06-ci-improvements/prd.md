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
- ~~`ci-ios-infoplist`~~ —— **已实现（2026-09-28），已归档**。原担忧（xcframework 缺 Info.plist 导致 Xcode 集成告警）不成立：`xcodebuild -create-xcframework` 会自行生成该清单。已下载 CI run 36291137069 的 `ios-template_release-arm64-qjs-ng` 实测核对（两个 slice + 完整字段 + arm64 Mach-O）。性质为 superseded，非新增实现。详见归档目录下该任务的 prd.md。
- `~~ci-release-packaging~~` —— **已完成（2026-09-29），已归档**。按 JS 引擎出 5 个包
  （v8 / qjs-ng / jsc / node / web），包内 `.gdextension` 与包内文件 1:1 由
  `misc/release/package.py` 从构建矩阵派生（门禁与发布共用同一派生点）。真实发布链路已
  完整跑通一次：v1.0.2 的 5 个 asset 全部上传成功（upload run 36466781401）。过程中修掉
  三个只在真实发版才暴露的缺陷（CI windows editor+node 链接、`get-version.js` 不打印版本、
  `$GITHUB_OUTPUT` 格式错）。
- `libnode-typeinfo-symbols` —— 已归档（2026-09-26，依赖切换工作中解决）。
