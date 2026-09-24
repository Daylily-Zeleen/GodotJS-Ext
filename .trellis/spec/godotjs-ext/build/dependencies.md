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

现有补丁脚本与它解决的编译期问题：

| 脚本 | 平台 | 作用 |
|---|---|---|
| `patch_rtti.py` | 全平台 | 打开 RTTI（下游 subclass v8 Delegate 需要 typeinfo） |
| `patch_libuv_console.py` | windows | 去掉 libuv 永不返回的控制台 resize 线程（它会长久 pin 住装载它的 DLL） |
| `patch_pic.py` | linux/android/ohos | 给 POSIX cflags 加 `-fPIC`（**静态**构建默认不加，归档无法链进 .so） |
| `patch_debug_info.py` | macos/ios | 关 `GCC_GENERATE_DEBUGGING_SYMBOLS`（否则 gyp 给每个目标加 `-gdwarf-2`，Release 归档 ~10GB） |
| `patch_no_ltcg.py` | windows | 去掉 `vcbuild.bat` 里 `release` 隐含的 `ltcg=1`（= `--with-ltcg`）。**必须**：带 LTCG 时归档成员是 LLVM bitcode，node.exe 自己能链（lld 认 bitcode），但 MSVC 消费者报 `LNK1107: invalid or corrupt file` —— **只在 embedder 侧炸**，已于本地复现该机制 |

## libnode 归档装配（最容易做错、且只在**下游**才炸的一环）

`scripts/node/merge_libnode.py` 把 node 构建出的几十个静态库合成**一个**自包含
`libnode.a` / `libnode.lib`。下游用 `--whole-archive` / `-force_load` / `/WHOLEARCHIVE`
整体链入，所以**装错集合或丢成员，只在 embedder 链接时才暴露**。三条铁律：

1. **合并集合 = node 自己链接的集合，不是目录里所有归档。**
   `out/Release` 下的归档比 node 实际链接的多 5 个（`libv8_init`、`libgtest`、
   `libgtest_main`、`libicutools`、`libtorque_base`）。其中 `libicutools.a` 是**给构建宿主**
   编的 ICU 对象、`libv8_init.a` 含 `setup-isolate-`**full**（与 `libv8_snapshot.a` 的
   `setup-isolate-deserialize` 冲突）→ 整体链入必然 `multiple definition` / 未定义符号。
   集合来源用 node 自己的链接元数据：unix 读 `out/node.target.mk` 的 `LD_INPUTS`
   （路径形如 `$(obj).target/...`，注意 `$(obj)` = `<out>/Release/obj`、`.target` 是**字面**
   后缀；`.mk` 落在 `out/` 而非 `out/Release/`）；windows 读 `node.vcxproj` 里
   `ConfigurationType=StaticLibrary` 的 `ProjectReference`。**不要用目录扫描兜底**。
2. **成员必须按位置提取，不能按名字。**
   gyp 用 `ar crs` 从 `heap/sweeper.o`、`heap/cppgc/sweeper.o` 建库，ar 只存 **basename**
   → 同一归档里 `"sweeper.o"` 会出现两次，`ar p <lib> <name>` 两次都返回**第一个**
   → cppgc 那份静默丢失（mac 腿因此 131 个未定义符号）。
   **同名成员是常态**：上游已知可用归档有 58 组重名成员（内容各不相同）。
   正确做法：流式解析归档结构（GNU `/off` + `//`、BSD `#1/len`、thin）**按偏移**取成员，
   落盘时每个成员一个独立子目录以保留原名，合并后校验成员数 + 名字多重集与输入一致。
   MSVC 的 `//` 串表用 NUL 分隔、整表以 `\n` 收尾，**终止符要取最早出现的那个**。
3. **Windows 也必须真合并。**
   node 自己的 `libnode.lib` 只有 node 的对象（~192 成员 / 2.9 万符号），而 embedder 需要
   v8/icu/openssl 等带来的 ~22 万符号。`build-windows.ps1` 若用 `Copy-Item` 直接拷它，
   下游就是 163 个未解析外部符号。合并用 `lib.exe /OUT:merged.lib @response`
   （`lib.exe` 会保留同名成员；**不依赖 vcvars**：脚本自行在 PATH 或 vswhere 结果里定位，
   并把 `lib.exe` 所在目录前置到 PATH——它需要这个才能找到自己的 DLL）。

| 平台 | 写归档方式 |
|---|---|
| windows | `lib.exe /OUT:<out> @filelist`（`merge_libnode.py` 内自定位） |
| macos/ios | `libtool -static -o <out> -filelist <list>` |
| linux/android/ohos | `ar rcs <out> @filelist` |

### 校验必须包含「整档链接」 smoke test

`scripts/verify_symbols.py` 老版本只查几个 RTTI 标记符号，**装错的归档照样绿**——
这就是三平台打包全坏而依赖 CI 报 success 的原因。现在对 node 追加三项，缺一不可：

