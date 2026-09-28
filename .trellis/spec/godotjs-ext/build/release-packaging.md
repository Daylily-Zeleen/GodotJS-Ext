# 发布打包规范（按 JS 引擎分包）

> 适用范围：`.github/workflows/misc_release.yml`、`.github/workflows/ci.yml` 的
> `verify-release-artifacts`、`misc/release/package.py`。
> 构建命令与依赖来源见 [scons-build.md](./scons-build.md) / [dependencies.md](./dependencies.md)。

## 唯一派生点（先记住这条）

**发布包的内容由构建矩阵派生，脚本是唯一派生点**：

```
ci.yml build matrix ──> misc/release/package.py ──> 每引擎一个包（含 per-engine .gdextension）
                            ^                                   ^
              ci.yml verify-release-artifacts          misc_release.yml upload job
```

- **不维护第二份平台/引擎清单**。要改「哪个引擎支持哪些平台」，只改 `ci.yml` 的矩阵。
- 发布 job 与 CI 门禁**共用**同一份 plan，因此**不可能出现「门禁绿但发出去的包缺平台」**。
- 反过来也成立：plan 写错时两边一起错 —— 所以 `assemble` 自带 1:1 自校验兜底（见下）。

## 引擎清单（6 种构建，5 个包）

| 引擎 | scons 选择 | 出包 | 备注 |
|---|---|---|---|
| v8 | 默认（无标志） | `v8` | 桌面 + android + ios |
| quickjs | `use_quickjs=yes` | ❌ | 被 quickjs-ng 上位取代；选项保留，但不建 CI 腿、不出包 |
| quickjs-ng | `use_quickjs_ng=yes` | `qjs-ng` | 覆盖面最广，含 web 上的 quickjs wasm |
| jsc | `use_jsc=yes` | `jsc` | 仅 macOS + iOS |
| node | `use_node=yes` | `node` | 仅 3 个桌面 editor 腿 |
| **web** | `platform=web` 且**不带**任何引擎标志 | `web` | 浏览器宿主 JS（`JSB_WITH_WEB`）；仅 web |

### `engine: web` 不是 `engine: v8`（踩过，2026-09-28）

platform=web 且不带引擎标志时，产物**必然是** `JSB_WITH_WEB`（浏览器宿主 JS），因为
`v8_prebuilt_libs` 没有 web 条目 → `is_library_supported` 为假 → `v8_support=None`
（`SConstruct:383-426`）。wasm 内字符串探针：`impl/web/` ×83、`impl/v8/` ×0。

因此 web 腿在矩阵里必须标 `engine: web`；标 `v8` 会让 `*-v8` 的后缀 glob 把浏览器 wasm
扫进 v8 包（旧行为）。`scons-build` action 对 `web` 与 `v8` 都传「不加引擎 flag」，两者
只在**发布归属**上不同。

## 包内 `.gdextension` 必须与包内文件 1:1

每个包内含**一份** `.gdextension`（`godotjs-ext.gdextension`；单库后不再有 runtime/editor 两份，
见 [../cpp/architecture-constraints.md](../cpp/architecture-constraints.md) 的「已合并为单库」节），
其 `[libraries]` / `[dependencies]` **只声明包内实际存在的文件**：

- `[configuration]` 段从仓库的 superset 文件**原样复制**（`compatibility_minimum` 等不能丢）。
- 键的 feature tag 必须是引擎真正拥有的：`editor` 腿 → `.debug.editor.`；`template_release` →
  `.release.`。**不得**声明矩阵里不存在的 target（旧手写文件里「声称 windows template_debug
  却无该 dll」这类条目就是这样来的）。
- macos：universal 产物占 arch-less 键（`macos.debug.editor`）；arm64-only 产物（v8/node/jsc）
  只能声明 `macos.debug.editor.arm64`，**不可**声明 arch-less 条目（Godot 先匹配 arch 键再回落，
  声明了 arch-less 就会去找不存在的 `...universal.dylib`）。
- ios：有 xcframework 的引擎（qjs-ng/jsc）指向 `...xcframework`；v8 的 iOS 腿无 xcframework
  （无 simulator 预编译），指向 device dylib。**分支由「文件是否存在」决定，不靠引擎名硬编码**。
- web：`web.release.threads.wasm32` ↔ `...wasm32.wasm`；`web.release.wasm32` ↔
  `...wasm32.nothreads.wasm`。
- `[dependencies]` 的 `bin/windows/node.dll` **只出现在 node 包**（且只挂在 windows 的
  `[libraries]` 键上）：单库只有 node 引擎那条腿链 libnode，其余引擎的包不带该依赖。

