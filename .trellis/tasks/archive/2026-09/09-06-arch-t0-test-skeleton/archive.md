# T0 测试骨架重组 与 V1 确定性预检

> 注：本文为历史归档；文中提及的 TASK_STATUS.md、.本地文档/ 等路径已不存在，现行机制见 `.trellis/spec/`。

> 归档说明：两项均已完成（2026-08-23）。第九章为 runtime/editor 双 doctest 套件设计，
> 第十一章为 codegen 确定性预检全过程与 T0 落地记录。当前态：runtime 27 用例 / editor 2 用例，
> 入口 --jsb-run-tests / --jsb-run-editor-tests。

---

## 九、C++ 测试拆分方案（runtime / editor）

> 结论先行：测试无法做成独立可执行文件（依赖运行中的引擎、ClassDB/Engine 单例与 `project.godot` 工程环境），维持「headless 启动 + 命令行开关 + SceneTree 退出码」模式；按目标库拆成两套 doctest 套件，各自持有物理入口，互不混跑。

### 9.1 现状
- 位置 `src/runtime/tests/`：1 个 main cpp（`jsb_test_main.cpp`，`DOCTEST_CONFIG_IMPLEMENT`）+ helpers + 8 个测试头（doctest，`third/doctest/doctest.h`）。
- 构建：SConstruct `tests=True` → 定义 `JSB_TESTS_ENABLED`，Glob `runtime/tests/*.cpp` 进当前唯一扩展库（SConstruct:780-783）。
- 入口：runtime `jsb_startup()`（`src/runtime/register_types.cpp:105-127`）扫描 `--jsb-run-tests` → `doctest::Context::run()` → `SceneTree::quit(exit_code)`（无主循环则 `std::exit`）。
- 内容归属：全部为 runtime 侧（path_util / sarray / source_map / any_runtime / qjs / v8 / shadow_realm）；尚无任何编辑器侧测试。`test_jsb_v8_runtime.h` 为 0 用例的注释壳（38 行）。

### 9.2 目标结构
```
src/testing/                     # 测试公共设施（双侧各编一份，仅 tests=True 参与）
  jsb_test_runner.h              #   header-only：try_run(flag) 参数解析+doctest Context+退出
  jsb_test_utils.h               #   从 jsb_test_helpers.h 抽出的引擎无关部分（计时/CWD 守卫/宏）
src/runtime/tests/               # runtime 套件（现有文件基本原位）
  jsb_test_main.cpp              #   runtime 侧 DOCTEST_CONFIG_IMPLEMENT + include 本套件全部测试头
  jsb_test_helpers.h             #   runtime 专属设施（GodotJSScriptLanguageIniter/V8ContextScope/StubBindings）
src/common/tests/                # （可选）无状态工具测试；短期可留在 runtime/tests 不动
src/godotjs_shared/tests/        # P2 后新增：api_tool core / StringNames / Logger 数据层测试
src/editor/tests/                # editor 套件（新建；T1 起放 codegen C++ 单测）
  jsb_editor_test_main.cpp       #   editor 侧 DOCTEST_CONFIG_IMPLEMENT
```
- 归属判据：被测代码在哪一侧，测试就放哪一侧；**godotjs-shared 与 src/common 的测试由 runtime 套件统一承载**（runtime 必然加载，且避免同一用例在两库里双跑）。
- `src/testing/` 对应 D3 的双侧共编哲学，但与生产代码分开存放，不混入 `src/common/`。

### 9.3 测试入口
| 套件 | 物理入口 | 触发参数 | 挂载时机 |
|---|---|---|---|
| runtime | `src/runtime/tests/jsb_test_main.cpp` | `--jsb-run-tests`（保留不变） | runtime entry 的 startup callback（即现 `jsb_startup()`，内部改为一行 `jsb::testing::try_run("--jsb-run-tests")`） |
| editor | `src/editor/tests/jsb_editor_test_main.cpp` | `--jsb-run-editor-tests`（新增） | editor 侧自己的 startup callback |

