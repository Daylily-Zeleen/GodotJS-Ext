# 设计：按引擎分包

> 对应 `prd.md`（要求 R1-R8 / AC）。事实依据全部来自 `research/evidence.md`（CI run 36291137069 实测）。

## 1. 结构总览

```
ci.yml 构建矩阵 ──(唯一来源)──> misc/release/package.py ──> 5 个包(内容+两份/一份 .gdextension)
                                      │
                     ┌────────────────┴─────────────────┐
        ci.yml verify-release-artifacts            misc_release.yml upload-<engine>
        （期望集 = plan，失败即红）                 （plan → 取 artifact → assemble → 传资产）
```

新增 **`misc/release/package.py`** 是「矩阵 → 包」的唯一派生点，四个子命令：

| 子命令 | 作用 | 调用方 |
|---|---|---|
| `plan --engine <E>` / `plan --all` | 解析 `ci.yml` 矩阵，输出该引擎的期望 artifact 名列表（stdout 每行一个） | 发布 job 与门禁 |
| `fetch --engine <E> --run <id> --dir <D>` | 按 plan 用 `gh run download` 取腿到 `<D>` | 发布 job（本地验证同样走它） |
| `assemble --engine <E> --artifacts <D> --out <O>` | 组装包内容 + 生成 `.gdextension` + **自带 1:1 校验** | 发布 job、本地验证 |
| `verify --all [--run <id> | --artifacts-json <f>]` | 门禁：plan 的每个引擎非空，且 run 里实际存在的 artifact 覆盖 plan | ci.yml 门禁 |

矩阵解析沿用现有门禁的做法（`yaml.safe_load('.github/workflows/ci.yml')['jobs']['build']['strategy']['matrix']['include']`），不新增第二份平台清单。

## 2. 引擎 ↔ artifact 腿 ↔ 包内容

artifact 名 = `{platform}-{target}-{arch}-{engine}[-nothreads]`（ci.yml:697-700）。

| 包 | 引擎 token | 腿（= plan 输出） | 包内 `bin/` |
|---|---|---|---|
| v8 | `v8` | windows-editor-x86_64、linux-editor-{x86_64,arm64}、macos-editor-arm64、android-template_release-{arm64,x86_64}、ios-template_release-arm64 | windows/ linux/ macos/ android/ ios/ |
| qjs-ng | `qjs-ng` | v8 的桌面+android+ios 同名腿（引擎换 qjs-ng，macos 为 universal）+ web-template_release-wasm32-qjs-ng[-nothreads] | + web/ |
| jsc | `jsc` | macos-editor-arm64、ios-template_release-arm64 | macos/ ios/ |
| node | `node` | windows-editor-x86_64、linux-editor-x86_64、macos-editor-arm64 | windows/ linux/ macos/ |
| web | `web` | web-template_release-wasm32-web[-nothreads]（**仅 2 条**，D1=B） | web/ |

**R2 的落地**：ci.yml 的 web 腿 `engine: v8` → `engine: web`；`.github/actions/scons-build/action.yml` 的 case 增加 `web) engine_arg="" ;;`。两条 web 腿于是不再被 `*-v8` 命中。

## 3. 包内布局与组装

```
<PKG>/                       # PKG = zip 名去 .zip
├── godotjs-ext.gdextension  # runtime（必出）
├── godotjs-ext.gdextension.uid
├── godotjs-ext-editor.gdextension      # 仅当源文件存在且包内有 editor 二进制
├── godotjs-ext-editor.gdextension.uid
├── LICENSE
└── bin/<platform>/…         # 各腿 artifact 的顶层内容，原样搬运
```

规则：

1. **合并**：各腿 artifact 顶层内容（`<platform>/…`、`addons/…`）合并进 `bin/`；artifact 里 scons-build 放的 `addons/…/LICENSE` 归一到包根 `LICENSE`，**不**产生 `bin/addons/`。
2. **原样保留**：`bin/` 下附属文件（`.pdb`/`.ilk`/`.exp`/`.lib`/`.a`、node helper 可执行、`node.def`）全部保留（非目标：不裁剪）。
3. **两份 `.gdextension` 从包内实际文件派生**：`assemble` 扫描合并后的 `bin/` 决定 `[libraries]`，扫描完再回读断言「每个声明的相对路径都存在」；任何「声明了但缺文件」或「有主库文件但没被任何键引用」=> `exit 1`。
   - editor 源文件不在仓库时（见 §7 跨任务）自动只出 runtime 一份；包内没有 editor 二进制时同理。
