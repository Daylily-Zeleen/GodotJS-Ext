# 修复 Orphan StringName（v8 2 个 + node 99/102）— 排查报告

> 任务：`.trellis/tasks/09-22-fix-orphan-stringname-v8-node/`
> 进度文件（AGENTS.md 要求：进度落文件，不落对话）

## 1. 已确认机制（引擎侧，只推导一次，不重复查）

`StringName::cleanup()`（`D:/Dev/godot/godot/core/string/string_name.cpp:79-125`，由
`unregister_core_types()` 在 `register_core_types.cpp:496` 调用）对表里每个 `_Data` 判定：

```cpp
if (d->static_count.get() != d->refcount.get()) { lost_strings++; print "Orphan StringName: ..." }
```

- `static_count` 只由 `StringName(ptr, p_static=true)`（宏 `SNAME`）递增。
- godot-cpp 的 `get_class_static()` 形如
  `static const StringName string_name = ::godot::StringName(U## #m_class);`（`third/godot-cpp/include/godot_cpp/classes/wrapped.hpp:273-277`）
  → **`p_static=false`**，`static_count == 0`，`refcount == 1`（函数局部静态持有）。
- 因此：**只要 {该函数局部静态所属的 DLL 在 `StringName::cleanup()` 时仍被加载}，该类的
  名字就必然被判为 orphan。** 与类是否被 JS 使用无关，与是否注册无关（每个被
  `GDEXTENSION_CLASS`/`GDCLASS` 包装并实例化过的类都会 `get_class_static()` 一次）。
- 释放唯一途径：DLL 卸载（或进程退出）。

这条模型解释了全部观测：
- orphan 名单 = 「被 godot-cpp 包装的类」∩（build_profile / omit 规则过滤），`static: 0` 恒成立。
- `static=0` 时 `total=4`（`Object`/`Thread`=5）之类计数 = 各静态容器（`ClassDB::classes`、
  `instance_binding_callbacks`、`class_register_order`、`engine_singletons`）键的额外引用。

## 2. 决定性实验（本轮新增，A/B 对照，可复现）

探针：`src/internal/jsb_dll_probe.cpp` + `.h`（**临时诊断，任务结束删除**）。写
`.agent_tmp/dllprobe.log`，并接 `jsb_shutdown()`（`src/runtime/register_types.cpp:112`）。
DllMain 的 `DLL_PROCESS_DETACH` 的 `lpvReserved` 直接给出「是谁卸载了我」：

| 腿 | 探针输出 | Orphan |
|---|---|---|
| **v8**（minproj） | `DETACH(FreeLibrary: DLL released before StringName::cleanup)` | **0** |
| **node**（minproj） | `DETACH(process-exit: DLL outlived StringName::cleanup)` | **99** |

日志：`.agent_tmp/p_C.log`（v8）、`.agent_tmp/p_D.log`（node）、`.agent_tmp/dllprobe.log`。

**根因（node 腿）**：node 构建的两个扩展 DLL（runtime + editor）都没能在
`StringName::cleanup()` 之前被卸载 → godot-cpp 里 per-class 的 `get_class_static()`
静态 `StringName` 全部仍存活 → 逐个被判 orphan。

即：**v8 腿靠 `~GDExtensionManager`（`unregister_core_types()` 中 `memdelete(gdextension_manager)`
→ `~GDExtension` → `close_library()` → `FreeLibrary`）卸载成功；node 腿同一个引擎路径没能把
DLL 的加载计数减到 0。**

### 已排除（本轮实测，不要重查）

| 假设 | 实测 | 结论 |
|---|---|---|
| `node.dll` shim（`PrepareNativeAddonHost` 的 `LoadLibraryW`，从不 FreeLibrary）钉住主 DLL | 移走 `minproj/.../bin/windows/node.dll`（绝对路径 → LoadLibraryW 必失败）后 orphan 仍 **99**（`.agent_tmp/e_B.log`） | **不是** node.dll |
| gdextension `[dependencies] node.dll` 段导致 | 删掉该段后 orphan 仍 **99**，DETACH 仍为 process-exit（`.agent_tmp/p_E.log`） | **不是** |
| 载荷差异（迷你工程 vs 全量 `project/`） | minproj 99 vs `project/` 102，差 `health/attack/name` | 无关 |

## 3. 当前未决 + 下一步

未决：**node 腿的 DLL 加载计数为何减不到 0**。已用 PEB 读 `LDR_DATA_TABLE_ENTRY.LoadCount`
（三个 node 侧模块都报 `loadCount=6`）——但该字段在 Win10+ 已废弃、取值不可信（同一进程内
三个模块数值完全相同即为佐证），故不作为结论。

下一步（按顺序，每次只换一个变量）：
1. 用**独立探针 DLL + 宿主**（`.agent_tmp/probe/probe_dll.cpp` / `probe_host.cpp`）在
   **无 Godot** 的环境下加载 libnode、跑与 `GlobalInitialize::init/shutdown` 相同的调用序列，
   再 `FreeLibrary`，看模块是否消失 → 判定「libnode 是否钉住其所在模块」。
2. 若钉住：定位到具体子系统（`InitializeOncePerProcess` / `MultiIsolatePlatform` /
   `v8::V8::Initialize` / `node::NewIsolate`+`Environment`）。
3. 若可解：在 node 侧不再产生该额外引用；若不可解（libnode 内部），则改为在
   `StringName::cleanup()` 之前显式释放 godot-cpp 的那些静态持有者（需要 DLL 内可访问的清理点，
   例如在 `jsb_uninitialize_module` 中清空 `ClassDB::instance_binding_callbacks` 等容器），
   并评估 `get_class_static()` 函数局部静态这一层是否仍会残留。

## 4. 环境/复现速查

```
# 构建（增量）
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_node=yes tests=yes -j17
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_v8=yes tests=yes -j17

# 三处部署位同时换 dll（node 腿还要 node.dll）
R=D:/Dev/godot/GodotJS-Ext
for D in "$R/bin/windows" "$R/project/addons/godotjs-ext.daylily-zeleen/bin/windows" \
         "$R/.agent_tmp/minproj/addons/godotjs-ext.daylily-zeleen/bin/windows"; do
  cp "$R/bin/windows/godotjs-ext.windows.editor.x86_64.dll" \
     "$R/bin/windows/godotjs-ext-editor.windows.editor.x86_64.dll" "$D/"
done

# 最小复现（约 1.5s）
cd .agent_tmp/minproj && "D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.exe" \
  --audio-driver Dummy --headless --path . --quit --verbose
grep -c "Orphan StringName" <log>
```

