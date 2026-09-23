# design.md — Orphan StringName 修复设计

## 1. 引擎判据（唯一裁决规则）

`StringName::cleanup()`（`D:/Dev/godot/godot/core/string/string_name.cpp:105-125`）：
```cpp
if (d->static_count.get() != d->refcount.get()) { print "Orphan StringName: ..." }
```
- `static_count` 仅由 `StringName(const char*, p_static=true)` 置 1（引擎侧宏 `SNAME`）。
- godot-cpp 的 `get_class_static()`（`third/godot-cpp/include/godot_cpp/classes/wrapped.hpp:274-277`
  与 `:487-493`）用**函数局部 `static const StringName`**，即 `p_static=false`
  → `static_count == 0`、`refcount == 1`。
- 该局部静态**只在所属 DLL 卸载时析构**；只要 `StringName::cleanup()` 执行时 DLL 仍映射，
  这类名字必然被判 orphan。**与 JS 是否使用该类无关。**

`unref()`（`:111-135`）另有副作用：`refcount` 归零且 `static_count > 0` 且
`CoreGlobals::leak_reporting_enabled`（默认 `true`）时 `ERR_PRINT("BUG: Unreferenced static
string to 0")`。**任何「把 static_count 抬到 1」的方案都必须保证该静态永不析构**，否则
在 v8 腿（DLL 早于 cleanup 卸载）会打印新 ERROR，违反 AC3。

## 2. 两个独立缺陷

| | 缺陷 A | 缺陷 B |
|---|---|---|
| 名称 | `health`、`attack` | ~80–100 个引擎/编辑器类名（`Node`/`Object`/`Resource`/`Label`…） |
| 腿 | **两腿都有**（v8 各 2 个） | **仅 node** |
| 判据 | 名字本身被 JSB 静态持有 | 扩展 DLL 未卸载 → godot-cpp 全部类静态存活 |
| 独立性 | B 修好后 A 仍在（v8 即证明） | A 修好后 B 仍在 |

### 2.1 缺陷 A 根因（已定位到代码，待一行实测确认）

`src/runtime/weaver/jsb_script_instance.cpp:293`
```cpp
static std::atomic<GodotJSScriptInstanceBase::PropertyList *> temporary_property_list{ nullptr };
```
- 进程级**跨实例复用池**。`free_property_list_cache()`（`:314-334`）在
  `get_capacity() > capacity && reach_count > 0` 之外，把 list 存回该静态池而不是 `memdelete`。
- 该静态是 trivial 类型（`std::atomic<T*>`），**无析构**，DLL 卸载也不会清池；池中
  `PropertyList` 里的 `PropertyInfo::name`（`StringName`）因此活过 `StringName::cleanup()`。
- 只有 `project/` 的 `tests/resource/player-resource.ts`（`accessor health/attack`）会写入
  这两个名字 → 与「minproj 无、`project/` 有」完全吻合（v8 腿只此 2 个）。

### 2.2 缺陷 B 根因（已量化定位，机制未定）

**裸宿主实验**（`.agent_tmp/freeloop.ps1`：纯 PowerShell，**无 Godot、无 JSB 代码运行**，
只做一次 `LoadLibraryExW` 再反复 `FreeLibrary` 直至 `GetModuleHandleW == 0`）：

| 镜像（单独放空目录） | 一次 Load 需要几次 FreeLibrary |
|---|---|
| node runtime（当前构建 / 含探针 / **不含探针备份** `24e9c764…`） | **3** |
| node editor | **3** |
| v8 runtime（`5e4d5f3c…`）/ v8 editor | **1** |

- **排除探针**：不含探针的备份镜像同为 3。
- **排除目录内容**：镜像单独放空目录结果不变；v8 镜像放进含 `node.dll` 的目录仍是 1。
- **排除瞬时线程 pin**：Load 后立即 free 与等 4s 后 free 同为 3。
- **按次增长**：Load 两次需 4 次 free（每次 Load 都稳定多拿 2 个）。
- **窗口**（同一次裸宿主加载的 `dllprobe.log`）：`STATIC-INIT`（本 DLL 首个 C++ 静态构造）
  读 `1` → `PROCESS-ATTACH`（`DllMain` 体内，静态构造已跑完）读 `3`。
  **→ 2 个 surplus 在「本 DLL 自己的加载过程中」取得**，无需 Godot/JSB 参与。

