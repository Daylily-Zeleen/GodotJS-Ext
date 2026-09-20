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

## 执行节奏（细则）

> **每轮必守的硬约束见 `AGENTS.md`「不绕圈」**；此处只放它没写的补充。

- **有依赖的步骤串成一次调用**：`编译 && 测试` 写成一条命令，不拆成两个 job。
- **允许探索性编译**：必须看到首次结果才知道下一步测什么时，编译是正当的。被禁的是**可预见的重复**——即事先就能确定结果不变的那些。

## 调用预算（细则）

> 与 `AGENTS.md`「不绕圈」互补：那条讲"不做什么"，这里给**上限**。

- **派发子代理必须带预算**：dispatch prompt 里写明「每审查项 ≤N 次调用、总上限 ≤M、触顶即标 UNVERIFIED 并收敛成报告」。开放式审查项（"check every call site"/"audit whether…"）不设上限必然被穷尽执行——实测 8 项审查无预算 → 145 次调用 / 81 轮次 / 21 分钟，且发送「wrap up now」后 **3 次调用**即交付完整结论。差异只在有无收口条件，不在能力。
- **主会话同样适用**：验证批次与改动批次对齐，**不按编辑逐次重跑**。实测一轮任务留下 14 份集成测试日志（`it-static*` 7、`it-dyn*` 5）、14 份构建日志（`build-qjs*` 4），同一配置多次重跑占相当比例。
- **优先专用工具**：`grep`/`glob`/`read` 直接调用，`bash` 留给跑二进制与短管线；把 grep/ls/sed/cat 包进 bash 属白烧预算（实测子代理 87 次 bash 调用里 38 次实为 grep，而同期它直调 `grep` 仅 14 次——**工具可用，包进 bash 是习惯**，不是受限）。另：子代理模板的 `tools:` 字段须写**真实工具名**（`glob`/`grep`，不是 `find`/`search`/`ast_grep`）——实测该字段写的是不存在/不匹配的名字却未阻断直调 grep，说明它是声明性的而非强制门禁，因此写错不会立刻报错，只会让"该用哪个工具"变得含糊。
- **既有缺陷出了本轮门禁就该停**：确认非本轮引入后 1~2 次调用取证即收手，结论写"遗留"，不深潜（实测 51/145 次调用耗在与本轮改动无关的既有 ctor 分发缺陷上）。
- **独立调用并发发起**：一轮内把彼此独立的读取/检索一起发（harness 支持并行），不要拆成多轮串行。
## 后台 job 与等待循环（2026-09-19 实例分析）

> `AGENTS.md`「不绕圈」第 1 条（不轮询 job）在本任务被**违反 4 次**，每次都退化成重复汇报。根因不是不知道规则，而是发了等待循环本身。

- **`bash` 的等待循环也会自转后台**：**取决于 agent 配置(omp 默认会把超时的bash调用自动转为后台任务)**，`for i in $(seq 1 90); do if grep -q "EXIT=" log; then break; fi; sleep 15; done` 这种「等构建完成」的循环跑得久（本任务最长 1200s）→ 被自动转后台 job。于是链条变成：scons 转后台 → 等待循环也转后台 → 构建完成后循环退出投递「EXIT=0」→ 触发重新响应 → 无新动作 → **重新输出一遍收尾汇报**。等待循环 job 什么都没干，只是每 15 秒读一次日志。
- **规则**：**不发等待循环**。长命令（构建/测试）发一次 `async: true`，结果**自动投递**；拿到结果继续下一步。收尾汇报只发一次；后续投递到达时若无新动作，**不产出文本、直接结束**。
- **判别信号**：`Backgrounded as job bg_N` + 自动投递的 `Background job bg_N has completed` 是**事实快照**，不是新信息。看到它先问「这产生什么新信息，谁会因此改变决定？」——答不出就不产出文本。

## 子代理派单与审查（2026-09-20 实例分析）

