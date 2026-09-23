# 依赖仓库维护规范（GodotJS-Dependencies）

> 适用范围：工作区内的 `GodotJS-Dependencies/`（**由我方维护**的预编译依赖构建仓库），
> 以及 `SConstruct` 里 lws / v8 / libnode 三个下载源的指向。
> 本仓自身的构建命令见 [scons-build.md](./scons-build.md)。

## 位置与形态（先读这段，别猜）

| 项 | 值 |
|---|---|
| 路径 | 工作区顶层 `GodotJS-Dependencies/` |
| 远端 | https://github.com/Daylily-Zeleen/GodotJS-Dependencies |
| git | **独立 git 仓**（有自己的 `.git`），本仓 `.gitignore:96` 排除 |
| 是否子模块 | **不是**，且**不要改成子模块** |
| trellis 登记 | `.trellis/config.yaml` 的 `packages` 里以 `git: true` 登记为 polyrepo 包 |

**为什么不做子模块**：主项目消费的是它的 **release 产物 URL**（`SConstruct` 的
`deps_url` + tag + `{name}_{version}.zip`），**不做源码路径引用**。子模块 pinned 的是源码
commit，会制造"版本一致"的假象——实际产物由 release tag 决定，两者可以不一致。独立 git +
release tag 才是这里真实的版本锚点。

## 它构建什么

三个组件，各平台全量：

- `v8` → `v8_<version>.zip`（内含顶层 `v8/`）
- `lws` → `lws_4.3.zip`（内含顶层 `lws/`）
- `node`（libnode）→ `node_<node_ref>.zip`（内含顶层 `libnode/`）

工作流：

- `.github/workflows/build_{v8,lws,node}.yml`：单组件（push/PR 路径过滤触发）
- `.github/workflows/build_all.yml`：**三组件全量 + 打包 + 发 release**（`workflow_dispatch`，
  需 `bundle_tag`），产物名与校验见该文件 `publish` job

构建脚本在 `scripts/node/`、`config/v8/`，CI 动作在 `.github/actions/`。

## 改依赖库的正确流程

1. 在 `GodotJS-Dependencies/` 里改（新分支 → 提交 → 推送）。
2. 触发 `build_all.yml`（`workflow_dispatch`）发一个新 release tag。
3. **回到本仓**改 `SConstruct` 的 `deps_release_tag`（必要时连带版本常量）指向新 tag。
4. 本仓跑构建验证；需要远端验证才提交推送本仓。

顺序不能反：先有 release，再改指向。产物名必须与 `download_dependency()` 的
`{name}_{version}.zip` 对得上，否则落到一个 404。

### 版本常量对齐（改前必查）

`SConstruct` 的四个常量决定了下载 URL：

```python
deps_release_tag  = "..."              # release tag
deps_v8_version   = "12.4.254.21"      # -> v8_12.4.254.21.zip
deps_lws_version  = "4.3"              # -> lws_4.3.zip
deps_node_version = "..."              # -> node_<此值>.zip
```

`download_dependency()` 取 `{name}_{version}.zip`，release 里的资产名由 `build_all.yml`
的 `zip -r` 那几行决定，两边必须逐字一致。zip 内顶层目录名也要与 `target_dir` 的
basename 一致（`node_*` 包内是 `libnode/`，`third/libnode` 的 basename 正是 `libnode`，
故无需 `archive_root`）。

### libnode 归档目录的 arch 拼写（易错，两侧必须一致）

`node_*.zip` 解出的平台目录**只在 Windows 用 node 原生的 `x64`**，其他平台一律用可移植拼写：

| 组件 | 平台目录 | 消费者代码 |
|---|---|---|
| libnode | `windows/x64`、`linux/x86_64`、`macos/arm64`、`android/arm64`、`ios/arm64` | `SConstruct:_libnode_platform_base()` |
| v8 | `windows_x86_64_release`、`linux.x86_64.release` … | `LibraryDetails` 默认 `platform_base()` |
| lws | `windows_x86_64_release`、`linux_x86_64_release` … | 同上 |

生产者侧的契约定义在 `scripts/verify_artifacts.py:node_dir()`（`x64` 仅 windows），
构建脚本 `scripts/node/build-*.sh` 按它落盘。

**两侧不一致的后果很隐蔽**：`download_dependency()` 按 zip 里真实的顶层目录解压成功
（它只认 `libnode/`），随后 `validate_library_support()` 去拼
`<plat>/<arch>/libnode.a` 找不到 → 返回 None → 构建死在
`check(False, "libnode prebuild lib is not found.")`，而包本身完好。改任一常量或拼写后，
先用上表逐平台核一遍再提交。

## 给上游源码打补丁的约定

依赖是从**官方上游**源码构建的（node 来自 `nodejs/node`，且用的是 `v24.x` 这类
**浮动分支**），补丁以脚本形式落在 `scripts/node/`，在构建脚本里显式调用。

- **不要提交裸 `.diff` 再用 `git apply`**：浮动分支会有上下文漂移，且失败时容易静默跳过。
- 照 `patch_rtti.py` / `patch_libuv_console.py` 的写法：Python 脚本 + **精确字符串替换 +
  fail-closed**（每处替换必须命中**恰好一次**，命中 0 次或多次直接报错退出；已打过补丁的树
  也拒绝重复打）。锚点失效就让构建响亮失败，而不是产出一个"看起来构建成功但实际没补丁"的库。
- 在对应 `build-*` 脚本里调用，`PowerShell` 必须显式查 `$LASTEXITCODE`
  （PowerShell 不会因原生命令失败而中止）。

## 常见坑

- **改了依赖仓库 ≠ 本仓生效**：本仓 `third/` 下是已下载的产物，`dependency_is_ready()`
  命中就跳过下载。换产物要么删掉对应 `third/<name>/` 让下次构建重下，要么手动替换。
- **node 的 libuv 补丁只影响 Windows**：`deps/uv/src/win/*`，其他平台编译不到，
  只有 `build-windows.ps1` 需要调用它。
- 全量 `build_all` 要数小时（v8 尤其久），**不要轮询**；触发后去干别的，完成会通知。
