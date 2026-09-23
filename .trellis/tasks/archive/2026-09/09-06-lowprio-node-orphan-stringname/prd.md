# 排查 node 构建 Orphan StringName 泄漏

> 来源：`.本地文档/低优先级.md` node Orphan 条目（P3，缺陷排查）。

## Goal

定位并修复 node 模式独有的 Orphan StringName 泄漏（v8/quickjs 构建无此现象）。

## Background

- 复现口径：`godot --headless --path ./project -- --bench`（`--verbose` 下 grep Orphan）；node 构建的泄漏计数为正，其他引擎为 0
- node 构建仅 windows 可用（macos/linux 被 libnode typeinfo 符号问题阻塞，见 `09-06-libnode-typeinfo-symbols`）——排查只能在 windows 上做
- 疑点方向：libnode 内嵌 V8 的 StringName/持久句柄生命周期与我方 `jsb_environment.h` delegate 体系的交互；node 启动/关闭时序与普通 V8 不同

## Requirements

1. 在 windows node 构建下复现并记录泄漏规模（bench 哪些 case 产生、计数是否随 calls 增长）
2. 定位泄漏源：区分我方桥接层泄漏 vs libnode 内部行为
3. 我方可修则修；libnode 侧问题并入 `09-06-libnode-typeinfo-symbols` 的上游沟通

## Acceptance Criteria

- [ ] 泄漏源定位结论落档（复现步骤 + 最小复现 case + 归因）
- [ ] 我方侧修复（若有）后 bench Orphan 计数为 0
- [ ] libnode 侧问题已并入上游沟通任务

## Notes

- 验收标准与判据见 `.trellis/spec/godotjs-ext/test/index.md`（Orphan StringName = 0）
- 诊断日志放 `.agent_tmp/`
