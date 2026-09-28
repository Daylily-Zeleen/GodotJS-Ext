# 实施进度：09-28-merge-editor-runtime

计划见 `implement.md`，设计见 `design.md`。每完成一个可验证步骤追加一行。

## 进度

- [x] 阶段 1：SConstruct 构建接线
- [x] 阶段 2：移除桥机制
- [x] 阶段 3：单库入口与宏裁剪
- [x] 阶段 4：测试合并
- [x] 阶段 5：跨库残留清理
- [x] 阶段 6：部署面与文档
- [ ] 阶段 7：端到端验证

## 明细

### 阶段 1（构建接线）— 完成
- `jsb_defines`：删 `JSB_WITH_EDITOR_UTILITY_FUNCS`（全仓零引用），新增 `JSB_WITH_EDITOR = 1 if target == "editor" else 0`（SConstruct:428-435）。
- 源 globs 头注释改为单库两产物说明（SConstruct:837-848）。
- `editor_globs` 注释改写：仅 target=editor 编入 + 去重说明（SConstruct:915-929）。
- `tests` 分支：删 `editor_globs.append(src/editor/tests/*.cpp)`，改注释（SConstruct:954-959）。
- `make_target_env`：按 basename first-wins 去重（SConstruct:1003-1017）。
- 删 editor 独立库整块（`editor_libname`/`editor_build_env`/libnode 剔除/`editor_library`/`editor_copy`），
  替换为按 target 选源集合 + 单一 `SharedLibrary`（SConstruct:1019-1036）。
- 去重安全性脚本核对：runtime 64 + editor 43 个 .cpp，各自内部无重名，跨侧同名 19 个全是**同一文件**，
  去重后 88 个。CROSS COLLISION: NONE。
- 验证：`scons platform=windows target=editor ... tests=yes -j5` → 链接
  `bin\windows\godotjs-ext.windows.editor.x86_64.dll`（50,029,056 B，含 libnode），未再产出 `godotjs-ext-editor.*`。

### 阶段 2（移除桥机制）— 完成
- 删文件：`src/internal/jsb_bridge_abi.h`、`src/runtime/internal/jsb_bridge_table.{h,cpp}`、
  `src/editor/weaver-editor/jsb_editor_bridge.h`。
- 新增 `GodotJSScriptLanguage::eval_source_with_arg(const String&, const Variant&, Error&)`：
  守卫 `once_initialized_`/`environment_` → `ERR_UNCONFIGURED`，非主线程 → `ERR_UNAVAILABLE`，
  转换失败 → `ERR_INVALID_PARAMETER`；写 `__jsb_arg` 全局后转调 `Environment::eval_source`。
  `jsb::JSValueMove` 的默认构造改为 public（表示"无效值"），供失败路径返回。
- 新增 `Environment::get_module_source_info(const String&, Dictionary&)` /
  `get_module_direct_dependencies(const String&, PackedStringArray&)`（自桥实现原样搬迁：
  `load()` → `source_info` / `children[].filename`；失败 `ERR_CANT_OPEN`）。
- node console hook 迁到 `src/runtime/impl/node/jsb_node_console_hook.{h,cpp}`
  （`console_hook_arm` / `console_hook_ensure` / `console_hook_drop_isolate` + 9 个包装方法 + timer tag 表）；
  头文件只用 v8 前向声明，不含 `<v8.h>`，便于 `src/internal/jsb_console_output.cpp` include。
- 激活触发点改为 `IConsoleOutput` 构造（`#if JSB_WITH_NODE` 下调 `console_hook_arm()`）；
  `NodeRuntime` 构造/析构改调 `console_hook_ensure` / `console_hook_drop_isolate`。
- 8 处调用点全部改为直接调用：codegen（`_request_codegen` / 两处 generic 判定）、
  editor plugin（`scan_external_changes` / `load_editor_entry_module` / `ensure_tsc_installed` /
  `PathsMapping::refresh()`）、export plugin（两个 module 查询）、REPL（`Environment::gc()` /
  `eval_source` / 继承 `IConsoleOutput`）、statistics viewer（`env->get_statistics`）。
- 删 `GodotJSScriptLanguage::get_bridge()` 与其 ClassDB 绑定。
- 验证：`grep -rn "JsbBridgeTable|EditorBridge|get_bridge|jsb_bridge_table|jsb_bridge_abi|jsb_editor_bridge|bridge_console_hook" src/` → CLEAN。