- 抽出 header-only `jsb::testing::try_run(flag)`：扫 `OS::get_singleton()->get_cmdline_args()` → 命中则构造 `doctest::Context` 执行 → 有 SceneTree 则 `quit(code)` 否则 `std::exit(code)`。两侧各调用一行，消除重复引导代码。
- editor 侧挂载（**T0 实施时修正**：原假设「godot-cpp 支持多次 `register_startup_callback`」不成立——`startup_func` 是覆盖式赋值（单次注册，覆盖不追加），见 godot-cpp `src/godot.cpp:296-298`；且单库形态下 `jsb_editor_library_init` 无人调用）：**P4 拆分前**入口函数 `_editor_tests_startup()` 实体放 editor 侧，由唯一的真实 startup callback（runtime `jsb_startup()`）在 TOOLS 形态下转调；**P4 拆分时**该函数改挂 `jsb_editor_library_init` 的 startup callback，转调删除。不可用 `MODULE_INITIALIZATION_LEVEL_EDITOR` 初始化回调充当入口（该时机主循环未就绪，拿不到 SceneTree）。
- 双 DLL 各自静态链 doctest 且各有 `DOCTEST_CONFIG_IMPLEMENT` TU：TEST_CASE 注册表按模块隔离，天然只收集本库用例；MSVC 符号默认不导出、不跨 DLL 冲突（Linux 侧确认 `-fvisibility=hidden` 即可）。**单库过渡形态**（P4 前）两套件共用唯一 `DOCTEST_CONFIG_IMPLEMENT` TU（`src/runtime/tests/jsb_test_main.cpp`），editor 用例经 `src/editor/tests/jsb_editor_test_main.cpp` 并入同一注册表；运行期以用例名首标签隔离：`--jsb-run-tests` → `-tc=[runtime]*`，`--jsb-run-editor-tests` → `-tc=[editor]*`（doctest `-ts` 过滤的是 TEST_SUITE 名而非用例名标签，勿混淆）。
- 明确不支持一次启动同跑两套件（进程退出码只能归一方）；CI 分两次 headless 调用分别触发。

### 9.4 SConstruct 改动
- `tests=True` 时：runtime 目标收 `src/runtime/tests/*.cpp`（不变）；P4 起 editor 目标收 `src/editor/tests/*.cpp`；两者均定义 `JSB_TESTS_ENABLED`、共用 `third/doctest` 头。P4 前的单库形态下，`editor/tests/*.cpp` 暂随 `godotjs_sources` 一并编译。
- 顺带清理已混入源码树的 `tests/jsb_test_main.windows.editor.dev.x86_64.obj` 残留（呼应 三 的 obj 清理注记）。

### 9.5 实施节奏（并入现有阶段，不新增顶层阶段）
| 步骤 | 时机 | 内容 |
|---|---|---|
| T0 | 随 P0/P1（现在） | 抽 `src/testing/` 公共头；建 `src/editor/tests/` 骨架与 editor 入口回调；两侧 main 改调 `try_run` —— 纯重组，行为零变化 |
| T1 | P1 内 | codegen C++ 重写的配套单测落 `src/editor/tests/`（生成器单元级断言，与 P0 端到端 diff 互补）；P1 验收追加「codegen 单测通过」 |
| T2 | P2 后 | 新增 `src/godotjs_shared/tests/`，归 runtime 套件执行 |
| T3 | P4/P5 | editor 套件随库迁移编译；CI 增加 `--jsb-run-editor-tests` 触发 |

### 9.6 注意事项
- helpers 依赖 cwd 位于工程根（`GodotJSScriptLanguageIniter` CHECK `project.godot` 存在）；editor 套件同样必须打开测试工程 headless 运行。
- 迁移时删除空壳 `test_jsb_v8_runtime.h`（或补实内容后再定）。
- `JSB_TESTS_ENABLED` 语义保持「本目标启用测试基建」；`jsb_source_map_cache.cpp` 等既有引用不受影响（均属 runtime）。

