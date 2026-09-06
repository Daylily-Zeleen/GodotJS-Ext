# AGENTS.md

> 本项目由 Trellis 管理开发流程：规范在 `.trellis/spec/`，任务在 `.trellis/tasks/`，本文件只保留入口指引。

## 项目约束速览（细则见 spec）

- **git**：不自行 commit/push/还原文件，一切经用户确认
- **临时文件**：一律放 `./.agent_tmp/`
- **构建**：编译命令只用规范组合（见 spec），绝不 `scons --clean`
- **生成文件**：`*.gen.*` / `*.def.*` 禁止直接编辑，改生成逻辑
- **语言**：项目文档用中文；commit message 英文

## Trellis 入口

- 开发工作流：`.trellis/workflow.md`（阶段、任务生命周期、spec 注入）
- 编码规范（spec）：`.trellis/spec/godotjs-ext/`（主代码 cpp / build / test 三层）、`.trellis/spec/godot-cpp/`、`.trellis/spec/third/quickjs-ng/`、`.trellis/spec/guides/`（流程守则 + 思维指南）
- 任务目录：`.trellis/tasks/`（`python ./.trellis/scripts/task.py list` 查看；已完成任务在 `archive/<年-月>/`）
- 包映射：`.trellis/config.yaml`（godotjs-ext / godot-cpp / third/quickjs-ng，默认包 godotjs-ext）
