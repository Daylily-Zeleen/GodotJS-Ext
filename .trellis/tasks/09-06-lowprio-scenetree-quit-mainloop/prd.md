# SceneTree quit 改经 MainLoop

> 来源：`.本地文档/低优先级.md` 原始愿望条目（P3；代码中无对应实现）。

## Goal

原始条目只有一句「quit SceneTree <- MainLoop」，无上下文。本任务第一步是**考古与评估**：弄清当年想解决的问题是否真实存在、是否已被引擎演进解决；评估后再决定实施或关闭。

## Background（2026-09-06 核实）

- `src/` 中无任何 `MainLoop` 相关的自定义退出实现；测试/集成测试的退出路径是 `SceneTree::quit(code)`（`src/testing/jsb_test_runner.h`）或 `std::exit`
- headless 退出阶段崩溃（SEGV/139）是已知引擎 teardown 问题（见 spec），但与本条目的关联未证实

## Requirements

1. 考古：从旧提交/会话记录中还原「quit SceneTree <- MainLoop」的原始动机（grep git log 早期提交）
2. 评估当前退出路径：`SceneTree::quit` vs 自管 `MainLoop` 在本项目（GDExtension + headless 测试）下的语义差异
3. 结论三选一：实施（写 design.md）/ 关闭（无实际问题）/ 转化为其他任务（如退出崩溃问题的一部分）

## Acceptance Criteria

- [ ] 动机考古结论落档
- [ ] 实施或关闭的决定附理由

## Notes

- 若与 headless 退出崩溃相关，优先在 `09-06-c1-dynamic-crash` 等崩溃专项中处理
