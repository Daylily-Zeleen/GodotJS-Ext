# 合并 editor 库与 runtime 库（按 target 条件编译产出两个产物）

## Goal

**合并两个库为一个**：不再维护 runtime 库与 editor 库两份，改为**单一库 +
按 `target` 条件编译**产出两个不同的目标产物；同时**移除 JsbBridgeTable 桥机制**，
并把两侧的 C++ 测试**合并为一套**。

触发原因：编辑器功能无法与 runtime 干净分离 —— 跨库依赖（如
`GodotJSScriptLanguage::get_singleton()`）无法消除，拆分只能靠 `JsbBridgeTable`
函数指针表经 ClassDB 跨 DLL 通信，带来 ABI 版本校验、空指针兜底、跨库 Variant 所有权
等一整套复杂度。既然无法分离，就不再分离。

## 现状（核对 src/ 与 SConstruct）

### 两份 gdextension / 两个库

- `project/addons/godotjs-ext.daylily-zeleen/godotjs-ext.gdextension`（主库）
- `project/addons/godotjs-ext.daylily-zeleen/godotjs-ext-editor.gdextension`（editor 库），
  只注册 `*.editor` 目标，导出游戏内不加载（README.md:241-244）
- 构建侧：`SConstruct:1022` 用 `make_target_env(env, "bin/windows/godotjs-ext-editor", "editor", editor_globs)`
  产出 editor 库

### 桥机制的范围

| 组成 | 位置 |
|---|---|
| ABI 契约（`JsbBridgeTable` 布局 + 函数指针 typedef） | `src/internal/jsb_bridge_abi.h` |
| runtime 侧实现与单例访问 | `src/runtime/internal/jsb_bridge_table.{h,cpp}`（:534 `g_bridge_table`、:553 `get_bridge_table()`） |
| editor 侧解析（经 ClassDB 取表地址） | `src/editor/weaver-editor/jsb_editor_bridge.h`（:53 `get_bridge()`，:74 用 `struct_size` 做版本校验） |

`EditorBridge::get_bridge()` 的调用点（合并后都应改为直接调用）：

- `src/editor/codegen/jsb_codegen_scene_descriptors.cpp`（:72、:156、:349）
- `src/editor/weaver-editor/jsb_editor_plugin.cpp`（:198、:1237、:1308、:1360）
- `src/editor/weaver-editor/jsb_export_plugin.cpp`（:242）
- `src/editor/weaver-editor/jsb_repl.cpp`（:59、:170、:242、:356）
- `src/editor/weaver-editor/jsb_statistics_viewer.cpp`（:70）
- `src/editor/tests/test_jsb_editor_bridge.h`（:63、:77、:92 —— 3 个桥相关用例）

### 两侧 C++ 测试

- runtime 侧：`src/runtime/tests/**`（直接调 `jsb::get_bridge_table()` 等）
- editor 侧：`src/editor/tests/**`，其中 `test_jsb_editor_bridge.h` 的用例专门验证
  **跨 DLL 生产路径**（注释 :30-34 明说它刻意走 ClassDB 解析以覆盖跨库边界）——
  合并后该前提消失，这些用例要么改写为直接调用，要么删除

## Requirements

1. **单一库**：合并后只保留一份 gdextension（`godotjs-ext.gdextension`），删除
   `godotjs-ext-editor.gdextension` 及其对应构建目标/`editor_globs`。
2. **按 target 条件编译产出两个产物**：以 `target=editor` 与 `target=template_release`/
   `template_debug` 定义**不同的宏**，用宏控制编辑器功能的编入；两套产物仍分别为
   editor 目标与模板目标。需明确宏命名与注入位置（现有 `SConstruct` 的
   `CompileDefines` 机制，约 377 行起）。
3. **移除 JsbBridgeTable 桥机制**：删除 `jsb_bridge_abi.h`、
   `src/runtime/internal/jsb_bridge_table.*`、`src/editor/weaver-editor/jsb_editor_bridge.*；
   所有原 `EditorBridge::get_bridge()` 调用点改为**直接调用** runtime 侧实现（不再有
   跨库边界、不再需要 `struct_size` 版本校验与空表兜底）。
4. **合并两侧 C++ 测试为一套**：原 editor 侧测试中依赖跨库语义的用例（尤其
   `test_jsb_editor_bridge.h`）按新结构改写或删除；测试入口与 `tests=yes` 的判别方式
   需保持一致（参见既有验证经验：日志出现 6 行 `[doctest]` 即测试套件已编入）。
5. **不要引入新的跨库/跨模块间接层**替代桥：合并的目的就是消除间接层。

## Acceptance Criteria

- [ ] addons 下只剩一份 gdextension；`godotjs-ext-editor.gdextension` 已删除
- [ ] `target=editor` 与 `target=template_release` 各自定义不同宏，且两套产物均构建成功
- [ ] 编辑器功能在 template 产物中**确实不被编入**（用宏裁剪，而非仅链接时排除）
- [ ] `JsbBridgeTable` / `EditorBridge::get_bridge()` / `get_bridge_table()` 在代码库中
      **无残留引用**（grep 为 0）
- [ ] 原桥调用点全部改为直接调用，行为不变（REPL eval、codegen 查询、统计填充、
      paths_mapping 刷新、export 的模块源信息、console sink 等逐一核对）
- [ ] editor 与 runtime 的 C++ 测试合并为一套，`tests=yes` 下一次性跑通，无重复用例
- [ ] CI 全绿（含所有平台的 editor / template 腿，以及 5 个 Test 腿）

## Notes

- 相关历史归档：`09-06-arch-split-v1-verify/archive.md` 记录了拆分尝试的判定 ——
  其第 2 条已判定 `EditorUtilityFuncs` 的跨库注册模式废弃、第 3 条列出了 editor 对
  runtime 的直接依赖点（`GodotJSScriptLanguage::get_singleton()`，7 个文件）。本任务是
  该拆分的**反向决策**：既然依赖无法消除，改为合并 + 条件编译。
- 本轮只新建并完善任务描述，**未改动任何代码**。
