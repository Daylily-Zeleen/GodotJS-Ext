# codegen TypeDB 值语义化与排查期改动收口

## Goal

1. **（A）codegen 存储改造**：`TypeDB` 的 5 个 map 与 `DocCache` 从「map 存指针 + `owned_*` 手工所有权 + 手工析构」改为**值语义**，删除冗余登记与手工释放路径。
2. **（B）排查期改动收口**：把上一轮为定位退出期 AV / orphan 而引入、但**未经证实**或**改变了既有语义**的改动回退或改到正确位置，并恢复被我移除的引擎侧 `close_library` 补丁。

两个工作流放在同一个任务里：它们共享同一套验收（冷/热七步流水线 + codegen 基线双轮 + doctest + 双位 dll），拆成两个任务只会把同一轮验证跑两遍。

## Requirements

### A. TypeDB / DocCache 值语义化

- A1 `jsb_codegen_type_db.h`：`classes` / `primitive_types` / `singletons` / `globals` / `utilities` 的 value 类型由 `XDecl *` 改为 `XDecl`；删除 `owned_classes_` / `owned_primitives_` / `owned_singletons_` / `owned_globals_` / `owned_utilities_` 及 `ordered_*()` 访问器；删除 `~TypeDB()`。
- A2 `jsb_codegen_type_db.cpp`：5 个 `load_*()` 改为**原地构造**（`XDecl &decl = map[key];` 后填字段），**不得**用 `insert(key, decl)`（godot-cpp 的 `HashMap::insert` 只有 `const TValue&` 重载，会深拷贝整个 decl）。key（暴露名）必须在取槽位前算好。失败/跳过路径不得留下默认构造的空条目。
- A3 `find_class()` / `find_primitive()` 保持 `const XDecl *` 返回类型（值语义下仍是节点内地址）。
- A4 `jsb_codegen_docs.h/.cpp`：`docs_` 改为 `HashMap<String, ClassDocEntry>`；删除 `destroy()` / `destroy_doc_cache()`；`find_doc()` 返回 `const ClassDocEntry *`，且**必须**跨后续 `find_doc()` 调用保持有效（缓存持续填充）。
- A5 `jsb_codegen_generator.cpp` 的 5 处 emit 循环（173/181/192/200/208）改为遍历对应 map（godot-cpp `HashMap` 迭代即插入序，与 `owned_*` 顺序逐条等价）。
- A6 生成产物**零 diff**：值语义化是纯存储布局重构，`misc/verify_codegen.py` 双轮必须全绿。

### B. 排查期改动收口

已核实**无需动作**（本轮之前的回退已生效，只需在 report 里记录）：`Environment::~Environment` 的 `deferred_class_post_binds_.clear()`、`p_keep_owner`/`default_objects_`/`release_default_objects()`、`jsb_shadow_realm.cpp` 的 lazy 缓存与 `purge_symbol_cache_isolate` 改名、`jsb_codegen_scene_descriptors.cpp` 的 `memdelete`、`api_tool_loader.cpp` 的 7 处 `if (!loaded_) return;`——当前工作树均已回到 HEAD 形态。

- B1 `ensure_tsc_installed()`：headless 分支的阻塞安装不得留在该函数内（原函数语义是 fire-and-forget）。阻塞点下移到命令行入口 `_install_project_files_from_cmdline()`；同时避免 headless 路径里出现两个并发 `npm install`。**具体方案见 design.md §B1（含两个选项，待用户确认）。**
- B2 `api_tool.cpp` / `api_tool_editor.cpp`：恢复 HEAD 的上报语义——访问器失败必须**有消息**；「store 尚未生成」按 `codegen-baseline.md` §api store 未生成时不要报 Error 降到 `Warning`，其余失败保持 `Error`。**不允许**任何「无消息 return」。允许保留的两处（见 design.md §B2）：`get_loader()` 可重建实例、构造期不读 store。
- B3 `register_types.cpp`：撤销 `godot::ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_CORE)`（实测未修复 step4/6 崩溃，且与 spec 已定稿的「引擎补丁」路线冲突）。
- B4 引擎工作树 `D:/Dev/godot/godot-4.7.2-patched`：恢复被我移除的 `core/extension/gdextension.cpp` 补丁（`close_library()` 卸载前清扫本扩展的 instance binding），并重建该引擎。这是对**仓库外**工作树的状态恢复，执行前按 build spec 报备。

