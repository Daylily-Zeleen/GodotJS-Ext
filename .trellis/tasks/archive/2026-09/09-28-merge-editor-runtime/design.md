# 设计：单一库 + target 宏裁剪，移除桥机制

> 依据 `prd.md`。本文件只写边界、契约与取舍；执行顺序见 `implement.md`。

## 1. 产物形态

**一个库、两份产物**，同一个 SCons 目标（`SharedLibrary`），源集合按 target 决定：

| target | 源集合 | 产物名 | 编辑器功能 |
|---|---|---|---|
| `editor` | `runtime_globs` + `editor_globs` | `godotjs-ext.<plat>.editor.<arch>.<ext>` | 编入 |
| `template_release` / `template_debug` | `runtime_globs` | `godotjs-ext.<plat>.template_<flavor>.<arch>.<ext>` | **不编入** |

- addons 下只剩 `godotjs-ext.gdextension`；`godotjs-ext-editor.gdextension` 删除。
  - 主 gdextension 缺的 editor 键要补：`linux.debug.editor.arm64`（CI 有 linux editor arm64 腿）。
- 唯一入口符号 `jsb_gdextension_init`；`jsb_editor_library_init` 删除。

### 宏

新增 `JSB_WITH_EDITOR`（`SConstruct` 的 `jsb_defines` → 生成 `src/jsb.gen.h`）：
`1` iff `env["target"] == "editor"`。它是"本产物是否含编辑器功能"的**唯一判据**，
用于共享 TU（`register_types.cpp`、测试入口）与需要感知产物的条件代码。

与既有 `JSB_TOOLS` 的关系（刻意保留两个，不合并）：

- `JSB_TOOLS` 镜像 godot-cpp 的 `TOOLS_ENABLED`（`godot-cpp/tools/godotcpp.py:456`：`editor_build = target == "editor"`），
  语义是"引擎侧 editor API 可用"，已被 25 个编辑器头/源使用；
- `JSB_WITH_EDITOR` 是我方构建配置对"产物含编辑器功能"的显式声明，来源是 `SConstruct` 的 target。
- 当前二者取值相同；保持分离是为了不让"引擎 API 可用性"与"我方产物裁剪"共用一个来源。

删除 `JSB_WITH_EDITOR_UTILITY_FUNCS`：全仓零引用（只有 `SConstruct` 定义、`jsb.gen.h` 输出），
其值（`target in [editor, template_debug]`）与编辑器功能无关，属历史残留。

### 为什么编辑器源仍按 glob 排除而不是全量宏裁剪

`src/editor/**`、`src/api_tool/editor/**` 依赖 `TOOLS_ENABLED` 头（`godot-cpp/gen/include` 的
`editor_plugin.hpp` 等），template 产物里这些头不存在，整 TU 无法编译。因此：

- **整文件**：editor 源只在 `target=editor` 时进源集合（编译期不存在，不是链接期排除）；
- **共享文件**（`register_types.cpp`、测试入口、`jsb_script_language.cpp` 的 templates include）：
  用 `JSB_WITH_EDITOR` 宏裁剪。

两者都是编译期裁剪；宏负责的是"同一 TU 内的分支"。

## 2. 桥机制移除后的调用面

删除：`src/internal/jsb_bridge_abi.h`、`src/runtime/internal/jsb_bridge_table.{h,cpp}`、
`src/editor/weaver-editor/jsb_editor_bridge.h`、`GodotJSScriptLanguage::get_bridge()` 及其
ClassDB 绑定。**不新增函数指针表/注册表/版本校验**。

原桥函数 → 直接调用（同库静态链接，自然签名：`String` / `Variant&` / `Error`）：

| 原桥槽 | 新落点 | 调用点 |
|---|---|---|
| `eval` | `GodotJSScriptLanguage::eval_source(const String&, Error&)`（已存在，返回 `JSValueMove`） | REPL |
| `eval_with_arg` | **新增** `GodotJSScriptLanguage::eval_source_with_arg(const String &p_source, const Variant &p_arg, Error &r_err)` | codegen `_request_codegen` |
| `get_module_source_info` | **新增** `Environment::get_module_source_info(const String &p_module_id, Dictionary &r_info)` | export plugin |
| `get_module_direct_dependencies` | **新增** `Environment::get_module_direct_dependencies(const String &p_module_id, PackedStringArray &r_deps)` | export plugin |
| `fill_statistics` | `Environment::get_statistics(Statistics&)`（已存在） | statistics viewer |
| `scan_external_changes` | `GodotJSScriptLanguage::scan_external_changes()`（已存在） | editor plugin |
| `is_global_class_generic` | `GodotJSScriptLanguage::is_global_class_generic(const String&)`（已存在） | codegen（2 处） |
| `request_gc` | `jsb::Environment::gc()`（已存在，static） | REPL |
| `add/remove_console_output` | `jsb::internal::IConsoleOutput` 基类直接实现（REPL 继承） | REPL |
| `refresh_paths_mapping` | `jsb::PathsMapping::refresh()`（已存在） | editor plugin |

