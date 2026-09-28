# 实施计划：常驻 Node 编辑器工具进程 + 源文件注释文档

> 依赖 `prd.md`（R1–R9 / A1–A11）与 `design.md`（§2 进程设施、§3 协议、§4 文档回填与刷新、§6 注释规则、§7 消费）。
> **构建/测试命令只用规范组合**（`AGENTS.md`）；不改 `*.gen.*` 生成物。
> 本轮为**规划**：未授权实施。

---

## 阶段 A：进程设施（C++，先做，可独立验证）

- [ ] **A1** `src/internal/jsb_process.{h,cpp}`：
  - 新增 `virtual bool write_stdin(const String &p_text)`、`virtual void on_stdout_line(const String &p_line)`；
  - Windows：`on_start` 造 stdin 管道（写端留宿主、读端给子进程）并填 `STARTUPINFO`；`WriteFile` 写入；
  - POSIX：第二个 `pipe()`，子进程 `dup2(read_end, STDIN_FILENO)`；`write()` 写入；
  - 关闭/幂等规则沿用 09-26 已定范式（读端归读取线程、宿主只关自己那端、`stop()` 幂等必 join）；
  - 现有 stdout 行为不变（仍 `JSB_LOG`），仅**逐行**再喂 `on_stdout_line`。
- [ ] **A2** 用例（`src/runtime/tests/` 新增或并入既有 process 用例）：
  起一个 `node -e` 回声脚本，断言 **写一行 → 收一行**、`stop()` 后 `write_stdin` 返回 `false`、
  进程自行退出后 `_is_running()` 为假且不崩。

**门禁**：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`
+ `--headless --path ./project --jsb-run-tests` 全绿。

---

## 阶段 B：工具进程与协议（Node 侧）

- [ ] **B1** 新工具源码（建议 `scripts/jsb.editor/src/tools/jsb.editor.tools.cts`，与 `src/signature/` 并列；
  `tsconfig.tools.json` 独立 Node 目标，产物 `scripts/out/jsb.editor.tools.cjs`，
  `SConstruct` 加一条 `PresetDefine` 并 `add_install_file` 到项目数据目录——与既有 `jsb.signature.extract.cjs` 同形）。
- [ ] **B2** 启动即加载 `typescript` 一次；主循环读 stdin 行 → 分派 `op` → 写 stdout 一行。
  非 JSON 行忽略并记日志；未知 `op` 回 `{"ok":false,"error":…}`。
- [ ] **B3** `op:"signatures"`：把既有提取器逻辑（含 `.js` 枚举、`allowJs` 强制、`shouldIgnorePath`、md5 增量、
  `serialize`）**搬进来复用**（同一份代码，不做第二实现）；产物与门控语义不变。
- [ ] **B4** `op:"doc"`（可带多个 `paths`）：对每个路径读源文件 → `ts.createSourceFile`（按后缀取 `ScriptKind`）
  → 按 design §6 提取类/成员文档 → 按 `source_md5` 进程内缓存 → 应答。
- [ ] **B5** JS 支持：`allowJs` 强制 + `jsconfig.json` 二选一 + 预设补 `"allowJs": true`
  （沿用早前版本已实测的方案，见 `design.md` §9 裁决 2）。

**门禁**：手工管道冒烟——`echo '{"id":1,"op":"doc","paths":["res://…"]}' | node scripts/out/jsb.editor.tools.cjs --project <abs>`
（起一次进程、连续两条请求、断言两条应答）→ 再进阶段 C。

---

## 阶段 C：宿主接线（C++）

- [ ] **C1** 新增编辑器工具管理器（建议 `src/editor/weaver-editor/jsb_editor_tool.{h,cpp}`）：
  启动/重启、请求 id 分配、请求-应答配对、**5s 超时**、**崩溃检测与自动重启 + 本次请求重试（至多 1 次）**。
  所有失败路径按 `design.md` §9.1 输出错误（`ERR_PRINT` / `JSB_LOG(Error)`），**不得静默跳过**。
- [ ] **C2** `jsb_editor_plugin.cpp`：4 处 `_regenerate_signatures()` 触发点改为"向常驻进程发 `op:"signatures"`"，
  摘要门控保留在宿主侧（未变化时连请求都不发）。
- [ ] **C3** 文档回填：`jsb_script.cpp` 的 `_ensure_signature_manifest()`（或紧邻的新钩子）按 design §4 发 `op:"doc"`，
  应答写入 `script_class_info_`；`GodotJSScript` 新增 `String doc_source_md5_`。
  **`@help` 优先**：只在 `doc.brief_description.is_empty()` 时补写。
- [ ] **C4** 常量 doc：新增 `jsb.internal.get_script_doc(target, name, field)`（`jsb_bridge_module_loader.cpp`
  的 quit/init 与 `godot.annotations.ts` 的 `set_script_doc` 对称），并在绑定层读取，
  使常量条目也能拿到文档。
- [ ] **C5** 数据模型：`ScriptBaseDoc` 增 `description`；`ScriptSignalInfo`/`ScriptConstantInfo` 各挂 `doc`。
- [ ] **C6** `_get_documentation()`：类补 `description`；`properties[]`/`methods[]` 补 `description`
  （消掉 `// TODO: 填充完整函数文档`）；新增 `signals[]`/`constants[]`。