- 部署位 md5（本轮构建，含临时探针，**任务结束需重建为干净产物**）：
  node runtime `234b659619b3c73992fe1d9963581519`、v8 runtime `3aae918d69fee28fabcc8bbeefed4b30`。
- 需还原的临时改动：`src/runtime/register_types.cpp:28,112`、`src/internal/jsb_dll_probe.*`。

## 5. 决定性实验（本轮）：脱离 Godot 的裸宿主 `FreeLibrary` 计数

**方法**：`.agent_tmp/freeloop.ps1` —— 在纯 PowerShell 宿主里对目标 DLL 做一次
`LoadLibraryExW(path, LOAD_WITH_ALTERED_SEARCH_PATH)`，然后反复 `FreeLibrary` 直到
`GetModuleHandleW(name) == 0`。**全过程没有 Godot、没有 JSB 代码运行**（只有 DLL 自身
的加载与 CRT/静态构造函数）。

| 镜像 | 单独放在空目录里，一次 Load 需要几次 FreeLibrary |
|---|---|
| node runtime（当前构建） | **3** |
| node editor（当前构建） | **3** |
| **含探针** 的 node runtime | **3** |
| **不含探针**（`.agent_tmp/dll/node/` 备份，md5 `24e9c764…`） | **3** |
| v8 runtime（当前备份，md5 `5e4d5f3c…`） | **1** |
| v8 editor | **1** |

**排除项**：
- **不是探针造成的**：不含探针的备份镜像同样是 3。
- **不是目录内容造成的**：把镜像单独放到只有它一个文件的空目录，结果不变；把 v8 镜像
  放到含 `node.dll` 的目录里，仍然是 1。
- 因此 surplus 完全是 **node 镜像自身的属性**（v8 镜像无此问题）。

**探针定位窗口**（同一次裸宿主加载的 `.agent_tmp/dllprobe.log`）：
- `STATIC-INIT`（本 DLL 第一个 C++ 静态构造函数内）：`EXT-RUNTIME=1`
- `PROCESS-ATTACH`（`DllMain` 体内，静态构造已跑完）：`EXT-RUNTIME=3`

→ 那 **2 个 surplus 引用是在本 DLL 自己的加载过程中取得的**，发生在
「C++ 静态构造函数 → DllMain」之间。这解释了 v8 腿为何是 0：v8 镜像加载后计数为 1，
引擎唯一那次 `FreeLibrary` 正好归零 → DLL 卸载 → godot-cpp 每类静态 `StringName` 析构。

**node 镜像与 v8 镜像的唯一实质差异**：node 用 `/WHOLEARCHIVE:libnode.lib` 静态链入
整个 libnode（376 MB / 631484 符号，见 `SConstruct:749`），v8 链 v8 monolith。
surplus 的调用者位于 libnode 的静态初始化中。

## 6. 两个缺陷（相互独立）

- **缺陷 A（两腿共有，各 2 个）**：`health`、`attack` —— `StringName` 本身。
- **缺陷 B（仅 node，~80–100 个）**：扩展 DLL 卸载不掉，godot-cpp 的
  `get_class_static()` 函数局部静态全部活到 `StringName::cleanup()`。
  修 A 不影响 B；B 消失则 A 仍存在（v8 腿即证明）。

## 7. 已废方法（本轮新增，勿重试）

- **在 `.CRT$XIB` 里装 loader 挂钩**：`_initterm_e` 之前的 CRT 初始化阶段重入 loader，
  稳定触发 `ERROR_DLL_INIT_FAILED (1114)`；把槽函数改成返回 `int` 并 `return 0` 也无效
  （仍 1114）。**结论：本进程内无法从 DLL 内部挂钩 loader API**（已烧 5 次构建，彻底放弃）。
- `dump_surplus_modules` 显示大量系统 DLL（`ucrtbase`=5、`combase`=11、`rsaenh`=32）
  计数 > 1 —— 属系统常态，不可作为「surplus = 缺陷」的判据；只有与 v8 镜像的**差值**可信。

## 8. 本轮新增实测（引擎内）

在含「无挂钩探针」的 node 构建上跑 minproj（`.agent_tmp/p_N3.log`）：`exit=0`、`startup=1`、
`dllerr=0`、`orphans=99`。探针输出：

- `PROCESS-ATTACH` 即 `EXT-RUNTIME=3` / `EXT-EDITOR=3` → **+2 在各自加载完成前就已取得**。
- 引擎中最终 `EXT-RUNTIME=8`，其中 **5 个是 CRT 线程 pin**
  （`THREADS: 39 live, 5 with a start routine inside godotjs-ext*/node.dll`），
  在 node shutdown 时释放；引擎唯一那次 `FreeLibrary` 再减 1 → **剩 2，永不归零**。
- `IMPORTERS: 0` —— 不是静态导入钉住。

**→ S1 的目标就是这 2 个加载期引用。**

## 9. 缺陷 A 已修复（实测证据）

**改动**

1. `src/runtime/weaver/jsb_script_instance.h`：新增
   `static void GodotJSScriptInstanceBase::free_temporary_property_list_pool();`
2. `src/runtime/weaver/jsb_script_instance.cpp`：实现之——`temporary_property_list.exchange(nullptr)`
   后 `memdelete` 取出的 `PropertyList`（其 `LocalVector<PropertyInfo>` 析构会释放
   `PropertyInfo::name` 的 `StringName`）。
3. `src/runtime/weaver/jsb_script_language.cpp`：在 `GodotJSScriptLanguage::_finish()`
   （`environment_->dispose()` 之后）调用之。该点经 `ScriptServer::finish_languages()`
   （`main.cpp:5255`）执行，**早于** `unregister_core_types()`（`main.cpp:5363`）。

**A/B 对照（同一工程 `project/`，同一命令，仅换 dll）**

| 部署的 v8 dll | Orphan StringName | 内容 |
|---|---|---|
| **修复前**（`.agent_tmp/dll/v8/`，md5 `5e4d5f3c…`） | **2** | `health (static: 0, total: 1)`、`attack (static: 0, total: 1)` |
| **修复后**（md5 `329c1311…`） | **0** | — |

