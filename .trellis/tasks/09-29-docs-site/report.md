# 进度：GodotJS-Ext 文档站（核心集 9 页 × 2 语言）

任务目录：`.trellis/tasks/09-29-docs-site/`
状态：**核心集实现完成并实测验证**；构建、i18n、搜索、主题、部署 workflow 全部落地。

---

## 目标（用户原话要点）

1. 参考 `godotjs/godotjs.github.io`，在 `./godotjs-ext.github.io/` 为本仓实现文档。
2. 注意**差异与新增特性**。
3. **完全不同的主题和布局**。
4. **i18n，至少中文与英文**。
5. 完成后**提交并推送**。

## 最终交付

### 技术选型（先 spike 后决定）

VitePress 1.6.4 + 自定义主题（`theme/Layout.vue` 完全替换默认主题）+ 配置级 i18n。
spike 证据在 `.agent_tmp/docs-spike/`（安装 126 包 / 构建 1.96s / 自定义布局与 `locales`
均实测通过）。选型对比与上游清单见 `research/upstream-docs-and-ssg.md`。

### 本次范围：核心集 9 页 × 2 语言（用户裁决）

| 页面 | 与上游的关系 |
|---|---|
| `index.md` | 重写：GDExtension 身份、Godot 4.7+、引擎/平台表 |
| `guide/installation.md` | **重写**：下载按引擎分包的 addons → 解压进工程（上游是"下载自制 Godot 编辑器"） |
| `guide/project-setup.md` | 重写：Install Preset Files 产物清单、pnpm/tsc、API 数据引导两段命令 |
| `guide/first-script.md` | 重写：`default export`、构造器 `identifier?`、async/await |
| `scripting/modules.md` | 重写：`godot` / `godot-jsb` / `godot.worker`、GArray/GDictionary、PackedArray↔ArrayBuffer、**BigInt/int64**、值语义陷阱 |
| `scripting/annotations.md` | **重写**：`createClassBinder()` 新式 + 旧式装饰器并存，`@bind.exposed.const/shared` |
| `scripting/signals.md` | 重写：`@bind.signal()`、`Callable.create` 语义、`as_promise()`（替代已移除的 `$wait`） |
| `runtime/engines.md` | **无上游**：6 引擎 × scons 选择 × 出包/平台矩阵、运行时 `jsb.impl` 探测 |
| `misc/differences.md` | **无上游**：与上游 GodotJS 的差异总表 |

每页的关键事实都可追到本仓 file:line（`README.md`、`.trellis/spec/godotjs-ext/build/*`、
`scripts/typings/*`、`scripts/jsb.runtime/src/godot.annotations.ts`、
`src/editor/weaver-editor/jsb_editor_plugin.cpp`、`project/tests/**` 等）。

### 主题（与 Material 完全不同）

| | 上游 Material | 本站 |
|---|---|---|
| 顶部 | tabs + 面包屑 | 单行 header：`$ GodotJS-Ext` + 闪烁光标 + 搜索 + 语言 `<select>` + 主题切换 |
| 导航 | tabs/抽屉 | **左侧持久树形 rail**（16 组内联，无顶部 tabs） |
| 配色 | `primary: black` + indigo | 深色默认（`#14120f`）+ 琥珀 `#e0a458`；浅色跟随切换 |
| 标题 | 默认无衬线 | **等宽标题** + `##` / `###` 前缀 |
| 目录 | 浮动高亮条 | 右栏静态 outline |
| 代码块 | Material 卡片 | 左侧琥珀边线 + 无圆角 |

### i18n

- **中文为默认语言**（root `/`），英文在 `/en/`；用户裁决。
- 导航/侧边栏由 `docs/.vitepress/data/nav.mts` **单一声明**（每项含 zh/en），两侧共用结构。
- `scripts/check-i18n.mts` 比对两侧 md 相对路径集合（排除 `docs/en` 自比），不一致即 `exit 1`。
- 自绘 `LocaleSwitch.vue`：切换时保留当前页语义（换 `en/` 前缀，不跳回首页）。
- VitePress 的 local search 为**每语言一份索引**，自绘 `SearchBox.vue` 用
  `MiniSearch.loadJSON()` 消费（provider 给的是序列化后的 MiniSearch 实例，不是记录数组）。

### 其他

- `docs/.vitepress/theme/HomeCards.vue`：首页卡片；因 VitePress 不重写裸 `<a href>`，链接经 `withBase`。
- 布局内所有链接（rail / brand / 搜索结果）同样走 `withBase`，**188 条站内链接的 base 缺失实测归零**。
- `.github/workflows/deploy.yml`：pnpm install → `check:i18n` → build → `deploy-pages`；
  `DOCS_BASE` 可覆盖 base（默认项目页 `/godotjs-ext.github.io/`）。
