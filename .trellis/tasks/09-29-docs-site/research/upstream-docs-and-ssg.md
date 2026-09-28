# 上游文档站清单与 SSG 选型（Research）

> 来源：本次会话直接读取的上游仓库（`godotjs/godotjs.github.io` @ main，
> tree sha `63e725f2f2bc8759a1fa01529e74bb32bbbac0c0`）与本地实测。
> 说明：原先委派的两个调研子代理都因**写文件通道不可用**未能落盘，本文件由主线按已核实的证据重写。

## A. 上游页面清单（按 mkdocs.yml 的 nav 分组）

| nav 组 | 页面 | 字节 |
|---|---|---|
| Home | `index.md` | 3252 |
| Docs / Getting Started | `documentation/getting-started.md` | 3645 |
| | `documentation/use-external-editor.md` | 641 |
| | `documentation/export-project.md` | 743 |
| Docs / Scripting | `godot-js-scripts/intro.md` | 1928 |
| | `decorators.md` | 3070 |
| | `signals.md` | 2625 |
| | `npm-dependencies.md` | 911 |
| | `cyclic-imports.md` | 852 |
| | `bindings.md` | 8329 |
| | `code-in-editor.md` | 5468 |
| | `auto-completion-and-codegen.md` | 3857 |
| Docs / Utilities | `utilities/repl.md` | 558 |
| | `utilities/source-map.md` | 330 |
| | `utilities/statistics.md` | 556 |
| | `utilities/debugger.md` | 2137 |
| Docs / Experimental | `experimental/worker.md` | 1589 |
| Docs / Building | `building-from-source/index.md` | 3047 |
| | `v8.md` | 5442 |
| | `quick-js.md` | 622 |
| | `javascript-core.md` | 305 |
| | `web.md` | 178 |
| Examples | `load-json-in-singleton.md` | 1576 |
| | `read-file-local.md` | 942 |
| | `use-gdscript-with-godot-js.md` | 1784 |
| | `reuse-custom-resources.md` | 2045 |
| Development | `dependencies.md` | 926 |
| | `compiler-options.md` | 1194 |
| | `godot-js-debugging.md` | 1581 |
| | `godot-unit-testing.md` | 212 |
| Misc | `breaking-changes.md` | 829 |
| | `architecture.md` | 489 |

合计 **32 个 md 页面**（外加 `mkdocs.yml`、两张 css、一个 `release-selector.js`）。

## B. 上游页面里"模块形态专属、必须重写"的内容

| 页面 | 需要重写的原因 |
|---|---|
| `getting-started.md` | 核心是 `<div id="release-selector">`：下载**自带 GodotJS 的 Godot 编辑器**，并按 OS/target/engine/Godot 版本筛选 → 本仓不发布引擎，只发布 addons 包 |
| `export-project.md` | 「下载导出模板」是引擎分发流程；GDExtension 下导出走本仓的 ExportPlugin |
| `building-from-source/{index,v8,quick-js,javascript-core,web}.md` | 讲的是**引擎模块**的编译（把 GodotJS 放进 Godot 源码树）；本仓是 `SConstruct` + GDExtension |
| `development/dependencies.md` / `compiler-options.md` | 引擎模块的依赖与编译开关 |
| `experimental/worker.md` | 用全局 `Worker`、`worker.ontransfer`；本仓是 `godot.worker` 模块的 `JSWorker` / `JSWorkerParent`，`ontransfer` 在类型声明中不存在 |
| `misc/architecture.md` | mermaid 图描述 `bridge → weaver / weaver-editor` 的模块分层；本仓已合并为单库两产物 |
| `misc/breaking-changes.md` | 只到 v1.0.0；本仓 1.0.x 的变更（按引擎分包、`[information]`/`[icons]`）需追加 |
| `index.md` 的平台表 | 5 引擎 × 12 平台的矩阵与本仓实际矩阵不同（本仓无 x86_32/wasm 以外的旧组合，且 node 是独立包） |
| `decorators.md` | 全部用旧式 `@Export` / `@ExportSignal` / `@Tool` / `@Icon`；本仓推荐 `createClassBinder()` |
| `bindings.md` | 内容基本可用（`godot` 模块、GArray/GDictionary、StringName、PackedArray），但需补 BigInt/int64 语义 |
| `signals.md` | 用 `@ExportSignal()`；需改成 `@bind.signal()`，`$wait` → `as_promise()` |

## C. 上游部署与 release-selector

- 部署：`.github/workflows/gh_pages.yml` = `pip install mkdocs-material` → `mkdocs gh-deploy --force`
  （`pymdownx.superfences` 提供 mermaid 支持，`docs/css/extra.css` 有 182KB 的定制样式）。
- `docs/javascripts/release-selector.js`（12.9KB）：`fetch("https://api.github.com/repos/godotjs/GodotJS/releases")`，
  从 asset 名里正则匹配 OS / target / engine / Godot 版本，动态渲染四组 `<select>` + 结果列表；
  默认选中 `editor` + `v8`。**本仓资产是 addons 包且按引擎切分**，这套筛选维度需要整体重设计。
- 上游图片素材带 `.licence` 伴随文件（logo 源自 `why-try313/godot-ECMAScript-cookbook`）→ 不可直接复用。

## D. 本地工具链实测

| 项 | 命令 | 结果 |
|---|---|---|
| Node / pnpm / Python | `node -v` / `pnpm -v` / `python --version` | v24.20.0 / 11.25.0 / 3.14.7 |
| npm registry | `npm ping` | PONG 836ms |
| VitePress | `npm view vitepress version` | 1.6.4 |
| Astro | `npm view astro version` | 7.3.5 |
| 安装 | `npm i -D vitepress@1.6.4` | rc=0，126 包，24s，无原生编译 |

## E. SSG 对比

| | VitePress 1.6.4 | Astro + Starlight | Docusaurus |
|---|---|---|---|
| 依赖体量 | 126 包（实测），无原生编译 | 显著更大（Astro + 集成） | 最大（React 运行时） |
| i18n 模型 | `locales` 配置 + 目录树，`localeIndex` 自动按路径解析；每 locale 独立搜索索引 | `i18n` 配置 + `src/content/<locale>/` | 目录 + `write-translations` 抽 JSON |
| 主题可完全替换 | **是**：`theme/Layout.vue` 是唯一根组件，替换后默认主题样式不再引入（实测产物只有自绘 DOM） | 需要覆盖其组件树，成本更高 | 需要 swizzle |
| 需要的插件能力 | mermaid / tabs 需自加 markdown-it 插件；内置 shiki 高亮、dead-link 检查 | 内置丰富 | 内置 |
| 构建 | `vitepress build docs`，2.1s（实测） | `astro build` | `docusaurus build` |
| 部署 | 静态产物直接交给 Pages | 静态产物 | 静态产物 |

## F. 结论

选 **VitePress 1.6.4**：实测主题替换彻底、i18n 为配置级、依赖最少、构建秒级，且与仓库既有
Node/pnpm 工具链一致。实现细节与实测证据见 `design.md` §1、`implement.md`。

残余未知（不阻塞本次交付）：

- 本地 `pnpm preview` 的 HTTP 服务在浏览器里对 `@localSearchIndex` 分块的加载曾出现
  `ERR_ABORTED`（换成新端口后正常）；未进一步定位，判断为预览服务自身的分块缓存问题，
  不影响静态产物（构建产物经直接 `import()` 验证可正常加载并驱动搜索）。
- GitHub Pages 上的真实部署尚未触发（需在新仓 Settings → Pages 选择 GitHub Actions）。
