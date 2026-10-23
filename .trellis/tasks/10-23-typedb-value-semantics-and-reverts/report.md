# 进度报告：codegen TypeDB 值语义化与排查期改动收口

> 每完成一个可验证步骤追加一行。裁决与证据都落这里，不落对话。

## 背景（来自用户 2026-10-23 的两条指示）

1. 做 TypeDB 值语义化（含删除 `~TypeDB()`）——用户原话：值语义下原有清理已无意义。
2. 上一轮那 6 条质问对应的改动继续处置。
3. 建 Trellis 任务（本任务）。

## 规划阶段（已完成）

- 创建任务 `10-23-typedb-value-semantics-and-reverts`（原 `.trellis/tasks/10-18-clean-project-pipeline` 的 §60 收尾内容并入本任务）。
- `prd.md` / `design.md` / `implement.md` 已写；`implement.jsonl`（8 条）与 `check.jsonl`（4 条）已配好，`task.py validate` 通过。

### 现状核实（本轮只读取证，逐条有命令依据）

| 项 | 状态 | 依据 |
|---|---|---|
| TypeDB/DocCache 存指针 + `owned_*` 手工所有权 | **待改造（本任务 A）** | `jsb_codegen_type_db.h:193-197,267-271` |
| `~TypeDB()` 已加、`destroy_typedb()` 已删（上一轮） | 改造后一并去掉 | 工作树 diff |
| Q1 `Environment::~Environment` 的 `deferred_class_post_binds_.clear()` | **已回退到 HEAD** | `jsb_environment.cpp` 不在 diff；grep 无 dtor 内 clear |
| Q2 `p_keep_owner` / `default_objects_` / `release_default_objects()` | **已回退到 HEAD** | grep 零命中 |
| Q3 `jsb_shadow_realm.cpp` lazy 缓存与 `purge_symbol_cache_isolate` 改名 | **已回退到 HEAD** | grep 零命中 |
| Q4 `jsb_codegen_scene_descriptors.cpp` 的 `memdelete` | **已回退到 HEAD**（`queue_free()`，行 356/428） | grep |
| Q5 `api_tool_loader.cpp` 的 7 处 `if (!loaded_) return;` | **已回退到 HEAD** | grep 零命中 |
| Q5 `api_tool.cpp` 访问器改为静默返回 | **待处置（本任务 B2）** | `api_tool.cpp` 工作树 diff |
| Q6 `ensure_tsc_installed()` headless 阻塞 | **待处置（本任务 B1，含选项待确认）** | `jsb_editor_plugin.cpp` 工作树 diff |
| `register_types.cpp` 的 `ClassDB::deinitialize(CORE)` | **待撤销（本任务 B3）**：实测未修复 step4/6 崩溃 | 该 diff + 上一轮冷轮日志 |
| 引擎工作树 `godot-4.7.2-patched` 的 `close_library` 补丁 | **已被我移除，待恢复（本任务 B4）** | `git status` 只有 `editor/doc/editor_help.cpp`；grep 无 `_free_instance_binding_of_extension` |
| 主工作树 `D:/Dev/godot/godot` 两份补丁 | 齐全，不动 | grep 命中 |

### 关键机制结论（本轮复核，供回归判读）

- step4/6 重新崩 `0xC0000005` 的原因：引擎工作树缺 `close_library()` 的卸载前 instance-binding 清扫 ⇒ 卸载后回调本 DLL 的 `free_callback`；dump 里 fault module 为 `<none>`（地址不在任何已加载模块内）。
- godot-cpp `HashMap` 的 entry 是独立堆节点、重哈希复用节点、迭代即插入序 ⇒ 值语义不损失地址稳定性与顺序，改造可行（证据见 design §A.0）。

## 待用户确认（进入实现前的闸门）

- B1 的实现位置（选项 1：阻塞留在 `ensure_tsc_installed()`；选项 2：恢复到调用方 `_install_project_files_from_cmdline()`）——计划默认选项 2。
- B4 需要动仓库外工作树 `D:/Dev/godot/godot-4.7.2-patched`（恢复补丁 + 重建引擎）。

## 执行记录

- [0] 用户裁定：B1 采**选项 2**（阻塞装包下移到命令行入口）；批准开工。`task.py start` → 状态 in_progress（无 session identity，degraded，不影响）。
- [0.2] 恢复引擎补丁：`D:/Dev/godot/godot-4.7.2-patched` 上 `git apply --include=core/extension/gdextension.cpp <engine_patches/godot-4.8-dev-extension-unload-and-doc-guard.patch>` → APPLIED；`git status` = `core/extension/gdextension.cpp` + `editor/doc/editor_help.cpp`；`grep` 命中 `_free_instance_binding_of_extension`(827) / `ObjectDB::debug_objects`(842)。
- [0.3] 引擎重建完成（后台 job，`scons platform=windows target=editor use_llvm=yes debug_symbols=yes -j16`，耗时 30.72s）：日志含 `Compiling core\extension\gdextension.cpp` + 两次 `Linking Program`。主工作树 `D:/Dev/godot/godot` 未动。
- [1] 派 `trellis-implement`（`ImplTypedbValueSem`）执行 A + B 的代码改动 + canonical 编译 + 双位部署（验收留给 check 阶段的说明已写入派单）。结果：A 的 5 个文件（`jsb_codegen_type_db.h/.cpp`、`jsb_codegen_docs.h/.cpp`、`jsb_codegen_generator.cpp`）改值语义；B3 删 `ClassDB::deinitialize(CORE)`（该文件回到 HEAD）；B2 加 `report_loader_unavailable()`（禁无消息 return）；B1 采选项 2。编译成功，md5 `b1597e27...`（双位一致）。
- [2] **A 的判据：同输入 A/B 差分（产物零回归）**
  - 备份值语义版 5 文件到 `.agent_tmp/value_src/`；用 `git show HEAD:<path>` 换入**指针版**（HEAD 无 `~TypeDB()`、含 `destroy_typedb`）；`scons ... -j6`（29.8s）+ 部署。
  - 指针版跑 `--generate-types`：exit=0，**ERROR=2** = `Pages in use exist at exit in PagedAllocator: union VariantPools::Bucket{Medium,Small}`（整份 decl 泄漏）→ 快照 `.agent_tmp/snapB_ptr/`（111 文件）。
  - `diff -r -q --strip-trailing-cr .agent_tmp/snapA_value/{gen,typings} .agent_tmp/snapB_ptr/{gen,typings}` ⇒ **零差异**（snapA = 值语义版产物，111 文件）。
  - 还原 5 文件 → 重编（30.4s）→ 部署 md5 `154fcb53...`（双位一致）→ 再跑 `--generate-types`：exit=0、**ERROR=0**、产物与 snapA 逐字节一致（`OUTPUT_IDENTICAL_TO_SNAPA`）。
  - 结论：A 是纯存储重构（产物零影响）；且**值语义下无需手工 `~TypeDB()` 即无 decl 泄漏**，指针版必须靠手工析构。