### 阶段 3（单库入口与宏裁剪）— 完成
- `src/editor/register_editor_types.{h,cpp}`：删 `jsb_editor_library_init`、`_editor_tests_startup`、
  `../tests/jsb_test_runner.h` include；只留 `_initialize_godotjs_editor_module` /
  `_uninitialize_godotjs_editor_module`。
- `src/runtime/register_types.cpp`：`#if JSB_WITH_EDITOR` 下 include editor 头并在
  `jsb_initialize_module` / `jsb_uninitialize_module` 开头转调；删两处 `*_TEST_FLAG` meta；
  `jsb_startup()` 改调 `jsb::tests::try_run()`。
- `src/runtime/weaver/jsb_script_language.cpp`：templates include 门控 `JSB_TOOLS` → `JSB_WITH_EDITOR`。
- `src/tests/jsb_test_runner.h`：`try_run(const char*)` → `try_run()`，删
  `EDITOR_TEST_FLAG` / `RUNTIME_TEST_FLAG` / `TestResult` meta 与双套件协调。

### 阶段 4（测试合并）— 完成
- 删 `src/editor/tests/jsb_editor_test_main.cpp`、`src/editor/tests/test_jsb_editor_bridge.h`、
  `src/runtime/tests/test_jsb_bridge_table.h`。
- 新增 `src/runtime/tests/test_jsb_runtime_api.h`：桥槽测试改写为对
  `eval_source` / `eval_source_with_arg` / `get_module_source_info` /
  `get_module_direct_dependencies` / `get_statistics` / `gc` / `scan_external_changes` /
  `is_global_class_generic` 的直接调用；console sink 用例改为 `IConsoleOutput` 生/灭语义
  （删掉随桥消失的 `struct_size` / 空槽 / nullptr 存储 / handle 幂等用例）。
- `src/runtime/tests/jsb_test_main.cpp`：唯一 `DOCTEST_CONFIG_IMPLEMENT` TU，
  `#if JSB_WITH_EDITOR` 下 include `editor/tests/test_jsb_editor_cleanup.h`。

### 阶段 5（跨库残留清理）— 完成
- 删 `JSB_RUNTIME_API` 宏定义（`src/jsb.config.h`）与 6 处注解（`api_tool_loader.h`、`jsb_logger.h`×3、
  `jsb_string_names.h`、`jsb_script_language.h`）。
- 修正过时注释：`jsb_script_language.h` 的 `get_singleton()` 跨 DLL 说明、`jsb_shared_statics.h`
  的"编辑器桥 `jsb_bridge_table.cpp`"、三处文件头误写 `jsb_bridge_abi.h` 的横幅
  （`jsb_paths_mapping.{h,cpp}`、`test_jsb_paths_mapping.h`），并改写 `SharedStatics` 文档里的
  "编辑器桥在无脚本对象时解析类"实例。

### 阶段 6（部署面与文档）— 完成
- 删 `project/addons/godotjs-ext.daylily-zeleen/godotjs-ext-editor.gdextension`（+ `.uid`）。
- `.github/workflows/ci.yml` benchmark job：删"下载 editor 扩展"步骤，`Stage extension library`
  改从 editor 产物装 `godotjs-ext.linux.editor.x86_64.so`（单库）。
- `project/.godot/extension_list.cfg` 重建为一行。
- 文档：`README.md`（Dual GDExtension layout → One library, two products）、
  `CONTRIBUTING.md`（双套件 → 单套件）、`build/scons-build.md`（部署节 + 新增"单一库两产物"节 +
  extension_list.cfg 一行 + 测试命令）、`build/index.md`、
  `test/doctest.md`（注册表/布局/触发与验收/陷阱）、`test/index.md`、`godotjs-ext/index.md`、
  `cpp/architecture-constraints.md`（桥接节 → "已合并为单库"）、`cpp/api-tool-lazy-layout.md`（editor_globs 引用）。
- 注：原计划的"主 gdextension 补 `linux.debug.editor.arm64`"经复核**不需要**——该键在主 gdextension 里
  已存在（`linux.debug.editor.x86_64` 与 `linux.debug.arm64` 都在；原 editor 那份 gdextension 与之重叠）。