4. **zip**：`cd <out> && zip -r <PKG>.zip <PKG>`（包内目录名 = zip 名，沿用现状）。

## 4. `[libraries]` 派生契约（键 ← 文件，逐条穷举）

所有键**只在该文件存在时**输出。`arch` 取腿的 arch，`target` 取腿的 target。

| 文件（相对 `bin/<platform>/`） | 键 |
|---|---|
| `godotjs-ext.windows.editor.<arch>.dll` | `windows.debug.editor.<arch>` |
| `godotjs-ext-editor.windows.editor.<arch>.dll` | `windows.debug.editor.<arch>`（在 editor 文件里） |
| `godotjs-ext.macos.editor.arm64.dylib` | `macos.debug.editor.arm64` |
| `godotjs-ext.macos.editor.universal.dylib` | `macos.debug.editor`（无 arch 键；universal 产物必须占 arch-less 键，见 R4） |
| 同上两条的 `-editor` 版本 | 同规则，写在 editor 文件里 |
| `godotjs-ext.linux.editor.<arch>.so` | `linux.debug.editor.<arch>` |
| `godotjs-ext-editor.linux.editor.<arch>.so` | `linux.debug.editor.<arch>`（editor 文件） |
| `godotjs-ext.android.template_release.<arch>.so` | `android.release.<arch>` |
| `godotjs-ext.ios.template_release.xcframework/`（目录） | `ios.release` |
| `godotjs-ext.ios.template_release.arm64.dylib`（无 xcframework 时） | `ios.release` |
| `godotjs-ext.web.template_release.wasm32.wasm` | `web.release.threads.wasm32` |
| `godotjs-ext.web.template_release.wasm32.nothreads.wasm` | `web.release.wasm32` |

`[dependencies]`：仅当 `bin/windows/node.dll` 存在，为**runtime** `.gdextension` 的每个
windows 库键写 `{ "bin/windows/node.dll": "" }`（相对包根，与库值同基准）。editor
`.gdextension` 不带该依赖（editor 扩展不链 libnode，与仓库自带的 editor 文件一致）。
其余情况整个 `[dependencies]` 段省略。

键集不做「未来平台」铺陈：**矩阵里没有的腿 = 包里没有的文件 = 不出现的键**。这样包内不再出现
「声称 windows template_debug 却无该 dll」（现存 superset 就有此类条目）。

## 5. 包名（R6）

- 平台 token（固定顺序）：`windows linux macos android ios web`
- zip = `godotjs-ext-<engine>-<platforms 用 '-' 连接>.zip`；若平台 token 序列与 engine token 相同且只有一个（`web` 引擎仅 web 腿）→ 省略平台段，得 `godotjs-ext-web.zip`

据此（D1=B）：`godotjs-ext-v8-windows-linux-macos-android-ios.zip`、
`godotjs-ext-qjs-ng-windows-linux-macos-android-ios-web.zip`、`godotjs-ext-jsc-macos-ios.zip`、
`godotjs-ext-node-windows-linux-macos.zip`、`godotjs-ext-web.zip`。

web 包只出 runtime 一份 `.gdextension`（无 editor 腿 ⇒ 无 editor 二进制 ⇒ 无 editor 键）；
其余 4 包出两份。README 的 Release 段必须写明：web 包需覆盖合并进所用桌面引擎包（包内目录结构一致）。

## 6. 工作流改造

### `misc_release.yml`

删除 `upload-v8` / `upload-qjs-ng`（:110-224），替换为：

```yaml
  upload:
    name: Upload ${{ matrix.engine }} Assets
    needs: [release]
    if: always() && (inputs.mode == 'upload-only' || needs.release.result == 'success')
    strategy:
      fail-fast: false
      matrix: { engine: [v8, qjs-ng, jsc, node, web] }
    runs-on: ubuntu-latest
    steps:
      - checkout
      - lookup release id            # 保留现有 github-script
      - Plan legs      run: python misc/release/package.py plan --engine ${{ matrix.engine }}
      - Fetch legs     run: python misc/release/package.py fetch --engine ... --run ${{ inputs.run_id }} --dir artifacts
      - Assemble       run: python misc/release/package.py assemble --engine ... --artifacts artifacts --out out
      - Upload asset   # name 由 assemble 打印的 PKG 名决定（github-script，保留现有形态）
```

取腿走 `gh run download`（`GH_TOKEN` 已有），**不用** `actions/download-artifact` 的 `pattern`：web 包的腿集不止一个后缀，glob 表达不了，而 `gh` 可按名字精确取。artifact 保留期 1 天，发布只在 CI 成功后立刻跑，够用（现状同样依赖它）。

