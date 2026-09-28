# GodotJS-Ext 文档站（中英双语）

## Goal

在 `./godotjs-ext.github.io/`（独立 git 仓，remote `Daylily-Zeleen/godotjs-ext.github.io`）内建立
**GodotJS-Ext 自己的**文档站：覆盖上游 `godotjs/godotjs.github.io` 的主题面，内容按本仓事实重写
（GDExtension 形态、多引擎、新特性），**主题与布局与上游完全不同**（上游为 Material for MkDocs），
并实现 **i18n（中文默认 / English）**。

用户价值：本仓是 GDExtension 分发形态、多引擎、自带 TS 工具链；上游文档面向"引擎模块 + 自制 Godot
编辑器分发包"，无法直接复用。使用者需要一个能查到"怎么装、怎么建工程、怎么用 API、怎么构建/发版"
的单一入口。

## Background（已核实事实，file:line）

### 本仓身份

- **GDExtension**，非引擎模块：`project/addons/godotjs-ext.daylily-zeleen/godotjs-ext.gdextension`
  （`entry_symbol = "jsb_gdextension_init"`、`compatibility_minimum = 4.7`、`reloadable = false`）。
- 要求 **Godot 4.7+**：README.md:28-29；godot-cpp 按 `API_VERSION = "4.7"` 生成。
- 单库两产物（2026-09-28 合并）：`target=editor` 编 runtime+editor，`target=template_*` 只编 runtime，
  同一入口符号（`.trellis/spec/godotjs-ext/build/scons-build.md`「单一库两产物」）。
- 许可：根 `LICENSE` = LGPL-2.1；README 末尾 "MIT License - see LICENSE" 指向 `GodotJS/LICENSE`
  （子模块），与根许可不一致（见 Risks）。

### 引擎矩阵（`.trellis/spec/godotjs-ext/build/release-packaging.md`）

| 引擎 | scons 选择 | 出包 | 平台 |
|---|---|---|---|
| v8 | 默认 | `v8` | 桌面 + android + ios |
| quickjs | `use_quickjs=yes` | ❌（被 qjs-ng 取代，本地可编） | — |
| quickjs-ng | `use_quickjs_ng=yes` | `qjs-ng` | 最广，含 web |
| jsc | `use_jsc=yes` | `jsc` | macOS + iOS |
| node | `use_node=yes` | `node` | 3 个桌面 |
| web | `platform=web` 且不加引擎标志 | `web` | 仅 web（宿主浏览器 JS） |

### 构建（`scons-build.md`）

- 规范命令：`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6`；
  叠加 `platform=`、`tests=yes`、`binding_mode=static|shared|dynamic`、引擎标志。**禁止 `scons --clean`**。
- 前提 `git submodule update --init`；产物落 `bin/<platform>/`，引擎加载
  `project/addons/godotjs-ext.daylily-zeleen/bin/`。
- JS 运行时由 `scripts/` 的 pnpm workspace 构建（`pnpm build` → `scripts/out/`），scons 构建时嵌入。

### 编辑器功能（`src/editor/weaver-editor/`）

- 底部 dock `GodotJS`：REPL（`jsb_repl.cpp:57-135`）+ Statistics（`jsb_statistics_viewer.cpp:37-72`）。
- 命令：Install Preset Files、Generate Types（`--generate-types`）、Generate API Data
  （`--godotjs-api-generate <extension_api.json>`，`jsb_editor_plugin.cpp:230-247,322-341`）、
  `Config Enabled Classes Bindings` 对话框（`jsb_config_classes_dialog.cpp:142-199`）。
- TSC watch（`jsb_editor_plugin.h:168-169`）、ExportPlugin（`jsb_export_plugin.cpp`）。
- 源码注释 → 编辑器脚本文档（常驻 Node 工具进程）：
  `.trellis/tasks/archive/2026-09/09-28-comment-doc-annotation/report.md`。
- 脚本类图标走 `.gdextension` 的 `[icons]` + 纯路径 SVG（`build/editor-icons.md`）；
  `[information]` 是本项目自有元数据，引擎不解析（`build/release-packaging.md`）。

### 用户侧 JS/TS API 面

- 模块：`godot`（对象/原语/变体）、`godot.annotations`（装饰器；新式 `createClassBinder()`
  与旧式 `legacy_decorators_check` 并存，`scripts/jsb.runtime/src/jsb.core.ts:126-631,714`）、
  `godot-jsb`（内部）、`godot.worker`（`JSWorker` / `JSWorkerParent`，
  `scripts/typings/godot.worker.d.ts:24-56`）。
