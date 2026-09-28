# 进度：解析注释为编辑器提供脚本文档（常驻 Node 工具进程）

任务目录：`.trellis/tasks/09-28-comment-doc-annotation/`
状态：**实现完成并实测验证**（阶段 A–D 全部落地；`A1`–`A11` 见下）

---

## 目标（用户原话要点）

1. 解析**源文件**（`.ts`/`.js`）注释，为 Godot 编辑器提供脚本类/成员文档。
2. `@bind.help()` **保留**，且**优先**于注释（「旧的注解仍然有效」）。
3. **常驻 Node 工具进程**（签名提取 + 文档提取），NDJSON over stdin/stdout。
4. 文档**直接写进 `ScriptClassInfo`**（不落盘），带刷新机制。
5. 崩溃**不静默**：告知 + 重启 + 重试本次请求；请求**超时 5s**并报出 op/请求/超时值。
6. `.js` 与 `.ts` 并列（硬前置：项目内 `typescript` 包）。
7. `@deprecated`/`@experimental` 的 JSDoc tag **暂不做**。

---

## 架构（关键裁决：**编辑器推**，不是运行时拉）

`runtime` 库不能调用 `editor` 库（依赖方向单向），而 `_get_documentation()` 在 runtime 侧被引擎调用
⇒ 运行时无法按需向工具进程请求。故：

```
编辑器触发（与 _regenerate_signatures 同节奏）
  └─ 常驻工具 {"op":"doc","all":true}
       └─ 应答 {"docs":{"res://<src>":{class:{brief,description},members:[...]}, ...}}
            └─ JsbBridgeTable::apply_script_docs（跨 DLL 唯一通道）
                 └─ 运行时 ScriptDocStore 暂存（键 = res:// 源路径）
                      └─ GodotJSScript::load_module_immediately() → _apply_pending_source_doc()
                           └─ 写 ScriptClassInfo（@help 优先）→ _get_documentation()
```

**为什么必须暂存**：编辑器安装/重扫时只处理文件、不实例化脚本，而文档只能在类信息就绪后挂上去。
`ScriptDocStore` 是进程级容器（`src/runtime/bridge/jsb_script_doc.{h,cpp}`），
在 `GodotJSScriptLanguage::_finish()` 显式清空（与 `SharedStatics::clear()` 同因：进程级容器持 String 活过 `StringName::cleanup()`）。

---

## 改动清单

### 新增
| 文件 | 内容 |
|---|---|
| `scripts/jsb.tools/src/jsb.doc.extract.cts` | 注释关联与提取（规则见下） |
| `scripts/jsb.tools/src/jsb.editor.tools.cts` | 常驻工具进程（NDJSON、`op` 分派、doc 缓存） |
| `scripts/jsb.tools/test/test-doc-extract.mts` | 注释规则回归（33 条断言，node --experimental-strip-types） |
| `scripts/jsb.tools/{package.json,tsconfig.json}` | 独立 pnpm 子项目（见「结构调整」） |
| `src/editor/weaver-editor/jsb_editor_tool.{h,cpp}` | `EditorToolClient`：启动/重启、5s 超时、崩溃重试 |
| `src/runtime/bridge/jsb_script_doc.{h,cpp}` | `ScriptDocStore` 暂存 |
| `src/runtime/tests/test_jsb_process.h` | 双向管道用例（真实 node 回声子进程） |
| `src/editor/tests/test_jsb_editor_tool.h` | 协议层用例（常驻/自愈/未知 op） |

### 修改
| 文件 | 内容 |
|---|---|
| `src/internal/jsb_process.{h,cpp}` | `write_stdin` / 行与 EOF 回调 / 两端 stdin 管道 +**修了一个既有缺陷**（见下） |
| `src/internal/jsb_bridge_abi.h` | `JsbVariantArgFn` + `apply_script_docs` 表项 |
| `src/runtime/internal/jsb_bridge_table.cpp` | `bridge_apply_script_docs` |
| `src/runtime/bridge/jsb_class_info.h` | `ScriptBaseDoc::description`、信号/常量各挂 doc |
| `src/runtime/weaver/jsb_script.{h,cpp}` | `_apply_pending_source_doc`、`_get_documentation()` 补 description/signals/constants |
| `src/runtime/weaver/jsb_script_language.cpp` | `_finish()` 清空 doc store |
| `src/runtime/tests/test_jsb_static_members.h` | 文档链路用例（暂存/消费/`@help` 优先/擦除语义） |
| `src/editor/weaver-editor/jsb_editor_plugin.{h,cpp}` | 常驻客户端接线、`_regenerate_script_docs`、3 个工具产物的安装 |
| `src/editor/weaver-editor/jsb_editor_pch.h` | `JSB_EDITOR_TOOL_NAME`（单一来源） |
| `scripts/jsb.tools/src/jsb.signature.extract.cts` | 导出 `runExtraction`/`listGodotScripts`/`serialize`；`.js` 枚举 + `allowJs` + jsconfig 二选一；`require.main` 守卫 |
| `SConstruct` | 嵌入/安装 3 个工具产物 |
| `scripts/presets/jsconfig.json.txt` | 补 `"allowJs": true` |
| `project/tests/static-members/static-members-target.ts` | `greet` 加 `@bind.help("explicit method brief")`（作为优先级的夹具） |

