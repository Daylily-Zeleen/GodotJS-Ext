# 执行计划：GodotJS-Ext 文档站（核心集 9 页 × 2 语言）

> 目录：`godotjs-ext.github.io/`（独立 git 仓，主仓 `.gitignore:97` 已排除）。
> 技术栈：VitePress 1.6.4 + 自定义主题 + 配置级 i18n + 默认语言中文（见 design.md，已 spike 实测）。
> 范围：**阶段 0–3 是本次交付**；阶段 4 及以后为登记项，不在本次验收内。

## 阶段 0：脚手架（先决条件）

- [ ] 0.1 仓库骨架：`package.json`（`type: module`；`dev`/`build`/`preview`/`check:i18n`）、
      `pnpm-lock.yaml`、`.gitignore`（`node_modules/`、`docs/.vitepress/dist/`、
      `docs/.vitepress/cache/`）、`README.md`（维护说明 + Pages 开启步骤 + `DOCS_BASE` 说明）。
      **不得覆盖已存在的 `LICENSE`**。
- [ ] 0.2 `docs/.vitepress/config.mts`：`title`/`description`/
      `base`（`process.env.DOCS_BASE ?? '/godotjs-ext.github.io/'`）；
      `locales.root = { label:'简体中文', lang:'zh-CN' }`、`locales.en = { label:'English', lang:'en-US', link:'/en/' }`；
      `themeConfig.search.provider='local'` + 每语言搜索文案；`plain` 侧边栏数据来自 `data/nav.mts`。
- [ ] 0.3 `docs/.vitepress/theme/`：`index.ts` + `Layout.vue`（自绘头部、左侧持久树形侧边栏、
      语言 `<select>`、右栏静态目录、页脚）+ `style.css`（`--accent:#e0a458`、深色默认、
      等宽标题、46rem 行宽、自定义代码块 gutter）。
- [ ] 0.4 `docs/.vitepress/data/nav.mts`：导航/侧边栏单一声明，每项 `{ zh, en, link }`，
      两语言共用结构（本次只登记核心集 9 页）。
- [ ] 0.5 `scripts/check-i18n.mts`：比对 `docs/**.md` 与 `docs/en/**.md` 相对路径集合
      （排除 `.vitepress/`、`public/`），不一致即非零退出。
- [ ] **验证（AC1/AC2/AC3/AC4 第一份证据）**：`pnpm install && pnpm build` → `rc=0`；
      `node scripts/check-i18n.mts` → `rc=0`；`pnpm dev` + 浏览器实测：
      `/` 中文、`/en/` 英文、语言切换器保留当前页语义、侧边栏渲染、`<html lang>` 正确。

## 阶段 1：内容骨架（双语同构）

- [ ] 1.1 建 18 个 md：`docs/{index.md, guide/installation.md, guide/project-setup.md,
      guide/first-script.md, scripting/modules.md, scripting/annotations.md,
      scripting/signals.md, runtime/engines.md, misc/differences.md}` 及各自 `docs/en/**` 对应文件。
- [ ] 1.2 每页先写标题 + 结构大纲（H2 列表），正文留待阶段 2/3 填实。
- [ ] 1.3 验证：`pnpm build` + `check-i18n` 通过 ⇒ 路由与双语结构立住。

## 阶段 2：入门页填实（事实最密集）

- [ ] 2.1 `index.md`：本仓身份（GDExtension、Godot 4.7+）、引擎/平台表、4 个快速链接。
- [ ] 2.2 `guide/installation.md`：从 Releases 取 `godotjs-ext-<engine>-*.zip` → 解压到工程
      `addons/godotjs-ext.daylily-zeleen/` → 引擎加载 `res://addons/.../godotjs-ext.gdextension`
      → 重启编辑器。**不写"下载自制编辑器"**（本仓不发布引擎）；引擎包选择（v8/qjs-ng/jsc/node/web）
      与平台对应关系来自 `misc/release/package.py` 与 release-packaging 规范。
