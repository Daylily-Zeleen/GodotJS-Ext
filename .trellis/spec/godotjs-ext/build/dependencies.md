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

### macos/ios 的 RTTI 是 **xcode_settings**，不是 cflags（踩过，2026-09-25）

`common.gypi` 对 RTTI 有两种完全不同的关闭写法：

- POSIX（linux/android/ohos）：`'cflags_cc': [..., '-fno-rtti', ...]`
- darwin（macos/ios）：`target_defaults.conditions['OS=="mac"'].xcode_settings` 里的
  `'GCC_ENABLE_CPP_RTTI': 'NO'`（`target_defaults` 在 common.gypi 行 138，该设置行 714）

**`patch_rtti.py` 曾长期断言"gyp 的 make 生成器忽略 xcode_settings，所以 darwin 上 RTTI 本来
就是开的"，因此对 macos/ios 只做检查、不改任何东西。这个断言是错的。**

gyp 的 make 生成器确实会把 xcode_settings 变成编译参数：
`tools/gyp/pylib/gyp/generator/make.py` 第 33/864/1420 行 → `XcodeSettings.GetCflagsCC()`，而
`tools/gyp/pylib/gyp/xcode_emulation.py:723-724` 就是

```python
if self._Test("GCC_ENABLE_CPP_RTTI", "NO", default="YES"):
    cflags_cc.append("-fno-rtti")
```

实测（macOS node 腿 CI 日志 `deps/v8/src/api/api.cc` 的编译行）：含 `-fno-rtti`，
**整条日志里 `-frtti` 出现 0 次**。后果是 v8 只发出 vtable 不发出 typeinfo：

| 符号 | linux 归档 | macOS 归档（我们的与 moluopro 参考版都一样） |
|---|---|---|
| `_ZTIN…ValueSerializer8DelegateE` / `…ValueDeserializer…` | 有 | **没有** |
| `_ZTVN…`（vtable） | 有 | 有 |

于是下游报 `"typeinfo for v8::ValueSerializer::Delegate", referenced from: typeinfo for
jsb::Serialization::VariantSerializerDelegate`。注意 **moluopro 的参考归档同样缺**——所以这不是
"对齐参考产物"能解决的，符号必须**新增**。

本地已复现并验证修法（`.agent_tmp/rtti_repro2.sh`）：v8 侧 `-fno-rtti` → vtable=1/typeinfo=0 →
下游链接失败且报的正是上面那句；`-frtti` → 两者齐全（typeinfo 可见性为 `V` = weak global）→
链接通过。`patch_rtti.py` 现在把该设置改成 `'YES'`（连同行尾 `# -fno-rtti` 注释一并纠正）。

- **教训**：涉及构建系统的"某生成器忽略某配置"这类断言，必须用**真实编译命令行**或
  **产物符号表**验证；只看文档字符串会原样继承错误结论。

现有补丁脚本与它解决的编译期问题：

| 脚本 | 平台 | 作用 |
|---|---|---|
| `patch_rtti.py` | 全平台 | 打开 RTTI（下游 subclass v8 Delegate 需要 typeinfo）。**darwin 分支不是空操作**，见下 |
| `patch_libuv_console.py` | windows | 去掉 libuv 永不返回的控制台 resize 线程（它会长久 pin 住装载它的 DLL） |
| `patch_pic.py` | linux/android/ohos | 给 POSIX cflags 加 `-fPIC`（**静态**构建默认不加，归档无法链进 .so） |
| `patch_debug_info.py` | macos/ios | 关 `GCC_GENERATE_DEBUGGING_SYMBOLS`（否则 gyp 给每个目标加 `-gdwarf-2`，Release 归档 ~10GB） |
| `patch_no_ltcg.py` | windows | 去掉 `vcbuild.bat` 里 `release` 隐含的 `ltcg=1`（= `--with-ltcg`）。**必须**：带 LTCG 时归档成员是 LLVM bitcode，node.exe 自己能链（lld 认 bitcode），但 MSVC 消费者报 `LNK1107: invalid or corrupt file` —— **只在 embedder 侧炸**，已于本地复现该机制 |

与补丁配套的**断言脚本**（不是打补丁，而是把上游"只 warn"的降级变成硬失败）：

