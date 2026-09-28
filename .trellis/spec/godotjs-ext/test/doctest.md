# C++ doctest 测试规范

> 构建前提 `tests=yes`。

## 形态

GDExtension 测试套件**不能是独立可执行文件**（依赖运行中的引擎、ClassDB/Engine 单例、`project.godot`）。已验证形态：headless 引擎启动 + 命令行标志 + doctest `Context` + `SceneTree::quit(exit_code)`，套件经构建开关编入扩展库本体。**单库单套件**（2026-09-28）。

## 核心机制（已核实，勿凭直觉推翻）

- 只有 `.gdextension` 文件 `entry_symbol` 指名的函数会被调用；别处的第二个 `*_library_init` 是死代码
- `InitObject::register_startup_callback` 是**覆盖式赋值**（godot-cpp `src/godot.cpp`），注册两次只保留最后一个
- 测试入口必须挂在**启动回调**上（主循环已存在 → `SceneTree::quit()` 可达）。绝不接 `MODULE_INITIALIZATION_LEVEL_*` 初始化器——那里没有主循环
- 入口模式：header-only `jsb::tests::try_run()`（`src/tests/jsb_test_runner.h`）扫命令行 → doctest Context → 有 SceneTree 则 quit 否则 `std::exit`

## doctest 注册表规则

- 每个链接模块**只能有一个 TU** 定义 `DOCTEST_CONFIG_IMPLEMENT`，第二个即重复符号链接失败
- 本库只有一个：`src/runtime/tests/jsb_test_main.cpp`。它在 `#if JSB_WITH_EDITOR` 下 include `editor/tests/*.h`，所以 editor 用例与 runtime 用例进**同一注册表**，一次 `Context::run()` 出一段汇总（3 行 `[doctest]`）
- 新增测试头：runtime 放 `src/runtime/tests/` 并在 `jsb_test_main.cpp` include；editor 放 `src/editor/tests/` 并在该文件的 `JSB_WITH_EDITOR` 段 include。**不要再新建第二个 IMPLEMENT TU**

- `-ts=NAME` 匹配 **TEST_SUITE 块名**，不是用例名内嵌的 `[tags]`。按用例名首标签过滤用通配：`const char *argv[] = {"suite", "-tc=[runtime]*"};` 且 `applyCommandLine` 必须在 `run()` 前

## 布局与归属

- `src/tests/`：双侧编译的公共设施（header-only runner），仅 `tests=True` 参与
- 被测代码在哪侧，测试放哪侧（runtime 归 `src/runtime/tests/`、editor 归 `src/editor/tests/`）
- **src/internal、src/compat（无状态工具层）的测试由唯一测试套件承载**
- 测试源码用构建选项（`tests=True`）+ 宏（`JSB_TESTS_ENABLED`）双重门控；绝不把 `tests/*.cpp` 无条件 glob 进发布构建
- 空的占位测试头直接删除，不要迁移

## 触发与验收

```
godot --headless --path ./project --jsb-run-tests
```
**单套件**（2026-09-28 合并为单库后）：`jsb::tests::try_run()` 由唯一启动回调 `jsb_startup` 调用，扫命令行 → 建 doctest Context → `context.run()` → 有 SceneTree 则 `quit(exit_code)` 否则 `std::exit(exit_code)`。`EDITOR_TEST_FLAG` / `RUNTIME_TEST_FLAG` / `TestResult` 三个 `Engine` meta 与双套件协调逻辑已删除（不再需要"等另一方完成"）。`--jsb-run-tests` 名与验收口径不变。

验收标准：**exit code == 0 且无泄漏**（无未释放 Resource、无 Orphan StringName）。

## 陷阱

- 测试辅助要求 cwd == 项目根（CHECK `project.godot`）；headless 一律带 `--path <project>`
- **扩展未加载 ⇒ 套件整段不跑**：`--jsb-run-tests` 只能触发已加载扩展；`project/.godot/extension_list.cfg` 缺失或指向不存在的 `.gdextension`（应一行，见 build spec）时没有任何 doctest 输出且无报错。判别：doctest 汇总为 0 段
- **测试头会编进任意 target，引用 `JSB_TOOLS` 专属符号的用例必须自行 `#if JSB_TOOLS`**：
  `src/runtime/tests/*.h` 被 `jsb_test_main.cpp` **无条件** include（只有 editor 用例段带 `JSB_WITH_EDITOR`），
  而 `tests=yes` 对 `target=editor` 与 `target=template_*` **都**成立（`SConstruct`）。`JSB_TOOLS` 镜像 godot-cpp 的
  `TOOLS_ENABLED`，模板腿为 0 ⇒ 依赖它的整条链路（`ScriptDocStore` / `ScriptDocEntry` / `_get_documentation()`）
  在模板腿不存在。实例：`test_jsb_static_members.h` 的两个 source-docs 用例曾因此让
  `scons target=template_release tests=yes` 报 33 个 `error C`。**CI 只给 editor 腿传 `tests=yes`**
  （`.github/actions/scons-build/action.yml` 的 `editor` profile），所以这类缺陷在 CI 上不可见——本地跑
  `target=template_release tests=yes` 才是判别手段。
- 目录重组时清理混入源码树的 `.obj` 残留