---
---

## 十一、V1 确定性预检执行记录（2026-08-23）

### 11.1 阻塞根因（已解）

- 现象：V1 首轮第三步 `--generate-types` 失败：`godot class not found 'GodotJSEditorHelper'`。
- 排查结论：**7-28 旧构建的引擎把 GodotJS 作为内嵌模块编入**（`modules/GodotJS/`，二进制内字符串证据确认），模块内 `GodotJSEditorHelper` 等用 `GDREGISTER_CLASS`（exposed）注册 → 进入 dump；现行 GDExtension 版改用 `GDREGISTER_INTERNAL_CLASS`（unexposed）→ 上游引擎按正确行为跳过，不写 `extension_api.json`。基线正是「内置模块时代」的产物。与本项目代码无关，但直接斩断 `--generate-types` 依赖链。
- 附带结论：7-28 引擎 × 当前扩展因 godot-cpp ABI 不兼容无法配对（插件实例化即崩，主检出复现），「换旧引擎跑校验」路径不可行。

### 11.2 修复（最小面）

- `src/editor/register_editor_types.cpp`：`GodotJSEditorHelper` 改回 exposed 注册（`GDREGISTER_CLASS`）。安全性依据：生成器 `NamingUtil::get_omitted_original_classes()` 硬编码过滤该类，不会进入生成产物；恢复的仅是「api store 里有此类供 JS 运行时解析」这一运行期前提——即内置模块时代的等价行为。
- `GodotJSEditorProgress` 在扩展代码中不存在（内置模块特有类），无需处理。
- `verify_codegen.py` 默认引擎保持指向当前 8-22 构建（修复后新引擎可用，旧引擎反而加载不了新扩展）。

### 11.3 首轮差异归因（35 处，全部非代码回归）

| 类别 | 内容 | 处置 |
| --- | --- | --- |
| 输入前提缺失 | 场景 d.ts 解析不到挂脚本类型（缺 `import Start …` 等）：测试项目 TS 未编译 | 已按手册 `pnpm install && npx tsc --noCheck` 编译；脚本注释补充此前提 |
| 基线漏收 | `gen/godot/.gdignore`：主检出既有产物同有此文件，基线副本漏收 | 手动补入基线 |
| 偶然输入条件 | `gen/godot/extension_api.json.gen.ts` 缺失：资源声明生成器会扫到项目根的 `extension_api.json` 并为其产出声明；基线生成时该文件恰在项目根，而校验链 api-generate 步消费删除它 | 脚本改为 dump 后暂存、api-generate 后放回（精确复现基线输入） |
| 行尾噪音 | typings 下 9 个文件纯 CRLF/LF 差异（git autocrlf 使工作区转 CRLF） | diff 按 CRLF→LF 归一化比较，行尾差异单独归类不计失败 |
| 基线过期 | godot0-5.gen.d.ts 分片含引擎文档注释漂移（当前引擎文档比基线生成时多性能指标等描述） | 属基线对旧引擎生成导致的过期，待双跑通过后决定是否刷新基线 |

### 11.4 校验脚本同步修正

- 暂存/放回 `extension_api.json`（见上表）；
  - **踩坑修正（第一轮 fixed 跑批）**：api-generate 步本身以该 json 为输入，dump 后直接移走会导致 api-generate 无输入失败、store 未建、JS 运行时无类数据连锁崩（`godot class not found 'Node'`）。正确做法：dump 后**复制备份** → api-generate 消费原文件 → 再把副本放回项目根。已改脚本并重跑。
- 归一化 diff + 「行尾差异(不计失败)」分类，退出码只看实质差异；
- tsconfig.json 比对同样按行尾归一化。

### 11.5 验收进度