| 脚本 | 平台 | 作用 |
|---|---|---|
| `verify_icu_config.py` | 全 unix | 校验 `config.gypi` 的 small-icu 配置与 locale 集合 |
| `verify_icu_data.py` | 全 unix | 校验 locale 数据真的编进了产物 |
| `verify_openssl_config.py` | 全 unix | 断言 `openssl_version >= 0x3000000f`，否则 node 的 OpenSSL 探针大概率失败（gyp 会静默丢掉 `ncrypto_engine`）。失败时重跑探针并打印 stderr |

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

   **GNU thin 归档的语义与厚归档不同（踩过，2026-09-25）**：gyp 在 unix 侧用
   `ar crsT <out>.a @<out>.a.ar-file-list` 建库，`T` = **thin**，即
   `!<thin>\n` magic。thin 归档的**普通成员不存 payload**，header 里 size 字段是该成员
   **所引用文件**的大小；只有特殊成员（符号索引 `/`、长名表 `//`）才真的带字节。
   按 size 前进会直接越过 EOF，**静默只剩第一个成员**，报
   `thin member ... is missing`（linux / android 两条 node 腿的实际失败原因）。
   正确走法：thin 下只对 `/`、`//`、`/SYM64/`、`/<ECSYMBOLS>/` 取 payload，其余成员
   `stored = 0`；成员字节从 `entry.name` 指向的路径读取。

   **长名引用字段可能是带 padding 的形式**：16 字节名字段里，offset 0 可以写成
   `/0`、`/0       `（右填空格）或 `/0             /`（binutils 2.38 会加结尾 `/`）。
   只认裸 `/N` 会让后两种落到"短名"分支，把**原始字段**当成路径——CI 报出的
   `thin member /0              is missing` 就是这么来的（13 个空格，逐字节可复现）。
   判定方式：取 `raw[1:]` 后 strip 再 rstrip(`/`) 再 strip，然后看是否全数字。

   **BSD/macOS 的连接器成员必须跳过（踩过，2026-09-25）**：macOS 归档里有
   `__.SYMDEF` / `__.SYMDEF SORTED`（每个输入库的 ranlib 符号索引）。它们不是目标文件；
   当普通成员提取出来交给 `libtool`，换来的是 `libtool: warning: not a mach-o` 然后被丢弃
   → 输出成员数**少于**读入数（mac 实测 `3630 members from 37 archives became 3594`，
   少的就是 36 个 SYMDEF），被本脚本自己的忠实性校验当场拒绝。
   GNU 侧的等价物（`/` 索引成员）本来就在跳过列表里。
   **注意 macOS 用 BSD 布局存这个名字**（`#1/<len>`，名字嵌在 payload 里），
   所以过滤必须打在**解析后的名字**上；只看 16 字节 header 字段会静默不生效。

   **`node.target.mk` 里的归档路径写法按 flavor 不同（踩过，2026-09-25）**：
   gyp 把静态库输出路径按 flavor 拼写，并写进**引用方** target 的 `LD_INPUTS`：

   | flavor | 写法 | 实际路径 |
   |---|---|---|
   | linux / android / ohos | `$(obj).target/libX.a` | `<build_out>/obj.target/...` |
   | macos / ios | `$(builddir)/libX.a` | `<build_out>/libX.a`（`PRODUCT_DIR == $(builddir) == out/Release`，与 libtool 落盘位置一致） |

   只认前者会让 mac 腿在**找到 `node.target.mk` 的前提下**报
   `could not derive the archive link set`（本仓实测）。两种都要解析；`$(builddir)` 那条
   在 linux 上天然是空集（所有归档都在 `obj.target/**`，已实测确认），故不产生副作用。

### libnode 必须能在**共享库**里链接（TLS 模型，踩过，2026-09-25）

下游把 `libnode.a` **整档**链进 Godot GDExtension（一个 `.so`/`.dll`/`.dylib`）。v8 的
线程局部变量用编译期宏决定寻址方式，**静态**构建在 linux/macos 上默认选最快的 local-exec，
而 local-exec 的重定位**不能**用于共享库：

```
relocation R_X86_64_TPOFF32 against hidden symbol
  `_ZN2v88internal18g_current_isolate_E' can not be used when making a shared object
```

`deps/v8/src/common/thread-local-storage.h`：

```c
#if defined(COMPONENT_BUILD) || defined(V8_TLS_USED_IN_LIBRARY)
#define V8_TLS_LIBRARY_MODE 1
#endif
#if V8_TLS_LIBRARY_MODE            "local-dynamic"      // 库模式
#else
#if defined(V8_TARGET_OS_WIN)      "initial-exec"
#elif defined(V8_TARGET_OS_ANDROID) "local-dynamic"
#else                              "local-exec"          // ← linux/macos 静态构建
#endif
#endif
```

`g_current_isolate_` / `g_current_local_heap_` 都带
`__attribute__((tls_model(V8_TLS_MODEL)))`。**node 只在 `node_shared=="true"` 时才定义
`V8_TLS_USED_IN_LIBRARY`**（`deps/v8/tools/v8_gypfiles/v8.gyp`），configure.py / node.gyp
都没有独立开关 → 静态 libnode 永远拿不到库模式。`scripts/node/patch_tls.py` 在
common.gypi 的 **target_defaults** 作用域加上该 define（必须在该作用域，才能覆盖全部
v8 target；那两个变量被编进多个不同归档）。

- **判据（不必重编 90 分钟）**：预处理真实头文件
  （`g++ -E -P -std=c++20 -DV8_HAVE_TARGET_OS -DV8_TARGET_OS_LINUX -I deps/v8 -I deps/v8/include -include ...`
  读 `V8_TLS_MODEL`）。**注意要显式给 `-std=c++20`**：`v8config.h` 会
  `#error "C++20 or later required."`，而 linux 腿固定 gcc-12，其默认标准不够。