- [3] 冷轮七步（`run_pipeline.py --clean`，墙钟 73s）：step2 exit=0 E4、step3-dump exit=3221225477 E1（豁免）、step3-apigen exit=0 E2、step4 exit=0 **E2**、step5 exit=0 **E0**、step6 exit=0 **E0**。
  - E 逐条归因：step2 4 条 = `the javascript file is missing: .../config-singleton.js`、`.../start.js` + 派生 `Failed to create an autoload, script '...config-singleton.ts' is not compiling.`、`script failed to load (res://tests/start.ts)`；step3-apigen/step4 各 2 条 = 同一对 `config-singleton` 报错（冷 project 里 tsc 在 step5 才跑 ⇒「TS 未编译」类，prd AC2 的容许项）。
  - step3-dump 的 1 条 = `Parameter "singleton" is null.`（该步崩溃伴随；产物 11870742 字节已写出，按产物存在性判成败）。
  - **step4 的 `0xC0000005` 消失 ⇒ B4（引擎补丁恢复 + 重建）生效**。
- [4] B1 生效证据：step5 `node node_modules/typescript/bin/tsc` exit=0 —— `--clean` 删掉了 `project/node_modules`，说明命令行入口的**同步** npm install（选项 2，在 `try_install_project_files()` 之前）确实跑完才进 step5。
- [5] B2 生效证据：冷轮 step2 / step3-apigen 各 **1049 条** `[API Tool] api store has not been generated yet, accessor returns empty...`（Warning），`Please call api_tool::initialize() first.` **0 条** ⇒ 上报存在、非静默。1049 条过噪（等于把日志淹掉），已让 implementer 改为**每进程一次**（Error 分支仍每次报）：实测把 store 临时移走后跑 `--import`，该消息 **1 条**（`.agent_tmp/step2-once.log`），store 已原样放回。
- [6] 热轮（`run_pipeline.py`，不带 `--clean`，墙钟 48s）：step2 / step3-apigen / step4 / step5 / step6 **ERROR 全 0**（step3-dump 仍为豁免的 1 条崩溃伴随行）；store 已存在故无 store-missing Warning（4 步均 0 条）。
- [7] step7 `--verbose` ×3（`--audio-driver Dummy --headless --path . --verbose`）：3/3 `exit=0`、`GODOTJS_TEST_PROJECT_COMPLETED=1`、`GODOTJS_TEST_PROJECT_FAILED=0`、`Orphan StringName=0`、`ERROR=3`。
  - 那 3 条与 2026-10-22 已验收基线 `FINAL_step7.log` **逐行相同**（`The object does not have any 'meta' values with the key 'missing'.` + 2× `Parameter "data" is null.`，回溯指向 `tests/indexed-props/test-indexed-props.ts:96`）⇒ 测试项目自身负例，非回归。
  - 对照：`FINAL_step7.log`（修前）`Orphan StringName` **90 条**，现 **0**；泄漏统计（`ObjectDB instances were leaked` / `resources still in use` / `PagedAllocator` / `unclaimed string names`）在 step7 日志中 **0 条**。