- 代码生成：`project/gen/godot/**` 的 `.gen.ts`；`scripts/typings/` 的 `godot.generated.d.ts`、
  `godot.minimal.d.ts`、`godot.mix.d.ts`、`godot.shadowRealm.d.ts`。
- 工程预设：`scripts/presets/{tsconfig.json,jsconfig.json,package.json,gdignore}.txt`，经编辑器
  "Install Preset Files" 安装。

### 测试与 CI

- C++ doctest 单套件：`scons ... tests=yes` + `godot --headless --path project --jsb-run-tests`。
- TS 集成测试：`scripts/test/run-runtime-matrix.mts`（哨兵 `GODOTJS_TEST_PROJECT_COMPLETED` /
  `GODOTJS_TEST_PROJECT_FAILED:`）；`project/tests/` 共 20 组场景（含 cross-environment、
  gen_dts_test、paths_test、中文路径…）。
- benchmark：`-- --bench [--gc] [--only=<组>]`（user args）。
- CI 矩阵 23 腿（`.github/workflows/ci.yml:403-561`）；发布链 `release.yml` → `misc_release.yml`
  → `misc/release/package.py`（按引擎出包 + `verify-release-artifacts` 门禁）。

### 上游文档站（对照基线）

- Material for MkDocs（`mkdocs.yml`，nav 五组 Home/Docs/Examples/Development/Misc，共 32 个 md 页），
  部署 `mkdocs gh-deploy --force`（`.github/workflows/gh_pages.yml`）。
- 上游 `getting-started.md` 核心是 **release-selector**
  （`docs/javascripts/release-selector.js` 拉 `api.github.com/repos/godotjs/GodotJS/releases`，筛
  OS/target/engine/Godot 版本）——面向"下载自制 Godot 编辑器"；本仓只分发 addons 压缩包，必须重写。
- 上游图片带 `.licence` 文件（logo 源自 `why-try313/godot-ECMAScript-cookbook`），**不复用其素材**。

### 部署位置（实测）

- `https://github.com/Daylily-Zeleen/godotjs-ext.github.io` 存在（HTTP 200）。
- `https://godotjs-ext.github.io/`、`https://daylily-zeleen.github.io/` 均 404；
  `github.com/godotjs-ext` 不存在 ⇒ 目前没有 org 页，默认是**项目页**

  `https://daylily-zeleen.github.io/godotjs-ext.github.io/`。

## Requirements

- **R1** 站点独立可构建、可本地预览、可部署到 GitHub Pages；位于 `./godotjs-ext.github.io/`
  （主仓 `.gitignore:97` 已排除该目录，且其自身是独立 git 仓）。
- **R2** i18n：**中文为默认语言（root `/`）**，English 在 `/en/`；两语言页面一一对应；
  语言切换器任意页面可用；每语言独立导航/侧边栏/搜索文案/`<html lang>`。
- **R3** 主题与布局**与上游完全不同**：不使用 Material for MkDocs，不复制其视觉
  （顶部 tabs、Material 图标集、其上流配色）。取向与配色见设计文档。
- **R4** 内容按本仓事实重写，不得照抄上游（安装方式、引擎矩阵、编辑器命令、构建流程差异最大）；
  上游没有的主题（GDExtension 打包、静态绑定、api_tool、benchmark、跨环境、源码注释文档）需新增。
- **R5** 单列「与上游 GodotJS 的差异」页面，至少覆盖：GDExtension vs 引擎模块、Godot 4.7 要求、
  多引擎（含 node / web）、单库两产物、按引擎分包、`[information]`/`[icons]`、源码注释文档、
  新式 `createClassBinder()` 与旧式装饰器并存。
- **R6** 代码示例与实际 API 对齐（`godot` / `godot.annotations` / `godot.worker` 的真实导出），
  不沿用上游已失效写法（如 `worker.ontransfer`、`jsb.core` 路径的注解导出）。
- **R7** 新仓自带构建 + 部署 workflow，不依赖主仓 CI。
- **R8** 同步更新主仓 `README.md` 中指向文档的链接（README.md:21,25,33,42,50-51 现指向上游
  `godotjs.github.io`）——**只改链接，不动其他主仓文件，不提交**。
