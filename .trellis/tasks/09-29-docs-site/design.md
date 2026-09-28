# 技术设计：GodotJS-Ext 文档站

## 1. 技术选型

### 结论

**VitePress 1.6.4**（本机 Node 24.20 / pnpm 11.25 / npm registry 可达），Markdown 优先 +
自定义 Vue 根布局 + 配置级 i18n。

### 实测证据（`.agent_tmp/docs-spike/`，一次性 spike）

| 项 | 命令 | 结果 |
|---|---|---|
| 安装 | `npm i -D vitepress@1.6.4` | `rc=0`，126 包，24s，无原生编译步骤 |
| 默认构建 | `npx vitepress build docs` | `rc=0`，1.96s |
| 自定义布局 | `docs/.vitepress/theme/{index.ts,Layout.vue}` | `rc=0`；产物 HTML 含 `CUSTOM LAYOUT` 与 `custom-shell` ⇒ **默认主题被完全替换** |
| i18n | `locales: { root: zh-CN, en: { link:'/en/' } }` + `docs/en/index.md` | `rc=0`；产出 `dist/index.html` 与 `dist/en/index.html`；`<html lang="zh-CN">` vs `<html lang="en-US">` |

关键结论：`theme/Layout.vue` 是唯一根组件，替换它即彻底摆脱默认主题，且不影响 Markdown 渲染管线
与 `<Content />`。

### 备选与否决理由

| 方案 | 否决理由 |
|---|---|
| Material for MkDocs（上游同款） | 直接违反"主题完全不同"；且需 Python 工具链，与本仓 Node 工具链不一致 |
| Astro Starlight | i18n 与主题替换均可行，但要覆盖其组件树；依赖与构建复杂度高于 VitePress |
| Docusaurus | React 运行时 + `write-translations` 两段式 i18n，手写双语文档更绕；依赖最重 |

Markdown 能力（代码高亮、mermaid、tabs、容器）用 markdown-it 插件按需最小引入，**不预先堆插件**。

## 2. 目录与 i18n 布局

**默认语言 = 中文（用户裁决）**：`docs/**` 中文（root `/`），`docs/en/**` 英文（`/en/`）。

```
godotjs-ext.github.io/
├─ .github/workflows/deploy.yml     # 新仓自带：build + deploy to Pages
├─ package.json                     # scripts: dev / build / preview / check:i18n
├─ pnpm-lock.yaml
├─ LICENSE                          # 已存在（LGPL-2.1）；不得改动
├─ README.md                        # 站点维护说明 + Pages 开启步骤
└─ docs/
   ├─ .vitepress/
   │  ├─ config.mts                 # 站点元数据 + locales + themeConfig + base
   │  ├─ theme/                     # 自定义主题（index.ts / Layout.vue / style.css / components）
   │  └─ data/nav.mts               # 导航树/侧边栏单一声明（每项含 zh/en 文案）
   ├─ public/                       # logo、favicon、图片（自绘，不复用上游素材）
   ├─ <中文页面>.md                  # 默认语言 root = zh-CN
   └─ en/<英文页面>.md               # English
```

- 两语言**同构**：`docs/<path>.md` ↔ `docs/en/<path>.md`。
- 结构一致性由 `scripts/check-i18n.mts` 校验（比对两侧 `.md` 相对路径集合，排除 `.vitepress/`
  与 `public/`），不一致即 `exit 1`；VitePress 对未翻译页返回 404、不静默回退，故该校验是硬门。

## 3. base path / 部署

- remote = `Daylily-Zeleen/godotjs-ext.github.io` ⇒ 默认**项目页**
  `https://daylily-zeleen.github.io/godotjs-ext.github.io/`。
- 实测：`https://godotjs-ext.github.io/`、`https://daylily-zeleen.github.io/` 均 404；
  `github.com/godotjs-ext` 不存在 ⇒ 无 org/用户页站点。
- 设计：`base = process.env.DOCS_BASE ?? '/godotjs-ext.github.io/'`；迁移到自定义域只需改环境变量。
- 部署需在新仓手动开启 Pages（Settings → Pages → Source: GitHub Actions），写入 README。

## 4. 内容架构（页面清单）

> **本次交付范围（用户裁决）：核心集 9 页 × 2 语言**。标 `[本次]` 的页面本次实现；标 `[后续]`
> 的只登记映射关系，由后续阶段承接，不计入本次验收。

上游 mkdocs nav 共 32 个 md 页面（Material for MkDocs，五组：Home / Docs / Examples /
Development / Misc）。本站映射：

### 本次实现的 9 页