- `scripts/node/verify_tls_config.py` 就是做这件事（从 common.gypi 读回 define，再预处理
  验证其**效果**——不能只查 define 是否存在，否则检查是同义反复），已接入全部 5 条 unix leg。
- 旁证：上游 moluopro 参考包（同一 node 线）的 linux `api.o` **0 个 TLS 重定位**，
  且参考归档**通过**同一条整档 `.so` 链接测试。

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
这就是三平台打包全坏而依赖 CI 报 success 的原因。现在对 node 追加四项，缺一不可：

- 成员数下限（上游 ~3630；node 自身对象档只有 ~190）；
- 跨库符号覆盖（v8 / libuv / nghttp2 / ICU / OpenSSL / zlib / zstd / cppgc 各取一个标记）；
- **`-shared -Wl,--whole-archive <archive> -Wl,--no-whole-archive` 整档链接**
  （linux 腿）：非 PIC → relocation 报错；混入 host 工具归档 → `multiple definition`；
  丢成员 → 未定义符号。**一条测试同时覆盖三类缺陷**（已实测：上游已知可用归档通过，
  坏归档失败）。注意**不要**加 `-Wl,--no-undefined`——已知可用归档会因缺 `libc++` 运行时
  符号而失败（假阳性）。
- **断言 Delegate 的两个 typeinfo 被真正定义**（`validate_node_delegate_typeinfo`）。
  之前这里写着"typeinfo 在上游就是 hidden、连 moluopro 参考产物也没有，所以改断言
  out-of-line 虚函数"——**那个结论是错的**：typeinfo 是被 `-fno-rtti` **根本没生成**，
  不是因为 visibility。只查虚函数会让一个"下游链不进去"的归档通过，这正是 macOS 腿带着
  缺陷发布、而本脚本报 success 的直接原因（原始设计文档
  `.monkeycode/specs/ci-build-complete/design.md:187` 本来要求的也正是 typeinfo）。

该检查直接解析符号表而不调 `nm`：`nm` 在 Linux runner 上读不了 Mach-O 归档，Apple nm 的
开关也不同；同时要求符号**非 LOCAL 且非 hidden**（LOCAL/hidden 同样满足不了下游引用，
放行它们等于重建同一个盲区）。已验证：linux 归档里这两个 typeinfo 是
**WEAK/OBJECT/default**，macOS 归档里缺失 → 脚本对前者通过、对后者失败。


## 常见坑

- **`configure` 只 WARN 不失败 = 必须自己设断言（踩过，2026-09-25）**。node 的
  `configure.py` 用 C 编译器预处理 `openssl/opensslv.h` 来求 `openssl_version`；编译器
  起不来（哪怕只是缺系统头）它只打印
  `WARNING: Failed to extract OpenSSL macros from headers` 然后**以 version 0 继续**，
  最后照样 `INFO: configure completed successfully`。而 `deps/ncrypto/ncrypto.gyp` 用
  `openssl_version >= 0x3000000f` 决定要不要建 `ncrypto_engine` 目标：version 0 →
  该目标不建 → `engine.cc` 被编进 `ncrypto` 且**丢掉**
  `NCRYPTO_ENGINE_COMPAT=1` / `OPENSSL_API_COMPAT=30000` / `OPENSSL_SUPPRESS_DEPRECATED`
  → 56 分钟后炸出 12 个 `use of undeclared identifier 'ENGINE_*'`。
  `scripts/node/verify_openssl_config.py` 把这个降级配置变成**硬失败**，并在失败时**重跑
  探针把 stderr 打出来**（否则只有一句 warning，诊断成本极高）；五条 unix node 腿都已接入。
  推而广之：**上游 configure/生成步骤里"只 warn"的分支，都要在本仓加显式断言**，
  否则失败会在几十分钟后以完全无关的形式出现。