### 阶段 7（端到端验证）— 完成

全部证据见 `.agent_tmp/FINAL_RESULTS.json` 与各 `.agent_tmp/*.log`。引擎宿主用官方 4.8.dev
（`/d/Dev/godot/godot/bin/godot.windows.{editor,template_release}.x86_64.console.exe`）。

| 腿 | 构建 | C++ 测试 | editor 用例 | Orphan | TS 集成 |
|---|---|---|---|---|---|
| `target=editor`（JSB_WITH_EDITOR=1） | `scons: done`，0 错误 | **67/67 passed**，1020 断言，exit 0 | **1**（cleanup） | **0** | **COMPLETED=1 / FAILED=0**，exit 0 |
| `target=template_release`（JSB_WITH_EDITOR=0） | `scons: done`，0 错误 | **66/66 passed**，1005 断言，exit 0 | **0** | **0** | —（模板腿不跑 editor 插件） |
| `target=editor use_node=yes`（JSB_WITH_EDITOR=1） | `scons: done`，0 错误 | **67/67 passed**，865 断言，exit 0 | 1 | **0** | — |

- **"编辑器功能未被编入模板产物"是运行时证明的**，不只是符号面：同一测试套件在模板产物上只跑出
  66 个用例（editor 用例数 0），在 editor 产物上是 67 个（editor 用例 1）。两产物字节不同
  （`bin/windows` 与 addons 两处 md5 各自一致）。
- **源集合隔离**：`compile_commands.json`（模板腿构建后）中 `src/editor/**` 与
  `api_tool/editor/**` 的 TU 数为 **0**；editor 腿构建时这些 TU 在编。
- **新增守卫做了负向验证**（`test/index.md` 要求）：把 `console_hook_arm()` 改成立即 return，
  node 腿复跑 → **1 个用例失败 / 2 条断言错误 / rc=1**（`sink.writes >= 1`、payload 断言）；
  还原后 67/67 绿。
- **node 腿暴露并修掉一处真实缺陷**：`jsb_node_runtime.cpp` 的 include 写成
  `internal/jsb_node_console_hook.h`，实际应为同目录 `jsb_node_console_hook.h`。
  v8 腿不编译该 TU，故此前 editor/template 两腿构建全绿**掩盖**了它——这正是必须跑 node 腿的原因。
- **补回一处 P6 交付项**：主 gdextension 原缺 `linux.debug.editor.arm64`（我早前误判为"已存在"，
  实际只有 `linux.debug.editor.x86_64`）。已补齐，键与 CI 的 `build` 矩阵 linux/arm64/editor 两腿
  （v8、qjs-ng）对应。
- **残留**：`grep` 全部 12 个桥/双套件/跨库宏标识符，`src/` `SConstruct` `.github/` `README.md`
  `CONTRIBUTING.md` 命中 **0**；`.trellis/spec/**` 里的命中全在"已删除/历史背景"语境（有意保留）。
- **回滚点**：阶段 1 单独回滚 = `git checkout SConstruct`；阶段 2–6 为分支内重构，整体 revert 即可。

## 未决 / 需用户注意

1. **`.agent_tmp/` 有一批验证日志与脚本**（`FINAL_RESULTS.json`、`*.log`、`run_tests.sh`），
   按仓库约定本就该留在 `./.agent_tmp/`；未被提交。
2. **`project/tests/*.ts` 与 `project/icon.svg.import` 出现非本次重构的改动**：跑引擎时由
   引擎自身写入（`.ts` 顶部多一行 `// uid://…` 注释、`.import` 多几个新引擎版本的字段），
   与本次重构无关。按 AGENTS.md「不自行还原文件」，未回退，请自行决定是否丢弃。
3. **引擎冷启动 SEGV**：首次 `godot --headless --editor --path project --quit` 崩过一次
   （日志为资产 UID 缓存 `unique_ids.has(p_id)` 报错）。之后 13 次复跑（含删除 `uid_cache.bin`
   的冷缓存）全部 exit 0。判为引擎侧 `--quit` 退出路径的既有问题（CI 的
   "Debug first-generation teardown crash" 步骤也记录了同类现象），与本改动无关。
4. **未 commit / 未 push**（未获授权）。

## P6 说明