- [x] 第一轮全流程（修复后脚本）跑通且差异仅剩已归因项：13 处（`.gdignore` 基线漏收 1 + typings 分片 12），无代码回归
- [x] 第二轮连跑（补 `.gdignore` 入基线后）：剩 12 处，全部为 typings 分片基线过期；**两轮产物交叉比对 66/66 文件行尾归一化后完全一致**，差异清单两轮相同 → 生成过程确定性成立（R4 无噪音）
- [x] 决定：刷新基线（用户 2026-08-23 拍板，执行见 11.8）

### 11.6 第二轮结果明细（2026-08-23）

- api-generate 步首次 exit=0；`extension_api.json.gen.ts` 正常产出并从差异清单消失（脚本备份/放回逻辑生效）。
- 剩余 12 处差异全部属「基线过期」：
  - `typings/godot0-5.gen.d.ts` 内容不同：引擎文档注释漂移（Performance 监控项描述等，当前引擎比基线生成时新增大量文档）；
  - `typings/godot6-11.gen.d.ts` 多余：新引擎类更多导致分片数从 6 涨到 12，基线只有旧分片。
- 结论：V1 确定性预检通过，可进入基线刷新决策 → T0 测试骨架重组。
- 过程留档：第一轮快照 `.agent_tmp/v1_round1_products/`（gen+typings 全量）；日志 `.agent_tmp/v1_fixed2_run1.log`、`v1_fixed2_run2.log`。


### 11.7 基线过期差异的语义级核查（2026-08-23，应用户要求出具明细）

对 12 处「基线过期」差异做类级语义比对（按大括号配对抽取每个类的代码块、剥离注释后比较成员签名）：

- 类集合：基线 1068 类 / live 1070 类；**新增 `Line3D`、`Trail3D`**，无移除；
- **共有类中成员签名有变化者仅 20 个**（如 `BaseButton.press()`、`DisplayServer.send_toast_notification`、`Label.get_maximum_font_size` 等），全部为 live 多出 API（新引擎功能增量），无删除/改签；
- 其余约 872 个共有类签名完全一致、仅文档注释有出入（引擎文档注释漂移）；
- 抽查确认上述新成员与两个新类**均存在于当前引擎 dump 的 `extension_api.json` 本身** → 差异源自引擎版本演进，非生成器行为变化；
- 分片布局说明：分片边界随类数变化整体洗牌（非字母序切分），同名文件 diff 会夸大差异，语义级比对才是有效口径。

结论不变且更精确：12 处差异 = 引擎版本演进（2 新类 + 20 个类的新增 API + 大量文档注释更新 + 分片重排），生成器零回归。刷新基线即可消除全部噪音。

### 11.8 基线刷新执行（2026-08-23，用户拍板）

- 数据源：V1 第二轮产物（`.agent_tmp/v1_fixed2_run2.log` 那次运行的 `project/gen`、`project/typings`），即「新引擎 + 修复后扩展」的确定性输出。
- worktree 侧：替换 `.本地文档/代码生成基线/{gen,typings}`（新基线 66 文件：gen/ 45 + typings/ 21，含 12 个 godot 分片与 `gen/godot/.gdignore`）。
- 主检出侧（`F:/GodotJS-Ext`）：同步替换 gen、typings、tsconfig.json；两侧 `diff -rq` 校验 **IDENTICAL**。
- 复验：worktree 内重跑 `verify_codegen.py --diff-only` → **EXIT=0 全绿**（仅剩行尾差异不计失败）。
- 后续注意：两侧基线此后以本次刷新为准，任一侧再更新须手动同步另一侧。

### 11.9 本地测试复验、提交、推送与 CI（2026-08-23）

