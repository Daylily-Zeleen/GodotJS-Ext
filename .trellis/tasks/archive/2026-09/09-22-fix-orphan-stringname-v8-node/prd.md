# 修复 TS 集成测试 Orphan StringName（v8 2 个 + node 大量）并统一测试标准

## Goal

两腿构建（v8 prebuilt / node）的 TS 集成测试退出时都残留 Orphan StringName，违反既有验收标准（exit 0 且无 Orphan StringName）。修到两腿均为 0，并让 node 腿不再被区别对待。

## 复现（已实测）

构建命令与 tasks.json 一致：

- v8：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_v8=yes tests=yes -j17`
- node：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes use_node=yes tests=yes -j17`

运行（`project/` 下，引擎为官方 `godot.windows.editor.x86_64.exe`，**不是**自建 `bin/windows/godotjs-ext.exe`）：

```
godot --audio-driver Dummy --headless --path . --verbose
```

实测（本次会话，dll 身份已用 md5 固定）：

| 腿 | Orphan 数 | 内容 |
|---|---|---|
| v8 | 2 | `health`、`attack`（各 static: 0, total: 1） |
| node | 102 | 上述 2 个 + ~80 引擎类名（`Node`/`Object`/`Resource`/`Label`…）+ 编辑器类名 |
| node（extension_list 去掉 editor 扩展） | 72 | 少掉 ~30 个编辑器侧类名 |

node 腿与测试项目内容无关：最小空工程 + `--quit` 仍残留 84 个。

## Requirements

1. v8 腿 `health`/`attack` 泄漏必须消除（定位 → 修复）。
2. node 腿全部 Orphan StringName 必须消除，不允许"node 例外"。
3. spec 补齐：把"Orphan StringName = 0"从隐含标准升级为**两腿同标准**的显式验收条款，并写入本次漏检的教训（何时检、怎么检、node 不豁免）。
4. 修复不得以牺牲对象生命周期正确性为代价（对象泄漏 / use-after-free 比 StringName 泄漏更严重）。

## Acceptance Criteria

- [ ] AC1：v8 腿 `godot --headless --path ./project --verbose` → exit 0、`GODOTJS_TEST_PROJECT_COMPLETED`、`grep -c "Orphan StringName" = 0`。
- [ ] AC2：node 腿同命令同判据，`= 0`。
- [ ] AC3：两腿均无新增 ERROR/WARNING（除既有预期条目，如 `get_meta('missing')` 的引擎 ERR）。
- [ ] AC4：`.trellis/spec/godotjs-ext/test/index.md` 明确写出两腿统一判据与自查命令；存量"已知遗留 orphan"条目按实测修订（当前实测 v8 腿已无该 3 个遗留项）。
- [ ] AC5：改动后重跑两腿完整 TS 套件与 C++ doctest（`--jsb-run-tests`），无回归。

## Constraints

- 只用规范 scons 命令；不 `--clean`。
- 未获授权不 commit / push / 还原文件。
- 诊断日志与一次性脚本一律 `.agent_tmp/`。
- 两腿 dll 必须分别构建并核对 md5（同一部署位）。

## Notes

- node 与 v8 复用同一套 runtime 逻辑，差异集中在 `JSB_WITH_NODE` 分支（`NodeRuntime` 初始化/更新、`node.dll` shim、console hook）。
- node 独有：编辑器侧 console hook（`jsb_bridge_table.cpp`）、`[dependencies] node.dll`。

## Technical Notes（已实测，勿重推）

### 引擎判据

`StringName::cleanup()`（`D:/Dev/godot/godot/core/string/string_name.cpp:105-125`）判定
`static_count != refcount` 即 orphan。godot-cpp 的 `get_class_static()`
（`third/godot-cpp/include/godot_cpp/classes/wrapped.hpp:274-277`、`:487-493`）用函数局部
`static const StringName`（`p_static=false` → `static_count==0`、`refcount==1`），**只在该 DLL
卸载时析构**。故凡 `StringName::cleanup()` 执行时仍映射的扩展 DLL，其全部 godot-cpp 类名必被判
orphan（与 JS 是否使用无关）。

### 两个独立缺陷

- **缺陷 A（两腿共有，各 2 个：`health`/`attack`）**：`src/runtime/weaver/jsb_script_instance.cpp:293`
  的进程级静态池 `temporary_property_list`（`std::atomic<PropertyList*>`，无析构）持有
  `PropertyInfo::name`，活过 `StringName::cleanup()`。仅 `project/tests/resource/player-resource.ts`
  的 `accessor health/attack` 会命中 → 与「v8 腿恰为这 2 个、minproj 无」吻合。
- **缺陷 B（仅 node，~99/102）**：node 侧两个扩展 DLL 卸载不掉。裸宿主实验
  （`.agent_tmp/freeloop.ps1`：纯 PowerShell，无 Godot、无 JSB 代码运行）实测**一次
  `LoadLibraryExW` 需要 3 次 `FreeLibrary`**；v8 镜像只需 **1** 次。探针把窗口钉在 DLL
  自身加载期（首个 C++ 静态构造读 1 → `DllMain` 读 3）；已排除探针自身、目录内容、瞬时线程
  pin（立即 free 与等 4s 同为 3；Load×2 需 free×4）。node 与 v8 的实质差异是
  `/WHOLEARCHIVE:libnode.lib`（`SConstruct:749`）。v8 腿因此天然为 0（计数 1，引擎唯一那次
  `FreeLibrary` 正好归零）。

### 已解决决策

- **缺陷 B 修法：S1 —— 消除 node 镜像加载期那 2 个多余引用**（不改第三方代码、两腿回到同一
  卸载机制）。已否决 S2（fork godot-cpp 子模块）与 S3（自我强卸，违反 Requirement 4）。

### 已废弃方法（勿重试）

- **在 DLL 内部挂钩 loader API（含 `.CRT$XIB` 最早初始化点，含把槽函数改为返回 `int` 并
  `return 0`）**：CRT/loader 初始化期重入 loader，稳定触发 `ERROR_DLL_INIT_FAILED (1114)`。
  本进程内无法从 DLL 内部挂钩 loader API。