- **macOS 上 ccache 包住的编译器必须仍是 xcrun shim（踩过，2026-09-25）**。
  `export CC="ccache $(xcrun --find clang)"` 会把编译器固定成
  `/Applications/Xcode_*.app/.../usr/bin/clang` 这个**绝对路径**，它绕过 xcrun 的 SDK
  解析，于是 **clang 找不到系统头**：`stdlib.h file not found`。表面症状是上面的 openssl
  探针失败 → ENGINE_* 未定义，和 ccache 看起来毫无关系。
  正确写法是 `export CC="ccache cc"` / `CXX="ccache c++"`（保留 shim，也正好与引入 ccache
  之前 gyp 记录的编译器一致）。**判断依据**：探针报 `stdlib.h` 之类的**系统**头缺失时，
  先怀疑编译器身份，而不是 OpenSSL 自己。

- **`build.ninja` 里没有编译规则（踩过，2026-09-25）**：gn 把 `rule cc` / `rule cxx`
  （`cc_wrapper` 生效处）写进 **`toolchain.ninja`**，`build.ninja` 只有目标图。
  用 `grep ccache build.ninja` 判断"ccache 没接上"会在**每条 unix 腿 48 分钟后**误报失败，
  而构建本身是好的。要做这类断言，就 `gn gen` 之后遍历**所有** `*.ninja`
  （`find ... -name '*.ninja' -exec grep -l ccache {} +`，注意 macOS 是 BSD grep，
  **没有** `--include`），并且放在**编译之前**——失败应该几秒内发生，不是一小时后。

- **改了依赖仓库 ≠ 本仓生效**：本仓 `third/` 下是已下载的产物，`dependency_is_ready()`
  命中就跳过下载。换产物要么删掉对应 `third/<name>/` 让下次构建重下，要么手动替换。

- **libnode 用 gcc-12 编译，消费方也必须用 gcc-12（踩过，2026-09-25）**。
  `scripts/node/build-linux.sh` 装并导出 `CC=gcc-12 CXX=g++-12`。归档里
  `print.o`（v8 的 SIMD 库 **Highway**，`hwy::detail::PrintArray/ToString/TypeName`）
  用到 `_Float16`，其 `_Float16 → double` 转换辅助函数 **`__extendhfdf2` 是 GCC 12 才加入
  libgcc 的**。而本仓 linux node 腿原先用 ubuntu-22.04 自带的 `g++`（= gcc-11）链接，
  SCons 又加 `-static-libgcc`（`third/godot-cpp/tools/linux.py`：`use_static_cpp`），
  于是静态链进 gcc-11 的 `libgcc.a` → `undefined reference to '__extendhfdf2'`。

  实测（`.agent_tmp/libgcc_probe3.sh`，直接解包 jammy 的 deb）：
  gcc-11 的 `libgcc.a` 里名为 `extendhfdf2` 的成员 **0 个**，gcc-12 的 **1 个**；
  用归档里真实的 `print.o` 对两者做 `ld -shared -Wl,--no-undefined` 链接：
  gcc-11 报出**与 CI 完全相同**的两条 `undefined reference to '__extendhfdf2'`，
  gcc-12 干净通过。**moluopro 的参考 linux 归档同样只有 `print.o` 引用它**，
  所以这不是本仓引入的回归。

  修法（方向 B）：`setup-godotjs-ext` 为 `linux + x86_64 + node` 装 `gcc-12/g++-12`
  并**预检** `g++-12 -print-libgcc-file-name` 里有 `__extendhfdf2`；
  `scons-build` 对该腿追加 `CC=gcc-12 CXX=g++-12`（与 arm64 传交叉编译器的写法一致，
  `SConstruct:45` 会把这些 ARGUMENTS 覆盖进 env）。
  另注：发布的 node 归档只有 `linux/x86_64`，**没有** linux arm64 产物，
  所以只有 x86_64 腿受此影响。

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
| node unix | `build-{linux,macos}.sh` 把 `CC`/`CXX` 前缀为 `ccache <compiler>`（gyp 的 make 生成器只认 `CC`/`CXX`；在 action 里注入无效，会被脚本后面的 `export CC=gcc-12` 冲掉）。**macOS 必须写 `ccache cc` / `ccache c++`（shim），不能写 `ccache $(xcrun --find clang)`**，理由见「常见坑」 |
| node windows | **未接**：node 的 `--use-ccache-win` 把 `/p:CLToolPath=<dir>` 交给 MSBuild，MSBuild 会往该目录追加 `clang-cl.exe`，所以 ccache 必须以 `argv[0]` 伪装成编译器，而 choco 只装了 `ccache.exe`。接它需要独立验证，未验证前保持关闭 |
| v8 unix | gn 的 `cc_wrapper = "ccache"`（写进 `args.gn`）；断言方式见「常见坑」的 `toolchain.ninja` 一条 |
| v8 windows | **未接**：MSVC/ClangCL 工具链下 `cc_wrapper` 是否生效**尚未验证**（曾经"验证过无效"的结论来自一个 grep 错文件的检查，已作废） |

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