- 本地 C++ 双套件复验（新引擎 + 含修复的 DLL，均满足手册验收项）：
  - `--jsb-run-tests`：exit=0，doctest **27/27 用例、443/443 断言全部通过**（日志 `.agent_tmp/cpp_tests_runtime_152905.log`）；
  - ~~`--jsb-run-editor-tests`~~ **勘误（2026-08-23 晚）：该参数当时并不存在**，实际执行的是 TS 集成测试（`--path ./project --verbose`）：exit=0，结尾 `GODOTJS_TEST_PROJECT_COMPLETED`、api_tool 正常终结，日志无 FAILED/泄漏痕迹（`.agent_tmp/cpp_tests_editor_153012.log`）。editor doctest 套件与该参数由 T0 补建（见 11.10）。
- 提交 `43ecda7`（register_editor_types.cpp exposed 修复）→ 推送 origin/refactor/split-editor-runtime（远端新建分支）。
- CI run **32625863900**：**completed success（6m45s，2026-08-23）**，含「Generate API data」步，本次修复路径经 CI 独立复核通过。
- 工作区其余 9 个改动文件系新引擎打开项目自动回填的资源 uid（.tscn 头部 / .ts 首行生成注释），非有意修改，不纳入提交。

### 11.10 T0 测试骨架重组实施（2026-08-23）

按第九章方案落地（第一个可提交节点，行为零变化 + 新增 editor 套件骨架）：

- 新增 `src/testing/`：
  - `jsb_test_runner.h`：header-only `jsb::testing::try_run(flag)`，扫命令行 → doctest Context → SceneTree::quit/std::exit；单库形态下按 flag 注入 `-tc=[runtime]*` / `-tc=[editor]*` 过滤。
  - `jsb_test_utils.h`：doctest 配置宏（原在 jsb_test_helpers.h）+ `JSB_TESTS_EXECUTION_SCOPE` 宏（自带 CONCAT 拼接，不再依赖 jsb.h 的 JSB_CONCAT）+ 引擎无关工具（ScopedTimer）。
- runtime 侧：`jsb_startup()` 内联引导代码改为一行 `try_run("--jsb-run-tests")`；`jsb_test_helpers.h` 只留引擎相关 fixture（StubBindings/Utils/CurrentWorkingDirectory/V8ContextScope/GodotJSScriptLanguageIniter）；`jsb_test_main.cpp` 保持唯一 `DOCTEST_CONFIG_IMPLEMENT` TU；全部 29 个用例名加 `[runtime]` 前缀标签；删除 0 用例空壳 `test_jsb_v8_runtime.h` 及 `jsb_test_main.windows.editor.dev.x86_64.obj` 残留。
- editor 侧：新建 `src/editor/tests/`（`jsb_editor_test_main.cpp` + `test_jsb_editor_skeleton.h` 2 个冒烟用例）；`_editor_tests_startup()` 实体在 `register_editor_types.cpp`，由 `jsb_startup()` 在 TOOLS+TESTS 形态下转调（**修正第九章假设**：godot-cpp `register_startup_callback` 为覆盖式赋值、且单库形态 `jsb_editor_library_init` 无人调用——详见 9.3 修正注记）。
- SConstruct：editor target 且 tests=True 时收 `src/editor/tests/*.cpp`；CPPPATH 增加 `src/testing`。
- 验证（新 DLL，headless 打开测试工程）：
  - `--jsb-run-tests`：exit=0，**27/27 用例、443/443 断言通过**（与重构前完全一致；2 skipped 为 editor 用例被过滤），无失败/泄漏痕迹（`.agent_tmp/t0_runtime_suite3.log`）；
  - `--jsb-run-editor-tests`：exit=0，**2/2 用例、2/2 断言通过**（27 个 runtime 用例被正确隔离跳过）（`.agent_tmp/t0_editor_suite2.log`）；
  - 首次实现踩坑记录：doctest `-ts` 过滤的是 TEST_SUITE 块名而非用例名标签（首跑 29 skipped 定位），改用 `-tc=[editor]*` 通配；`applyCommandLine` 需在 `run()` 前调用。
- 待办：提交推送后核查 CI（CI 暂只跑 `--jsb-run-tests`；`--jsb-run-editor-tests` 触发按 9.5/T3 于 P4/P5 接入 CI）。

