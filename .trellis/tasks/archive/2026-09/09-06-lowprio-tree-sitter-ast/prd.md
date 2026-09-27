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

---

# 结论：否决（2026-09-28）

**不引入 tree-sitter。** 依据 `research/tree-sitter-evaluation.md`（评估日期 2026-09-24）：

| 目标 | 判定 |
|---|---|
| 替换生成 .d.ts 的重写点（#5–#7） | ❌ 输入是我方自己生成、形状已知的文本，零健壮性收益，纯增依赖 |
| 替换用户源码类名提取（#1–#4） | ⚠️ 健壮性确有提升，但失效面很窄（类头必须单行），代价是 ~9 MB 语法源码 + 六平台构建接线 + MSVC 巨型 TU 特例 |
| 支撑参数/返回值/信号参数 | ❌ 类型信息不可反射也不必解析 —— 既有注解路线（@export_*/@signal/@rpc）已覆盖，扩展成本更低 |

实测代价（GitHub API / npm 实测）：tree-sitter-typescript 语法源码 8.34 MiB、
tree-sitter-javascript 2.72 MiB、npm 包解包体积 37.0 MiB。

**触发条件**（若未来重开）：出现「必须解析用户源码、且注解无法表达」的需求
（跨语言类型检查、编辑器内跳转定义/重命名重构）。届时最小方案：仅 JS 语法、
仅编辑器腿、仅替换 #1–#4，保持「不加载模块、纯文本、任意线程」约束。

本任务归档为 closed-with-reason，非实现完成。