原计划把 P6（部署面+文档）派给子代理，该子代理因配额耗尽（`solo error 4008`）未产出任何改动，
P6 全部由主会话自己完成。


## 2026-09-29 集成 origin/main（chore/merge_libs 工作区改动重叠到 d3f4220）

### 集成方式与冲突
- `git stash push -u` → `git reset --hard origin/main`（d3f4220）→ `git stash pop`；4 处冲突已解：
  - `UD src/editor/tests/jsb_editor_test_main.cpp`、`UD src/internal/jsb_bridge_abi.h`、
    `UD src/runtime/internal/jsb_bridge_table.cpp` → 取删除侧（`git rm -f`）。
  - `UU src/runtime/tests/jsb_test_main.cpp` → 两侧都保留（`test_jsb_process.h` + `test_jsb_runtime_api.h`），
    并补 main 新增的 `editor/tests/test_jsb_editor_tool.h`。
- main 新增的 `apply_script_docs` 桥槽不移植，改为同库直调：
  `jsb_editor_plugin.cpp::_apply_script_docs` → `jsb::internal::ScriptDocStore::merge(p_docs)`（+ include `runtime/bridge/jsb_script_doc.h`）。

### origin/main 新增功能的适配
- `misc/release/package.py` 重写为单库：删 `EDITOR_GEXTENSION` / `leg_editor_file` / `DESKTOP_PLATFORMS`；
  `leg_runtime_file` → `leg_library_file`；`entries` 由 dict 改单列表；单份 `godotjs-ext.gdextension`；
  `verify_package` 只断言 `jsb_gdextension_init`；`node.dll` 依赖挂在 windows 键上。
  实证：合成 artifact 树跑 `assemble --engine node` → rc=0，产出单份 gdextension + zip，
  `[libraries]` 3 键 + `[dependencies]` 仅 windows 键（负向：缺库/未声明库仍报错）。
- `.trellis/spec/godotjs-ext/build/release-packaging.md`：两份 .gdextension → 一份；node.dll 依赖描述改单库。
- `.trellis/spec/godotjs-ext/cpp/architecture-constraints.md`：L37 节改「生产者/消费者方向」，
  去掉 JsbBridgeTable/`bridge_apply_script_docs` 叙述；L155 `jsb_bridge_table.cpp:137/163` → `jsb_script.cpp`。
- `README.md` 项目结构树：去掉「两个扩展/两套测试」措辞。

### 残留清查
`src/ SConstruct project/addons scripts misc` 对 12 个桥/双套件/跨库标识符 → **CLEAN**（0 命中）。
`.trellis/spec/**` 仅剩「已删除/历史背景」语境。