**node 镜像与 v8 镜像的实质差异**：node 用 `/WHOLEARCHIVE:libnode.lib` 静态链入整个
libnode（376 MB / 631484 符号，`SConstruct:749`）；v8 链 v8 monolith。libnode 的
`--undefined-only` 扫描只出现 `__imp_LoadLibraryExW`（3 个成员），**无** `GetModuleHandleExW`
/ `LdrAddRefDll` / `FlsAlloc` —— 即新窗口挂钩已无静态线索，需动态定位。

**这解释了 v8 腿为何是 0**：v8 镜像加载后计数为 1，引擎唯一那次 `FreeLibrary`
（`~GDExtension` → `close_library`）正好归零 → DLL 卸载 → godot-cpp 类静态全部析构。

## 3. 缺陷 B 的策略：已定 **S1**

**决策（用户确认）**：消除 node 镜像加载期那 2 个多余引用，使 node DLL 像 v8 一样卸载。

理由：不改任何第三方代码；两腿回到同一卸载机制；不触碰对象生命周期正确性。

已否决：
- **S2**（fork `third/godot-cpp` 子模块，让 `get_class_static()` 产出 `p_static=true` 且永不析构的
  名字）：需维护上游分叉（当前 pinned `d7b6162`，仓内无补丁机制），且必须规避 §1 的
  `ERR_PRINT("BUG: Unreferenced static string to 0")` 副作用。
- **S3**（shutdown 自我强卸）：违反 Requirement 4。

**定位手段（实现阶段第一步，均不侵入 loader）**：
1. `LdrRegisterDllNotification` 回调时间线 —— 回调运行在**发起加载的线程**上，在其内部
   `CaptureStackBackTrace` 即可指名调用者；回调**只能用 PEB 助手**
   （`CreateToolhelp32Snapshot`/`OpenThread`/`GetProcAddress` 会重入 loader → 崩溃）。
2. 被动 IAT 反向扫描：遍历所有模块的 IAT/delay-IAT，统计指向本模块地址的 slot。
3. 候选机制核对：VEH（`AddVectoredExceptionHandler` 使 loader 持有处理者所在模块的引用）、
   CRT 线程 pin、libnode 内部静态注册。

**已知边界（实测）**：引擎运行中 `EXT-RUNTIME` 计数为 8，其中 5 个是 CRT 线程 pin
（`THREADS: 39 live, 5 with a start routine inside godotjs-ext*/node.dll`），会在 node shutdown
时释放（8 → 3）；引擎唯一那次 `FreeLibrary` 再减 1 → 剩 2 → 永不归零。**这 2 个即 S1 的目标。**

## 4. 缺陷 A 的设计（安全、两腿通用）

- 在 `jsb_shutdown()`（引擎 `unregister_core_extensions()` 阶段调用，**早于**
  `unregister_core_types()` → `StringName::cleanup()`）中把静态池取出并 `memdelete`：
  `temporary_property_list.exchange(nullptr)` 后释放。
- 不改变 `PropertyList` 的稳定指针语义（池内对象仅被单线程通过 exchange 取用）。
- 无对象生命周期风险：释放的是 JSB 自有的临时容器，不涉及引擎对象。

## 5. 验证设计（两腿同判据）

```bash
# v8
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_v8=yes tests=yes -j17
# node（需同时部署 node.dll）
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_node=yes tests=yes -j17

godot --audio-driver Dummy --headless --path ./project --verbose   # exit 0 && grep -c "Orphan StringName" == 0
godot --audio-driver Dummy --headless --path ./project --jsb-run-tests   # C++ doctest + TS 套件
```