两次运行均 `exit=0`、`GODOTJS_TEST_PROJECT_COMPLETED`、`GODOTJS_TEST_PROJECT_FAILED` 计数 0。
日志：`.agent_tmp/v8_prefix.log`（2 个）、`.agent_tmp/v8_fixA.log`（0 个）。

**探针已删除**（`src/internal/jsb_dll_probe.*`），`src/runtime/register_types.cpp` 已还原
（`grep -rn "jsb_dll_probe\|jsb_probe_" src/ SConstruct` 无输出）。

## 10. 阶段状态

- 计划产物已就绪：`prd.md`（含 Technical Notes）、`design.md`（§3 已锁 S1）、`implement.md`、
  `implement.jsonl` / `check.jsonl`（已录入 spec 条目）。
- **决策已定：S1**（消除 node 镜像加载期那 2 个多余引用）。
- 待用户批准最新 planning summary 后方可 `task.py start` 并动产品代码。
- 当前工作区含临时诊断（`src/internal/jsb_dll_probe.*` + `src/runtime/register_types.cpp` 调用点），
  已列为 implement.md 阶段 0 的清理项。

## 11. 缺陷 B 根因定案：S1 前提被实测否定（本轮重大发现）

**根因（精确到调用点与源码）**：node 腿那 2 个永久引用来自 **libuv 的 `uv__console_init()`**
（`src/win/tty.c:190-193`，经 `core.c:211-212` 的 `uv__init()` ← `uv__once_init()` ←
`uv_once(&uv_init_guard_, uv__init)`）。它执行两条 **`QueueUserWorkItem(..., WT_EXECUTELONGFUNCTION)`**：

1. `uv__console_init` → `QueueUserWorkItem(uv__tty_console_resize_message_loop_thread, ...)`（tty.c:190）
2. 该线程内 → `QueueUserWorkItem(uv__tty_console_resize_watcher_thread, ...)`（tty.c:2358），watcher 是 `for(;;) Sleep(33); WaitForSingleObject(...)` **永不完结**

**机制（tiny.dll/host3 单独验证）**：ntdll `RtlQueueWorkItem` 对**回调所在模块**执行
`LdrAddRefDll`，**引用随回调执行期存续**——快速返回的回调释放、永不返回的回调永久占用。
实测：`qwi_quick`（返回）loadCount 1→2→1；`qwi_long`（永不返回）1→2 且稳定，需 2 次 FreeLibrary。

**与 node DLL 完全吻合**：1（LoadLibrary）+ 2（永久）= 3 次 FreeLibrary；`uv_library_shutdown()`
不释放（host4 实测 count 恒 3）；v8 无 libnode → 无 libuv → 初始即 1。

**时序**：`uv__console_init` 在**我们 DLL 的 CRT 静态初始化期**（`_initterm`）即被 libnode 的
静态构造触发，先于任何 JSB 代码，无法从我们侧阻止。

**结论：S1（"消除 2 个多余引用"）不成立**——那 2 个引用是 libuv 为永不返回的控制台线程
按设计持有的模块 pin，且回调代码就在我们的映像内，进程存续期间**不可能**让它们归零。
node 扩展 DLL 在进程内合法地无法卸载 ⇒ godot-cpp 类名函数局部静态（`p_static=false`）在
`StringName::cleanup()` 时仍存活 ⇒ 必判 orphan。这不是泄漏，是"模块驻留+非静态类名"的系统性结果。

**遗留决策（需用户定）**：见附言——S2（改 godot-cpp 头 2 行）作为正统修复重新浮出，但触碰
已锁定子模块；是否有替代，需权衡。详见对话当前消息。

## 12. 同行对照调研：godothub/gode（用户指定，2026-09-23）

结论先置顶：**gode 在 node 环境处理上与我们在原理上无差别，Windows 上同样会出现本 orphan，
只是它从未在 Windows 上跑过集成测试（未检出）。**

证据（`.agent_tmp/gode/`，系统代理拉取 tarball）：
- **同 libnode 同版本**：`.github/workflows/test.yml` `LIBNODE_URL=
  https://github.com/moluopro/libnode/releases/download/24.18.0/libnode.zip` —— 与我们
  `SConstruct:333` 完全一致（moluopro/libnode, 24.18.0）。
- **同链接方式**：`CMakeLists.txt:210` Windows 用 `/WHOLEARCHIVE:libnode.lib`，
  非 Windows `-force_load` / `--whole-archive` —— 与我们 `/WHOLEARCHIVE` 相同。
- **同 godot-cpp 上游**：`.gitmodules` 指向 godotengine/godot-cpp（官方，未 fork），
  `get_class_static()` 同样是函数局部非静态 `p_static=false` StringName。
- **同生命周期**：`NodeRuntime::init_once()` 用 `node::NewIsolate` / `CreateEnvironment` /
  `uv_run(uv_default_loop…)`；`shutdown()` 只做 `node::Stop/FreeEnvironment` +
  `v8::V8::Dispose/DisposePlatform` —— **无任何 DLL 卸载/FreeLibrary/LdrUnload 处理**
  （`grep FreeLibrary|LdrUnload|unload` 于 src/ 为空）。
- **gode 的 Windows 差距**：`build.yml` 只构建 `libgode_runtime.dll`+`node.dll`（windows-2022），
  `test.yml` 的 smoke **只在 ubuntu-22.04 跑** —— Windows 上从未有集成测试/Orphan 检查。
  Linux 无 libuv `src/win/tty.c` 的 console 线程 pin 问题 → gode 在 Linux 天然看不到本 orphan。

推论：gode 只要在 Windows 加载它那个 WHOLEARCHIVE:libnode.lib 的 DLL，`uv__console_init`
照样排队两条永不返回的线程池回调 → pin 住该 DLL → 所有被触发的 godot-cpp 类名 StringName
在 cleanup 时全部 orphan。它注册的绑定类比我们更多（generator 全量引擎类），orphan 只会更多。
**同源实现 gode 也没有第三条路 → 佐证 S2（改 get_class_static 为 p_static=true 且永不析构）
是唯一同时满足两腿 AC2/AC3 的正统修法。**

## 2026-09-23 阶段1：本地 node 构建验证（路径2 落地）

**用户拍板四步大任务**：①本地 clone node 开发验证 → ②成功后在依赖仓库主分支提交推送 patch、CI 通过 → ③触发依赖仓库一次发布 → ④开新开发分支迁移依赖源指向依赖仓库，本地跑通全部测试（C++ 单测/TS 集成/基准），提交推送、CI 通过。