- [ ] 2.3 `guide/project-setup.md`：Install Project Files（`scripts/presets/*.txt` 落 `tsconfig.json`/
      `jsconfig.json`/`package.json`/`.gdignore`）、`pnpm install`、`npx tsc`、底部 dock 的
      TSC watch / Generate Types 按钮。
- [ ] 2.4 `guide/first-script.md`：`export default class X extends Node`、必须 `default export`、
      构造器 `constructor(identifier?: any) { super(identifier) }`、`async/await`、
      新脚本用 GodotJSScript 语言模板。

## 阶段 3：脚本与运行时填实

- [ ] 3.1 `scripting/modules.md`：`godot`（对象/原语/变体）、`godot-jsb`（内部）、
      `godot.worker`；GArray/GDictionary `create`/`proxy`；PackedArray ↔ ArrayBuffer；
      StringName 透明处理；值语义陷阱（`node.position.x = 0` 无效）。
- [ ] 3.2 `scripting/annotations.md`：新式 `createClassBinder()` 与旧式装饰器并存（真实导出名来自
      `scripts/jsb.runtime/src/jsb.core.ts` 与 `godot.annotations.ts`）；`@Export`/`@ExportEnum`/
      `@ExportSignal`/`@Tool`/`@Icon`/`@OnReady`/`@Rpc`/`@ExportRange`… 及 `deprecated`/`experimental`/
      `help`。**不照抄上游 `jsb.core` 路径的注解导出。**
- [ ] 3.3 `scripting/signals.md`：`Signal<T>` 声明、`Callable.create(this, fn)` 的
      相等性与 GC 语义（不要用 lambda 捕获 `this`）、`as_promise()`、emit 参数必须为 Godot 原生类型。
- [ ] 3.4 `runtime/engines.md`：6 种引擎（含 quickjs 的"保留但不发布"）、scons 选择标志、
      平台覆盖矩阵、各引擎的适用场景；与上游"多引擎"描述差异（上游无 node、无 GDExtension 分发）。
- [ ] 3.5 `misc/differences.md`：差异总表（GDExtension vs 引擎模块、4.7 要求、单库两产物、
      按引擎分包、`[information]`/`[icons]`、源码注释文档、新式/旧式装饰器并存、静态绑定、
      api_tool、benchmark、跨环境测试）。

## 阶段 4（登记，非本次范围）

`build/*`（源码构建、引擎、scons 选项、打包、CI）、`testing/*`（C++ doctest、TS 矩阵、benchmark）、
`editor/*`、`scripting/{codegen,worker,npm,cyclic-imports,code-in-editor}`、
`runtime/{bindings,architecture,numeric,utility,cross-environment}`、`examples/*`、
`misc/{breaking-changes,faq}`。顺序与依赖见 design.md §4。

## 验证命令（每次改动后必跑）

```bash
cd godotjs-ext.github.io
pnpm install
pnpm build                    # rc=0，且无 dead link 警告
node scripts/check-i18n.mts   # rc=0
pnpm dev                      # 浏览器实测
```

## 风险点 / 回滚

| 位置 | 风险 | 回滚 |
|---|---|---|
| `docs/.vitepress/theme/Layout.vue` | 自定义根组件写坏 ⇒ 全站不可渲染 | 单文件；`theme/index.ts` 去掉引用即回默认主题 |
| `docs/.vitepress/config.mts` | `base` 写错 ⇒ 部署后资源 404 | `DOCS_BASE` 环境变量覆盖，不改代码 |
| `scripts/check-i18n.mts` | 误报阻塞 | 独立脚本，可单独修 |
| 主仓 `README.md` | 链接改错 | 单文件单处改动，`git checkout -- README.md` 即回 |

## 收尾（本次范围内）

- [ ] `.github/workflows/deploy.yml`：`pnpm install --frozen-lockfile` → `pnpm build` →
      `actions/upload-pages-artifact` + `actions/deploy-pages`（`permissions: pages: write, id-token: write`）。
- [ ] 主仓 `README.md` 文档链接指向本站（design.md §6）。
- [ ] 与上游并排截图对比（AC4）；`check-i18n` 终验（AC2）。
