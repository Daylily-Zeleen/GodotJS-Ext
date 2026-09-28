# 执行计划：合并 editor/runtime 库

> 边界与契约见 `design.md`。每一步都给出可机械核对的验证命令。
> 规范编译命令（增量，禁 clean）：
> `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`

## 阶段 0：基线

1. 记录当前状态：`git status`（干净）、`git rev-parse HEAD`。
2. 基线构建 + 测试（改动前的一次取证，证明测试套件在本机可跑）：
   `godot --headless --path ./project --jsb-run-tests` → exit 0，日志含两段 doctest 汇总
   （runtime `50|50` + editor `3|3`）。
3. 基线模板构建：`scons target=template_release ...`（产物存在即可）。

## 阶段 1：构建接线（SConstruct）

1. `jsb_defines` 增 `JSB_WITH_EDITOR = 1 if env["target"] == "editor" else 0`；
   删 `JSB_WITH_EDITOR_UTILITY_FUNCS`。
2. `editor_globs` 保留定义，但**不再单独建目标**：
   - `if env["target"] == "editor": source_globs = runtime_globs + editor_globs else: runtime_globs`
   - 用**保序去重**构造（两侧 glob 有 19 个同名文件，见 `design.md` §5）。
3. 删除 `editor_libname` / `editor_build_env` / `editor_library` / `editor_copy` 整块，
   以及其中的 libnode 剔除逻辑。
4. `tests=yes` 分支：runtime 测试 glob 保留；删除 `src/editor/tests/*.cpp` glob
   （该目录只剩头文件）。
5. 验证：`scons target=editor ... tests=yes` 与 `scons target=template_release ...` 均成功；
   `bin/windows/` 下**不再**出现 `godotjs-ext-editor.*`。

## 阶段 2：移除桥机制

1. 删除文件：`src/internal/jsb_bridge_abi.h`、`src/runtime/internal/jsb_bridge_table.{h,cpp}`、
   `src/editor/weaver-editor/jsb_editor_bridge.h`。
2. `src/runtime/weaver/jsb_script_language.{h,cpp}`：
   - 删 `#include "../internal/jsb_bridge_table.h"` 与 `get_bridge()`；删 `_bind_methods` 里的
     `bind_method(D_METHOD("get_bridge"), ...)`；
   - 新增 `jsb::JSValueMove eval_source_with_arg(const String &p_source, const Variant &p_arg, Error &r_err)`
     （守卫：`once_initialized_` / `is_main_thread`，失败返回 `ERR_UNCONFIGURED` / `ERR_UNAVAILABLE`）。
3. `src/runtime/bridge/jsb_environment.{h,cpp}`：新增
   `Error get_module_source_info(const String &p_module_id, Dictionary &r_info)` 与
   `Error get_module_direct_dependencies(const String &p_module_id, PackedStringArray &r_deps)`
   （从桥实现原样搬迁：`load()` → `source_info` / `children[].filename`）。
4. node console hook 迁移：新建 `src/runtime/impl/node/jsb_node_console_hook.{h,cpp}`，
   内容为原 `jsb_bridge_table.cpp` 的 `#if JSB_WITH_NODE` 段（`console_hook_arm` /
   `console_hook_ensure` / `console_hook_drop_isolate` + 9 个包装方法 + timer tag 表）；
   `jsb_node_runtime.cpp` 改 include 新头。
5. `src/internal/jsb_console_output.cpp`：`#if JSB_WITH_NODE` 下在 `IConsoleOutput` 构造中
   调用 `jsb::impl::console_hook_arm()`（template 产物无 sink ⇒ 永不 arm）。
6. 调用点改直接调用（逐个核对，行为不变）：
   - `src/editor/codegen/jsb_codegen_scene_descriptors.cpp`（3 处：eval_with_arg ×1、generic ×2）
   - `src/editor/weaver-editor/jsb_editor_plugin.cpp`（4 处：scan_external_changes、eval ×2、PathsMapping::refresh）
   - `src/editor/weaver-editor/jsb_export_plugin.cpp`（1 处：两个 module 查询）
   - `src/editor/weaver-editor/jsb_repl.cpp`（4 处 + ctor/dtor sink 改为继承 `IConsoleOutput`）
   - `src/editor/weaver-editor/jsb_statistics_viewer.cpp`（1 处：get_statistics）
7. 验证：`grep -rn "JsbBridgeTable\|EditorBridge\|get_bridge" src/` = 0。

