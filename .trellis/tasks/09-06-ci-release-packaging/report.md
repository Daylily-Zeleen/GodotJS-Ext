# 进度（09-06-ci-release-packaging）

## 2026-09-28 实施轮（完成）

### 改动

| 文件 | 改动 |
|---|---|
| `misc/release/package.py`（新增） | `plan` / `fetch` / `assemble` / `verify` 四子命令；「矩阵→包」唯一派生点 |
| `.github/workflows/ci.yml` | 两条 web 腿 `engine: v8` → `engine: web`（含机理注释）；`verify-release-artifacts` 删掉内联 python 与 `v8/qjs-ng` 豁免，改调 `package.py plan --all` + `verify` |
| `.github/workflows/misc_release.yml` | 删 `upload-v8`/`upload-qjs-ng`，改单个 `upload` job（`matrix.engine: [v8,qjs-ng,jsc,node,web]`）：plan → `gh run download` 按精确名取腿 → assemble → 传资产名由脚本输出 `PACKAGE_NAME`；`permissions` 加 `actions: read`；checkout 固定到 `inputs.ref` |
| `.github/workflows/release.yml` | `publish` job 权限补 `actions: read`（打包 job 要取本 run 的 artifact） |
| `.github/actions/scons-build/action.yml` | engine case 增加 `web) engine_arg=""`；input 描述同步 |
| `README.md` | Release 段改为「每引擎一包」+ 资产表 + web 包合并用法说明 |
| `.trellis/spec/godotjs-ext/build/{index,release-packaging}.md` | 新增发布打包规范并从构建索引链入 |

### 实测证据（本地，`.agent_tmp/`）

取自 live run **36345300641** 的 23 条腿（参照 run 36291137069 的 artifact 已过 1 天保留期，
`gh run download` 报 `no valid artifacts found`；该 run 的 web 腿仍是旧名 `-v8`，取腿时按名映射
`wasm32-v8 → wasm32-web`）：

- `plan --all` = **23 条**，分配 **v8=7 / qjs-ng=9 / node=3 / jsc=2 / web=2**。
- 5 个包全部 assemble 成功（0 error / 0 traceback），产物：
  `godotjs-ext-v8-windows-linux-macos-android-ios.zip`(199.1 MiB)、
  `godotjs-ext-qjs-ng-windows-linux-macos-android-ios-web.zip`(213.6 MiB)、
  `godotjs-ext-jsc-macos-ios.zip`(12.1 MiB)、
  `godotjs-ext-node-windows-linux-macos.zip`(325.9 MiB)、
  `godotjs-ext-web.zip`(20.9 MiB)；每个 zip 顶层目录名 == zip 去扩展名。
- 键正确性逐包核对：v8 → `macos.debug.editor.arm64` + `ios.release` 指 device dylib；
  qjs-ng → `macos.debug.editor`（universal）+ `ios.release` 指 xcframework + 两组 web 键；
  jsc → `macos.debug.editor.arm64` + xcframework；node 的 `[dependencies]` 有 `bin/windows/node.dll`
  （**仅 runtime 一份**，editor 文件不带，与仓库自带文件一致）；web 包只 2 个 web 键、只 runtime 一份。
  **非 node 包无 `node.dll`**；v8 包无任何 web 产物。
- 门禁双向验证：对**改名前的 run**（36345300641，web 腿叫 `-v8`）→ `verify` **exit 1**，
  报 `web: expected artifacts missing`；对改名后的 artifact 名集合 → **exit 0**，`All 23 release legs
  are present across 5 engines`。`gh api` 路径与 `--artifacts-json` 两条入口都验过。
- **阴性对照**（证明校验非空转）：删掉 `jsc` 某腿的 runtime dylib → assemble **exit 1**
  （`did not produce its runtime library … silently drops a platform`）；塞入一个未被声明的
  `godotjs-ext-bogus.*.dylib` → assemble **exit 1**（`packaged but not declared`）。
- YAML：`misc_release.yml` / `ci.yml` / `scons-build/action.yml` 三者 `yaml.safe_load` 通过；
  upload 矩阵 = `[v8,qjs-ng,jsc,node,web]`，ci 矩阵 engine 集合 = 同 5 个。