- 工具链本地核实：MSVC cl.exe 在 `D:\App\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231`（不在默认 C 盘，首次搜索漏）；clang-cl 22.1.8；nasn（WinLibs mingw64，不在 PATH）；git；python 3.14.7 **受 node 24.18 支持**（BUILDING.md 明列 `Python 3.14`；configure 冒烟通过 python 门，仅卡 nasm PATH）。D 盘剩 131G。**零新增工具即可本地构建 node**。
- node v24.18.0 已 shallow clone 到 `.agent_tmp/node-src`（commit 20da4aea）。
- 补丁脚本 `.agent_tmp/patch_libuv_console.py`（fail-closed：锚点必须恰出现 1 次），已应用到 node-src（`deps/uv/src/win/tty.c` 的 `uv__console_init` 里 `QueueUserWorkItem(uv__tty_console_resize_message_loop_thread,...)` 注释掉；因 watcher 线程只在该 message-loop 线程内排队，一条注释即消除两个 pin）。已验证 anchors 齐全（134/142/191/2363/2390/2421 行）。
- 未动错锚点：line-read 线程的 QWI（628-633，真实 TTY 读写要用）保留。

### 阶段1 构建障碍（2026-09-23，已全部绕过）

- **VS 定位**：node ≥24 强制 ClangCL（`vcbuild.bat:285` `ClangCL is required`)。
  vswhere 组件过滤（VC.Tools.x86.x64 / VC.Llvm.Clang / VC.Llvm.ClangToolset）在本机 VS18
  实例一个都查不到 → wrapper 报 "Failed to find suitable VS with Clang toolset" / MSB8020。
  但 **ClangCL 工具集实际存在**（`MSBuild\...\v180\Platforms\x64\PlatformToolsets\ClangCL\
  Toolset.props`+`.targets`，ARM64/Win32/ARM64EC 亦在；LLVM 二进制 `VC\Tools\Llvm\x64\bin\
  clang-cl.exe` v22.1.3）。MSB8020 只是 vswhere 过滤路径的假象。
- **绕过**：`_build_node.bat` 先 `vcvars64.bat`（D 盘 VS18），vcbuild 传 `vs2026` 参数 →
  不清 VCINSTALLDIR、VisualStudioVersion=18.0 直接 `goto found_vs2026`，跳过 vswhere。
  configure 全过（`--clang-cl=22.1.3`），MSBuild 编译 node.vcxproj 已证实 `_ToolsetFound=true`、
  ClangCL 命中生效。
- **注意**：powershell.exe 才驱动 bat（`cmd //c` 被 MSYS 破坏只剩 banner）；cwd/redirect 路径要写绝对路径；vcbuild 在 vcvars64 环境下 conf 残留 config.gypi 要清理。
- **教训**：本地全量构建不是"装一堆工具"——工具链全在本机（MSVC D 盘、clang-cl、nasm、python3.14 被 node24 官方支持），卡住的只是 VS 发现路径，已绕过。

### 关键发现（2026-09-23 18:5x）：依赖仓库 build-windows.ps1 的 ltcg 缺陷

- 链条：`SConstruct:749 /WHOLEARCHIVE:libnode.lib` + MSVC `link.exe`（/WX /NATVIS）链接。
  moluopro 下发的 libnode.lib 是**纯 COFF**（MSVC 能链，工作树一直如此）。
- 依赖仓库 `build-windows.ps1` 调 `vcbuild.bat ... release ...`，而 `vcbuild.bat:86`
  release → `ltcg=1` → configure `--with-ltcg`（ThinLTO scoped to libnode）。产物
  libnode.lib 含 **LLVM bitcode 对象**，只能被 `lld-link` 消费。
- 我用 patch 版构建的 libnode.lib（457MB）替换后 → MSVC 链接 `LNK1136: 无效或损坏的
  文件` + 全部 node::/uv:: 符号未解析。llvm-ar/dumpbin 确认：新 lib 是 clang-cl bitcode。
- **结论**：依赖仓库的新 build-windows.ps1 产出的 libnode.lib 无法被 Godot 的 MSVC 链
  （可能是用户怀疑的 AI 引入 bug）。本地验证需**解除 release→ltcg 硬绑**重构建 COFF 版。
- 本地已在 node-src/vcbuild.bat:86 解除（`set config=Release&set cctest=1`，去掉 `ltcg=1`），
  该编辑仅本地验证用、不入库。COFF 版 libnode.lib 重构建中。

### 决定性突破（2026-09-23 22:1x）：torque /Od 绕过 clang22 codegen bug，完整构建成功

- **根因**：clang22 `/O2` 编译 V8 torque（STL 密集）触发 `0xC000001D`（非法指令）——编译
  器 codegen bug，非 OOM（12GB 空闲仍崩、固定模板实例化点崩）。standalone torch_base 降
  `/Od` 编过（RC=0，产 30+ obj + torque_base.lib）证死。
- **解法**：torque_base.vcxproj + torque.vcxproj 改 `<Optimization>Disabled</Optimization>` +
  vcbuild 加 `noprojgen`（防 re-projgen 覆盖 /Od）。完整 `vcbuild x64 release small-icu
  vs2026 no-cctest noprojgen` → **PWSH_RC=0**，0xC000001D=0。
- **产物**：v8 库全部纯 COFF 产出（v8_base_without_compiler 1.35GB / v8_compiler 583MB /
  v8_initializers 682MB / ...），libnode.lib 377MB 纯 COFF。
- **坐实依赖仓库 Windows 缺陷**：Linux 有 `merge_libnode.py` 合并 v8_*.a 进 libnode.a
  （moluopro libnode 含 1597 v8 obj），但 build-windows.ps1 只 `Get-ChildItem -Filter
  libnode*.lib | Select -First 1` 拿官方未合并 libnode.lib（仅 3 个 v8 obj）→ Windows 产物
  缺 v8，MSVC /WHOLEARCHIVE 链接必 LNK2019。这就是"只是编译通过、没保障有效"的实证。
- **下一步**：用 llvm-lib 合并全部库（libnode + v8_* + libuv + openssl + icu* + 其余）为
  自包含 libnode.lib → 替换 third/libnode → scons 重链 node 腿 → 实测 orphan=0。

### 链接成功（2026-09-23 22:3x）：自包含 libnode.lib 重链 node 腿 SCONS_RC=0