> **实例**：两次派 `trellis-check` 复核 `validated_call` 缺省值清理，均**在 15 分钟硬限被杀且零输出**（分别 25 / 40 次请求，从未调用 `yield`）。transcript 显示预算全花在重新推导引擎内部机制（`check_argc`、`MethodBind`、`CRASH_BAD_INDEX`）上，**一行业务结论都没留下**。
> **归因**：这不是「超时」，是**派单没有边界** + 我只会 `hub wait` 不会看进度。两次都是**烧完一无所获**，比烧时间更糟。

### harness 强制的闸门（写在 prompt 里的上限不在此列）

`~/.omp/agent/config.yml` 的 `task:` 段：

| 设置 | 语义 |
|---|---|
| `maxRuntimeMs` | 每 spawn 硬墙钟；**`0` = 禁用** |
| `softRequestBudget` | 过线注入 wrap-up 通知；**1.5× 时强制停止并 yield 部分结果** ← 唯一「自动收尾」机制 |
| `softRequestBudgetNotice` | 是否注入那条 wrap-up 通知 |
| `maxConcurrency` | 只限并发，**不限单只烧量** |

- **`task` 工具本身没有 timeout / 预算字段**（可传字段仅 `context` / `tasks[]` / `name` / `agent` / `task` / `effort` / `outputSchema` / `schemaMode` / `isolated`）→ 派单时**无法下发硬闸**。
- **prompt 里的字面约定不被强制**：实测我在 prompt 写「~12 次调用上限」，它跑到 **40 次**且仍未 yield。写上限**不能**替代规则 3。
- `maxRuntimeMs=0` 时，兜底退到 `softRequestBudget`。**注意是 1.5× 才强停**：实测跑速约 2.7 请求/分钟，预算 150 时 → 150 次（≈55 分钟）注入收尾提示、**225 次（≈83 分钟）才强制停止**。撤掉 15 分钟墙钟后，单只子代理最坏可跑到近 1.5 小时，而中间没有任何硬拦。

### 派单硬规则

1. **切成可判定项**：每项只要 `PASS / FAIL / UNVERIFIED` + 一行证据。**禁止**「尝试证伪任意路径」「尽量找反例」这类**没有自然停止点**的开放式提问——必然发散。
2. **合同先行**：把已核实的事实（符号、行号、不变量、宏体语义）**原样**塞进 `context`，并明写「**不要重新推导这些**」。落在 `report.md` / `.agent_tmp/*.log` 里的结论直接标 `accepted from log`。
3. **写死收尾条件**（**最重要**）：prompt 必须含「**达到 N 次工具调用立即 yield 已得结论，未决项标 `UNVERIFIED`**」。上限不强制，这句才保命——两次惨败的根因正是「被杀时零输出」而非「跑太久」。
4. **给 `outputSchema`**：结构化、有界的答案，比开放式提问收敛。
5. **派单后按需监工，不轮询**（与 `AGENTS.md`「不轮询 job」是同一条规则，此处不再另立对立条款）：默认靠自动投递；仅在长时间无投递、或结果明显偏离预期时，才用 `history://<id>` 看 transcript，并用 `hub cancel` 硬杀（这是唯一的硬杀开关）。**反复 `jobs`/`wait` 会顶掉自动投递**——实测本任务因此被 harness 判为工具调用循环 14 次。
6. **优先复用已有子代理**：`hub` 消息给 idle/parked 的（它已持有上下文），而非重新 spawn。
7. **agent 侧预算**优于 prompt 侧：类级 `## Investigation Budget`（见 `.omp/agents/trellis-check.md`）在每个该类型 spawn 的 system prompt 里，比派单文本写一遍粘性强得多。

## 压缩摘要的 `Next Steps` 不是待办清单（2026-09-20 实例分析）

