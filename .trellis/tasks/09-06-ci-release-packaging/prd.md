# 按 JS 引擎分包发布（并配套正确的 .gdextension）

> 来源：`.trellis/tasks/09-06-ci-release-packaging`（P3，ci，父任务 `09-06-ci-improvements`）。
> 2026-09-28 按用户口述的真实意图重写；同日据实测（CI run 36291137069 的 artifact）补齐
> 「引擎 ↔ 构建腿 ↔ 包内容」的完整事实，见 `research/evidence.md`。

## Goal

**发布单位 = JS 引擎**：每个 release 包对应一个 JS 引擎，且只含该引擎真实存在的平台/架构
产物；包内每份 `.gdextension` 的 `[libraries]` / `[dependencies]` 只声明包内实际存在的文件，
用户拿到的包不再出现「声称支持某平台、却没有对应二进制」或「二进制在包里、却没有被任何
`.gdextension` 引用」的情况。

## 引擎清单（本轮的唯一权威表述）

仓库一共有 6 种 JS 引擎构建，其中 **5 种进入发布**：

| # | 引擎 | scons 选择 | 是否出包 | 说明 |
|---|---|---|---|---|
| 1 | v8 | 默认（无标志） | ✅ `v8` | 桌面 + android + ios |
| 2 | quickjs（原版） | `use_quickjs=yes` | ❌ | 被 quickjs-ng 上位取代；不出包，也不进 CI 矩阵 |
| 3 | quickjs-ng | `use_quickjs_ng=yes` | ✅ `qjs-ng` | 覆盖面最广（含 web 上的 quickjs wasm） |
| 4 | jsc | `use_jsc=yes` | ✅ `jsc` | 仅 macos + ios |
| 5 | node | `use_node=yes` | ✅ `node` | 仅 3 个桌面 editor 腿 |
| 6 | **纯 web** | `platform=web` 且不带其它引擎标志 | ✅ `web` | 浏览器宿主 JS（`JSB_WITH_WEB`）；仅 web |

`use_quickjs` 构建选项本身保留（用户仍可自己编原版 quickjs），本轮只是**不为它出包、不建 CI 腿**。

## Background

### 现流程（`.github/workflows/misc_release.yml`）

只有两个 job，按**后缀 glob** 抓取本 run 的 artifact 并各打一个 zip：

| job | glob | 产出 zip |
|---|---|---|
| `upload-v8` | `*-v8`（misc_release.yml:114） | `godotjs-ext-v8-windows-linux-macos.zip`（:146） |
| `upload-qjs-ng` | `*-qjs-ng`（:184） | `godotjs-ext-qjs-ng-windows-linux-macos.zip`（:216） |

两个问题：

1. **jsc / node 完全没有发布路径**：`*-v8` / `*-qjs-ng` 覆盖不到它们，`verify-release-artifacts`
   （ci.yml:702-742）也只校验 v8 / qjs-ng（脚本里 `eng not in ('v8','qjs-ng'): continue`），
   漏洞是静默的。
2. **zip 名与实际内容不符**：名写 `...-windows-linux-macos`，glob 命中的却是全部平台
   （实际还含 android / ios / web）。

### 实测事实（run 36291137069，23 条构建腿 + `scripts-out`；详见 `research/evidence.md`）

- 矩阵在 ci.yml:409-555；artifact 名 = `{platform}-{target}-{arch}-{engine}`（web nothreads 追加 `-nothreads`）。
- **`engine: v8` 的 web 腿编出来的不是 v8，而是纯 web（浏览器宿主 JS）**：该 wasm 内
  `impl/v8/` 出现 0 次、`impl/web/` 83 次、`jsb_web` 484 次；对照 `engine: qjs-ng` 的 web 腿是
  `impl/quickjs/` 84 次 + `JS_NewRuntime` 6 次。原因是 `SConstruct:379-383,426`：platform=web 时
  v8 预编译库描述符里没有 web 条目 → `v8_support=None` → `JSB_WITH_WEB=1`。
  → 今天 `*-v8` 把浏览器 JS 的 wasm 扫进了 v8 包；**引擎标签本身是误导的**。