---

## 注释规则（用户拍板，`design.md` §6）

1. 取「声明前的**最后一段** `/** */`」，声明 = 其全部装饰器/修饰符 + 本体 ⇒ 注释在装饰器**上/下**都算；
2. 注释末端与声明之间出现**空行** ⇒ 无文档；
3. 注释之前**紧邻另一条注释** ⇒ 不取（TS 会把两条合成一个 span，实测踩到）；
4. **首行 = brief，全文 = description**；只取紧邻声明那段，不看文件头。
5. **必须源码扫描**：注释写在装饰器之后时 `node.jsDoc` / `ts.getJSDocCommentsAndTags` **都为空**（实测）。

覆盖：类 / 方法 / 属性 / getter / `accessor` 信号 / static 常量。

---

## 评审轮修复（用户 2026-09-28 指出，全部实测）

| # | 问题 | 处理 |
|---|---|---|
| 1 | 新文件的版权头带了 `Copyright (c) Contributors of GodotJS` | 按 `spec/godotjs-ext/cpp/index.md` 的质量检查改为只留项目自身版权（`jsb_editor_tool.cpp`、`jsb_script_doc.h` 两处） |
| 2 | `doc` 成员在 release 下仍占空间 | **按用户裁决**：不改基类，只把每个 `doc` 成员包进 `#if JSB_TOOLS`（调用点零改动）。同时把**既有的** `ScriptPropertyInfo::doc` / `StatelessScriptClassInfo::doc` 一并门控 —— 它们原先无条件存在。实测 `target=template_release` 构建通过（证明没有 `JSB_TOOLS` 之外的引用） |
| 3 | `signals[]` / `constants[]` 没填 deprecated/experimental | 输出侧补齐（与 method/property 同一字段面）；解析侧补读 `MemberDocMap`（常量按 JS 属性名查 map，信号用其 name）。**已实测确认 TS 侧无可达写法**：`@bind.deprecated()` 叠在 `@bind.signal()` / `@bind.exposed.const()` 上会被类型检查拒绝（TS1240），故该路径当前只对**纯 JS 项目**可用；代码注释已写明 |

`@ts-expect-error` 说明：夹具里 `@bind.help("explicit method brief")`（为 R6 优先级的断言而加）
在类型上无法解析（现代装饰器叠加，TS1241），而项目预设 `noEmitOnError: false` 使其仍被 emit
（`@bind.help` 在 VS Code 里实测可用）。加 `@ts-expect-error` 后 `tsc` 类型检查保持全绿，运行期行为不变。

---

## 结构调整（用户 2026-09-28 指出）

**问题**：`scripts/jsb.editor/src/signature/` 这个名字已经名不副实（现在含文档提取与常驻工具进程），
且它**不参与** `jsb.editor` 的 AMD bundle —— 借住别人的包、还得靠 `exclude` 排除，形态别扭。

**处理**：提为**独立 pnpm 子项目 `scripts/jsb.tools/`**（与 `jsb.editor` / `jsb.runtime` 同级）。

```
scripts/jsb.tools/
  package.json      @godot-js/jsb-tools  build=tsc  test=node --experimental-strip-types ...
  tsconfig.json     module:commonjs, rootDir ./src, outDir ../out
  src/jsb.signature.extract.cts   签名提取（runExtraction / listGodotScripts / serialize）
  src/jsb.doc.extract.cts         注释关联与提取
  src/jsb.editor.tools.cts        常驻工具进程（NDJSON）
  test/test-doc-extract.mts       33 条注释规则断言
```

- `scripts/jsb.editor` 回到纯 `tsc`，`tsconfig.json` 里那行 `exclude: ["src/signature"]` 删除
  （不再需要：新包不参与任何 bundle）
- `SConstruct` **无需改动**：`pnpm -r build` 自动纳入新包、产物仍落 `scripts/out/`
- 所有引用路径同步更新（`jsb_editor_tool.h`、`jsb_signature.h`、`test_jsb_signature.h`、安装注释、
  `spec/.../generated-files.md`）

**实测**：`pnpm install`/`pnpm build` 识别 5 个 workspace（原 4 个）→ `jsb.tools build: Done`；
工具单测 33/33；全链路（构建 / 两套 C++ 套件 / 全量验收）全绿。

---

## 验证（全部实测，`.agent_tmp/`）