> **实例**：本会话中一份 2831 字符的推理块被**逐字重复 225 次**（跨 34 小时、8 种近似变体），内容是把 compaction 摘要的 `Next Steps` 清单逐项重述。该现象在 Trellis 引入前的三个对照会话里重复数为 **0 / 2 / 0**。

- **格式来自 omp，不是 Trellis**：内置的 compaction 提示词强制输出 `### In Progress` / `### Blocked` / `## Next Steps` 等段（`You MUST output only the structured summary`）。
- **内容由 Trellis 决定**：计划被规定成一条不可跳过的链（`implement -> check -> update-spec -> commit`），而 `task.py archive` 之前 `status` 恒为 `in_progress`，于是"下一步"永远关不掉——`report.md` 字样横跨本会话 9 份摘要中的 7 份。
- **规则**：进入某一步之前，先确认它**没有被用户当轮指令否决、且未被已完成的工作覆盖**。摘要里的 `Next Steps` 是上一轮的意图快照，不是必须逐项偿还的欠账；发现某步已完成时，**直接做下一步，不复述这一步**。

## 工作流面包屑的维护规则（2026-09-21 沉淀）

> 背景：`.trellis/workflow.md` 是 **trellis 模板管理的文件**（在 `.trellis/.template-hashes.json` 的 managed 表里），`trellis update` 会整文件覆盖它。`[workflow-state:*]` 块又是面包屑注入器（`.omp/extensions/trellis/index.ts`）逐行正则消费的——**位置不能挪，只能编辑**。所以这里沉淀的是**规则本体**，workflow.md 里只保留每轮注入用的短句。

### 用户当轮指令优先于工作流流程

- 面包屑里的 `Flow: implement -> check -> update-spec -> commit` 是**默认顺序**，不是硬约束。**用户当轮指令优先**：例如用户说"不要提交"则 Phase 3.4 不适用、说"先只解答不改代码"则所有编辑步不适用。
- 进入任何一步前先确认：它**未被用户当轮指令否决**、**未被已完成的工作覆盖**。
- 发现某步已完成或被否决时，**直接做下一步，不复述这一步**——不要花输出复述"workflow 说要做 X 但用户说不要"。

### 不要复述面包屑内容

- `[workflow-state:*]` 块**每轮注入**，是当前上下文的一部分。不要 re-derive / restate / re-list 它或它的步骤；它已经在上下文里了。

### 维护流程：trellis update 后怎么保住定制

1. 始终用 `trellis update -s`（跳过所有用户改过的文件），**不要用 `-f`**——`-f` 会把 workflow.md 等 managed 文件整文件覆盖，抹掉本段和面包屑里的定制。
2. `trellis update` 对 `AGENTS.md` 是**块级合并**（`<!-- TRELLIS:START -->…<!-- TRELLIS:END -->` 之间可覆盖，块外保留），所以 AGENTS.md 的定制安全。
3. 万一 workflow.md 被覆盖：面包屑里需要重贴的是两行——`Flow (default order — the user's instruction for this turn overrides it…): …` 和 `Do not re-derive, restate, or re-list this flow…`。全文见本文件，复制回去即可。

### workflow.md 的死指针（已被替换为实际存在的路径）

- `.trellis/spec/cli/backend/workflow-state-contract.md` 与 `.trellis/scripts/inject-workflow-state.py` **在本检出中不存在**（上游模板的历史引用；本平台注入器是 `.omp/extensions/trellis/index.ts`，状态写者是 `.trellis/scripts/task.py`）。检索到这两个名字说明引用已过期，不要去找。

## 最终汇报（细则）

> 格式要求（目标 → 动作+证据 → 最终状态 → 遗留）见 `AGENTS.md`；此处只放它没写的补充。

- 进度文件路径：Trellis 任务 → `.trellis/tasks/<任务>/report.md`；非任务 → `.agent_tmp/progress.md`

## 文档语言

项目自述类文档（spec、任务、归档、知识文档）一律用**中文**书写；代码注释与 commit message 惯例不变（commit message 英文）。