- [8] doctest：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` → 部署 → `--headless --path . --jsb-run-tests`：`test cases: 96 | 96 passed`、`assertions: 1437 | 1437 passed`、`Status: SUCCESS!`、exit=0、orphan=0。**收尾已重跑 canonical 编译并重新部署**（交付 dll 非测试变体），md5 `a9eee83abdad5a80eca7f2af14c34eb4`（双位一致）；`third/godot-cpp` 的 `git status --porcelain` 为空。
- [9] `misc/verify_codegen.py` 报 29 处差异 → 归因 = **`.codegen-baseline` 过期（输入漂移）**，不是本改动：
  - `[gen]` 多余项对应项目里新存在的源文件（`tests/instance-hooks/*.ts`、`tests/notification/*.ts`、`tests/node-runtime/*`、`icons/GodotJSScript.svg` —— 均有 `ls` 证据）；缺失项 `godotjs-ext-editor.gdextension.gen.ts` 对应已被单库合并去掉的旧文件名（现在只有 `godotjs-ext.gdextension`）。
  - `[typings]` 分片内容是**引擎文档文本**差异（例：`map's internal merge rasterizer cell scale` vs `map internal merge rasterizer cell scale`，单分片 1274 行），[INFERENCE] 来自基线快照与现用引擎不是同一构建。
  - 因 A/B 差分已证明本改动对产物零影响，**未刷新 `.codegen-baseline`**（该目录归用户决定）。prd AC1 已按此改为「同输入 A/B 差分」判据并写明理由。
- [10] 已知遗留（均非本任务引入，未处理）：`--generate-types` 退出期仍有 **11 个 `FileAccess` 泄漏**（`WARNING: 11 ObjectDB instances were leaked`，refcount=1）；本轮之前的日志（`clearmeasure*.log`、`dev_6-gentypes.log`、`bisect1.log`）同样是 11 ⇒ 既有问题，且本次改造消除的是 decl 泄漏（`PagedAllocator VariantPools` 两条 ERROR 已消失）。归属在 api_tool/doc 读取侧的 FileAccess 持有链。
- [11] 仓库外状态变更（已报备）：`D:/Dev/godot/godot-4.7.2-patched` 的 `core/extension/gdextension.cpp` 补丁已恢复并重建引擎（30.7s，日志含 `Compiling core\extension\gdextension.cpp` + 两次 `Linking Program`）；`D:/Dev/godot/godot`（主工作树）未动。
- [12] 未 commit / 未 push（需另行授权）。
- [13] `trellis-check`（`CheckTypedbValueSem`）独立复核：**通过，未改动代码**。它逐条核对 `check.jsonl` 的 spec（容器存值规程、api store 上报口径、零 PIN、零残留引用）、自己重跑了热轮（step2/3-apigen/4/5/6 `exit=0 ERROR=0`）、`--generate-types`（`ERROR=0`、`PagedAllocator` 0）、step7（`exit=0`、COMPLETED=1、FAILED=0、Orphan=0），并复验了「移走 store → `--import`」的 once-only Warning（=1）。结论与证据写在本报告「Check」小节。
  - 它另报：`jsb_codegen_type_db.h:48` 有**既有**的 `using namespace`（HEAD 同行亦有 → 非本次引入，未动）。
  - 格式：`misc/format_src.py --dry-run` 全局报 15 个文件待格式化（含 HEAD 既有违规），其中 `src/api_tool/api_tool.cpp` 的 2 处宏续行对齐是**本次引入** → 已用 `clang-format -i --style=file` 单文件修好（该文件现 `--style=file` 输出与自身零差异：`FILE_NOW_FORMATTED`），未触碰其它既有违规文件。
- [14] 格式修好后**重新验收**（避免交付未验证产物）：canonical 重编（32.6s）+ 部署，md5 **`2deb9f3d461d9abcd2d97e233f64f0a3`**（双位一致）；`--generate-types` `exit=0 ERROR=0`；step7 `exit=0 / COMPLETED=1 / FAILED=0 / Orphan=0 / ERROR=3`（同已验收基线的 3 条负例）。**最终交付 dll md5 = `2deb9f3d461d9abcd2d97e233f64f0a3`**（[8] 里的 `a9eee83a...` 是格式修好前的构建）。

## 审计问答（用户 4 问，2026-10-23 第二轮）

- [15] **Q1「基线差异是不是改之前就有？」= 是，基线本身过期一个月**：
  - `.codegen-baseline/` 的文件 mtime = **2026-09-28/29**（`ls`）；而 `[gen] 多余` 对应的输入文件加入时间分别是 `tests/instance-hooks|notification` = **2026-10-13**、`icons/GodotJSScript.svg` = **2026-09-29**（`git log -1 -- project/...`），且都在 HEAD 里存在（`git ls-files`）。
  - 关键对照：`diff -r -q .codegen-baseline/typings` 对各版本的差异文件数 **指针版 15 / 值语义版 15，完全相同** ⇒ 差异与本次改动无关；typings 分片差异是引擎文档文本（例 `map's internal…` vs `map internal…`，单分片 1274 行）。
  - 结论：`misc/verify_codegen.py` 的基线是 09-28 的快照，不能用它判本次重构；A 的判据是同输入 A/B 差分（[2]），且末次交付构建再次核对 `OUTPUT_IDENTICAL_TO_SNAPA`。
- [16] **Q2「FileAccess 泄漏是不是你搞出来的？src/api_tool 改动解决了什么？」= 不是；api_tool 改动解决了可复现的假 ERROR**：
  - 泄漏**早于本任务**：`clearmeasure*.log`/`bisect1.log`（**10-21**）、`dev_6-gentypes.log`/`final_dev_6-gentypes.log`（**10-22**）都是 `11 ObjectDB instances were leaked`；今天用 **HEAD 指针版**构建跑 `--generate-types` 同样 11（`gentypes_ptr.log`）。
  - 分步定位：`--import` **0**、`--godotjs-api-generate` **0**、`--generate-types` **11**（三份 --verbose 日志，均用 `grep -o "[0-9]* ObjectDB instances were leaked"` 取数）。
  - **根因（结构性证据）**：`FileSplitter`（`jsb_codegen_writer.h:421-431`）持 `Ref<FileAccess> file_` 与**裸指针** `ModuleWriter *toplevel_`，**没有析构函数**、也没有任何地方 `memdelete(toplevel_)`（全仓 grep 零命中）；`toplevel_` 下的 `FileWriter` 又持 `Ref<FileAccess> file_`（:409），而 `Writer::~Writer` 是 `= default`（:88）不级联析构 ⇒ **每个分片泄漏一条 writer 链 + 一个 FileAccess**。typings 分片数 = `godot0..godot10` = **11**，与计数一一对应。`--import`/`apigen` 不写分片 ⇒ 0 泄漏，吻合。
  - **api_tool 改动的实测收益**（`--head` vs 现版，均重编部署后测）：
    | 场景 | HEAD | 现版 |
    |---|---|---|
    | store 存在 `--import` | 0 ERROR | 0 ERROR |
    | store 缺失 `--import` | **8 ERROR**（其中 4 条是 `Open paly load file failed…header.capi` / `Initialize error…` / `load utility functions failed`） | **4 ERROR**（api_tool 相关 0 条）＋ 1 条 `Warning`（每进程一次） |
    | store 缺失 `--godotjs-api-generate`（json 在） | 0 exit / **6 ERROR**（3 条 api_tool 假 ERROR） | 0 exit / **0 ERROR** ＋ 1 条 `Warning` |
    ⇒ 改动消除的是「数据还没生成」被当成 ERROR 的 6 条（cold apigen）/4 条（cold import），产物与退出码不变；不是「掏空实现」（现版每个失败路径都有分级消息，且 Warning 有 `--import` 实测 1 条的证据）。
  - 修复与否：`FileSplitter` 一行（`~FileSplitter(){ close(); memdelete(toplevel_); }`）即可，属**既有缺陷**且不在本任务范围，未动。
- [17] **Q3「`release_base()` 是什么鬼」= 已删除**（连同配套抑制逻辑）：
  - 它的实现是 `base = Ref<GodotJSScript>()`（`base` 是 `Ref<GodotJSScript>`，jsb_script.h:78），等价于既有的 `base.unref()`（jsb_script.cpp:1044）。
  - 方向被注释写反了：`base` 是**派生 → 基类**的强引用，清它只能让**基类**提前析构，不可能让「本脚本」活得更久；而引擎契约恰恰是「派生持有基类」——引擎自身的 `GDScript` 就是 `Ref<GDScript> base;`（`modules/gdscript/gdscript.h:93`）并由 `get_base_script()` 交出（`gdscript.cpp:264`），`Script::get_base_script()` 在引擎里是**纯虚**（`core/object/script_language.h:147`）。清掉它反而偏离引擎契约。
  - 实测：去掉后冷轮七步与带它时**逐项相同**（2-import E4 / apigen E2 / install E2 / tsc 0 / gentypes 0）、step7 ×2 绿（exit 0 / COMPLETED 1 / FAILED 0 / Orphan 0 / ERROR 3 与已验收基线同）。
  - 连带删除：`release_placeholders()` 与为它发明的 `_released_placeholders()` 抑制集（那段注释自己写着「引擎之后仍会回调 `_placeholder_erased()`」——即我们在删引擎仍持有的对象，再特判由此产生的回调）、以及 `_finish()` 里那段只剩空转的 `doomed` 块。结果：`jsb_script.cpp` / `jsb_script.h` **回到 HEAD**（`git diff` 为空），`jsb_script_language.cpp` 只保留有实测依据的部分（`__jsb_arg` 复位、`ResourceCache` 表项摘除、`_validate` 行列兜底、outline `名称:行号`）。
- [18] **Q4「撤掉引擎补丁后有没有重编再测？」= 上一轮没有可查的重编记录，结论不作数；本轮做了带重编的对照**：
  - 对照 A：`git apply -R --include=core/extension/gdextension.cpp <patch>` → `git status` 只剩 `editor/doc/editor_help.cpp` → `scons … -j16`（35.0s，日志含 `Linking Program …console.exe`）→ `--godotjs-install-project-files` 与 `--generate-types` **均 3221225477**。
  - 对照 B：`git apply …` 复原 → 重建（33.9s）→ 两条命令 **均 0**。
  - 测量口径的坑（已踩）：msys bash 的 `$?` 对未处理异常进程报 **5**，用 `python -c subprocess.run(...).returncode` 才能拿到 **3221225477**。以后判崩溃退出码只用后者/流水线脚本。
- [19] 最终交付（`release_base/placeholders` 撤销后重编的那一版）：md5 `87be33185a6f3e3b5c2a227769cf7fe4`（双位一致）；冷轮七步 2-import E4 / dump 崩溃豁免 / apigen E2 / install E2 / tsc 0 / gentypes **E0**；step7 ×3 全绿（Orphan 0、泄漏统计 0）；doctest `96|96`、`1437|1437`、SUCCESS、orphan 0；`--generate-types` 产物与 snapA 逐字节一致。

## 审计问答第 3 轮（用户 3 问，2026-10-23）

- [20] **Q1 查明：那 1049 次「空返回」来自 `populate_string_names_replacements()`，后果是类名映射被永久留空且从不重建**：
  - 临时给 `report_loader_unavailable(__FILE__,__LINE__)` 加 `__FILE__/__LINE__` 打印（仅诊断构建，已回退、已重编复核），在 store 缺失下跑 `--import`：`1057` 次调用中 **1049 次来自 `api_tool.cpp` 的 `has_class()`**（另 6 处是 `list_*`/`has_*` 各 1~2 次，共 8 次）。
  - `has_class()` 的唯一批量调用点是 `src/internal/jsb_class_visibility.cpp:100`（`get_exposed_original_class_list()`，遍历 `ClassDB::get_class_list()` ≈1049 个类）；该函数的调用者是 `GodotJSScriptLanguage::populate_string_names_replacements()`（`jsb_script_language.cpp:1494`），而它**只在整个进程的 `_init()` 里被调用一次**（`:239`），**之后没有任何地方在数据生成后重建**（`grep` 编辑器插件内零命中）。
  - 于是 store 缺失时：`has_class()` 对**每个**类都返回 false ⇒ `exposed_class_list` 为空 ⇒ `StringNames` 的类名替换表（`Object`→`GObject`、`Array`→`GArray` …）**整表缺失**，而且这一整轮进程都不再补建。这正是「不能拿空结果当正常」的实证——不是"少打一条日志"的问题。
  - 结论：**正确修法在调用方**——`_init()` 阶段若 `!api_tool::has_generated_data()` 则不缓存（标记未填充）并在 api 数据生成完成后重建；消息级别见下（待用户裁定）。
- [21] **Q2 精确机制与修法**：`FileSplitter` 本身**确实被 `memdelete`**（`new_splitter()` 删上一个、`~GodotTSDGenerator` 删最后一个），泄漏的是它**创建却从不释放的 writer 链**：`FileSplitter::FileSplitter` 里 `toplevel_ = memnew(ModuleWriter(memnew(FileWriter(p_path, p_types, file_)), "godot"))`，而 `jsb_codegen_writer.h:108` 明确写着这些 writer「heap-allocated and NOT owned by the base writer - callers must delete them」；`FileSplitter` 没有析构、全仓无 `memdelete(toplevel_)`。↑ 于是每个分片漏掉 `ModuleWriter` + 内层 `FileWriter`，而 `FileWriter` 持 `file_` 的第二份 `Ref<FileAccess>` ⇒ `~FileSplitter` 只释放一份 ⇒ 退出时该 FileAccess 引用计数 1 ⇒ 11 分片 = 11 条泄漏（与 `Reference count: 1` 完全吻合）。
  - 修法：给 `FileSplitter` 加析构，保存并释放它创建的两个堆 writer：`FileWriter *file_writer_`（新成员）+ `~FileSplitter(){ memdelete(toplevel_); memdelete(file_writer_); }`（`close()` 保持不变）。**不能**改成「让 `BufferingWriter` 析构时删 `base_`」——`jsb_codegen_generator.cpp:549` 等处的 `base_` 是栈对象（`&runtime_gen`）。
- [22] **Q3：doctest 的 `96` 不是 Orphan**。原始行：`[doctest] test cases: 96 | 96 passed | 0 failed | 0 skipped` / `[doctest] assertions: 1437 | 1437 passed | 0 failed |` / `Status: SUCCESS!` —— 96 = **doctest 用例数**，1437 = 断言数，全部通过。同一份日志里 `grep -c "Orphan StringName"` = **0**，没有可修项。
- [23] 为上述诊断临时插桩的构建已回退并重编（`grep -c DIAG-STORE-MISSING` = 0），当前交付 dll md5 `71b1b7fb7bb47e2706dca5c2f612d1d4`（双位一致），冒烟：`--generate-types` exit 0 / ERROR 0，step7 exit 0 / COMPLETED 1 / Orphan 0。
- [24] **待用户裁定**：(a) Q1 修法的消息级别——调用方 gating 后打 `Warning`（冷轮不刷 ERROR）还是 `Error`（按"抛错提示先生成"，代价是冷轮 step2 多 1~2 条 ERROR，与既有「仅 TS 未编译类」门禁冲突）；(b) Q2 的 `FileSplitter` 析构修复是否现在做。

## 审计问答第 4 轮（用户裁定 + 两项修复，2026-10-23）

- [25] **Q1 按用户裁定的口径实现**（"没数据就抛警告，没数据还调就抛错误"）：
  - **调用方 gating**：`GodotJSScriptLanguage::populate_string_names_replacements()` 顶部 `if (!api_tool::has_generated_data())` ⇒ **不调 api_tool**，只补与数据无关的部分（单例、保留字），并打一条 `WARN_PRINT("[jsb] api data is not generated yet: class / utility / global-constant name replacements are skipped, and will be rebuilt after '--godotjs-api-generate'.")`。实测打开的就是那 1049 次 `has_class()` + `list_utility_functions()` + `list_global_constants()`。
  - **访问器仍被漏判调用 ⇒ Error**：`report_loader_unavailable()` 去掉 Warning 与"只报一次"守卫，两个分支都 `ERR_PRINT`（"…but an accessor was called. Guard the call with api_tool::has_generated_data()…"）。消息级别回到"缺数据就报错"，同时把"该谁报"分清。
  - **生成后补建**：`_generate_api_tool_data_from_cmdline()` 成功分支里调 `GodotJSScriptLanguage::populate_string_names_replacements()`。菜单生成走 `api_tool::full_generate_and_reboot()`（`api_tool_editor.cpp:206-226` 起**新进程**跑 `--godotjs-api-generate`）⇒ 那个新编辑器进程的 `_init()` 同样因 store 缺失而跳过、随后落进本函数生成数据 ⇒ 这一处钩子覆盖两条路径（否则该会话整轮缺类名映射）。为此把该静态函数移到 `public:`（原为私有，调用点仅 `:239`）。
- [26] **Q2 `FileSplitter` 修复已落地并实测**：
  - 改动：`FileSplitter` 新增成员 `FileWriter *file_writer_`（构造时保存内层 writer）与析构 `~FileSplitter(){ memdelete(toplevel_); memdelete(file_writer_); }`（`jsb_codegen_writer.h:421-441`、`.cpp:812-815`）。`close()` 不变。
  - 实测：`--generate-types --verbose` → exit 0、ERROR 0、**`ObjectDB instances were leaked` 归零**（修前 11），而 typings 分片数仍为 11 ⇒ 与"每个分片漏一个 FileAccess"的定位一致。
- [27] **Q3 答复（引擎侧改动范围）**：**不止 `EditorHelp::_gen_extensions_docs`**。本轮做了带重编的对照：撤掉 `core/extension/gdextension.cpp` 那处补丁并重建（35.0s）⇒ step4/6 立刻 `3221225477`；复原并重建（33.9s）⇒ 两者 `0`。那处（`close_library()` 卸载前清扫本扩展的 instance binding）是扩展侧**做不到**的（扩展无法枚举引擎侧所有对象；试过的 `ClassDB::deinitialize(CORE)` 已证明无效）。
- [28] **Q4 答复（`jsb_script` 改动被去掉、placeholder 清理是否需要）**：
  - 去掉了什么：`release_base()`（`base = Ref<GodotJSScript>()`，方向与注释写反，且与引擎 `GDScript::base` 的契约相反）、`release_placeholders()`、为它发明的 `_released_placeholders()` 抑制集、`_finish()` 里只剩空转的 `doomed` 块。`jsb_script.cpp` / `jsb_script.h` 现在**内容与 HEAD 完全一致**（`git diff` 为空）。
  - placeholder 清理**在已跑过的流程里不需要**（证据）：去掉后冷轮七步逐项相同；冷轮 step2 恰好会走 placeholder 路径（`Failed to create an autoload, script '…config-singleton.ts' is not compiling.` 即引擎为未编译脚本创建 placeholder），该轮 exit=0、无 `may have some memory leak issue`、无泄漏统计；step7 ×3 orphan 0、泄漏统计 0；doctest 全绿。
  - 但我必须说明**验证边界**：上述覆盖的是 CLI 七步 / step7 / doctest；**没有**覆盖长时间编辑器会话里的 `reload_scripts` 等交互路径。若你要保守，恢复 `release_placeholders()`（但**不要**恢复那个抑制集——那段注释自认"引擎之后仍会回调 `_placeholder_erased()`"，即我们删了引擎仍在持有的对象再特判回调）是一分钟的事。
- [29] 本轮修复的重编 + 冒烟：md5 `8254602003baa83f352964c5d8dd2e57`（双位一致）；`--generate-types --verbose` exit 0 / ERROR 0 / 0 泄漏。
- [30] 首轮 gating 后仍剩 1 条漏判 ERROR，逐层查清并补完：
  - 插桩（`report_loader_unavailable(__FILE__,__LINE__)`，真冷项目跑 `--import`）定位到 **`api_tool::is_loaded()`** —— 它被 `CHECK_LOADER_V` 包着，于是"查询是否加载"也会抛 Error。这是我把 Error 口径一刀切套到**纯查询**上的错误。
  - 修法：`is_loaded()` 改为静默查询（`loader != nullptr && loader->is_loaded()`）；它的两个调用点（`jsb_editor_plugin.cpp:1298/1354`）本来就是"未加载就 skip + 那句 Warning"的正确写法。
  - 另补：`jsb_godot_module_loader.cpp` 的四个 api 分支（utility / global constant / dynamic class / global enum）统一由顶部 `const bool api_ready = api_tool::has_generated_data();` 门控，缺数据时按"查不到"处理（该文件在无 store 时也会被调用）。
  - 复核：冷轮 step2 = **1 条 Warning + 0 条 api_tool ERROR**（ERROR 只剩「TS 未编译」4 条）。
- [31] 本轮最终验收（md5 **`f7fd5b34058596d7ebc46251418198eb`**，双位一致）：
  - 冷轮 `--clean`：2-import E4 / 3-dump 崩溃豁免 E1 / 3-apigen E2 / 4-install E2 / 5-tsc E0 / 6-gentypes **E0**（E 组成全部是「TS 未编译」类）。
  - 热轮：2-import / 3-apigen / 4-install / 5-tsc / 6-gentypes **ERROR 全 0**。
  - step7 `--verbose` ×3：exit 0 / COMPLETED 1 / FAILED 0 / **Orphan 0** / 泄漏统计 0。
  - doctest：`96|96`、`1437|1437`、SUCCESS、orphan 0；随后已重编 canonical 并部署（交付 dll 非测试变体）。
  - `--generate-types --verbose`：exit 0 / ERROR 0 / **泄漏 0**（FileSplitter 修复生效，修前 11）。
  - `--generate-types` 产物与 `snapA_value` 逐字节一致（A 的纯重构结论在本轮构建上仍然成立）。
- [32] 本轮遗留（未动，待用户决定）：`misc/verify_codegen.py` 的 `.codegen-baseline` 仍是 09-28 快照；`release_placeholders()` 的恢复与否（见 [28] 的验证边界说明）。

**A：TypeDB / DocCache 值语义化（done）**

改动文件与要点：
- `src/editor/codegen/jsb_codegen_type_db.h`：5 个 map 的 value 由 `XDecl *` 改为 `XDecl`；删 `owned_*`、`ordered_*()`、`~TypeDB()`；`_load_primitive_type` 返回类型改 `void`。
- `src/editor/codegen/jsb_codegen_type_db.cpp`：删 `~TypeDB()` 定义；`find_class` / `find_primitive` 直接 `return classes.getptr(name)`（返回类型仍是 `const XDecl *`）；5 个 loader 全部改为「先算 key（暴露名）→ 跳过/校验分支前置 → `XDecl &decl = map[key]` → 填字段」，不再有 `memnew`/`insert(decl)`；`_load_primitive_type` 的 utilities 早退分支与函数末尾两处登记合并为**顶部一次**取槽位（早退分支只 `return;`）。
  - `load_utilities` 的 key 用 `NamingUtil::get_member_name(utility_func->get_name())`，与 `build_method_decl` 写入 `r_decl.name` 的表达式是同一份，已加中文注释指明两处必须同步。
  - 重复 key 检测：按 design §A.3，在 5 个 loader 取槽位前各加一条 dev 构建断言 `jsb_checkf(!map.has(declared_name), "duplicate <x> key: %s", ...)`（`JSB_WITH_CHECK` 才展开）。**未实测运行**（见「未做/遗留」），但已把「重复即停」的能力固化为断言，check 阶段冷/热轮一跑即会暴露。
- `src/editor/codegen/jsb_codegen_docs.h/.cpp`：`docs_` 改 `HashMap<String, ClassDocEntry>`；删 `DocCache::destroy()` / `destroy_doc_cache()` 及其 friend 声明；`find_doc()` 改为**先确认文档存在**（`@GlobalScope` 恒存在；其余 `api_tool::find_document()` 返回空则落 negative cache 返回 nullptr）再 `ClassDocEntry &entry = docs_[key]` 原地填充、`return &entry`。返回类型仍 `const ClassDocEntry *`，且跨后续调用有效（entry 为 map 节点内地址）。
  - 关键决策：**不能**在确认存在前取槽位，否则"无文档"的类会被留成默认构造的空 entry，第二次 `docs_.getptr` 会命中它而非返回 nullptr，破坏 negative cache 语义。
- `src/editor/codegen/jsb_codegen_generator.cpp`：5 处 emit 循环（singleton/class/primitive/global/utility）改为 `for (const KeyValue<String, XDecl> &kv : types_.<map>)`；class 循环内取 `const ClassDecl &cls = kv.value` 后再判 singleton 去重。顺序等价性由 design §A.0（HashMap 迭代 = 插入序）保证。

**B：收口三处（done）**

- **B3**：`src/runtime/register_types.cpp` 删除 `godot::ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_CORE)` 及其整段中文注释（SERVERS 结语处）。
- **B2**：`src/api_tool/api_tool.cpp`——
  - 新增 `report_loader_unavailable()`：拿不到「已加载 store」时**必定产出消息**；`loader && !loader->has_generated_data()` ⇒ `WARN_PRINT`（"store 尚未生成"，级别落点按 `codegen-baseline.md` §api store 未生成时不要报 Error）；其余 ⇒ `ERR_PRINT("Please call api_tool::initialize() first.")`。
  - `CHECK_LOADER_V` / `CHECK_LOADER` 宏改为 `if (get_loaded_loader() == nullptr) { report_loader_unavailable(); return ret; }`——**不再有无消息 return**（宏体见 grep 证据）。加了 `get_loader()` / `get_loaded_loader()` 的前置声明供宏与 helper 使用。
  - 保留两处偏离 HEAD（各带注释依据）：`get_loader()` 不用一次性 `static`（`finalize()` 会 memdelete singleton，之后仍需可重建）；构造/获取 loader 时不读 store（`initialize()` 会立刻 `read_header()`，冷启动必然失败）。
  - 更新 `get_loaded_loader()` doc 注释：自身不上报，上报责任在调用方宏（原「静默」措辞与"禁无消息"冲突）。
  - **`api_tool_editor.cpp` 未动**：其 `generate_api_tool_data()` 仍保留上一轮"去掉 initialize()"的形态（工作树既有改动）。按 design §B2 第 4 点，是否恢复 `initialize()` 要由冷轮日志判定（降级为 Warning 后调用它只产生 Warning 就不必加回特例）——本 dispatch 不跑运行时验收，故**留待 check 阶段按冷轮日志决定**（已列入遗留）。
- **B1（选项 2）**：`src/editor/weaver-editor/jsb_editor_plugin.cpp`——
  - `ensure_tsc_installed()` 恢复 HEAD 形态，仅加 headless 早退（`if (headless) return;` + 中文注释：命令行装包由入口同步负责，避免与调用方并发写 `node_modules`）；`create_process` fire-and-forget 路径不变。
  - `_install_project_files_from_cmdline()` 在 `try_install_project_files({}, true)` **之前**，headless 下用 `OS::get_singleton()->execute(exe_path, {"install"}, output, true)` 同步装包；失败保留 HEAD 文案与级别（`JSB_LOG(Error, ...)` + 逐行 `Error` 输出），成功记 `Log`。用 headless 守卫，保证任一模式下只有一次 npm（非 headless 走 `install_project_files()` 内的异步分支）。

**编译与部署证据**
- `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` 成功两次（第二次仅 api_tool.cpp 注释变更后重编）：无 `error`，末行 `scons: done building targets.`，Wall time ≈ 41/40s。
- 部署：`cp bin/windows/godotjs-ext.windows.editor.x86_64.dll project/addons/godotjs-ext.daylily-zeleen/bin/windows/`；`md5sum` 两处一致 = `b1597e2767505094b01f6c43ab3b8804`。
- `git status --porcelain third/godot-cpp` 为空。

**验收 grep 证据**
- `grep -rn "owned_classes_\|owned_primitives_\|owned_singletons_\|owned_globals_\|owned_utilities_\|ordered_*(...)\|destroy_typedb\|destroy_doc_cache" src/` ⇒ **0 命中**（exit=1）。仅剩的 `owned_*` 字样出现在注释里。
- `grep -n "deinitialize(GDEXTENSION_INITIALIZATION_CORE)" src/runtime/register_types.cpp` ⇒ **0 命中**。
- api_tool.cpp 访问器失败路径均为 `report_loader_unavailable(); return ...;`，无「无消息 return」。

**未做 / 遗留（交 check 阶段）**
- 未跑任何运行时验收（`misc/verify_codegen.py` 双轮 / `run_pipeline.py` 冷热轮 / step7 / doctest），按派单约束留给 check 阶段。
- 5 个 map 的重复 key 是**静态断言兜底**、未实跑确认；若冷轮 dev 构建触发 `duplicate ... key` 断言即命中 prd 停止条件（须停手交用户）。
- `api_tool_editor.cpp` 是否需要恢复 `initialize()`：待冷轮 step2/step3-apigen/step4 日志（看是否还有 Error 级输出）决定。
- `report_loader_unavailable()` 使用 `WARN_PRINT`/`ERR_PRINT`（Godot 原生宏，与 api_tool 既有错误上报一致），check 阶段需在冷轮日志里确认能看到那条 `WARNING` 且 `ERROR:` 计数为 0。

- [B2 收尾] `report_loader_unavailable()` 的 Warning 分支改为**每进程只报一次**（`static bool warned_store_missing` 守卫），文本末尾追加 "(this message is reported only once)"，并加中文注释写明依据。**证据**（Main 冷轮实测）：store 未生成时访问器被高频调用，`step2-import.log` 与 `step3-apigen.log` 各出现 **1049 条**同一条 `[API Tool] api store has not been generated yet...`，日志被淹（正是 `codegen-baseline.md` §api store 未生成时不要报 Error 想避免的后果）。Error 分支（store 在却读不出 / 未 initialize）**保持每次都报**（真实配置问题）。未改成静默 return、未降级 Verbose。仅改 `src/api_tool/api_tool.cpp`。重编 + 部署后两处 md5 一致 = `fe695a2f4299afe8b17cc9c4217949e2`（旧值 `b1597e2767505094b01f6c43ab3b8804`）。

---

## 撤掉引擎补丁后的实测（用户要求，2026-10-23）

- [33] **引擎补丁已按指示撤掉并重编**（`git apply -R --include=core/extension/gdextension.cpp` → `git status` 只剩 `editor/doc/editor_help.cpp` → `scons … -j16` 39.3s）。在该引擎上跑当前扩展（md5 `1d2e3a74d7bf1c13ed35d88458a5770c`）：

| 步骤 | 结果 |
|---|---|
| 2-import | exit 0 / ERROR 4（全是「TS 未编译」类） |
| 3-dump | `3221225477`（既有豁免） |
| 3-apigen | exit 0 / ERROR 2 |
| **4-install** | **`3221225477` 崩** |
| 5-tsc | exit 0 |
| **6-gentypes** | **`3221225477` 崩**（ERROR 0，产物已写出） |
| step7 `--verbose` | exit 0 / COMPLETED 1 / FAILED 0 / Orphan 0 |

  ⇒ **"剩下的清理都能在扩展里做"不成立**：撤掉补丁后 step4/step6 必崩。
- [34] 两次扩展侧替代尝试都失败（均已回退，`register_types.cpp` 现与 HEAD 一致）：
  1. SERVERS 结语调 `godot::ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_CORE)` → step4/6 **仍 `3221225477`**。它只清 godot-cpp 记录过的 `engine_singletons`，覆盖不到全部带 binding 的对象。
  2. 按 `Engine::get_singleton_list()` 逐个 `object_free_instance_binding(singleton->_owner, token)` → 变成 **`0xC0000374`（堆损坏）**。`get_singleton()` 返回的 `Object*` 就是 godot-cpp 包装本身，释放回调会析构并 `free` 它，遍历里立刻产生悬空指针。
  ⇒ 扩展侧**无法枚举**引擎里所有带本扩展 binding 的对象（godot-cpp 的 `get_object_instance_binding` 没有注册表可遍历），`ObjectDB::debug_objects()` 是引擎内部接口、未通过 GDExtension 暴露。
- [35] 结论：`core/extension/gdextension.cpp` 那处补丁是**必需**。可选路线三条，等用户定：
  (a) 保留补丁（当前工作树=已撤，需重新 `git apply` + 重建）；
  (b) 扩展侧自建注册表（在 `Environment::bind_godot_object` 记下每个绑定的对象指针，退出时逐个释放；要处理对象提前销毁的失效）；
  (c) 引擎向扩展暴露 ObjectDB 遍历能力（上游改动，代价最大）。

- [36] **"对象都在 `object_db_` 里"——对，但不覆盖崩溃面**。实测证据：
  - 给引擎补丁的清扫函数插桩打印（`[DIAG-SWEEP] class=…`，仅诊断构建，已移除并重编）后跑 `--generate-types`：被清扫的是 **39 个对象**，类型为
    `ResourceFormatLoader`×7、`ResourceImporterScene`×4、`ResourceImporterLayeredTexture`×4、`ResourceFormatSaver`×3、
    `Time`、`ResourceUID`、`IPWindows`、`GDExtensionManager`、`GDExtension`、`RefCounted`、以及 16 种 `ResourceImporter*` 各 1。
  - 这 39 个**没有一个**是 JS 绑定对象：它们是 godot-cpp 经 `internal::get_object_instance_binding()` 给**引擎侧对象**建的包装
    （token = 库指针），而 `Environment::bind_godot_object()` 只给"JS 访问到的对象"建 `Environment*` 绑定。
  - `object_db_` 在卸载前已空：`~Environment()` 里有 `jsb_check(object_db_.size() == 0)`（`jsb_environment.cpp:431`），
    这一步在退出流程里通过 ⇒ 用 `object_db_` 枚举"要归还的 binding"时集合为空。
  - 于是"按 `object_db_` 逐个释放"实际释放不了这 39 个；我两次尝试（`ClassDB::deinitialize(CORE)` / 枚举 `Engine::get_singleton_list()`）
    的结果分别是仍崩 `3221225477` 与堆损坏 `0xC0000374`，均已回退（`register_types.cpp` 现与 HEAD 一致）。
- [37] 引擎补丁已复原（去掉诊断打印）并重建（45.3s），`git status` = `core/extension/gdextension.cpp` + `editor/doc/editor_help.cpp`；
  step4 / step6 = **0 / 0**。
- [38] **`generate_api_tool_data` 恢复调用 `initialize()`（用户指正：之前删掉是错的）**。错误点：我删它的理由是"store 没生成时它会打 ERROR"，
  但忽略了它内部还做 `update_base_dir()`（`api_tool_loader.cpp:102`），且"已有 store"的**重新生成**路径也走这里——这两种情况都不该跳过。
  修法：**`if (has_generated_data()) initialize();`**——`has_generated_data()` 只查目录/文件存在性（`api_tool_loader.cpp:777-788`），不读 header、无副作用；
  store 缺失就跳过（此时"数据还没生成"不是错误），存在就加载（并刷新 base_dir_）。
  实测：冷轮（store 缺失）step3-apigen 无 `Initialize error`/`Open paly load file failed`，`Api data generated successfully`；
  热轮（store 已存在）`--godotjs-api-generate` 同样 0 ERROR 且生成成功。冷轮全量复核不变（E4/豁免/E2/E2/0/0）。

- [39] **（修正 [38]）`generate_api_tool_data` 无条件调 `initialize()`，"store 未生成"的报错在 `initialize()`/`PayloadReader` 内部降级为 Warning（用户裁定）**。
  错误点：[38] 的 `if (has_generated_data()) initialize();` 把"没数据就不初始化"套到了**生成数据的那条路径**上——
  生成 store 正是这一步要做的事，跳过初始化等于跳过 base_dir 刷新与 header 加载。
  改动：`generate_api_tool_data()` 无条件 `initialize()`（HEAD 原行为）；
  `ApiLoader::initialize()` 对 `ERR_FILE_NOT_FOUND`（store 未生成）打 Warning 并返回 err，其余失败仍 `ERR_FAIL_COND_V_MSG`；
  `ApiToolPayload::open()` 同口径（`ERR_FILE_NOT_FOUND` → Warning，其余 → ERR_FAIL）。
  实测：冷轮（store 缺失）`--godotjs-api-generate` exit 0、ERROR 0、2 条降级 Warning、生成成功；
  热轮（store 已存在）exit 0、ERROR 0、生成成功；冷轮全量 2-import E4 / dump 豁免 / apigen E2 / install E2 / tsc 0 / gentypes 0（与 [31] 一致）。
  另：[38] 里"热轮测试失败 rc=1"是我自己把输入 json 用掉了（`generate_api_tool_data` 生成后会删除/还原输入文件），与改动无关。

- [40] **按用户指令撤掉 `src/api_tool/api_tool.cpp` 与 `src/runtime/bridge/jsb_godot_module_loader.cpp` 的全部改动**（均回 HEAD）。`api_tool_loader.cpp` / `api_tool_payload.h` 里"store 未生成 → Warning"的降级保留（那是"没数据就抛警告"口径的落点）。回退后冷轮七步：2-import E4 / dump 豁免 / apigen E2 / install E2 / tsc 0 / gentypes 0（与 [31] 一致）；`--import` 冷启日志仅 2 条 api_tool 相关 Warning（`header.capi` 缺失 + 编辑器侧 skip），无 ERROR 增量。
- [41] **"上千个 ERROR/WARNING"的调用路径（证据：`[DIAG-SITE]` 插桩 1049 次命中 `api_tool.cpp:276 has_class`）**：
  `Main::setup2()` → `ScriptServer::init_languages()`（`main/main.cpp:3867`）→ `GodotJSScriptLanguage::_init()`（`jsb_script_language.cpp:227`，运行期日志第 4 行）→ `populate_string_names_replacements()`（`:1494`）→ `ClassVisibility::get_exposed_original_class_list(…, false)`（`:1500` → `jsb_class_visibility.cpp:60`）→ 遍历 `ClassDB::get_class_list()`（本项目 1049 个类）→ `api_tool::has_class(name)`（`jsb_class_visibility.cpp:100`）→ `CHECK_LOADER_V` → loader 未加载 ⇒ 每类一条，共 1049 条。同一函数尾部还有 `list_utility_functions()`/`list_global_constants()` 各 1 次。
  触发条件 = api store 不存在 + `ScriptServer::init_languages()` 仍会执行（setup2 阶段，早于任何数据生成步骤）。数据生成之后的进程（step4/5/6、正常编辑器）`_init` 时 store 已存在，整条链一次都不触发。

## Check（独立抽查，CheckTypedbValueSem，2026-10-23）

> 全部为本轮独立复跑/复读证据，不是复述。交付 dll 保持 canonical：`md5sum bin/windows/...dll project/addons/.../bin/windows/...dll` 两处均为 `a9eee83abdad5a80eca7f2af14c34eb4`（未重编，未动代码）。

### 1 规范合规（逐条 + 证据）

| 项 | 结论 | 证据 |
|---|---|---|
| 头文件无 `using namespace` | **通过（本次）**；存在**既有**违规 | `grep -n "using namespace"` 命中仅 `jsb_codegen_type_db.h:48 using namespace godot;`，`git show HEAD:...jsb_codegen_type_db.h` 同一行亦在；`git diff \| grep "^+.*using namespace"` **0 命中** ⇒ 非本次引入（既有文件，按 spec「既有文件不用回改」不动） |
| 无临时文件散落业务目录 | **通过（源码）**；1 处管线副产物 | `git status --porcelain` 未跟踪项仅 `.trellis/tasks/*` 与 `project/package-lock.json`；`src/`、根、`scripts/` 无散落临时文件（全部在 `.agent_tmp/`）。`project/package-lock.json` = 本流水线 step4 `npm install` 的**正常副产物**（非测试脚本/dump），建议入 `.gitignore` 或忽略，非本改动引入 |
| 未改 `*.gen.*` / `*.def.*` | **通过** | `git status --porcelain \| grep -E "\.gen\.\|\.def\."` ⇒ exit=1（0 命中） |
| 新增文件版权头正确 | **通过（N/A）** | `git status` 无新增源文件（`src/` 下无 `??`） |
| 容器存值优先（key 先算/跳过前置/不用 `insert(key,decl)`/不靠 `operator[]` 判重） | **通过** | 逐 loader 复读 `jsb_codegen_type_db.cpp`：`load_classes` 先 `continue`（api_class==nullptr）→ `declared_name` → `jsb_checkf(!classes.has(...))` → `classes[declared_name]`；`_load_primitive_type`/`load_singletons`/`load_globals`/`load_utilities` 同型（utilities key = `get_member_name(utility_func->get_name())`，与 `build_method_decl` 写 `r_decl.name` 同表达式）。全文件 `grep "\.insert(" ` 无 `map.insert(key, decl)` 用法。`jsb_codegen_docs.cpp::find_doc`：先 `getptr` 正缓存 → `missing_docs_.has` 负缓存 → **确认文档存在/落 negative cache** → 才 `docs_[p_class_name]`（避免提前取槽位留空 entry） |
| api store 上报口径（codegen-baseline.md 表） | **通过** | `api_tool.cpp::report_loader_unavailable()`：`loader && !has_generated_data()` ⇒ `WARN_PRINT` + `static warned_store_missing` **每进程一次**；否则 `ERR_PRINT` **每次**。`CHECK_LOADER_V`/`CHECK_LOADER` 宏体 = `if (get_loaded_loader()==nullptr){ report_loader_unavailable(); return ret/; }` ⇒ **无消息 return 零容忍**。逻辑分支复核 + 运行时实证见 §4 |
| 未引入 PIN / `_exit` 拦卸载手段 | **通过** | `grep -rn "GetModuleHandleExW\|_exit(\|FLAG_PIN\|LdrAddRefDll" src/` ⇒ 仅 7 处命中全为**函数名假匹配**（`_input_focus_exit`/`_thread_exit`/`on_thread_exit`/`if (...)...`），无 `GetModuleHandleExW`/`PIN`/`_exit()`/`LdrAddRefDll` 实质命中 |
| 撤销 `ClassDB::deinitialize(CORE)` | **通过** | `grep -rn "deinitialize(GDEXTENSION_INITIALIZATION_CORE)" src/` ⇒ exit=1（0 命中） |

### 2 引用完整性

- `grep -rn "owned_classes_\|owned_primitives_\|owned_singletons_\|owned_globals_\|owned_utilities_\|ordered_classes()\|ordered_primitives()\|ordered_singletons()\|ordered_globals()\|ordered_utilities()\|destroy_typedb\|destroy_doc_cache\|deinitialize(GDEXTENSION_INITIALIZATION_CORE)" src/` ⇒ **exit=1（0 命中）**。
- 补充：`grep -rn "owned_" src/editor/codegen/` 仅剩 3 处**注释**（type_db.h:193 / type_db.cpp:173）与无关局部变量 `owned_class`（:633/634）。
- `grep -rn "destroy()\|destroy_doc_cache\|doc_cache" src/editor/codegen/` ⇒ exit=1（0 命中）；`grep -rn "~TypeDB" src/` ⇒ exit=1 ⇒ `DocCache::destroy()` / `destroy_doc_cache` / `~TypeDB()` 全清。
- `find_class` / `find_primitive` 签名**未变**：`jsb_codegen_type_db.h:208/209`（`const ClassDecl *find_class(const String &) const;` / `const PrimitiveClassDecl *find_primitive(const String &) const;`）与 `git show HEAD:...` 逐字相同；调用点仅 `jsb_codegen_generator.cpp:283`、`jsb_codegen_mutations.cpp:211/215`，均按 `const ClassDecl *` 使用。

### 3 格式

`python misc/format_src.py --dry-run`（clang-format 23.1.0）⇒ 15 文件待格式化，其中**本次改动文件** 3 个：
- `src/api_tool/api_tool.cpp` — **本次引入**：HEAD 版 clang-format **无**违规（`.agent_tmp/api_tool_head.cpp` dry-run 静默）；现工作树仅 2 处差异 = `CHECK_LOADER_V`/`CHECK_LOADER` 宏续行反斜杠对齐（`-Wclang-format-violations`，纯空白）。
- `src/runtime/weaver/jsb_script_language.cpp`、`src/runtime/tests/test_jsb_script_language_queries.h` — **既有**：HEAD 版 dry-run 同样报违规 ⇒ 非本次引入。
- 建议修法（未执行）：对 `api_tool.cpp` 的宏续行按 clang-format 列对齐，或 `clang-format -i src/api_tool/api_tool.cpp`。**未改动**（遵守「无实锤缺陷不为风格偏好改动，避免把已验证产物打回未验证」，且保持 canonical md5 `a9eee83...`）。

### 4 独立运行抽查（各一次，本轮实跑）

1. **热轮** `python .agent_tmp/run_pipeline.py`（不带 `--clean`，墙钟 47.78s）：
   - step2-import `exit=0 ERROR=0`；step3-dump `exit=3221225477`(0xC0000005, 既有豁免) `ERROR=1`；step3-apigen `exit=0 ERROR=0`；step4-install `exit=0 ERROR=0`；step5-tsc `exit=0 ERROR=0`；step6-gentypes `exit=0 ERROR=0`。
   - 热轮 store 已存在：step2/step3-apigen `api store has not been generated` = **0**、`Please call api_tool::initialize() first.` = **0**（`grep -c`）。与 report [6] 一致。
2. **`--generate-types`（=step6，`--headless --editor --path . --generate-types`）**：`exit=0`、`ERROR=0`、`grep -c "PagedAllocator" .agent_tmp/step6-gentypes.log` = **0**（`VariantPools` decl 泄漏已消失）。退出期仅 `WARNING: 11 ObjectDB instances were leaked at exit`（既有遗留，见下）。
3. **step7** `cd project && <engine> --audio-driver Dummy --headless --path . --verbose`（本轮 1 次）：
   - `exit=0`；`GODOTJS_TEST_PROJECT_COMPLETED`=1、`GODOTJS_TEST_PROJECT_FAILED`=0；`Orphan StringName`=**0**（对照 `FINAL_step7.log` **90** 条）；`leaked|still in use|PagedAllocator|unclaimed string`=0。
   - `ERROR:`=3，与已验收基线 `FINAL_step7.log` **逐行相同**（`The object does not have any 'meta' values with the key 'missing'.` + 2×`Parameter "data" is null.`）⇒ 测试项目自身负例，非回归。

### 5 附加独立实证（api store 上报口径，就地不强改状态）

临时把 `project/.godot/.api_dumping` 移出（`.agent_tmp/api_dumping_stash`）→ `--headless --editor --path . --import` → **原样放回**（`header.capi` 已复原）：
- `grep -c "api store has not been generated"` = **1**（文本含 `(this message is reported only once)`）⇒ **每进程一次**成立；
- `grep -c "Please call api_tool::initialize() first."` = **0** ⇒ 无 Error 误报；
- `ERROR:`=4 = `failed to attach module ...config-singleton.js (36)` / `Failed to create an autoload ...` / `failed to attach module ...start.js (36)` / `script failed to load (res://tests/start.ts)` ⇒ 与冷轮 step2 同类的「store 缺失致模块挂载失败」容许项，**非**本改动引入、**非**「无消息 return」。

### 6 既有遗留（确认描述准确，未处理）

- `--generate-types` 退出期 **11 个 `FileAccess` 泄漏**（`WARNING: 11 ObjectDB instances were leaked at exit`）：本轮 step6 同样为 11，与 report [10] 及更早日志（`clearmeasure*.log`/`dev_6-gentypes.log`/`bisect1.log`）一致 ⇒ 既有；本次消除的是 decl 泄漏（`PagedAllocator: VariantPools` 两条 ERROR 已消失）。
- `.codegen-baseline` 过期（输入漂移：项目新增测试文件 + 引擎文档文本差异）：未刷新，归用户决定；A/B 差分已证本改动对产物零影响。

### 7 Check 裁决

**通过（无实锤功能缺陷，未改动代码）。** 唯一新增偏差 = `api_tool.cpp` 2 处宏续行反斜杠对齐（纯格式、clang-format 才可见，无功能影响），按边界未修；另有 2 处既有格式违规与 1 个既有 `using namespace`（均在 HEAD 已存在）。所有必查规范项、引用完整性、3 项独立运行抽查均达标。