| 本站 | 上游对应 | 处理 |
|---|---|---|
| `index.md` 首页 | `index.md` | 重写：GDExtension、平台表、引擎表 |
| `guide/installation.md` | `documentation/getting-started.md` | **重写**：下载 addons 压缩包并解压进工程——上游是"下载自制 Godot 编辑器"，本仓不发布引擎 |
| `guide/project-setup.md` | 同上 | 重写：Install Preset Files、`pnpm i`、`tsc`、编辑器命令 |
| `guide/first-script.md` | `documentation/godot-js-scripts/intro.md` | 重写：`default export`、构造器 `identifier?`、async/await |
| `scripting/modules.md` | `godot-js-scripts/bindings.md` | 重写：`godot` / `godot-jsb` / `godot.worker` 模块面；GArray/GDictionary/proxy；PackedArray/ArrayBuffer；StringName；值语义陷阱 |
| `scripting/annotations.md` | `godot-js-scripts/decorators.md` | **重写**：新式 `createClassBinder()` 与旧式装饰器并存；真实导出名 |
| `scripting/signals.md` | `godot-js-scripts/signals.md` | 重写：`Signal<T>`、`Callable.create`、`as_promise()` |
| `runtime/engines.md` | **无上游** | 新增：引擎矩阵（v8 / qjs-ng / jsc / node / web）、选择方式、平台支持 |
| `misc/differences.md` | **无上游** | 新增：与上游 GodotJS 的差异总表（本任务 R5 落点） |

### 后续阶段登记（不在本次验收内）

| 本站 | 上游对应 |
|---|---|
| `scripting/{code-in-editor,npm-dependencies,cyclic-imports,codegen,worker}.md` | 同名上游页；`worker` 需重写（`JSWorker`/`JSWorkerParent`，上游的 `ontransfer` 在本仓 typings 中不存在） |
| `runtime/{bindings,architecture,cross-environment,numeric,utility}.md` | `misc/architecture.md` 重写；其余无上游（`binding_mode=`、跨环境、int64/bigint、source-map/debugger/statistics 合并） |
| `editor/{dock,menus-and-commands,script-docs,export}.md` | `utilities/{repl,statistics}.md` 合并、上游散落命令聚合页、源码注释文档（无上游）、`export-project.md` 重写 |
| `build/{from-source,engines,scons-options,packaging,ci}.md` | `building-from-source/*`、`development/{dependencies,compiler-options}.md` 重写；`packaging`/`ci` 无上游 |
| `testing/{cpp,typescript,benchmark}.md` | `development/godot-js-unit-testing.md` 重写；其余无上游 |
| `examples/*`（4 篇重写 + worker + static-members） | `examples/*` |
| `misc/{breaking-changes,faq}.md` | `breaking-changes.md` 重写 + 新增 |

> 全量约 40 页 × 2 语言 ≈ 80 个 md。本次交付其中的 18 个内容页（9 × 2）。

## 5. 主题设计（与 Material 完全不同）

取向：**"技术手册 / 终端优先"**，而非 Material 的"卡片 + 圆角 + 分级阴影 + 顶部 tabs"。
**已定（用户裁决）**：默认语言中文（root = zh-CN）；强调色琥珀 `#e0a458`；深色默认。

- 导航形态：**左侧持久树形侧边栏**（无顶部 tabs，无 Material 的路径面包屑条）。
- 排版：标题用等宽字体 + 全角标点；正文无衬线；行宽约 46rem。
- 配色：`--accent: #e0a458`（琥珀）；深色默认，浅色跟随 `prefers-color-scheme`。
  上游 Material 用 `black primary` + indigo 系，区别显著。
- 交互：右栏目录为静态列表（非浮动高亮条）；代码块自定义边框 + 行号 gutter；无 FAB、无圆角卡片。
- 语言切换器：自绘 `<select>`（非 Material 的 globe 下拉）。
- 主题实现全部位于 `docs/.vitepress/theme/`，Markdown 管线复用 VitePress。

## 6. 主仓侧改动（用户裁决：更新 README 链接）

- 只改 `README.md` 中指向文档的链接（现指上游 `godotjs.github.io`：README.md:21,25,33,42,50-51）。
- 不动其他主仓文件；不提交。

## 7. 风险

| # | 风险 | 缓解 |
|---|---|---|
| 1 | 事实漂移（源码变动后文档过期） | 关键页注明 file:line 来源；差异集中在 `misc/differences.md` 单点维护 |
| 2 | 双语漂移 | `scripts/check-i18n.mts` 结构校验进 CI（AC2） |
| 3 | 许可表述：根 `LICENSE` 为 LGPL-2.1，主仓 README 末尾写 "MIT License - see LICENSE" | 文档站按 LGPL-2.1 表述，并把该不一致作为遗留项提给用户（**不擅自改主仓**） |
| 4 | 图片：上游素材带 `.licence` 且不可复用 | 自建截图/图示；不复制上游 logo/图片 |
| 5 | base path 变更（迁到 org 页/自定义域名） | `DOCS_BASE` 环境变量控制 |
| 6 | 部署需人工在新仓开启 Pages | 写入 README 的部署步骤 |
| 7 | 后续阶段加页导致结构不够用 | 导航/侧边栏数据驱动（`docs/.vitepress/data/nav.mts`） |

## 8. 验证方式

- 站点：`pnpm build` 必须 `rc=0`；`pnpm dev` 本地起服务并用浏览器实测（语言切换、侧边栏、链接）。
- 结构：`node scripts/check-i18n.mts` `rc=0`。
- 内容：核心集每页的事实性陈述抽检可追到主仓 file:line。
- 视觉：与上游 `godotjs.github.io` 并排对比，确认主题显著不同（AC4）。