## Acceptance Criteria

- [ ] 1. **codegen 产物零回归（同输入 A/B 差分）**：把 A 涉及的 5 个文件换回 HEAD（指针版）重建，在**同一输入**（同一 api store、同一 `extension_api.json`、同一已编译 TS）下跑 `--generate-types`，产物 `project/gen` + `project/typings` 与值语义版**逐字节一致**（`diff -r -q --strip-trailing-cr` 零差异）。
      > 为什么不以 `misc/verify_codegen.py` 的全流程为判据：`.codegen-baseline` 已过期（基线快照早于 `tests/instance-hooks/*`、`tests/notification/*`、`icons/GodotJSScript.svg` 的加入，且 typings 分片含引擎文档文本差异），把它当绝对基线会把输入漂移记成本改动回归。是否刷新基线由用户定。
- [ ] 2. **冷轮七步**：`python .agent_tmp/run_pipeline.py --clean` 下 step2 / step3-apigen / step4 / step5 / step6 的 `exit==0`；`ERROR:` 仅允许两类且逐条归因：(a)「TS 未编译」类（冷 project 里 tsc 在 step5 才跑：step2 4 条 = 2 条 `javascript file is missing` + 2 条派生 `Failed to create an autoload` / `script failed to load`，step3-apigen 与 step4 各 2 条）；(b) step3-dump 崩溃伴随的 `Parameter "singleton" is null.`（该步 `0xC0000005` 豁免，判成败按产物存在性）。**step5 / step6 必须 `ERROR==0`**。
- [ ] 3. **热轮**：再跑一次不带 `--clean` 的全流程，结论与冷轮一致。
- [ ] 4. **step7**：`--verbose` ×3，每次 `exit==0`、`GODOTJS_TEST_PROJECT_COMPLETED==1`、`GODOTJS_TEST_PROJECT_FAILED==0`、`Orphan StringName==0`。
- [ ] 5. **doctest**：`96|96`、`1437|1437`（未放宽、未删除任何断言）。
- [ ] 6. **无新增泄漏**：值语义化后 `--verbose` 退出期不出现 decl 泄漏类统计（`PagedAllocator`/`ObjectDB instances were leaked`/`resources still in use` 计数不得高于改造前）。
- [ ] 7. **产物一致性**：`bin/windows/*.dll` 与 `project/addons/godotjs-ext.daylily-zeleen/bin/windows/*.dll` md5 一致；`third/godot-cpp` 的 `git status --porcelain` 为空。
- [ ] 8. **可归因**：每一项保留下来的非常规改动（B1 的阻塞位置、B2 的 Warning 级别、引擎补丁 B4）在任务 `report.md` 里有实测依据与取舍说明；无一项靠抑制/降级/静默掩盖。

## Constraints

- 允许改：`src/**`、`misc/**`、`.trellis/spec/**`（收尾更新规范）。
- 不改：`third/godot-cpp`（交付时 `git status --porcelain` 必须为空）、`project/tests/**` 的断言语义来「变绿」、`SConstruct` 的依赖 tag / 版本常量、`.github/workflows/`。
- 不 commit / push / 还原文件（引擎工作树补丁恢复已获许可，见 B4）。
- 临时文件只放 `./.agent_tmp/`。
- 验收必须来自**实测**；机制性断言未实测不得写入。
- 不得降级日志级别、不得削弱 `jsb_check` / 断言来变绿。

## Stop Conditions

- 值语义化导致 codegen 产物出现**无法归因**的 diff → 停，交用户。
- 需要改 `third/godot-cpp` 才能完成 A → 停，交用户。
- B2 恢复上报后冷轮出现新的 ERROR，且错误来自「调用方本不该在 store 缺失时调用」以外的原因 → 停，交用户定级别/位置。
- 同一问题不同解法 ≤ 5 次。
