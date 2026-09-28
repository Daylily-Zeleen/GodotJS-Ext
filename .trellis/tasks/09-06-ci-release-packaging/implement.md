# 实施计划（09-06-ci-release-packaging）

> 依赖 `design.md`；顺序即提交顺序。每步的验证命令必须真实跑过再进下一步。
> 临时产物一律落 `.agent_tmp/`。

## 前置事实（已实测，不必重查）

- 参照 run：**36291137069**（23 条构建腿），artifact 名与内容见 `research/evidence.md`。
- `engine: v8` 的 web 腿产物是纯 web（`JSB_WITH_WEB`），其 wasm 内 `impl/web/` 83 次、
  `impl/v8/` 0 次。
- `gh` 本地已登录（scopes: repo/workflow），`gh run download` 可直接用。

## 步骤

### 1. 新增 `misc/release/package.py`

- `plan`：读 `.github/workflows/ci.yml` 的 build 矩阵，按 `engine` 过滤出期望 artifact 名
  （`{platform}-{target}-{arch}-{engine}{-nothreads}`，web nothreads 的判定同现有门禁）。
- `fetch`：按 plan 逐条 `gh run download <run> -n <artifact> -D <dir>/<artifact>`。
- `assemble`：合并腿 → 写 `<PKG>/godotjs-ext.gdextension`（+ editor）→ 1:1 自校验 → zip。
  键派生规则严格照 `design.md` §4；**只看文件是否存在**，不硬编码引擎名。
- `verify`：plan 全引擎非空 + 实际 artifact 覆盖 plan（输出缺失清单并 `exit 1`）。
- 顶部写清：**这是「矩阵 → 包」的唯一派生点**；改矩阵不必改本脚本，改键规则只需改这里。

验证（本地、离线可做）：

```bash
# 一条命令取齐 23 腿（同一 run）
python misc/release/package.py fetch --all --run 36291137069 --dir .agent_tmp/legs
# 逐引擎组装 + 1:1 校验
for e in v8 qjs-ng jsc node web; do
  python misc/release/package.py assemble --engine $e --artifacts .agent_tmp/legs --out .agent_tmp/pkgs
done
```

判据：5 个 package 目录生成、脚本零 exit≠0；`unzip -l` 抽查每包内容与 `design.md` §2 表一致；
把某个 `bin/<platform>/xxx.dll` 改名重跑 → 必须报「声明了但缺文件」并失败（阴性对照）。

### 2. `ci.yml`：web 腿改标 `engine: web` + scons-build 接受该值

- `.github/workflows/ci.yml:498-534` 两条 web 腿 `engine: v8` → `engine: web`（注释写明产物是
  `JSB_WITH_WEB`，附探针证据）。
- `.github/actions/scons-build/action.yml:131-139` 增加 `web) engine_arg="" ;;`。

验证：`python misc/release/package.py plan --all` 输出里出现
`web-template_release-wasm32-web[-nothreads]` 且**不再**有 `web-…-v8`；`*-v8` 的 plan 只余 7 条。

### 3. `ci.yml`：门禁改调 package.py

- `verify-release-artifacts`（:702-742）去掉内联 python 与 `eng not in ('v8','qjs-ng')` 豁免，
  改为 `python misc/release/package.py verify --all`（内部拉实际 artifact 名集合比对）。

验证：本地用 36291137069 的 artifact 名列表跑 `verify`（脚本支持 `--artifacts-json` 本地注入），
必须报出「`web-…-web` 缺失」（该 run 里 web 腿还叫 `-v8`）——这恰好证明门禁对改名前的 run 会红；
再用改名后的假列表跑一次必须绿。

### 4. `misc_release.yml`：两个 job → 引擎矩阵 job

按 `design.md` §6 替换 `upload-v8` / `upload-qjs-ng`（:110-224）。

验证：`python -c "import yaml;yaml.safe_load(open('.github/workflows/misc_release.yml'))"` 解析通过；
`python misc/release/package.py plan --engine web` 在该 job 的运行时参数下能被引用（本地跑一次同命令）。
真实链路靠步骤 6 的 `workflow_dispatch`。

### 5. 文档

- `README.md` 的 Release 段：资产名变更 + 「每个包 = 一个 JS 引擎、只含该引擎的腿」；
  写明 **D1=B 的用法**：`godotjs-ext-web.zip` 只含 web wasm，需把 `bin/web/` 覆盖合并进所用桌面
  引擎包（或直接用 qjs-ng 包，其 web 腿提供 quickjs 版 wasm），否则桌面编辑器里加载不了扩展。

### 6. 端到端验证（真跑）

1. `gh workflow run ci.yml`（workflow_dispatch）→ 等 `Verify Release Artifact Names` 绿：
   证明矩阵改名 + 门禁覆盖 5 引擎。
2. 取该 run 的 23 腿，本地重跑步骤 1 的 `fetch` + `assemble`（不碰真实 release）。
3. 发布链路：等下次真实发布（或对既有 v1.0.1 用 `upload_assets.yml` 手动指定 run id 试验，
   失败可 `gh release delete-asset` 回退）。

判据：5 个引擎各有一个 asset；每个包内 `.gdextension` 的键与 `bin/` 文件 1:1；
`godotjs-ext-v8-…zip` 内**没有** web 产物；`node.dll` 只出现在 node 包。

## 验收对照

| AC | 证明手段 |
|---|---|
| 5 包各一个、无漏 | 步骤 6.1 门禁绿 + release 资产清单 |
| 5 包恰好划分 23 腿 | 步骤 1 的 plan（7+9+2+3+2=23）+ assemble 的合并日志 |
| 包里不含他引擎腿 | 步骤 6 后 `unzip -l` 逐包比对 `design.md` §2 |
| 键↔文件 1:1 | 步骤 1 的 1:1 自校验 + 阴性对照 |
| macos / ios 键符合实情 | 步骤 1 产物里 v8/qjs-ng 两包 macos 键对比；v8 ios 无 xcframework |
| web threads/nothreads 两组键 | qjs-ng 与 web 两包的 web 键 |
| node.dll 只属 node 包 | 逐包 grep `[dependencies]` |
| zip 名如实 | 资产名 vs 包内平台目录 |
| 门禁不再静默漏包 | 步骤 3 的阴性对照（旧名 run 必红） |

## 回滚点

- 步骤 1（新增脚本）：删文件即回滚，无副作用。
- 步骤 2-4（workflow）：单 commit 回退即恢复旧发布行为。
- 步骤 6 的已发布资产：`gh release delete-asset` + 旧 workflow 重跑。