### 自校验是硬门（两向都要）

`assemble` 在写完后断言：

1. 每条声明都能在包内找到（`声明了但缺文件` → 失败）；
2. `bin/` 下**每个**可加载库（`.dll/.so/.dylib/.wasm` 与 `.xcframework` 目录）都被某份
   `.gdextension` 引用（`打包了但没声明` → 失败）；
3. 每个构建腿都真的产出了它的 runtime 库（否则报「silently drops a platform」）。

判定**不依赖文件名前缀白名单**——白名单会让「意外的库文件」静默进包（实测踩过）。
`node.dll` 之所以合规，是因为它在 `[dependencies]` 里被声明，而不是被豁免。

## 取 artifact：按精确名，不要用后缀 glob

`misc_release.yml` 用 `gh run download -n <artifact>`（经 `package.py fetch`）。**不要**改回
`actions/download-artifact` 的 `pattern`：一个包的腿集不再共享单一后缀（qjs-ng 与 web 都含
wasm 腿），而 glob 正是历史上把浏览器引擎混进 v8 包的机制。

- 权限：取本 run 的 artifact 需要 `actions: read`（`misc_release.yml` 与 `release.yml` 的
  `publish` job 都已显式声明；`upload_assets.yml` 早有）。
- checkout 要 `ref: ${{ inputs.ref }}`：plan 取自**被发布那一版**的 `ci.yml`。

## 发布触发链的坑（2026-09-29 真实发版踩过）

发布链是 `changeset 推 main → changesets 开版本 PR → 合并 → CI → release.yml → misc_release.yml`。
以下三处都只在**真实发版**时才会暴露，本地/`assemble` 单测覆盖不到：

1. **`node release/get-version.js` 必须打印版本**（`scripts/release/get-version.js`）。
   工作流用 `version="$(node release/get-version.js)"` 取 tag；该文件既是模块（`publish.js`
   里 `getVersion()` 是按函数调用）又是可执行文件，必须同时满足。若只 `export` 不打印，
   取到**空字符串**，而空 tag 在 `gh release view ""` 上返回 `not found`——与「release 尚不
   存在」不可区分，于是 should-publish 恒 `true`：release 被建出来，随后 5 个 upload job
   全在找 tag `""` 时报 `HttpError: Not Found`。`getVersion()` 还要兼容 `pnpm pkg get version`
   历来的两种输出（JSON 字符串 / 裸值），否则 `JSON.parse("1.0.1")` 抛错（v1.0.1 当年即此因）。
2. **写 `$GITHUB_OUTPUT` 必须是 `key=value`**。`grep '^PACKAGE_NAME=' | cut -d= -f2-` 会把键
   一起切掉，runner 直接报 `Invalid format '<value>'` 并中止该 job（上传前就死）。原样
   `grep '^PACKAGE_NAME=' >> "$GITHUB_OUTPUT"` 即可。
3. **`should-publish=false` 时只跑 `Check release condition`**，release 与 upload 全 skipped；
   这是正常短路（版本已有 release 时如此），不是失败。要发新版必须先让 Changesets 合并
   版本 PR 把 `scripts/package.json` 升上去。

诊断入口：失败时先看 `Check release condition` job 日志里的 `ci:should-publish=` / `version=`，
再看各 `Upload <engine> Assets` job 是死在 lookup（release 不存在/取不到 id）还是
`$GITHUB_OUTPUT`（Invalid format）。

## 本地验证发布包（不必真发版）

```bash
# 取参照 run 的全部腿（artifact 保留期仅 1 天，尽快取；过期就换一个成功 run）
python misc/release/package.py fetch --engine v8 --run <run-id> --dir .agent_tmp/legs
for e in v8 qjs-ng jsc node web; do
  python misc/release/package.py assemble --engine $e --artifacts .agent_tmp/legs --out .agent_tmp/pkgs
done
# 门禁的离线形态（用 gh api 或 --artifacts-json 注入实际 artifact 名列表）
python misc/release/package.py verify --all --run <run-id> --repo <owner>/<repo>
```

判据：5 个包各生成一个 zip，脚本零 exit≠0，且每个 zip 的顶层目录名 == zip 去扩展名。

**阴性对照必须做**（证明校验不是空转）：删掉某个腿的 runtime 库 → `assemble` 必须失败；
塞一个未被声明的库文件 → 必须失败。

## 本地开发用的 `.gdextension` 不受影响

`project/addons/godotjs-ext.daylily-zeleen/*.gdextension` 是**全平台 superset**，供本仓测试
项目在任意腿下加载，**不要**按引擎裁剪它；per-engine 版本只在打包产物里生成。