## 阶段 3：单库入口与宏裁剪

1. `src/editor/register_editor_types.{h,cpp}`：删 `jsb_editor_library_init`、`_editor_tests_startup`、
   `../tests/jsb_test_runner.h` include；保留 `_initialize_godotjs_editor_module` /
   `_uninitialize_godotjs_editor_module`（按 level 分发）。
2. `src/runtime/register_types.cpp`：
   - `#if JSB_WITH_EDITOR` 下 include `editor/register_editor_types.h`，在
     `jsb_initialize_module` / `jsb_uninitialize_module` 末尾转调 editor 模块函数；
   - 删两处 `EDITOR_TEST_FLAG` meta 设置；`jsb_startup()` 改调 `jsb::tests::try_run()`。
3. `src/runtime/weaver/jsb_script_language.cpp`：`templates.gen.h` 的 include 门控
   `JSB_TOOLS` → `JSB_WITH_EDITOR`。
4. `src/tests/jsb_test_runner.h`：`try_run()` 去掉 flag 参数与双套件 meta 协调
   （删 `EDITOR_TEST_FLAG` / `RUNTIME_TEST_FLAG` / `TestResult`）。
5. 验证：template 产物符号面不含编辑器类：
   `dumpbin /symbols` 或 `strings` 搜 `GodotJSEditorPlugin` 在 template DLL 中为 0。

## 阶段 4：测试合并

1. 删除 `src/editor/tests/jsb_editor_test_main.cpp`、`src/editor/tests/test_jsb_editor_bridge.h`。
2. `src/runtime/tests/jsb_test_main.cpp`：`#if JSB_WITH_EDITOR` 下 include
   `editor/tests/test_jsb_editor_cleanup.h`。
3. `src/runtime/tests/test_jsb_bridge_table.h` → `test_jsb_runtime_api.h`：用例改为直接调用
   新 API；删随桥消失的用例（struct_size / 空槽 / nullptr 结果存储 / handle 幂等）。
4. 验证：`godot --headless --path ./project --jsb-run-tests` → exit 0、单段 doctest 汇总、
   `grep -c "Orphan StringName"` = 0。

## 阶段 5：跨库残留清理

1. `src/jsb.config.h`：删 `JSB_RUNTIME_API` 宏与相关注释段；`src/api_tool/core/api_tool_loader.h`、
   `src/internal/jsb_logger.h`、`src/runtime/internal/jsb_string_names.h`、
   `src/runtime/weaver/jsb_script_language.h` 去掉注解。
2. 注释事实修正：`jsb_script_language.h` 的 `get_singleton()` 注释、
   `src/runtime/bridge/jsb_shared_statics.h:58`。

## 阶段 6：部署面与文档

1. `project/addons/godotjs-ext.daylily-zeleen/`：删 `godotjs-ext-editor.gdextension`；
   主 gdextension 补 `linux.debug.editor.arm64`。
2. `project/.godot/extension_list.cfg` 重建为一行（本地状态，不入库）。
3. `.github/workflows/ci.yml`：benchmark job 删除 editor-extension 复制步骤与相关注释。
4. 文档：`README.md`（Dual GDExtension layout 段）、`CONTRIBUTING.md`（双套件描述）、
   `.trellis/spec/godotjs-ext/build/scons-build.md`、`test/doctest.md`、
   `cpp/architecture-constraints.md`（桥接节）、`cpp/api-tool-lazy-layout.md`（`editor_globs` 引用）。

## 阶段 7：端到端验证

1. `scons target=editor ... tests=yes -j5` → 部署两份产物（editor / template_debug）md5 与 addons 一致。
2. `godot --headless --path ./project --jsb-run-tests` → exit 0、无 Orphan、单套件。
3. `scons target=template_release ...` → 用 template_release 宿主跑 TS 集成测试
   （`godot --path ./project --verbose`，结尾哨兵 `GODOTJS_TEST_PROJECT_COMPLETED`）。
4. 编辑器功能实机验证：`godot --headless --editor --path project --quit` → 生成
   `.godot/extension_list.cfg` 一行、插件加载无错；`--godotjs-api-generate` 走通。
5. CI 面核对：`verify-release-artifacts` 的期望名列表不受影响（矩阵未改）。

## 回滚点

- 阶段 1 之后（构建接线）：`git checkout SConstruct`。
- 阶段 2–4（源码）：整体 revert 分支改动即可；无数据迁移。