| 项 | 命令 | 结果 | 日志 |
|---|---|---|---|
| 构建（editor） | `scons platform=windows target=editor compiledb=no debug_symbols=yes dev_build=yes tests=yes -j5` | `rc=0` | `doc_r6_build.log` |
| 构建（**release**，验证门控） | `scons platform=windows target=template_release compiledb=no dev_build=no -j5` | `rc=0` | `doc_r3_release.log` |
| C++ 套件 | `<godot> --headless --path ./project --jsb-run-tests` | `rc=0`；runtime **72/72** cases、**1103/1103** assertions；editor **6/6**、**38/38** | `doc_r6_tests.log` |
| 全量验收 | `<godot> --audio-driver Dummy --headless --path ./project --verbose` | `rc=0`；**Orphan StringName = 0**、`COMPLETED = 1`、`FAILED = 0`、`STATIC-MEMBERS-GD-OK = 1` | `doc_r6_accept.log` |
| TS 类型检查 | `cd project && node node_modules/typescript/bin/tsc`（先删 `.godot/.tsbuildinfo`） | `rc=0`、**0 errors** | — |
| 注释规则 | `node --experimental-strip-types scripts/jsb.editor/test/test-doc-extract.mts` | `checks=33 failures=0` | — |
| codegen 基线 | `python misc/verify_codegen.py --godot <GODOT>`（先 `--update-baseline`） | `rc=0`、`✅ 校验通过` | `doc_codegen_up.log` / `doc_codegen_verify.log` |
| 编辑器端到端（工具协议） | 见 editor 套件 | 同一进程连续应答、业务错误不杀进程 | `doc_d6_tests.log` |

### 负向控制（3 条，均**实测失败**后还原并复验绿）

| # | 削减 | 结果 |
|---|---|---|
| ① 双向管道写短路（`_write_stdin` 恒 true） | `test_jsb_process.h(161/163/167)` 三条 ERROR、`69/70` | `doc_a1_negctl_tests.log` |
| ② 成员级 `@help` 优先被反转（注释无条件覆盖） | `test_jsb_static_members.h(794)` ERROR（`@bind.help wins`） | `doc_negctl2_tests.log` |
| ③ 注释关联的**空行拒绝**分支短路 | `test-doc-extract`：`failures=1`，`member 'plain' must NOT carry a doc` | 会话内 |

> ①的第一次尝试砍在类级守卫上 ⇒ **用例仍全绿**（那条分支没有覆盖）。改成成员级守卫才复现失败。
> **教训：负向控制必须确认削减真的落在被测路径上**（与 09-26 记录的教训同型）。

### 超时路径（临时桩，已还原）

把 `project/.godot/jsb.editor.tools.cjs` 换成"只读不回"的桩进程后跑套件：

```
ERROR: the editor tool request timed out after 5000ms: op='ping', request={"id":1,"op":"ping"}
       (the tool process was kept alive; it may still be working on it)
```

实测：**恰好 5000ms**、报文含 op/请求/超时值、**不重启进程**、不挂死（整轮 42.6s 正常收场）；
还原真实工具后 6/6 + 72/72 复验绿（`doc_restore_tests.log`）。

---

## 顺带修掉两个**真实的既有/新引入缺陷**

### 1. Windows `_flush()` 的终止符缺失（既有）

用 `MultiByteToWideChar(..., p_length)` 转换后**没写终止符**，而下一行 `String(buffer.ptr())`
按 C 串读取 ⇒ **每行都带上未初始化尾部垃圾**（实测日志 `echo:pingn`、`echo:pong꘾Ʌ`）；
对 UTF-8 子进程输出用 ACP 解码还会乱码。
修法：先按 UTF-8 解码并用**往返比对**判定，失败才退回 ACP 且显式 `buffer[num] = 0`。

### 2. `EditorToolClient` 重启路径上的 use-after-free（本次引入，自查发现）

`request()` 的重启/写失败分支原先直接 `process_.reset()`，**绕过了 `Process::stop()` 的 join**；
而读取线程持有 `ProcessImpl*` 裸指针 ⇒ 销毁即 UAF。这正是 09-26 为短命进程修过的同一类缺陷
（`jsb_process.cpp` 的 `Process::stop()` 注释写明「不按 `is_running()` 提前返回」的原因），
我在新代码里以"直接 reset 更快"的形式重新引入了它。
修法：统一走新增的 `_teardown_process()`（`stop()` + `reset()`），三处调用点全部改掉。

---

## 已知边界（如实记录）

- **`_get_member_line()`** 仍是 `-1`（本次不做源码行号）。
- **`@deprecated` / `@experimental` 的 JSDoc tag 不生效**（用户裁决：暂不做），仍走装饰器。
- **`_get_documentation()` 不遍历 base 链**：本脚本没写注释时不会继承基类脚本的文档（与 `@help` 现状一致）。
- **编辑器安装路径未在无头模式端到端跑通**：`try_install_project_files()` 在 headless 下会跳过安装
  （`jsb_editor_plugin.cpp:1067` 的既有行为），因此工具产物的安装由 `scons` + 手工复制验证；
  协议与消费链路已由 editor 套件（真实子进程）与全量验收覆盖。
- `.codegen-baseline/` 是本检出首次建立（gitignore，本地自持）。
- 生成物 `project/gen/`、`project/typings/`、`.godot/` 下的工具产物均为构建产物，未入库。

## 遗留 / 待用户裁决

- 无阻塞项。用户未授权提交 ⇒ **未 commit**（工作树改动见上表）。