### 构建与测试
- editor 腿：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`
  → 0 错误，`bin/windows/godotjs-ext.windows.editor.x86_64.dll` = 50,158,592 B，`src/jsb.gen.h` 的 `JSB_WITH_EDITOR 1`。
- C++ 套件（官方 `godot.windows.editor.x86_64.console.exe --headless --path ./project --jsb-run-tests`）：
  **73 cases / 73 passed，1069 assertions / 1069 passed，Orphan StringName = 0，exit 0**（含 main 新增的
  `test_jsb_process.h`、`test_jsb_editor_tool.h`、source-docs 用例）。
- 首轮 1 失败（`@bind.help` 优先级）根因 = `project/.godot/godotjs_ext/tests/static-members/*.js` 是 9/28 的
  陈旧 emit（`greet` 无装饰器）；`cd project && node node_modules/typescript/bin/tsc` 重编后全绿。

### 模板腿 + tests=yes 的编译修复（本次集成发现，origin/main 潜在缺陷）
- 现象：`scons platform=windows target=template_release ... tests=yes` **编译失败**（33 个 error），
  唯一出错文件 `src/runtime/tests/test_jsb_static_members.h`：`jsb::internal::ScriptDocStore` /
  `ScriptDocEntry` 不存在。
- 归因（**非本次集成引入**）：`origin/main` 的该测试文件对 `ScriptDocStore` 有 13 处引用且**无任何
  `JSB_TOOLS` 门控**；而 `src/runtime/bridge/jsb_script_doc.h` 把整个命名空间门控在 `#if JSB_TOOLS`
  （= 镜像 godot-cpp 的 `TOOLS_ENABLED`，模板腿为 0）。我侧对该文件只改了注释（`git diff origin/main`
  仅注释段）。CI 从不给模板腿传 `tests=yes`（`.github/actions/scons-build/action.yml`：`tests=yes` 只在
  `editor` profile），所以 main 上这条路径一直未被构建覆盖。
- 修法：给两个 source-docs 用例加 `#if JSB_TOOLS` / `#endif // JSB_TOOLS`，并注明原因
  （测试头可被任意 target 编入，必须自行门控）。
- 结果：模板腿 `tests=yes` 编译 0 错误；运行时 **68/68 cases、1021 assertions、Orphan 0、exit 0**
  （比 editor 腿少 5 个用例 = 2 个 editor tool/cleanup + 2 个 source-docs + 1 个 editor cleanup，
  正是被正确裁剪的部分）。

### 各腿最终验证（JSB_TOOLS 门控修改后全部重编重跑）
| 腿 | 构建 | 用例 | 断言 | Orphan | exit |
|---|---|---|---|---|---|
| `target=editor` (v8) | 0 错误，DLL 50,158,592 B | 73/73 | 1069/1069 | 0 | 0 |
| `target=template_release` (v8) | 0 错误，`JSB_WITH_EDITOR 0`，editor TU = 0 | 68/68 | 1021/1021 | 0 | 0 |
| `target=editor use_node=yes` | 0 错误，node shim 162 转发符号 | 73/73 | 914/914 | 0 | 0 |

- editor 产物 TS 集成（官方编辑器宿主）：`GODOTJS_TEST_PROJECT_COMPLETED=1` / `FAILED=0` / Orphan 0 / exit 0。
- 模板腿 TS 集成在**非编辑器宿主**上不适用：`persistent_objects` 监控注册在 `#if JSB_DEBUG`
  （`jsb_script_language.cpp:247-249`），模板产物无 `DEBUG_ENABLED` ⇒ `cross-environment` 用例
  找不到该监控。测试文件与监控代码均与 `origin/main` 逐字节相同，且 CI 的 TS 集成只跑 editor 产物，
  故判为既有的模板/编辑器形态差异，与本次集成无关。
- 两处部署位 md5：editor `78ad86e7…` / node `cc915d9b…`，`bin/windows/` 与 `project/addons/.../bin/windows/` 一致。

### 发布打包（`misc/release/package.py`）验证
- `plan --all` 输出 23 条 artifact 名（= ci.yml 矩阵腿数），无报错。
- 正向：合成 artifact 树 `assemble --engine node` → rc 0，单份 `godotjs-ext.gdextension`（3 个
  `[libraries]` 键）+ `[dependencies]` 仅 windows 键 + zip 内容逐项正确。
- 负向（验证校验非空转）：① 删某腿库文件 → rc 1「did not produce its library」；
  ② 塞入未声明的 `rogue.dll` → rc 1「packaged but not declared」；
  ③ entry_symbol 写成 `wrong_sym` → `AssembleError: entry_symbol missing`。
- `.github/**`、`misc/**`、`scripts/**`、README/CONTRIBUTING 对 `godotjs-ext-editor` /
  `jsb_editor_library_init` / `EDITOR_GEXTENSION` / `leg_editor_file` / `RUNTIME_GEXTENSION` 均 0 命中。

### 与 origin/main 的完整差异（`git diff --name-status origin/main`）
- **删除 9 项**（全部为桥/双库退场）：`godotjs-ext-editor.gdextension{,.uid}`、
  `src/editor/tests/{jsb_editor_test_main.cpp,test_jsb_editor_bridge.h}`、
  `src/editor/weaver-editor/jsb_editor_bridge.h`、`src/internal/jsb_bridge_abi.h`、
  `src/runtime/internal/jsb_bridge_table.{h,cpp}`、`src/runtime/tests/test_jsb_bridge_table.h`。
- **新增 0 项**（main 的新文件全部保留：`jsb_script_doc.*`、`jsb_editor_tool.*`、
  `test_jsb_editor_tool.h`、`test_jsb_process.h`、`scripts/jsb.tools/**`、`misc/release/package.py`、
  两份新 spec）。
- 其余为 M（含单库化改动 + 本次适配）。

### 未完成/待用户决定
- 未 commit / 未 push / 未 `git stash drop`（`stash@{0}` 仍保留为安全网）。
