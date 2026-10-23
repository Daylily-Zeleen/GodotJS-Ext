# 执行计划

> 所有命令在仓库根 `D:/Dev/godot/GodotJS-Ext` 执行（除注明 `cd project`）。
> `ENG=D:/Dev/godot/godot-4.7.2-patched/bin/godot.windows.editor.x86_64.llvm.console.exe`（可用 `JSB_GODOT` 覆盖）。
> 进度写本任务 `report.md`，每完成一个可验证步骤追加一行。

## 阶段 0：前置（含一次需报备的仓库外状态恢复）

- [ ] 0.1 报备：恢复 `D:/Dev/godot/godot-4.7.2-patched` 里被我移除的 `core/extension/gdextension.cpp` 补丁并重建该引擎；主工作树 `D:/Dev/godot/godot`（两份补丁齐全）不动。
- [ ] 0.2 `cd D:/Dev/godot/godot-4.7.2-patched && git apply --include=core/extension/gdextension.cpp /d/Dev/godot/GodotJS-Ext/.agent_tmp/engine_patches/godot-4.8-dev-extension-unload-and-doc-guard.patch`，`git status --porcelain` 应显示 `core/extension/gdextension.cpp` + `editor/doc/editor_help.cpp` 两个文件。
- [ ] 0.3 重建引擎（**后台** `async: true`，不轮询）：`scons platform=windows target=editor use_llvm=yes debug_symbols=yes -j16`（在该工作树内）。重建完成前先做阶段 1 的只读检查。

## 阶段 1：改动前取证（只读）

- [ ] 1.1 `python misc/verify_codegen.py --godot <ENG> --diff-only` —— 记录改造前的基线状态（应全绿或差异可归因）。
- [ ] 1.2 记录当前 canonical dll 的 md5（`bin/windows/godotjs-ext.windows.editor.x86_64.dll` 与 `project/addons/godotjs-ext.daylily-zeleen/bin/windows/` 两处）。
- [ ] 1.3 值语义化前置核实：确认 5 个 map 的 key 无重复（按 design §A.3 的检测方式；若发现重复 → 停并交用户）。

## 阶段 2：A —— TypeDB / DocCache 值语义化

- [ ] 2.1 `jsb_codegen_type_db.h`：5 个 map 的 value 改为值类型；删 `owned_*`、`ordered_*()`、`~TypeDB()` 声明。
- [ ] 2.2 `jsb_codegen_type_db.cpp`：删 `~TypeDB()` 定义；5 个 loader 按 design §A.2 改原地构造（key 先算、跳过分支前置、`_load_primitive_type` 返回 `void` 且只取一次槽位）。
- [ ] 2.3 `jsb_codegen_docs.h/.cpp`：`docs_` 值语义化；删 `destroy()` / `destroy_doc_cache()` 与其调用点（含 `~TypeDB` 内那处）。
- [ ] 2.4 `jsb_codegen_generator.cpp`：5 处 emit 循环改遍历 map（173/181/192/200/208）。
- [ ] 2.5 全仓搜引用：`owned_classes_` / `owned_primitives_` / `owned_singletons_` / `owned_globals_` / `owned_utilities_` / `ordered_*()` / `destroy_typedb` / `destroy_doc_cache` 必须零命中（LSP 查 `find_class` / `find_primitive` 引用点确认签名未变）。
- [ ] 2.6 编译 + 部署：
  `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6`
  `cp bin/windows/godotjs-ext.windows.editor.x86_64.dll project/addons/godotjs-ext.daylily-zeleen/bin/windows/`
- [ ] 2.7 产物零回归：`python misc/verify_codegen.py --godot <ENG>`（含双轮确定性）。任一 diff 逐项归因；无法归因 → 停。

## 阶段 3：B —— 排查期改动收口

- [ ] 3.1 **B3**：删 `register_types.cpp` 里的 `godot::ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_CORE)` 及其注释块。
- [ ] 3.2 **B2**：按 design §B2 恢复 `api_tool.cpp` 的上报语义（宏恢复 `ERR_FAIL_*_MSG`；「store 尚未生成」降级为 `Warning`，其余保持 `Error`；保留可重建的 `get_loader()` 与「构造不读 store」两点，各自写明依据）。
- [ ] 3.3 **B2 证据判定**：先不改 `api_tool_editor.cpp`，跑冷轮 step2/step3-apigen/step4，用日志判定 `generate_api_tool_data()` 是否还需要去掉 `initialize()`；只在实测产生 `Error` 时才动它，并把证据写进 report。
- [ ] 3.4 **B1**：按 design §B1 选项 2 实施（`ensure_tsc_installed()` 恢复 HEAD 形态；headless 不起进程；命令行入口 `_install_project_files_from_cmdline()` 在 `try_install_project_files()` **之前**用 `OS::execute()` 同步装包，失败时按原级别报错）。
- [ ] 3.5 编译 + 部署（同 2.6 两条命令）。

## 阶段 4：验收（每组改动各一次，不重复重编）

- [ ] 4.1 冷轮：`python .agent_tmp/run_pipeline.py --clean` → step2/3-apigen/4/5/6 `exit==0` 且 `ERROR==0`（step3-dump 的 `0xC0000005` 豁免，按产物存在性判成败）。
- [ ] 4.2 热轮：`python .agent_tmp/run_pipeline.py`（不带 `--clean`），结论与冷轮一致。
- [ ] 4.3 step7 ×3：`cd project && <ENG> --audio-driver Dummy --headless --path . --verbose` → `exit==0`、`COMPLETED==1`、`FAILED==0`、`Orphan StringName` 计数 0（**必须带 `--verbose`**）。
- [ ] 4.4 泄漏对照：step6 的 `--verbose` 日志中 decay 类统计（`ObjectDB instances were leaked` / `resources still in use` / `PagedAllocator`）不高于改造前。
- [ ] 4.5 doctest：`scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` → 部署该 dll → `cd project && <ENG> --headless --path . --jsb-run-tests` → `96|96`、`1437|1437`。
- [ ] 4.6 **收尾重编**：重跑 canonical 编译命令并重新部署（交付 dll 不得是测试变体），两处 md5 一致；`third/godot-cpp` 的 `git status --porcelain` 为空。

## 阶段 5：收尾

- [ ] 5.1 `report.md`：A/B 各组结论 + 每条保留改动的实测依据 + 已回退清单 + 已知遗留。
- [ ] 5.2 规范更新（`trellis-update-spec`）：`architecture-constraints.md` 的「处置：钉住扩展 DLL」一节已被撤销（PIN 已废），改成当前机制与处置；`codegen-baseline.md` 的 step4 判据与 api store 上报口径按最终实现校准；`cpp/coding-standards.md` 增一条「容器值语义优先，避免 map+owned 双登记」。
- [ ] 5.3 不 commit / push（需另行授权）。

## 回滚点

- 阶段 2（A）与阶段 3（B）互相独立，可分别整组回退，不影响对方。
- 阶段 4 任何一个红：先确认是「漏跑前置步骤」还是真回归；真回归按组回退并交用户，不得靠降级日志/削弱断言收场。
