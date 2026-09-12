# 静态绑定 class 族 thunk 共用化 — implement.md
> 2026-09-12 更新：规划工件（父任务 09-11 + 三子任务全部工件）经用户指令已**单独提交推送**，原“随步骤 1 一并提交”裁决作废。本任务实施提交只含代码与任务目录增量。

## 执行清单（有序，每步一提交）

1. **SConstruct 接线**
   - `opts.Add(EnumVariable("binding_mode", "...", "static", ["static", "shared", "dynamic"]))` 替换 `SConstruct:36` BoolVariable；顶部补 `EnumVariable` import
   - `:820-836` 分支改按 `binding_mode != "dynamic"` 驱动；codegen 命令追加 `--binding-mode <mode>`
   - 宏注入：static→`JSB_WITH_STATIC_BINDINGS`；shared→`JSB_WITH_STATIC_BINDINGS`+`JSB_WITH_SHARED_THUNKS`；dynamic→无
   - 同步迁移（干净切换不留别名）：`.github/workflows/ci.yml:91,101`、`misc/bench_matrix.py:142`、spec `build/scons-build.md:25`、`cpp/generated-files.md:17`
2. **共享 thunk**：新增 `src/static_binding/thunks/shared_class_methods.h`（`SharedClassMethodData` + `shared_class_method_thunk`，体迁移自 `class_methods.h:66-125`，三处替换见 design §2.2）
3. **codegen shared 发射**：`static_binding_codegen.py` 增 `--binding-mode`；先跑 `--binding-mode static` 与改造前输出 diff（必须为空，形态 A 零回归判据）→ 再实现 shared 路径（签名去重 → 单点实例化 → 每类表 → `find_<Class>` 返回 binding 指针；`dispatch.h` 增 shared 类型/声明）
4. **挂载端**：`jsb_object_bindings.cpp:145` 增 `JSB_WITH_SHARED_THUNKS` 分支（eager 解析 + `Method(member_name, binding->thunk, &binding->data)` + 失败回退，见 design §2.4）
5. **运行时暴露**：`jsb_bridge_module_loader.cpp` `STATIC_BINDING_ENABLED` → `BINDING_MODE` 三态字符串；`benchmark.ts`/BENCH_JSON `staticBinding` 适配
6. **bench_matrix**：`--leg` 增 `shared`（构建 flag = `binding_mode=shared`）
7. **spec 收尾**：`generated-files.md`（codegen 条目补 `--binding-mode`）、`scons-build.md`（参数表）同步——经 trellis-update-spec 流程

## 验证命令（每步构建后跑，验收纪律见 spec test/index.md）

- 形态 A 零回归：默认构建 `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` → C++ 双套件 + TS 集成全绿（与基线一致）
- shared 构建：同上加 `binding_mode=shared`
- 实例化计数：`grep -c "shared_class_method_thunk<" src/static_binding/gen/dispatch_class.gen.cpp` ≤ 1,530；形态 A 路径 `class_method_thunk<` 计数在 static 模式产物中不变
- C++ 双套件（两形态各跑）：`"D:/Dev/godot/godot/bin/Godot_v4.7.1-stable_win64.exe" --headless --path ./project --jsb-run-tests`（exit 0、无 Orphan、无泄漏）
- TS 集成：api 数据生成 + `cd project && node_modules/.bin/tsc --noCheck` + `--verbose` 跑至 `GODOTJS_TEST_PROJECT_COMPLETED`（重点覆盖 class 方法默认参数调用路径）
- bench 三腿：`python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix`（static/shared/dynamic；md5 前后核验、invalid=0）

## 风险文件 / 回滚点

- `SConstruct`（构建接线，错则全构建挂）——步骤 1 单独提交
- `jsb_object_bindings.cpp`（挂载热路径）——步骤 4 单独提交
- `static_binding_codegen.py` 大改——步骤 3 以 "static 模式输出 diff 为空" 作进入 shared 发射的前置闸门

## Definition of Done

- 执行清单全勾 + prd.md AC 全绿 + 三腿体积/性能数字记录进报告（供父任务收口）