- **桌面腿只有 editor target**：桌面平台没有 template_debug / template_release 腿，所以桌面
  只有 `*.debug.editor.*` 键有实物；桌面包只支持「编辑器内使用」，不含导出模板（沿用现状）。
- **artifact 里已有 editor 扩展的 dll/so/dylib，但发布步骤只拷 runtime 的
  `.gdextension`**（misc_release.yml:136）→ 包内 editor 二进制是死重量，用户项目里
  REPL / 代码生成 / export plugin（`godotjs-ext-editor.gdextension`）不可用。
- jsc / qjs-ng 的 ios 腿产出 `...xcframework`；**v8 的 ios 腿只有 device dylib，无 xcframework**
  （scons-build action.yml:209 对 v8 跳过 simulator 合成）。
- 只有 node 的 windows 腿产出 `bin/windows/node.dll`（v8 / qjs-ng 的 windows 腿没有）。
- 包内目录名 = zip 名（`godotjs-ext-v8-windows-linux-macos/`），zip 根即 addon 目录
  （`bin/<platform>/...` + `*.gdextension`）。

## Requirements

**R1 发布单位 = 引擎（5 个包）**：`v8` / `qjs-ng` / `jsc` / `node` / `web`，各产出一个 release asset；
替换现有两个按后缀 glob 的 job；不产出「全引擎合并包」。5 个包必须**恰好划分**矩阵的全部构建腿
（见 `research/evidence.md` 的映射表：7 + 9 + 2 + 3 + 2 = 23）。

**R2 引擎标签必须真实**：web 腿在 ci.yml 的 `engine` 由 `v8` 改为 `web`（scons 侧仍等价于「不加引擎
flag」），artifact 名随之变为 `web-template_release-wasm32-web[-nothreads]`，使
`*-v8` 与 `*-web` 不再互相污染、包内容归属由名字即可判定。scons-build action 的 engine 分支同步
（`web) engine_arg=""`）。

**R3 包内容 = 该引擎的矩阵腿**：包内平台/架构只能来自 ci.yml 构建矩阵中 `engine=<该引擎>` 的腿；
**矩阵是唯一来源**，不维护第二份平台清单。桌面平台不新增 template 腿（见非目标）。

**R4 包内 `.gdextension` 与包内文件一一对应**：runtime 与 editor **两份**都要出（R7），
`[libraries]` / `[dependencies]` 从包内实际二进制派生：

- 不出现「声明了但包里没有」的条目（硬失败）；
- macOS 键符合各引擎架构实情：universal 产物 → 无 arch 键（`macos.debug.editor`）；
  arm64-only 产物（v8 / node / jsc）→ 只声明 `macos.debug.editor.arm64`，**不得**声明
  arch-less 的 universal 条目；
- ios：有 xcframework 的引擎（qjs-ng / jsc）指向 `...xcframework`；v8 只有 device dylib，指向该 dylib；
- web：`web.<release>.threads.wasm32` 与 `web.<release>.wasm32` 两组键分别对应
  `...wasm32.wasm` 与 `...wasm32.nothreads.wasm`；web 包（D1=B，仅 2 条 web 腿）只出 runtime
  一份 `.gdextension`，不含 editor 键；
- 目标键只覆盖矩阵真实存在的 target（桌面 = editor；android/ios/web = template_release）。

**R5 `[dependencies]` 的 node.dll 只属于 node 包**：仅当包内存在 `bin/windows/node.dll` 时，
给 windows 各库键挂该依赖；非 node 包不得出现该依赖。

**R6 包名如实反映所含范围**：zip 名 = `godotjs-ext-<tokens>.zip`，`<tokens>` = 引擎 token +
包内平台 token（按固定顺序，去重）；包内目录名 = zip 名（去 `.zip`）。粒度到平台，不到架构
（架构差异由包内 `bin/` 树与 `.gdextension` 体现）。

