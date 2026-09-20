# AGENTS.md

> 本项目由 Trellis 管理开发流程：规范在 `.trellis/spec/`，任务在 `.trellis/tasks/`。
> 本文件保留**每轮必守的硬约束**与入口指引。

## 项目约束

- **git**：不自行 commit / push / 还原文件。授权**仅限当轮明确说出的动作**，不跨轮继承、不外推：「修复 X」只授权改文件，不含提交；「任务做完了」不构成提交理由。改文件 ≠ 提交 ≠ 推送，三者分别授权
- **临时文件**：一律放 `./.agent_tmp/`
- **构建**：编译命令只用规范组合（见 spec），绝不 `scons --clean`
- **生成文件**：`*.gen.*` / `*.def.*` 禁止直接编辑，改生成逻辑
- **语言**：项目文档用中文；commit message 英文

## 不绕圈（最高优先级：违反 = 白烧用户的 token 与时间）

动手前必须能一句话答出：**这一步产生什么新信息，谁会因此改变决定？** 答不出 → 不做。

1. **不轮询 job**：后台 job 完成会自动投递；`hub wait` 只是等一个窗口，`Still Running` 不是失败。反复 `wait`/`jobs` 会顶掉自动投递，退化为自行复述
2. **不重复查已知**：已在上下文里的结论不再重查一遍
3. **不重复重编**：改头文件 / 注释 / 参数名会触发全量重编，单次 100~190s。为措辞、风格做验证性编译，禁止；要对照编译就一次列全所有变体，每个只编一次
4. **旧结论 ≠ 现状**：记忆中的故障结论必须先重新验证再用（例：曾测代理不通便长期 `-c proxy=` 覆盖，代理早已恢复仍失败，而原样 `git push` 本来可用）。判断外部故障时用不同路径各测一次，差异即根因
5. **不为次要问题验证**：措辞不确定就写中性描述（零验证成本）。验证成本 > 出错成本时，不验证
6. **外部失败：先查配置，再有限重试**。网络 / 凭据 / 服务 / 构建等外部失败**允许重试**，但顺序与上限固定：
   - 先查配置与环境（不拿记忆里的旧结论当现状，见第 4 条）
   - 次数 ≤3、总时长 ≤2 分钟；每次调整参数后再试，不是原样循环
   - 触顶即停，汇报「已排除什么 / 当前判断 / 下一步命令」，交用户定

## 汇报

- **对话只承载两种输出：需要用户决策的问题，以及任务结束时的一次收尾汇报。** 工具调用卡片本身就是进度，不再为「说明正在做什么」产出文本；后续自动投递到达且无新动作时，**不产出文本、直接结束**
- 任务结束 → 按次条格式做完整汇报，不做中途完整汇报
- 收尾覆盖全流程：**目标 → 动作+证据 → 最终状态 → 遗留**，禁止只汇报最后一步
- 「已修复 / 已验证 / 配置会生效」这类断言必须有实测证据；机制性断言未实测不得声称
- **进度落文件**（任务 → `.trellis/tasks/<任务>/report.md`；否则 `.agent_tmp/progress.md`）：每完成一个可验证的步骤追加一行——原来写进对话的那句进度，改为写进这里。汇报从文件出——对话会被压缩，文件不会

## Trellis 入口

- 开发工作流：`.trellis/workflow.md`（阶段、任务生命周期、spec 注入）
- 编码规范（spec）：`.trellis/spec/godotjs-ext/`（主代码 cpp / build / test 三层）、`.trellis/spec/godot-cpp/`、`.trellis/spec/third/quickjs-ng/`、`.trellis/spec/guides/`（流程守则 + 思维指南）
- 任务目录：`.trellis/tasks/`（`python ./.trellis/scripts/task.py list` 查看；已完成任务在 `archive/<年-月>/`）
- 包映射：`.trellis/config.yaml`（godotjs-ext / godot-cpp / third/quickjs-ng，默认包 godotjs-ext）
- **每轮的工作流面包屑可跳过**：用户 prompt 里单独出现 `no-trellis` 时，该轮不注入 `<workflow-state>`（开关见 `.trellis/config.yaml` 的 `prompt_injection.skip_keyword`）。用户说"这轮别管工作流"即用此词

<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

This project is managed by Trellis. The working knowledge you need lives under `.trellis/`:

- `.trellis/workflow.md` — development phases, when to create tasks, skill routing
- `.trellis/spec/` — package- and layer-scoped coding guidelines (read before writing code in a given layer)
- `.trellis/workspace/` — per-developer journals and session traces
- `.trellis/tasks/` — active and archived tasks (PRDs, research, jsonl context)

If a Trellis command is available on your platform (e.g. `/trellis:finish-work`, `/trellis:continue`), prefer it over manual steps. Not every platform exposes every command.

If you're using Codex or another agent-capable tool, additional project-scoped helpers may live in:
- `.agents/skills/` — reusable Trellis skills
- `.codex/agents/` — optional custom subagents

Managed by Trellis. Edits outside this block are preserved; edits inside may be overwritten by a future `trellis update`.

<!-- TRELLIS:END -->
