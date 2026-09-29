# 执行计划：TODO 全量调研

## 前置

- 范围与分类口径见 `prd.md`
- 调研基线：`HEAD = f6e62c5`（工作树 clean）

## 步骤

### 1. 建立可复现的清单

```bash
git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc > .agent_tmp/todo_dump.txt
```

产出 188 行，编号 1..188。**后续所有分类的编号必须与这份清单一致**，报告末尾附复现命令。

### 2. 分组读取上下文

按「模块族」分批读源码上下文（每批一起读，避免逐条跳文件）：

| 批次 | 范围 | 条数 |
|---|---|---|
| B1 | `src/runtime/weaver/`（script / script_language / script_instance / resource_loader） | 约 55 |
| B2 | `src/runtime/bridge/`（environment / class_info / type_convert / module* / 其它） | 约 60 |
| B3 | `src/runtime/impl/`（jsc / quickjs / web / node） | 约 30 |
| B4 | `src/editor/` + `src/internal/` + `src/api_tool/` + `src/compat/` + `scripts/` + `.clang-format` | 约 25 |

每批动作：

1. 读 TODO 所在函数的完整实现（`read` 行区间，不只读注释行）
2. 对「疑似已过时」的条目，用 `git log -S "<原文片段>"` 确认是否已被后续提交实现
3. 对「疑似受上游限制」的条目，在 `third/godot-cpp` 与引擎头文件中确认接口是否存在
4. 产出该批的分类草稿（文件:行 → 类别 → 理由）

### 3. 汇总裁定

- 合并四批草稿，对齐编号 1..188
- 复核跨批重复的同一句注释（如 `dirty but approaching solution for hot-reloading` 出现 3 次）：
  同类合并说明，但仍逐行计数
- 统计各类条数；对 A 类按「可独立成 PR」分组

### 4. 产出报告

`.trellis/tasks/09-29-todo-audit/research/todo-audit.md`：

1. 清点范围与排除项（与 188 口径对齐）
2. 分类统计表
3. **全量逐条表**：`# | 文件:行 | 原文 | 类别 | 理由`
4. A 类最小改动清单（按 PR 分组）
5. B 类「删 / 改写」建议
6. 结论与建议的推进顺序

## 验证

- 报告条目数 == 188（脚本核对）
- 随机抽 10 条回读源码，确认类别与理由成立
- 不做任何代码改动：交付时 `git status --porcelain` 只应出现任务文档

## 回滚点

本任务纯调研，无代码改动；回滚 = 丢弃 `.trellis/tasks/09-29-todo-audit/`。

## 并行化

B1–B4 四批互不依赖，可作为独立只读调研切片并行（每批给足：批次范围、编号清单、分类口径、输出格式）。
