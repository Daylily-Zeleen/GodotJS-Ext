# 项目流程守则

> 这些是**硬性流程约束**，适用于所有任务与会话。违反会直接造成损失（丢工作区状态、污染仓库、破坏用户审查流程）。

## git 操作

1. **绝不未经用户确认执行 `git commit` / `git push`**。需要提交时主动询问，说明拟提交的内容与理由
2. **绝不未经用户确认还原文件**（`git checkout --`、`git restore`、`git clean` 等任何丢弃工作区改动的操作）。必要时主动询问，列出拟还原的文件及理由
3. 用户暂存区里的内容可能正是用户要审查的 diff——**不要 unstage / 改动暂存区**，除非用户要求

## 临时文件

- 所有临时产物（测试脚本、诊断日志、一次性代码）放 `./.agent_tmp/`；不存在则先创建
- 禁止散落在项目根、`src/`、`project/`、`scripts/` 等业务目录
- 任务结束按需清理；`.agent_tmp/` 已在 `.gitignore`

## 任务管理（Trellis）

- 任务状态、待办、归档一律走 `.trellis/tasks/`（`python ./.trellis/scripts/task.py`）；不在根目录或其他位置维护平行的任务清单文件
- 任务过程中的可沉淀知识写 `.trellis/spec/`（长期规范）或 `.trellis/tasks/<任务>/`（任务级研究/design）
- 已完成任务经 `task.py archive` 归档到 `.trellis/tasks/archive/`；重要设计结论随手沉淀进 spec，不散落仓库外

## 文档语言

项目自述类文档（spec、任务、归档、知识文档）一律用**中文**书写；代码注释与 commit message 惯例不变（commit message 英文）。