- 两次合并集修复后链接通过：
  1. 排除 icutools（ztrans.obj 与 icui18n 重复 → 首轮 LNK2005）；
  2. 排除 v8_init（setup-isolate-full.obj 与 v8_snapshot 的 setup-isolate-deserialize.obj
     重复定义 SetupIsolateDelegate::SetupHeap/SetupBuiltins → LNK2005）；
  3. 显式加入生成 obj `node_snapshot.obj`（定义待链接的 GetEmbeddedSnapshotData）。
- 合并集 = moluopro 已知好库（36 lib，dumpbin 对照其顶目录分布）+ node_snapshot.obj，
  排除 gtest/torque_base/cctest。产物 3.19GB 纯 COFF、1598 个 v8 obj（moluopro 为 1597）。
- 产物 md5：merge 失败中间态均已备份；最终 `libnode_self.lib` md5=77bed8f/a998ff49；
  部署 third/libnode 后 md5=a998ff49462e54ac6d63acc3c84a4d16。
- node 腿重链 SCONS_RC=0：`godotjs-ext.windows.editor.x86_64.dll` 120MB、
  `godotjs-ext-editor.windows.editor.x86_64.dll` 104MB。
- 该 libnode 由完整构建（torque /Od 绕过 clang22 bug）产出，src 中 tty.c 已含 console
  pin 消除补丁 → 期望 node DLL 可卸载、orphan=0。下一步：部署两处 + 实测验收。

### ★ node 腿验收通过（2026-09-23 22:5x）—— 决定性成果

自包含 libnode.lib（含 tty.c console-pin 消除补丁，由本地完整构建产出）重链 node 腿后实测：

```
godot --audio-driver Dummy --headless --path project --verbose
Orphan StringName = 0      (期望 0)  ✓
GODOTJS_TEST_PROJECT_COMPLETED = 1   ✓
GODOTJS_TEST_PROJECT_FAILED   = 0    ✓
exit = 0                            ✓
ERROR 计数   = 0                     ✓ (AC3)
WARNING 计数 = 0                     ✓ (AC3)
result: PASS - all checks passed     ✓
```

- node 腿 dll（含补丁）：editor 120MB / runtime 104MB，两处部署位 md5 一致
  （`38b962210efa4197e8de787b93c4bdf2` / `77dca83a0e0c5f482aa4df08ad2fa9a4`）。
- 机制闭环：本地完整 node 构建（torque /Od 绕过 clang22 codegen bug）→ 自包含 libnode.lib
  （合并 moluopro 已知好集 + node_snapshot.obj）→ 链接 node 腿 → tty.c 补丁消除 2 个
  console-resize pin → DLL 可卸载 → 类名 StringName 于 cleanup 前释放 → Orphan=0。
- 待办：v8 腿回归（AC1/两腿同标准）；spec 更新（AC4）；清理。

### 现状提示
- 当前 third/libnode/windows/x64/libnode.lib = **自包含补丁版**（md5 a998ff49，3.19GB）。
- 已备份：moluopro 原版 `.agent_tmp/libnode_backup/libnode.lib.moluopro`；各 merge 中间态
  `.libnode.lib.merged/.merged_prev/.cur/.moluopro_restored`。
- bin/windows 现为 node 腿；v8 腿需重新 scons use_v8=yes 构建。

### ★ v8 腿回归通过（2026-09-23 23:0x）—— AC1 + 两腿同标准

v8 腿重编（scons use_v8=yes）部署后实测（同一判据）：

```
Orphan StringName = 0   ✓ (期望 0)
COMPLETED = 1           ✓
FAILED   = 0            ✓
exit     = 0            ✓
ERROR 计数 = 0           ✓
WARNING 计数 = 0         ✓
```

- 两腿最终:dll 身份 md5 —— v8 腿 `373ee7f0`(runtime)/`2b3a9196`(editor);
  node 腿 `38b96221`(runtime)/`77dca83a`(editor)。均两处部署位一致。
- **结论：AC1/AC2/AC3 全部达标。** 缺陷 A(temporary_property_list 池)+ 缺陷 B(node 缺 2
  pin)修复后,两腿 Orphan StringName 均 = 0,无新增 ERROR/WARNING。

### 待办
- AC4: 更新 spec test/index.md 两腿统一判据 + 自查命令 + node 不豁免 + 教训。
- AC5: 两腿 C++ doctest(--jsb-run-tests)。
- 清理: 临时资产、决定 third/libnode 去向(自包含补丁版 or moluopro 备份)、补丁脚本入库。

### ★ C++ doctest 通过（v8 腿，AC5）

`godot --headless --path project --jsb-run-tests`：
```
[doctest] test cases: 3   | 3   passed | 0 failed
[doctest] assertions: 12  | 12  passed
[doctest] Status: SUCCESS!
[doctest] test cases: 50  | 50  passed | 0 failed
[doctest] assertions: 581 | 581 passed
Orphan StringName = 0, ERROR = 0, exit = 0
```

两腿完整 TS 套件 + C++ doctest 均无回归（AC1/AC2/AC3/AC5 达标）。

## 收尾状态（2026-09-23）

### 最终验收（全部实测，两腿分别构建 + 双处 md5 核验）

| 腿 | dll md5 (runtime/editor) | TS 套件 | C++ doctest |
|---|---|---|---|
| v8  | 373ee7f0 / 2b3a9196 | Orphan=0, COMPLETED=1, FAILED=0, ERROR=0, WARNING=0, exit 0 | 3+50 cases 全过 |
| node | 8543872f / d336577b | Orphan=0, COMPLETED=1, FAILED=0, ERROR=0, WARNING=0, exit 0 | 3+49 cases 全过 |

- AC1 ✓ AC2 ✓ AC3(无新增 ERROR/WARNING) ✓ AC5(TS 套件+doctest 无回归) ✓
- AC4 ✓ spec test/index.md 已改两腿统一标准（node 不豁免 + 硬验收 + 移除过时"已知遗留"）

### 部署与第三方状态
- 部署位 `project/addons/godotjs-ext.daylily-zeleen/bin/windows/`（当前=node 腿），与
  `bin/windows/` md5 两处一致。
- `third/libnode/windows/x64/libnode.lib` = 自包含补丁版（md5 a998ff49，3.19GB，含 tty.c
  console-pin 补丁，纯 COFF + 1598 v8 obj）。
- 备份在 `.agent_tmp/libnode_backup/`：moluopro 原版（376MB）+ merge 各中间态。

