# 内置类型静态构造 thunk（P4 降级项恢复）

> 静态绑定收尾任务之一。P4 阶段降级项：819fb3b 因类型试配问题降级。

## Goal

恢复内置类型的 static 构造 thunk：Constructors 组当前走 dynamic 构造，static 构造 thunk 因「类型试配问题」被降级；基准数据裁决后恢复实施。

## Requirements

- 按 `.trellis/tasks/09-06-static-bindings-wrapup/design.md` §4.0 的设计实施构造器 thunks
- 与运算符 ptrcall thunk（819fb3b 判不可行项）一并基于基准数据重新裁决：可恢复则实施，确认不可行则记录证据并关闭
- 实施后 Constructors 组走 static 构造路径，回退日志（miss → WARNING，扩展类接口 miss → ERROR）行为与 P1 定稿一致

## 依赖

- **依赖 `bench-operators-expansion` 的基准数据**作为裁决输入
- 类型试配问题的历史结论见 commit 819fb3b 与设计文档 §4.0

## Acceptance Criteria

- [ ] 裁决结论落档（恢复实施或永久关闭，附基准数据证据）
- [ ] 若实施：Constructors 组 bench 显示 static 路径收益；非法参数/回退路径行为符合设计 §7.1 日志级别定稿
- [ ] 全量 bench + C++ 测试 + TS 集成测试通过（exit 0、无泄漏、Orphan StringName=0）

## Notes

- ptrcall 参数内存必须按 `MaxSizeEncodeArgType` 分配——见 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`