- **R9（范围）** 本次交付**核心集 8 页 × 2 语言（≈16 个 md）**，站点骨架/主题/i18n/部署流水线全部完成：

  | 本站页（zh root + `en/`） | 上游对应 |
  |---|---|
  | `index.md` 首页 | `index.md`（重写：GDExtension、平台表、引擎表） |
  | `guide/installation.md` | `documentation/getting-started.md`（重写：下载 addons + 解压进工程） |
  | `guide/project-setup.md` | 同上（Install Preset Files、`pnpm i`、`tsc`） |
  | `guide/first-script.md` | `documentation/godot-js-scripts/intro.md` |
  | `scripting/modules.md` | `godot-js-scripts/bindings.md` |
  | `scripting/annotations.md` | `godot-js-scripts/decorators.md`（重写：新/旧装饰器并存） |
  | `scripting/signals.md` | `godot-js-scripts/signals.md` |
  | `runtime/engines.md` | **无上游**（引擎矩阵） |
  | `misc/differences.md` | **无上游**（与上游差异总表） |

  > 表内 9 项：首页 + 3 篇 guide + 3 篇 scripting + 2 篇新增；`scripting` 三篇与 `guide` 三篇
  > 构成"入门 + 脚本"核心集，`runtime/engines.md` 与 `misc/differences.md` 是本仓独有主题。
  > 其余主题（编辑器、构建、打包/CI、测试、示例、FAQ）由 `implement.md` 的后续阶段承接，
  > **不在本次验收范围内**（见 Out of Scope）。

## Acceptance Criteria

- **AC1** `cd godotjs-ext.github.io && pnpm install && pnpm build` → `rc=0`，产出静态站点；
  `pnpm dev` 可本地预览。
- **AC2** `/` 渲染中文、`/en/` 渲染英文；两语言 `.md` 相对路径集合一致，由
  `node scripts/check-i18n.mts` 校验并以 `rc=0` 通过；`<html lang>` 分别为 `zh-CN` / `en-US`。
- **AC3** 语言切换器在首页与每一页可用，切换后落在同语义页面（非回首页）。
- **AC4** 站点视觉与上游显著不同（不同配色/排版/导航结构）：深色默认 + 琥珀 `#e0a458` 强调色 +
  等宽标题 + 左侧持久侧边栏，与上游 Material（顶部 tabs、Material 图标、其上游配色）并排截图对比可辨。
- **AC5** 核心集 9 页的每条事实性陈述可追到主仓 file:line（页面头部或行内注明来源），抽检通过。
- **AC6** `godotjs-ext.github.io/.github/workflows/deploy.yml` 存在且命令与 AC1 一致（本地可复现同一
  命令）；README 写明启用 Pages 的手动步骤。
- **AC7** 无 Material/MkDocs 依赖残留；未复制上游品牌素材（logo/图片）；`LICENSE` 未被改动。
- **AC8** 主仓 `README.md` 的文档链接指向本站（R8），且主仓无其他改动（`git status` 仅 `.gitignore`
  既有改动 + `README.md`）。

## Out of Scope

- 不修改主仓 C++/TS 源码；不做 API 参考自动生成（`godot.generated.d.ts` 级别）。
- 不搬运上游图片素材；需要图示的自建。
- 不做自建 Godot 编辑器分发包（本仓不发布引擎二进制）。
- 本次不交付：编辑器页、构建/打包/CI 页、测试页、示例页、FAQ（后续阶段，见 `implement.md`）。
- 不翻译上游既有英文页面的原文；本站内容按本仓事实重写。

## Risks

| # | 风险 | 缓解 |
|---|---|---|
| 1 | 事实漂移（源码变动后文档过期） | 关键页注明 file:line 来源；差异集中在 `misc/differences.md` 单点维护 |
| 2 | 双语漂移 | `scripts/check-i18n.mts` 结构校验（AC2） |
| 3 | 许可表述：根 `LICENSE` 为 LGPL-2.1，而主仓 README 末尾写 "MIT License - see LICENSE" | 文档站按 LGPL-2.1 表述，并把该不一致作为遗留项提给用户，**不擅自改主仓 LICENSE 或 README 许可段** |
| 4 | base path：日后迁到 org 页/自定义域名 | `DOCS_BASE` 环境变量控制，不改代码 |
| 5 | 页数后续扩张（阶段 3/4）导致主题结构不够用 | 导航/侧边栏为数据驱动（`docs/.vitepress/data/nav.mts`），加页只加数据 |
| 6 | 部署需人工在新仓开启 Pages（Settings → Pages → Source: GitHub Actions） | 写入 README 的部署步骤 |

## Deferred（已记录，非阻塞）

- 后续阶段的页面清单与顺序：`implement.md` 阶段 3/4。
- 上游 docs 站的逐页清单与 SSG 对比：`research/upstream-docs-and-ssg.md`（后台调研产出）。
- 本仓特性清单（引擎/构建/编辑器/API 面）：`research/godotjs-ext-vs-upstream.md`（后台调研产出）。