### 实施中修掉的两个真缺陷（由阴性对照暴露）

1. 只为「包内有的文件」出键是不够的 —— 某腿整个缺 runtime 库时会被**静默跳过**。现在每个腿
   必须产出其 runtime 库，否则 assemble 失败。
2. 原先「未被引用的库」扫描按文件名前缀白名单判定豁免，导致**意外的库文件**能静默进包。
   现在改为按后缀全量检查：`bin/` 下每个 `.dll/.so/.dylib/.wasm`/`.xcframework` 都必须被某份
   `.gdextension` 引用（`node.dll` 之所以合规，是它在 `[dependencies]` 里被声明，不是被豁免）。

### 最终状态

- 代码/工作流/文档/规范全部落地；本地 5 包全绿 + 门禁正反两向 + 阴性对照（3 例）均实测通过。
- **未 commit、未 push**（按项目规则需用户明确授权）。
- 未做：真实发布链路端到端（需 `workflow_dispatch` 跑一次 CI 让 web 腿以新名产出，再走
  `release.yml` / `upload_assets.yml`）——属外部环境验证，本地无法离线完成。

## 2026-09-28 规划轮

- 重写 `prd.md`：把用户口述的 6 种引擎（v8 / quickjs / quickjs-ng / jsc / node / 纯 web，
  其中 quickjs 不出包、纯 web 单独出包）写成唯一权威表述；确认共 5 个发布包。
- 取实测证据（`.agent_tmp/`）：`gh run download 36291137069` 的 23 条构建腿 +
  `gh release download v1.0.1` 的现网包。新增 `research/evidence.md`，关键结论：
  - **`engine: v8` 的 web 腿产物其实是纯 web（`JSB_WITH_WEB`）**：wasm 内 `impl/web/` 83 次、
    `impl/v8/` 0 次（对照 qjs-ng 腿 `impl/quickjs/` 84 + `JS_NewRuntime` 6）。→ 现有 `*-v8`
    glob 把浏览器 JS 的 wasm 混进了 v8 包；需把该腿改标 `engine: web`。
  - 23 条腿可恰好划分给 5 个引擎：v8=7、qjs-ng=9、jsc=2、node=3、web=2。
  - 桌面只有 editor target（现网 v1.0.1 包同样只有 editor 产物）→ 不新增 template 腿。
  - artifact 里已含 editor 扩展 dll/so/dylib，但发布步骤只拷 runtime 的 `.gdextension` →
    editor 二进制是死重量。
  - v8 的 ios 腿只有 device dylib、无 xcframework；只有 node 的 windows 腿产出 `bin/windows/node.dll`。
- 新增 `design.md`：`misc/release/package.py`（plan/fetch/assemble/verify 四子命令）为
  「矩阵 → 包」唯一派生点；`[libraries]` 键←文件逐条派生表；包名规则；workflow 改造形态；
  与 `09-28-merge-editor-runtime` 的兼容（两份 `.gdextension` 按「文件是否存在」条件出）。
- 新增 `implement.md`：6 步（脚本 → 矩阵改标 → 门禁改调 → 发布 job 矩阵化 → 文档 → 真跑端到端），
  每步带验证命令与判据；含阴性对照（删/改一个 dll 必须让 assemble 失败）。
- 配置 `implement.jsonl` / `check.jsonl`（各 5-6 条真实 spec/research 条目）。
- 未改任何构建/发布代码；未 commit。

### 决策

- **D1 已定（2026-09-28，用户选 B）**：web 包只含 web wasm（threads+nothreads）+ runtime 一份
  `.gdextension`（仅 web 键），不附桌面库。已写入 `prd.md`「决策」与 `design.md` §9；
  用法（把 `bin/web/` 合并进桌面引擎包，或直接用 qjs-ng 包）写进 `implement.md` 步骤 5 的文档要求。
- 无剩余阻塞项。规划产物齐备（`prd.md` / `design.md` / `implement.md` / `research/evidence.md` /
  `implement.jsonl` / `check.jsonl`），待用户批准后 `task.py start` → Phase 2 按 `implement.md` 6 步执行。