**门禁**：构建 + `--jsb-run-tests`；`--headless --path ./project` 全量验收绿。

---

## 阶段 D：端到端、回归、负向控制

- [ ] **D1** 夹具（`project/tests/static-members/` 或同级）：类注释（含空行分段）+ 方法/属性/信号/常量注释
  + 注释在装饰器上/下 + 空行隔离反例 + 一条与注释并存的 `@bind.help(...)`。
- [ ] **D2** JS 夹具：一份 `.js` 脚本（与等价 `.ts` 对照），验证 A4。
- [ ] **D3** C++ 用例断言 `_get_documentation()` 的 brief/description/`@help` 优先；
  GDScript 侧检查脚本读同一字典（对齐既有 `static-members-gdcheck.gd` 形态）。
- [ ] **D4 刷新机制用例**：
  - L1：改夹具脚本注释 → 重新加载 → 断言新文档（旧值不再出现）；
  - L2：`_reload()` 后文档重新填充；
  - L3：模拟请求失败/超时 → 断言**未把"未取到"写成"已同步"**（下次会重问）。
- [ ] **D5 常驻性用例**：连续两次触发，断言工具进程**只启动一次**（计数/pid 判据）。
- [ ] **D5.1 崩溃恢复用例**：kill 工具进程 → 断言 ①有错误输出且含"重启"提示；②重启后**本次请求被重试并成功**（doc 或 signatures 结果正确）。
- [ ] **D5.2 超时用例**：用测试夹具制造 >5s 的应答 → 断言 ①错误输出含 `op`/文件/超时值；②该请求**未写 md5** ⇒ 下一次查询会重问。
- [ ] **D5.3 启动失败用例**：把工具产物挪走（或指向不存在的 node）⇒ 断言有错误输出、编辑器不崩、恢复后自愈。
- [ ] **D6 负向控制（≥2 条，实测失败后还原）**：
  ① 注释关联的"空行拒绝"分支短路 ⇒ 空行用例失败；
  ② 回填顺序反转（注释无条件覆盖）⇒ `@help` 优先用例失败；
  ③ （可选）进程重启路径短路 ⇒ D5/D3 之一失败。
- [ ] **D7** 编辑器端到端：`--headless --editor --path ./project --quit-after N`
  — 起工具进程、出 `.sig`、文档可读；再跑一次 ⇒ 摘要未变则不发请求。
- [ ] **D8** 全量：`--audio-driver Dummy --headless --path ./project --verbose`
  （`FAILED = 0`、`Orphan StringName = 0`）+ `python misc/verify_codegen.py --godot "<GODOT>"`。
- [ ] **D9** TS 编译：`node node_modules/typescript/bin/tsc`（先删 `.godot/.tsbuildinfo`）。

---

## 回滚点

| 点 | 做法 |
|---|---|
| 阶段 A 后 | `Process` 的 stdin 能力是纯增量；不改任何既有调用点 |
| 阶段 B 后 | 新工具产物不安装、宿主不发请求 ⇒ 行为回到现状 |
| 阶段 C 后 | 触发点改回"起短命进程"（旧提取器产物仍在），文档回填整段关闭 |
| 阶段 D 前 | 夹具与用例可整批删除 |

## 完成定义

`prd.md` 的 A1–A11 均有实测证据（命令 + 日志路径）；`report.md` 记录各门禁与负向控制结果。