- 成员数下限（上游 ~3630；node 自身对象档只有 ~190）；
- 跨库符号覆盖（v8 / libuv / nghttp2 / ICU / OpenSSL / zlib / zstd / cppgc 各取一个标记）；
- **`-shared -Wl,--whole-archive <archive> -Wl,--no-whole-archive` 整档链接**
  （linux 腿）：非 PIC → relocation 报错；混入 host 工具归档 → `multiple definition`；
  丢成员 → 未定义符号。**一条测试同时覆盖三类缺陷**（已实测：上游已知可用归档通过，
  坏归档失败）。注意**不要**加 `--no-undefined`——已知可用归档会因缺 `libc++` 运行时
  符号而失败（假阳性）。


## 常见坑

- **改了依赖仓库 ≠ 本仓生效**：本仓 `third/` 下是已下载的产物，`dependency_is_ready()`
  命中就跳过下载。换产物要么删掉对应 `third/<name>/` 让下次构建重下，要么手动替换。
- **node 的 libuv 补丁只影响 Windows**：`deps/uv/src/win/*`，其他平台编译不到，
  只有 `build-windows.ps1` 需要调用它。
- 全量 `build_all` 要数小时（v8 尤其久），**不要轮询**；触发后去干别的，完成会通知。

## CI 迭代姿势（不要每轮都全量）

全平台全量是数小时。验证一个平台的问题时按下面三步逐级放大。

### 1. 只构建、只验证一个平台

四个 workflow 的 `workflow_dispatch` 都接受 `platforms`，且支持 **`平台-架构`** 粒度：

```bash
# 只跑 windows 的 node 腿（1 个 job，而不是 30 个）
gh workflow run build_node.yml --repo Daylily-Zeleen/GodotJS-Dependencies \
  --ref <branch> -f node_version=v24.x -f platforms=windows-x86_64
```

拼写：`linux-x86_64`、`linux-arm64`、`macos-arm64`、`macos-x86_64`、`windows-x86_64`、
`windows-arm64`、`android-arm64`、`android-arm32`、`android-x86_64`、`ios-arm64`、`ohos-arm64`。

### 2. 编译缓存（ccache）——已接入 node 与 v8

两者的腿都是「编译占绝大部分」（node windows 实测 52 分钟里 51 分钟在编译；v8 单腿
31~87 分钟，且此前**完全没有编译缓存**）。

| 组件 | 接入方式 |
|---|---|
| node windows | `vcbuild.bat ... ccache <dir>`（= `--use-ccache-win`），由 `build-windows.ps1 -CcacheDir` 传入 |
| node unix | `build-{linux,macos}.sh` 把 `CC`/`CXX` 前缀为 `ccache <compiler>`（gyp 的 make 生成器只认 `CC`/`CXX`；在 action 里注入无效，会被脚本后面的 `export CC=gcc-12` 冲掉） |
| v8 | gn 的 `cc_wrapper = "ccache"`（unix 写进 `args.gn`；windows 写进内联 gn args） |

**两条硬性要求**（都不是可选项）：
- 缓存键**必须带 `github.run_id`**：`actions/cache` 命中已有 key 时**不会覆盖**，固定 key
  会让缓存永远停在第一次的状态（只存不更新）。配合 `restore-keys` 前缀实现"暖启动 + 每轮发布"。
- `CCACHE_DIR` 必须**显式设成被缓存的那个目录**：ccache 各平台默认路径不同（macOS 是
  `~/Library/Caches/ccache`），设错等于缓存了个空目录。

### 3. 不等完整发版，直接取单平台产物

`build_all.yml` 的 publish 除正式 tag 外，把**每个 组件+平台+架构** 打成独立 zip，发到
`ci-<run_id>` release，并附 `ci-assets.txt` 映射清单：

```
<component>_<platform>_<arch>.zip      # v8 / lws / node 统一拼写
v8_include.zip                          # v8 头文件只发一次（22 条腿内容相同，避免重复打包）
```

- 资产名对三种 staging 目录名做了归一化（`linux.x86_64.release`、`windows_x86_64_release`、
  `windows/x64` → 统一 `_` 拼接）
- node 用 **node 原生** arch 拼写（windows 是 `x64`），v8/lws 用 `x86_64`
- **release 资产匿名可读（实测 200），workflow artifact 匿名下载是 401** —— 走 release 而非
  artifact，因此**不需要跨仓 token**（PAT 会过期、且泄漏面是"本仓任何 workflow 都能读依赖仓"）
- 保留策略：`keep_ci_releases`（默认 3）、`keep_full_releases`（默认 5）自动清理旧发布；
  当前 run 刚创建的永不删

本仓侧入口：`.github/workflows/ci.yml` 的 `deps_run` / `deps_engines` 输入；本地则设
`GODOTJS_DEPS_STAGING` 指向解压后的 staging 树（`SConstruct` 据此跳过 release 下载，
且 staging 不完整时**响亮失败**，不会静默用错产物）。