**R7 打包逻辑单点**：新增脚本 `misc/release/package.py`，同时承担「计划（矩阵→期望 artifact 集）」
「生成（artifact→包内 `.gdextension`）」「校验（声明↔文件 1:1）」「打包」，CI 门禁与发布 workflow
都调用它；矩阵仍是唯一来源，脚本是唯一派生点。

**R8 门禁覆盖全部发布引擎**：`verify-release-artifacts`（ci.yml:702-742）改为按
`misc/release/package.py` 的期望集校验全部 5 个引擎；任一发布引擎的期望腿缺失、或期望集为空
→ 失败。不得再次出现「某引擎整体漏包且无人发现」。

## Acceptance Criteria

- [ ] `v8` / `qjs-ng` / `jsc` / `node` / `web` **各有一个** release asset；无引擎被漏掉（AC-R1）
- [ ] 5 个包的内容恰好划分矩阵的 23 条构建腿：无腿被漏掉、无腿被两个包同时收录（AC-R1/R3）
- [ ] 包内不含其它引擎的腿：v8 包不含 web 产物（web 的后端不是 v8）与 qjs-ng/jsc/node 腿（AC-R2/R3）
- [ ] 每个包内两份 `.gdextension`（runtime + editor；无 editor 腿的 web 包只出 runtime）的
      `[libraries]`/`[dependencies]` 条目与包内实际文件一一对应，不存在「声明了但包里没有」（AC-R4）
- [ ] macOS 键符合实情：qjs-ng → `macos.debug.editor`（universal）；v8/node/jsc → `macos.debug.editor.arm64`（AC-R4）
- [ ] ios 键符合实情：qjs-ng/jsc → `...xcframework`；v8 → device dylib（AC-R4）
- [ ] web 的 threads / nothreads 两组键都正确（含 qjs-ng 包的 web 键指向 quickjs wasm）（AC-R4）
- [ ] `[dependencies]` 的 `node.dll` 只出现在 node 包（AC-R5）
- [ ] zip 名如实反映所含平台；包内目录名 = zip 名（AC-R6）
- [ ] `verify-release-artifacts` 覆盖全部 5 个引擎，且任一发布引擎的期望腿消失时该 job 失败（AC-R8）
- [ ] 打包脚本可在本地对 `gh run download` 下来的真实 artifact 跑通（5 个引擎全绿），
      不依赖真实发布（AC-R7）

## 非目标（本轮不做）

- 不新增桌面 template_debug / template_release 构建腿（桌面仍是 editor-only，与现状一致）
- 不裁剪包内附属文件（`.pdb` / `.ilk` / `.exp` / `.lib` / `.a`）——现状如何保持如何
- 不删除 `use_quickjs` 构建选项，也不删除 `third/quickjs` 子模块
- 不产出「全引擎合并包」；不新增 release 资产以外的分发渠道（npm 等）
- 不改 changeset / 版本号 / clang-format / `compatibility_minimum` 流程
- 不改本地开发用的 superset `.gdextension`（`project/addons/.../godotjs-ext.gdextension` 仍
  声明全部平台，供本仓测试项目使用）；per-engine 版本只在打包时生成

## 决策（已定 2026-09-28）

**D1：web 包只含 web wasm，不附桌面库 —— 用户选 B。**

- 只含 web 的包在桌面编辑器里加载不了（纯 web 引擎没有桌面实现；编辑器需要桌面 runtime 库 +
  editor 扩展；两个引擎包又不能共存）。B 接受这一点：`godotjs-ext-web.zip` 只装
  `bin/web/` 两个 wasm + runtime 一份 `.gdextension`（仅 web 键），使用者把它覆盖合并进所用桌面
  引擎包（包内目录结构一致），或直接用 qjs-ng 包（其 web 腿提供 quickjs 版 wasm）。
- 已否决 A（web 包自带 v8 的 4 条桌面 editor 腿，可独立使用但 ≈480MB 重复二进制）。
- 详细权衡见 `design.md` §9。
