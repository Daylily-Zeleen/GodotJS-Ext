# api_tool 缓冲区修复 static 路径适用性审计

> 静态绑定收尾任务之一。低优先级。

## Goal

最终审计确认：ptrcall 参数缓冲区修复（参数/返回内存按 `sizeof(internal::MaxSizeEncodeArgType)` 分配，commit 5af231d）在 static 路径下无遗漏调用点。

## 现状

- static dll 的 validated_call 已按同方案修复
- 源码级已覆盖全部 17 处参数存储分配点（main 分支逐一确认同源）
- static 路径不经过 api_tool（static-first dispatch），理论不受影响——需审计实证该论断

## Requirements

- 枚举全部 ptrcall 参数内存分配点（grep `stack_alloc` / `alloca` 于 `src/api_tool/` 与 `src/static_binding/`），确认无一按 `sizeof(Variant)` 分配
- 确认 static 路径的参数编组（`marshal_one` / thunks）无独立分配点绕过 MaxSizeEncodeArgType
- 若发现遗漏点：修复并同步更新 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md` 的「强制规则」

## Acceptance Criteria

- [ ] 分配点清单落档（文件 + 行号 + 分配方式），17 处全部为 MaxSizeEncodeArgType 或等价
- [ ] static 路径审计结论明确（有/无遗漏；有则已修）
- [ ] bench 全量回归通过（exit 0、invalid=0、Orphan StringName=0）

## Notes

- 审计是只读为主的小任务，PRD-only 即可，无需 design.md