### 未完成/需用户决策
- **工作树未 commit**（AGENTS.md 逐轮授权——本次授权仅"修复+验证"，无提交授权）。
- spec 修订：`.trellis/spec/godotjs-ext/test/index.md` 已改；`SConstruct` 未改（依赖源仍是
  moluopro，未切 Daylily-Zeleen/GodotJS-Dependencies——那是后续独立任务）。
- 纯净度：`bin/windows/` 现为 node 腿；若需 v8 腿跑 `scons use_v8=yes` 重编即可。
- 遗留备份/中间产物在 `.agent_tmp/`：node_v24_18_0.tar.gz(117MB)、node-v8 构建 out/ 等，
  用户可决定是否清理。

## 交付（2026-09-23 收尾）

### 已提交并推送（用户授权）
- `49c0722 fix: release temporary property list pool to avoid orphan StringName`
  —— 缺陷 A：`src/runtime/weaver/`（jsb_script_instance.{h,cpp} + jsb_script_language.cpp）。
- `7350411 fix: allow local git ops inside .agent_tmp, keep remote-side guards`
  —— `.omp/hooks/pre/git-guard.ts`：.agent_tmp 内本地操作放行，远端不可逆操作仍拦。

### spec 处理（用户斟酌后采纳直觉）
- 删掉我为"两腿统一标准"新增的强调行——不提例外即无例外，原句已隐含适用所有腿。
- 净改动只剩：**删除过时的"已知遗留（不视为失败）orphan"条目**（实测两腿均为 0）。
  该例外条目才是本次漏检的成因（AI 会拿它做归因逃逸），删掉它比加强调有效。
- 该 spec 改动**未提交**（不在本次授权范围），待用户决定。

### 补丁行为影响（用户第 4 问）
见下节分析。

## 补丁改版（用户要求）：从「删 spawn」改为「node 提供 shutdown 能力，我们调用」

### 为什么改
旧版（注释掉 `uv__console_init` 里的 spawn）虽然消 pin，但代价是**丢失 SIGWINCH-on-resize**
（Windows 控制台尺寸变化通知）。用户指出这个仓库加 node 就是为了完整支持 node 生态，功能不该残。

### 新版设计
保留 spawn（运行期 SIGWINCH 完整），新增 `uv__tty_console_cleanup()`：
- 线程照常由 `uv__console_init` spawn；只在能创建 stop event 时才 spawn。
- message-loop 线程：`DuplicateHandle` 发布自身 handle + `GetCurrentThreadId()`；
  保存 `HWINEVENTHOOK`，退出前 `UnhookWinEvent`（回调在本模块，必须摘掉）。
- watcher 线程：改为 `WaitForMultipleObjects{resized, stop}`，非 resized 即 break 返回
  （原先是 `WaitForSingleObject(INFINITE)` 死等）。
- `uv__tty_console_cleanup()`：`SetEvent(stop)` + `PostThreadMessageW(WM_QUIT)`，
  然后 `WaitForMultipleObjects(…, TRUE, 2000)` **有界等待两个回调真正返回** ——
  模块引用在回调返回前一直持有，必须等，不能假设。
- 新增 `pUnhookWinEvent` / `pPostThreadMessageW`（winapi.h typedef + extern、winapi.c
  定义/union/GetProcAddress 加载），照 `pSetWinEventHook` 的既有模式。
- 挂进 `uv_library_shutdown()`（`uv-common.c`，`#ifdef _WIN32`）；声明在 `uv-common.h`。

### 我们这边的调用点（零改动即可拿到）
`src/runtime/impl/node/jsb_node_global_init.cpp:71` 的 `GlobalInitialize::shutdown()`
已调 `uv_library_shutdown()` → 自动触发上面的清理。GodotJS 侧无需新增调用。

### 验证
- `deps/uv/libuv.vcxproj` 单独编译：**LIBUV_RC=0** ✓
- 待：完整 node 重编 → 重新合并自包含 libnode.lib → 重链 node 腿 → 实测 Orphan=0

## 13. 定案：缺陷 B 的根因是构建接线，不是"没法停线程"

中途把 pin 判为"不可释放"、并据此去改调用点，是**走错了方向**。真因在 `SConstruct`：
`/WHOLEARCHIVE:libnode.lib` 挂在基础 env（`SConstruct:749`），而 editor 目标是
`base_env.Clone()`（`SConstruct:929`）→ **白继承整份 libnode**。

于是 editor DLL 里躺着一份它**从不使用**的 libuv，其中 `uv__console_init` 起了两个
永不返回的回调 → ntdll `LdrAddRefDll` → editor DLL 进程内无法卸载 → 它持有的 godot-cpp
类名全被判 orphan。

实证（editor DLL 从不过 JS）：`src/editor`、`src/api_tool`、`src/compat`、`src/editor/codegen`
内**零** `uv_*` / `node::` / `napi_` / `v8::` 调用；共享 `src/internal` 里那几处 `v8::` 全在
**宏体内**（`JSB_ISOLATE_SCOPE` / `JSB_HANDLE_SCOPE`），展开点都在 `src/runtime/bridge/*.cpp`，
editor 不编译这些 TU。

### A/B 对照（node 腿，每次 editor dll 都完整重编，非 up-to-date）

| 配置 | editor dll 体积 | `uv_*` 导出 | Orphan |
|---|---|---|---|
| 原状 | 99.7 MB | 318 | 102 |
| 只给 runtime 腿加 cleanup | 99.7 MB | 318 | **77** |
| runtime + editor 都加 cleanup（曾误以为正解） | 99.7 MB | 318 | 0 |
| **剔除 editor 的 libnode（editor 源码零改动）** | **5.1 MB** | **0** | **0** |

77 那一档的构成直接点名 editor：`GodotJSEditorPlugin` / `GodotJSExportPlugin` / `EditorPlugin`
在列，而 `GodotJSScript` / `GodotJSScriptLanguage` / `ResourceFormat{Loader,Saver}GodotJSScript`
全为 0 —— runtime 已卸载，剩的就是 editor DLL。

**结论：`register_editor_types.cpp` 不需要改**（用户质疑正确）。给不需要 JS 引擎的 DLL 灌
`/WHOLEARCHIVE`，代价不止是 ~95MB：第三方静态库把**自己的运行时（线程、全局状态）**也一起
带进那个 DLL，连卸载语义都被改写了。

## 14. 最终修法：仅 `SConstruct`，`src/` 零改动

