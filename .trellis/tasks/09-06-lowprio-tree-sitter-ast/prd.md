# tree-sitter 解析 AST 替代正则

> 来源：`.本地文档/低优先级.md` tree-sitter 条目（P3，编辑器优化）。

## Goal

用 tree-sitter 与 tree-sitter-typescript 解析 TypeScript AST，替代当前基于正则的解析（场景/资源类型提取、脚本结构分析等处）。

## Requirements

- 先盘点仓库中所有基于正则的 TS/JS 源码解析点（`src/editor/codegen/`、`scripts/jsb.editor/src/` 中 grep 正则字面量），列出清单
- 评估 tree-sitter 集成载体：Node 侧（`scripts/jsb.editor/`，npm 依赖）或 C++ 侧（第三方库），二选一并说明理由
- 逐点替换：语义等价为验收口径（相同输入产出相同的解析结果集合）
- 性能护栏：替换后场景/资源生成耗时不劣化

## Acceptance Criteria

- [ ] 解析点清单落档（文件 + 行号 + 现用正则）
- [ ] 载体决策记录（Node vs C++，含依赖体积与构建影响）
- [ ] 替换点全部通过语义等价对比
- [ ] codegen 全量校验通过（`misc/verify_codegen.py` 与基线 diff 仅预期变化）

## Notes

- 基线校验规程见 `.trellis/spec/godotjs-ext/test/codegen-baseline.md`