`release.yml` / `upload_assets.yml` 调用面不变（仍然 `misc_release.yml` 的 `mode=full|upload-only`）。

### `ci.yml`

- 两条 web 腿 `engine: v8` → `engine: web`（含注释：该腿产物为 `JSB_WITH_WEB`，与 v8 无关，附 `research/evidence.md` 的探针结论）。
- `verify-release-artifacts`（:702-742）改为：`package.py plan --all` 得到期望集（全 5 引擎），与
  `gh api …/artifacts` 的实际名集合比对；并断言每个引擎的期望腿数 > 0。原脚本里
  `if eng not in ('v8','qjs-ng'): continue` 的静默豁免随之消失。

## 7. 跨任务与兼容

- **依赖 `09-28-merge-editor-runtime`**（planning）：该任务会删掉 editor 扩展与
  `godotjs-ext-editor.gdextension`。本设计的 assemble 以「源文件是否存在 + 包内是否有 editor 二进制」
  为条件出 1 或 2 份 `.gdextension`，两个任务的先后顺序都不需要改代码；但**打包逻辑不要写死两份**。
- 本仓开发用 superset `.gdextension`（`project/addons/...`）**不改**：它声明全平台是为了让本仓测试项目
  在任意腿下都能加载；per-engine 版本只在打包产物里生成。
- 资产名变更属破坏性变更：旧名 `godotjs-ext-{v8,qjs-ng}-windows-linux-macos.zip` 消失。README 的
  Release 段落同步更新（Installation 文档在外部站点，不在本仓）。
- 回滚：改动集中在 `misc/release/package.py`（新增）、`misc_release.yml`、`ci.yml`、
  `scons-build/action.yml` 一行；回退 commit 即恢复旧行为。已发布的错误资产可用
  `gh release upload --clobber` / `gh release delete-asset` 修。

## 8. 风险

| 风险 | 处置 |
|---|---|
| 门禁与发布共用 plan，若 plan 写错则两边一起错（同源错误） | assemble 的 1:1 校验与 `verify` 的「期望腿必须都在 run 里」互为交叉检查；另在本地对 run 36291137069 的 23 腿跑通全量 assemble |
| 发布 job 首次真实运行时才发现 `gh run download` 细节（如 artifact 保留期过期、并发限流） | 本地用同一命令对 36291137069 跑通；发布出错可 `upload_assets.yml` 重跑（现状已有此恢复路径） |
| web 包单独不可用（D1=B 的已接受代价） | 文档写清合并用法；qjs-ng 包自带 quickjs 版 web 腿作为替代路径 |
| `.gdextension` 里 macos/ios 键选择错误会让用户在真机上加载失败 | 键按 §4 从文件名推导，且 1:1 校验兜底；macos universal / arm64 与 ios xcframework / dylib 的分支各由「文件是否存在」决定，不靠引擎名硬编码 |

## 9. 决策点

**D1（已定 2026-09-28，用户选 B）：web 包只含 web wasm，不附桌面库。**

- 事实：编辑器里要加载扩展并做 web 导出，需要 **桌面 runtime 库（runtime 扩展）+ editor 扩展**；
  纯 web 引擎没有任何桌面实现（`JSB_WITH_WEB` 仅在 `jsb_platform == "web"` 为 1，`SConstruct:426`），
  因此只含 web wasm 的包在桌面编辑器里 `find_extension_library` 找不到任何匹配键，扩展不会加载
  （`core/extension/gdextension_library_loader.cpp:74-97`，失败表现为 `GDExtension dynamic library
  not found`，`core/extension/gdextension.cpp:808`）。两个引擎包也不能共存（同名 addon 目录 +
  都注册 ScriptLanguage）。
- **采纳 B**：`godotjs-ext-web.zip` = `bin/web/godotjs-ext.web.template_release.wasm32{,.nothreads}.wasm`
  + runtime 一份 `.gdextension`（仅 web 键）+ LICENSE。使用方式：把 `bin/web/` 覆盖合并进所用桌面引擎包
  （包内目录结构一致），或直接用 qjs-ng 包（其 web 腿提供 quickjs 版 wasm）。
- 代价（已接受）：web 包不能单独在编辑器里用；README/Release 说明必须写清合并用法。
- 另两支已否决：A（web 包自带 v8 的 4 条桌面 editor 腿，可独立使用但 ≈480MB 重复）；
  qjs-ng 包内同时提供 quickjs 版 web 腿（现状即如此，保留）。