- `README.md`：本地开发、内容布局、加页步骤、base、Pages 一次性开启指引。

## 验证（全部实测）

| 项 | 命令 / 方式 | 结果 |
|---|---|---|
| 双语结构门 | `node scripts/check-i18n.mts` | `rc=0`，`9 pages x 2 locales` |
| 构建 | `pnpm install --frozen-lockfile && pnpm build` | `rc=0`，2.3s，**无 dead-link 警告** |
| 全站页面 | 浏览器逐页（zh/en 各 9 页） | 18/18：`<html lang>` 正确、rail 10 项、active 恰 1 项、outline 5–12 项 |
| 语言切换 | 在 `/scripting/annotations.html` 切到 English | `/en/scripting/annotations.html`，`lang=en-US`，标题与 active 项同步 |
| 搜索 | 英文页搜 `export` / 中文页搜 `信号` | 20 条 / 13 条，首条带正确锚点链接 |
| 主题 | 点击切换 + 重载 | `gje--light` 生效并持久化（`bg` `#fbf9f5` ↔ `#14120f`），可切回 |
| 卡片导航 | 点击首页卡片 | 落到 `/guide/installation.html`（base 前缀正确） |
| 站内链接 base | 扫描 `dist/**/*.html` 的所有 `href="/..."` | 缺失 base 的：**0**（修复前 188） |
| 视觉对照 | 与 `https://godotjs.github.io/` 并排截图 | 布局/配色/排版显著不同 |

### 过程中修复的缺陷（均由实测暴露）

1. `check-i18n.mts` 把 `docs/en/**` 也算进中文树 → 恒报不同步。改为跳过 `en/` 子树。
2. 右栏 outline 恒为空：`markdown.headers` 必须是**顶层** `markdown` 选项，写进 `themeConfig` 被忽略。
3. 搜索无结果：`@localSearchIndex` 返回的是 **MiniSearch 序列化实例**（字符串），
   不是记录数组 → 改用 `MiniSearch.loadJSON()`。
4. 主题切换类挂在 wrapper 上，`body` 背景不生效 → 改挂 `document.documentElement`。
5. 首页卡片与 rail 用裸 `href`，VitePress 不重写 → base 丢失，188 处 → 用 `withBase` 归零。

## 已知边界

- **后续页面未做**（登记在 `implement.md` 阶段 4，不在本次验收内）：编辑器、构建/打包/CI、
  测试、示例、FAQ、`scripting/{codegen,worker,npm,cyclic-imports,code-in-editor}` 等。
- GitHub Pages **尚未真实部署**：需在新仓 Settings → Pages 将 Source 设为 GitHub Actions（属用户侧操作）。
- 主仓 `README.md` 末尾「MIT License - see LICENSE」与根 `LICENSE`（LGPL-2.1）仍不一致：
  本次只按事实在文档站写 LGPL-2.1，**未擅自改主仓许可段**（README.md:311 与 CONTRIBUTING.md:152）。
- 本地 `pnpm preview` 更换端口前曾对搜索分块报 `ERR_ABORTED`；换端口后正常，未进一步定位
  （静态产物经直接 `import()` 验证可加载并驱动搜索）。

## 提交

- `godotjs-ext.github.io`：`d377385`（站点）、`ed855ee`（搜索链接 base 修复）→ 均已推送到 `main`。
- `GodotJS-Ext`：`e8ffa20`（README 文档链接）、`b0d87d4`（许可表述修正）→ 均已推送到 `main`。

## 部署（已实际启用并验证）

- GitHub Pages 已通过 API 启用（`build_type=workflow`）：`has_pages=true`。
  UI 路径：仓库 **Settings → Pages → Source = GitHub Actions**。
- `Deploy Docs #36486298678`（workflow_dispatch）与 `#36487107541`（`ed855ee` push）
  两次运行 **build + deploy 全绿**。
- 线上实测：`https://daylily-zeleen.github.io/godotjs-ext.github.io/` 与 `/en/` 均 `200`；
  浏览器逐页（zh/en 各 9 页）`rail=10 / active=1 / outline 5–12 / <html lang>` 全部正确；
  搜索返回 20 条且首条锚点跳转成功；语言切换保留当前页语义。

### 线上部署暴露并修复的缺陷

**搜索结果的链接前缀被加了两遍**：local-search provider 自己就用
`path.join(site.base, relativePath)` 生成文档 id，我又套了一次 `withBase`，
得到 `/godotjs-ext.github.io/godotjs-ext.github.io/...`（404）。
修复：`hit.id` 原样使用。提交 `ed855ee`。
