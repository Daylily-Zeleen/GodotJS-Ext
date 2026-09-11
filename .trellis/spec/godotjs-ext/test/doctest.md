# C++ doctest 测试规范

> 构建前提 `tests=yes`。

## 形态

GDExtension 测试套件**不能是独立可执行文件**（依赖运行中的引擎、ClassDB/Engine 单例、`project.godot`）。已验证形态：headless 引擎启动 + 命令行标志 + doctest `Context` + `SceneTree::quit(exit_code)`，套件经构建开关编入扩展库本体。

## 核心机制（已核实，勿凭直觉推翻）

- 只有 `.gdextension` 文件 `entry_symbol` 指名的函数会被调用；别处的第二个 `*_library_init` 是死代码
- `InitObject::register_startup_callback` 是**覆盖式赋值**（godot-cpp `src/godot.cpp`），注册两次只保留最后一个
- 测试入口必须挂在**启动回调**上（主循环已存在 → `SceneTree::quit()` 可达）。绝不接 `MODULE_INITIALIZATION_LEVEL_*` 初始化器——那里没有主循环
- 入口模式：header-only `jsb::tests::try_run(flag)`（`src/tests/jsb_test_runner.h`）扫命令行 → doctest Context → 有 SceneTree 则 quit 否则 `std::exit`

## doctest 注册表规则

- 每个链接模块**只能有一个 TU** 定义 `DOCTEST_CONFIG_IMPLEMENT`，第二个即重复符号链接失败
- 双 DLL 各自静态链 doctest、各有 IMPLEMENT TU，注册表按模块隔离；MSVC 符号默认不导出不跨 DLL 冲突
- 单进程跑双套件是允许的，但绝不把两套件合并进同一个 `doctest::Context`——两套 Context 注册表互不可见，跨套件协调（完成位/退出码）只经 `Engine` 单例 meta，见「触发与验收」

- `-ts=NAME` 匹配 **TEST_SUITE 块名**，不是用例名内嵌的 `[tags]`。按用例名首标签过滤用通配：`const char *argv[] = {"suite", "-tc=[runtime]*"};` 且 `applyCommandLine` 必须在 `run()` 前

## 布局与归属

- `src/tests/`：双侧编译的公共设施（header-only runner），仅 `tests=True` 参与
- 被测代码在哪侧，测试放哪侧（runtime 归 `src/runtime/tests/`、editor 归 `src/editor/tests/`）
- **src/internal、src/compat（双侧编译的无状态工具）的测试由 runtime 套件统一承载**（runtime 必然加载，避免用例在两库里双跑）
- 测试源码用构建选项（`tests=True`）+ 宏（`JSB_TESTS_ENABLED`）双重门控；绝不把 `tests/*.cpp` 无条件 glob 进发布构建
- 空的占位测试头直接删除，不要迁移

## 触发与验收

```
godot --headless --path ./project --jsb-run-tests
```
**单 flag 双套件**（5a58912 起）：一个 `--jsb-run-tests` 同时跑 runtime + editor 两套件——两 DLL 启动回调各自 `jsb::tests::try_run`，经 `Engine` 单例 meta（`RuntimeTest`/`EditorTest` 完成位 + `TestResult` 退出码）协调，双方都完成后由后完成者 `SceneTree::quit`，退出码取首个非零值。旧 `--jsb-run-editor-tests` 与"每套件单独调一次 headless"的模式已废弃。

验收标准：**exit code == 0 且无泄漏**（无未释放 Resource、无 Orphan StringName）。

## 陷阱

- 测试辅助要求 cwd == 项目根（CHECK `project.godot`）；headless 一律带 `--path <project>`
- **editor 套件静默缺席**：`--jsb-run-tests` 只能触发已加载扩展的套件；`project/.godot/extension_list.cfg` 缺 editor 行（应两行，见 build spec）时 editor 套件整段不跑且无任何报错。判别：日志无 `viaEditorTest`、doctest 汇总只有一段
- 目录重组时清理混入源码树的 `.obj` 残留