**前置条件守卫保留**（`lang == nullptr || !is_initialized()`、`Thread::is_main_thread()`）：
原先在桥实现里，现移入新方法或调用点，语义不变（`ERR_UNCONFIGURED` / `ERR_UNAVAILABLE` 等
错误码映射到直接调用的返回值）。

### 新增两处 API 的落点理由

- `eval_source_with_arg`：需要 isolate/context + `TypeConvert::gd_var_to_js` + 写 `__jsb_arg`
  全局，与 `eval_source` 同源（同一守卫、同一错误码），放 `GodotJSScriptLanguage` 而非调用点内联，
  避免把 V8 样板散进编辑器 TU。
- 两个 module 查询：`Environment` 持有 `module_cache_`，查询实现（`load()` + `source_info` /
  `children` 数组遍历）本来就贴着它；export plugin 是唯一消费者。

### node console hook 的落点

`jsb_bridge_table.cpp` 里的 node console 包装（9 个方法 mirror 到 `IConsoleOutput` + forward 到
node 原生实现、`console.time/timeEnd` 接管、按 isolate 的 timer tag 表）迁到
`src/runtime/impl/node/jsb_node_console_hook.{h,cpp}`（`JSB_WITH_NODE` 门控，node 模式的
runtime 源集合里）。

激活条件不变（"本进程出现第一个 console sink 时"），但触发点从 `bridge_add_console_output`
改为 `IConsoleOutput` 构造：`jsb_console_output.cpp` 在 `#if JSB_WITH_NODE` 下调用
`jsb::impl::console_hook_arm()`。**不按 `JSB_WITH_EDITOR` 门控**——判据是"有没有 sink"本身：
template 产物里没有任何 `IConsoleOutput` 实例，arm 永不被调用；editor 产物里 REPL 构造即触发。
`NodeRuntime` 的构造/析构仍调用 `console_hook_ensure` / `console_hook_drop_isolate`。
这是同库内的普通函数调用，不构成新的间接层。

## 3. 测试合并

- 单 doctest 注册表：`src/runtime/tests/jsb_test_main.cpp` 是唯一 `DOCTEST_CONFIG_IMPLEMENT` TU，
  在 `#if JSB_WITH_EDITOR` 下 include `src/editor/tests/*.h`；`src/editor/tests/jsb_editor_test_main.cpp` 删除。
- `src/tests/jsb_test_runner.h` 的 `try_run(flag)` 简化为 `try_run()`：不再需要
  `Engine` meta 双套件协调（`RuntimeTest`/`EditorTest`/`TestResult` 全删）。`--jsb-run-tests`
  flag 名与"exit code == 0 且无泄漏"的验收口径不变；日志从两段 doctest 汇总变一段（3 行）。
- `test_jsb_bridge_table.h`：桥槽测试全部改写为直接调用新 API（文件名改
  `test_jsb_runtime_api.h`，用例覆盖 eval/eval_with_arg/module 查询/统计/gc/scan/generic/console sink）；
  随桥消失的用例（`struct_size`、空槽、`nullptr` 结果存储拒绝、handle 幂等删除）删除，不做等价重写。
- `test_jsb_editor_bridge.h` 删除：它唯一覆盖的是"跨 DLL 生产路径"（ClassDB 解析 + 跨库 Variant），
  该前提消失；其行为断言已被 runtime 套件的直接调用用例覆盖。
- `test_jsb_editor_cleanup.h` 保留，纳入同一注册表。

## 4. 跨库残留清理

`JSB_RUNTIME_API`（`src/jsb.config.h`）与其 6 处使用（`ApiLoader::singleton`、
`Logger::_print_*`、`StringNames::singleton_`、`GodotJSScriptLanguage::singleton_`）
只为"editor 库导入 runtime 数据符号"而存在，单库后无消费者 → 删除宏与注解；
`GodotJSScriptLanguage::get_singleton()` 保留非 inline 定义（行为不变，仅去掉过时注释）。

`jsb.config.h` 顶部关于 .def 导出数据符号、`jsb_script_language.h` 的 `singleton_` 注释、
`jsb_shared_statics.h` 中"`jsb_bridge_table.cpp` 也解析类"的注释一并改写成单库事实。

## 5. 风险与取舍

- **风险：编辑器源与 runtime 源进同一 obj 树**（`.build/runtime/`）。已核对：并集内
  basename 无冲突（`api_tool`/`internal`/`compat` 的重复来自两侧各自 glob，合并后去重即可）。
  实现上用"有序去重"构造源列表，而不是拼接两个列表。
- **风险：editor 腿的 libnode 剔除逻辑失效**。原 editor 库靠剔除 libnode 规避 orphan
  StringName；合并后 editor 产物**必须**链 libnode（含 runtime 源），该剔除块删除。
  orphan 验收标准不变（runtime 腿本来就链 libnode 且 orphan=0）。
- **风险：`~` 副本陷阱**。单库后不再有"扩展 A 链接扩展 B"的形态，该陷阱对本任务自然消失。
- **取舍：不搬迁目录**。`src/editor/**` 位置不动（PRD 未要求），减少 diff 与 include 面。
- **回滚**：本任务是分支内重构，无数据迁移；回滚 = `git revert`/重置分支。