从 editor 目标剔除 libnode（基础 env 的 `/WHOLEARCHIVE` 被 Clone 继承，需显式摘掉
`LIBS` 里的 libnode 与 `LINKFLAGS` 里的 whole-archive/force_load）：

```python
if node_support is not None:
    editor_build_env['LIBS'] = [lib for lib in editor_build_env['LIBS']
                                if 'libnode' not in str(lib)]
    editor_build_env['LINKFLAGS'] = [
        flag for flag in editor_build_env['LINKFLAGS']
        if '/WHOLEARCHIVE' not in str(flag).upper()
        and '--whole-archive' not in str(flag)
        and '-force_load' not in str(flag)
    ]
```

`node_support is not None` 守卫保证 v8 腿（不链 libnode）完全不受影响。

### 顺带作废的一处改动：runtime 侧额外的直接调用

曾在 `GlobalInitialize::shutdown()` 里**直接**调 `uv__tty_console_cleanup()`。A/B 证明
**不必要**：打过补丁的 libnode 已把该调用挂在 `uv_library_shutdown()` 内
（`uv-common.c` 的 `#ifdef _WIN32` 分支），而 node 腿本来就在调 `uv_library_shutdown()`。
撤掉后 orphan 仍为 **0**（runtime dll 确实重编：md5 `27e27ff952e0e1e152eb64f500dcaa97`
≠ 含调用的 `04755c01bbd43e0c0caf83f4304c32f8`）。

**保留它有害**：会让 node 腿**硬依赖**补丁符号 → 官方未打补丁的 libnode **链接失败**。
去掉后补丁变为**可选**：不打补丁也能编译（只是 orphan 回到非零）。已移除。

## 15. 验收实测（最终形态：仅 SConstruct；两腿统一标准）

| 腿 | runtime / editor md5 | Orphan | COMPLETED | FAILED | ERROR | WARNING | doctest |
|---|---|---|---|---|---|---|---|
| v8 | `44e085de1af47f83ce590986cb949efb` / `269877b857185a1d44be99fdb7c243b2` | **0** | 1 | 0 | 0 | 0 | 2 套件 SUCCESS，0 failed，orphan 0 |
| node | `27e27ff952e0e1e152eb64f500dcaa97` / `9f77ff4b821db12d0106049e043b13bf` | **0** | 1 | 0 | 0 | 0 | 3+49 pass / 12+418 assert / SUCCESS，orphan 0 |

editor dll 体积 **5.1 MB**（原 99.7 MB），`uv_*` 导出 **0**（原 318）。
基线：v8 原 2 → 0；node 原 102 → **0**。

AC1 ✓ / AC2 ✓ / AC3 ✓ / AC4 ✓（spec 已写入统一判据、排查顺序、教训、已否定方案）/ AC5 ✓

## 16. 遗留（未完成，需授权或决策）

1. **libnode 补丁尚未落地到依赖源**。node 腿 orphan=0 仍依赖手工打过补丁的
   `third/libnode/windows/x64/libnode.lib`（md5 `8e4aed2e75de7a9e431b3c822b32dbfd`）——
   官方 `moluopro/libnode` 不含 `uv__tty_console_cleanup`。
   注意与早期状态的区别：**现在它不再是链接依赖**，用官方库也能编译，只是 orphan 回到非零
   （不再是"构建不过"，降级为"验收不过"）。
   正式闭环需把补丁落到 `GodotJS-Dependencies`（clone 官方 node 后 apply），push 分支触发 CI
   → 取产物。**本轮未 push / 未改依赖源 URL**（`SConstruct:93 deps_node_url` 仍指向 moluopro）
   ——改动前须经用户确认。
   **补丁已持久化**：`libuv_console_shutdown.patch`（同目录），由 `node-src` git checkout
   （node **v24.18.0**）的 `git diff -- deps/uv/` 导出，5 文件 +134/-14。已验证可干净应用到
   纯净 v24.18.0（`git apply --check` → **APPLIES_CLEAN**）。
   注：`.agent_tmp/patch_libuv_console.py` 是**旧版**（注入 `uv_console_cleanup`），与最终符号
   `uv__tty_console_cleanup` 不一致，勿用；以本 patch 文件为准。
2. 未 commit / push 本仓库（逐轮授权原则，本轮未获授权）。
3. 废弃的 S2 补丁脚本 `misc/build/apply_godotcpp_static_class_name_patch.py` 已删除；
   `SConstruct` 的 S2 钩子残留已确认为无。
4. 其他存量遗留保持原状：tsc 存量类型错误、`RegEx` 的 StringName、
   `api_tool::get_loader()` static-local 句柄隐患。

---

# 2026-09-24 依赖闭环：libuv 补丁落地依赖仓库 + 主仓依赖源切换 + CI node 腿恢复

## 目标（5 项）

0. 评估 `GodotJS-Dependencies` 是否子模块化 + 维护约定落 spec
1. libuv console-shutdown 补丁应用并发布（依赖仓库）
2. 主仓 lws/v8/libnode 下载源指向我方依赖仓库
3. `09-22-fix-orphan-stringname-v8-node` / `09-06-lowprio-node-orphan-stringname` 归档判定
4. CI 补 node 构建与测试，验证 `09-06-lowprio-lws-pic` / `09-06-libnode-typeinfo-symbols` 已被替换后的依赖解决

## 已完成

### [0] 依赖仓库形态与 spec

- 结论：**不做子模块**，登记为 trellis polyrepo 包（`.trellis/config.yaml` `packages` 段，`git: true`）。
  理由：主仓消费的是 release 产物 URL，非源码路径；submodule pin 源码 commit 会制造版本一致假象。
- 新增 `.trellis/spec/godotjs-ext/build/dependencies.md`；`build/index.md` 加检查项；`scons-build.md` lws 条目改为"全平台启用（含 Linux，PIC）"。

### [1] libuv 补丁（依赖仓库，已推送）

- `scripts/node/patch_libuv_console.py`（fail-closed 精确替换），接线进 `build-windows.ps1`（`$LASTEXITCODE` 显式检查）。
- 本地三重验证：纯净 v24.18.0 打补丁成功；已打过补丁的树二次运行正确 fail-closed；与手工补丁逐文件 diff 等价（`tty.c` 仅差一空行）。

### [2] 主仓依赖源切换（代码已改）

- `SConstruct`：`deps_release_tag = "260924-node-libuv-console"`、`deps_v8_version = "12.4.254.21"`、
  `deps_lws_version = "4.3"`、`deps_node_version = "v24.x"`、
  `deps_url = "https://github.com/Daylily-Zeleen/GodotJS-Dependencies/releases"`；删除 `deps_node_url`，node 下载改走统一路径。

### [4] 关键发现：libnode 归档目录拼写不匹配（本次修复）

- 生产者（依赖仓库）：`build-linux.sh` → `staging/libnode/linux/x86_64`，`verify_artifacts.py:node_dir()` 的契约是
  **仅 Windows 用 `x64`**，其他平台用可移植拼写 `x86_64`。
- 消费者（主仓旧 `SConstruct`）：`_libnode_platform_base()` 把 `x86_64` **无条件**映射成 `x64` → Linux 期望
  `third/libnode/linux/x64/libnode.a`。
- 后果：下载成功但 `validate_library_support()` 返回 None → 构建死于
  `check(False, "libnode prebuild lib is not found.")`，而包其实是好的（即新增的 linux node 腿必然失败）。
- 修法：消费端对齐生产端契约（`x64` 仅 Windows），并注释说明两侧必须一致。

### [4] CI 改动（主仓 `.github/workflows/ci.yml`）

- build 矩阵：删除"node macos/linux 暂时禁用"注释块，新增 linux x86_64 / macos arm64 两条 node leg。
- test 矩阵：host-node 从仅 Windows 扩到 windows/linux/macos 三条；job 名加 `os` 以区分。
- `run-runtime-matrix.mts`：去掉 `host-node` 的 Windows-only 门槛（`needsHostNode = isRuntimeSelected(...)`）与对应的 FAIL 分支。
- `fetch-godot`：修正 macOS 资产解析——macOS 资产是 `Godot.app` 包，旧的
  `find -maxdepth 1 -name 'Godot*' -type f` 在 macOS 上匹配不到任何东西（引擎路径为空 → 后续步骤必失败）。
- "Put Godot on PATH"：macOS 用 `exec` 包装脚本暴露为 `godot`，保持可执行文件在 .app 包内。
- Node 依赖缓存路径 `third/node` → `third/libnode`（前者从来不存在，缓存从未命中）。
- `GODOT_ASSET_MACOS` 环境变量补齐。

### 仍待完成

- 等依赖仓库 release `260924-node-libuv-console` 产出（CI run `35925542298` 进行中，0 失败）。
- 主仓提交推送（已授权）→ 远端 CI 实测 node macos/linux 腿。
- 归档判定（第 3、4 项）。

## 归档判定（第 3、4 项）

已归档：`09-06-lowprio-node-orphan-stringname` → `.trellis/tasks/archive/2026-09/`。

- 判据：其 3 条 AC 全部由 09-22 任务的缺陷 A/B 修复覆盖——「泄漏源定位结论落档」写在
  `.trellis/spec/godotjs-ext/test/index.md`（缺陷 A：进程级静态池持有 `PropertyInfo::name`；
  缺陷 B：node 镜像加载期多 2 个引用）；「修复后 Orphan 计数为 0」已实测两腿皆 0；
  「libnode 侧问题并入上游沟通任务」由本轮把补丁落到 `GodotJS-Dependencies` 完成闭环。
- `--skip-branch-validation`：该任务工作直接落在 `main`，从未有独立分支（`task.json` 的
  `branch` 为 null），故跳过分支元数据校验。

### [4] 又两处会让新腿必挂的缺陷（本次修复，均已取证）

**缺陷 C：libnode 归档目录 arch 拼写两侧不一致**（见上文 [4] 关键发现）。消费端修法已落地并注释。

**缺陷 D：macOS editor 扩展在 arm64 产物上加载不了。**
- 取证：下载既有绿 run `35882603586` 的 `macos-editor-arm64-v8` 产物，`bin/macos/` 下只有
  `godotjs-ext-editor.macos.editor.arm64.dylib` 与 `godotjs-ext.macos.editor.arm64.dylib`；
  而 `godotjs-ext-editor.gdextension` 的 `[libraries]` **只有** arch-less 的
  `macos.debug.editor = "...universal.dylib"`（对照：主 gdextension 早就有 `.arm64` 键）。
- 引擎语义取证（`D:/Dev/godot/godot/core/extension/gdextension_library_loader.cpp:83-120`
  `find_extension_library`）：逐 key 按 `.` 拆 tag 全匹配者胜，**匹配 tag 数多者优先**。
  故 arm64 上 4-tag 的 `macos.debug.editor.arm64` 会压过 3-tag 的 universal 键；没有该键时
  引擎去开 `...universal.dylib`（该产物不存在）→ 扩展加载失败。
- 影响面：`--godotjs-api-generate` 由 editor 扩展注册（`src/editor/weaver-editor/jsb_editor_plugin.cpp:195`）
  → editor 扩展加载不了，新加的 macOS test leg 的 api 生成链直接断。
- 为何至今未暴露：**macOS 从来只是 build 腿，没有任何 test leg 去加载它**（test 矩阵原只有 Linux/Windows）。
- 修法：给 editor gdextension 补 `macos.debug.editor.arm64`，与主 gdextension 同构。
  Linux 侧已核对无需改动（`linux.debug.editor.x86_64` 与产物名一致）。

**缺陷 E：新 macOS test leg 会挂在 GNU `timeout`。** CI "Run C++ unit tests" 的非 Windows 分支
用 `timeout 120 ...`；`timeout(1)` 属 coreutils，而 macOS runner 镜像**不装 coreutils**
（已核 `macos-15`/`macos-26` 两份 runner-images README，均无 coreutils、无 gtimeout）。
修法：`command -v timeout` 有则用，否则回落 `perl -e 'alarm shift; exec @ARGV'`（两平台都有 perl）。

### 验证

- `SConstruct` 两处 leg 求值冒烟（`-n`，不编译）：`use_v8=yes` 与 `use_node=yes` 均 `rc=0` 完成图求值。
- `SConstruct` / ci.yml / fetch-godot / misc_*.yml YAML+AST 校验全 OK。
- 归档目录拼写逐平台核算：windows/macos/android/ios 修前修后同值；linux x86_64 由 `linux/x64`
  改为 `linux/x86_64`（与生产端 `build-linux.sh:53`、`verify_artifacts.py:node_dir()` 一致）。
- 注意：本机 `third/libnode` 仍是**旧 moluopro 布局**（`linux/x64`），切源后 Linux node 腿
  必须重新下载（已写入 spec 的"常见坑"）。
