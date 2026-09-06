# P1 codegen C++ 化实施

> 注：本文为历史归档；文中提及的 TASK_STATUS.md、.本地文档/ 等路径已不存在，现行机制见 `.trellis/spec/`。

> 归档说明：已完成（2026-08-23 启动，2026-08-24 完成）。TS codegen 全量重写为 C++，
> verify_codegen.py 对基线 diff 全绿，双 doctest 套件通过。
> 成果物：src/editor/codegen/*（Writer/TypeDB/Mutations/Annotations/Generator），
> 删除 jsb.editor.codegen.ts 与双侧 jsb_editor_utility_funcs。

---

## 十二、P1 codegen C++ 化实施（2026-08-23 启动，2026-08-24 完成 ✅）

目标：用 C++ 重写 `scripts/jsb.editor/src/jsb.editor.codegen.ts`（4271 行）的生成逻辑，数据源直连 api_tool + ClassDB + NamingUtil（与 JS 版同源），删除 `jsb.editor.codegen.ts` 消费路径与两侧 EditorUtilityFuncs（D1/D4）。验收：`verify_codegen.py` 对 `.本地文档/代码生成基线` diff 全绿；双 doctest 套件回归通过；codegen 单测落 editor 套件（T1）。

### 12.1 勘察结论（数据流）

- JS 版 TypeDB 消费 5 类数据：classes / primitive_types / singletons / globals / utilities，全部来自 `jsb.editor.*`（EditorUtilityFuncs 组装）。C++ 侧这些组装逻辑已存在（`jsb_editor_utility_funcs.cpp` 的 build_* 系列：api_tool 查询 + NamingUtil 命名映射），C++ 版生成器直接内联同等逻辑即可，无需任何 JS 中转。
- 生成主流程（TSDCodeGen.emit）：aliases → singletons（复用 class emit，singleton_mode）→ classes（跳过 singleton 同名类；`is_original_class_exposed` 过滤）→ primitives → globals → utility functions → jsb.utility_functions（GLOBAL_GET/EDITOR_GET 硬编码 2 条）→ IgnoredClasses/IgnoredClassEnums 接口 → jsb.runtime.gen.d.ts（annotation_types 序列化）→ cleanup 多余分片。分片规则：单文件 >1024*900 字节或 >9200 行则新开 `godotN.gen.d.ts`。
- 类型名推导链（TypeDB.make_classname/make_typename）：VariantNames（godot 内部名 + GodotJS 名双向映射）→ RemappedPrimitiveTypeNames（NIL→any/BOOL→boolean/INT→int64/FLOAT→float64/STRING→string）→ KeywordReplacement（default_ 等 17 项）→ RESOURCE_TYPE hint 展开联合类型 / ARRAY hint 泛型参数 / DICTIONARY hint 键值泛型 → 未暴露类回退 `IgnoredClasses["X"]`/`find_exposed_base_class`。
- TypeMutations 表约 570 行（AnimationLibrary/AnimationMixer/GArray/GDictionary/Callable 等 20+ 类的泛型化覆写，含正则行变异 mutate_parameter_type/mutate_return_type/mutate_template）+ InheritedTypeMutations（Node/AnimationMixer 继承传播）+ annotation_types 表约 580 行（jsb.runtime.gen.d.ts 的类型描述符常量）——这两张表是移植工作量主体，逐字面迁移。
- 场景/资源 d.ts：SceneTSDCodeGen/ResourceTSDCodeGen 消费 `GodotJSEditorHelper.get_scene_nodes/get_resource_type_descriptor` 返回的 Dictionary 描述符（DescriptorType 枚举 C++ 侧已有镜像，jsb_editor_helper.cpp:50），TypeDescriptorWriter 按 15 种描述符类型递归序列化 + add_import 处理 User 类型的 import 行。
- `_request_codegen` 反向调用链（C++→JS 用户脚本导出的 `codegen(request)` 函数）：随 P1 一并删除，用户脚本自定义 codegen 能力终止（D1 既定）。
- editor bundle（`scripts/out/jsb.editor.bundle.js` 经 SConstruct PresetDefine 嵌入）：P1 后仅剩 jsb.editor.main.ts（auto_complete/run_npm_install），bundle 内容缩减但保留模块名。
- 关键等价物：`names.get_variant_type`（JS）= Variant::get_type_name 往返（C++）；`get_class_doc` = api_tool::editor::find_document（.bdoc 直接读）；VERSION_DOCS_URL 硬编码 "https://docs.godotengine.org/en/latest"。

### 12.2 文件规划

- `src/editor/codegen/jsb_codegen_writer.h/.cpp`：Writer 体系（AbstractWriter/FileWriter/IndentWriter/ModuleWriter/NamespaceWriter/ClassWriter/EnumWriter/InterfaceWriter/ObjectWriter/PropertyWriter/GenericWriter/TypeDescriptorWriter）+ DocCommentHelper + name_string/KeywordReplacement。
- `src/editor/codegen/jsb_codegen_type_db.h/.cpp`：数据装载（api_tool 直查）+ make_classname/make_typename/make_args/make_return/make_signal_type/make_literal_value + VariantNames/RemappedPrimitiveTypeNames/get_primitive_type_name_as_input。
- `src/editor/codegen/jsb_codegen_mutations.h/.cpp`：TypeMutations/InheritedTypeMutations 表 + get_type_mutation 合并逻辑（含正则行变异的 std::regex 实现）。
- `src/editor/codegen/jsb_codegen_annotations.h/.cpp`：annotation_types 常量表（GDictionary 字面量构建）。
- `src/editor/codegen/jsb_codegen_generator.h/.cpp`：TSDCodeGen 主流程 + SceneTSDCodeGen/ResourceTSDCodeGen + CodegenTasks 进度（对接 GodotJSEditorPlugin add_progress_task 静态方法或简化为日志）。
- plugin 重接线：generate_types/generate_scene_nodes_types/generate_resource_types 改调 C++ 生成器（同步执行后回调 complete）；删除三段内嵌 JS 脚本字符串与 `_on_generate_completed`。
- 清理项：`scripts/jsb.editor/src/jsb.editor.codegen.ts` 删除；`jsb_editor_utility_funcs.cpp/hpp`（editor 侧实现 + runtime 侧 throwing stub）删除；`jsb_environment.cpp` dummy module 注释更新；SConstruct 收编新目录。

### 12.3 执行记录

- （进行中）Writer 体系与 TypeDB 移植。
- 会话接续记录（2026-08-24，自群组会话 mt4ncewnev1oq2 迁入 CLI 继续）：
  - 清单 1–7 已完成（Writer/TypeDB/Mutations/Tables/Annotations/Generators/SConstruct 收编）；清单 8 基本完成——`_generate_types_from_cmdline` 已加 `is_scanning()` 重延迟等待修复首轮扫描竞态（r1 失败根因）。
  - r2 校验失败新根因待查：约 30 次 `Object was deleted while awaiting a callback` 后消息队列 flush 硬崩、产物未落盘；等待修复已生效但生成未跑完。另 r1 日志中 `[API Tool] initialize` 打印出现 4 对（唯一调用方 `register_types.cpp:104`），疑似多进程输出交错，未定论。
  - 本轮补齐文档注释接线（上一会话改了一半）：`TypeDB` 挂 `DocCache docs_` 并暴露 `find_doc`；`ClassWriter` 声明 `docs_` 成员并在初始化列表解析、覆写 `get_class_doc()`（从 protected 移到 public：EnumWriter 经非派生基类指针调用）；补实现此前只有声明没有实现的 `destroy_typedb`（顺带修复 TypeDB owned decls 泄漏）。构建错误全部收敛在 writer.cpp 一处（errors_round6 的 mutations/type_db 错误已在上一会话修完）。
  - 构建转绿后端到端调试，连续定位并修复三个运行期根因：
    1. **消息队列洪泛崩溃**（r3 复现：`Message queue out of memory` 后硬崩）：`_generate_types_from_cmdline` 用 `call_deferred()` 自我重排队等待首轮扫描，同帧 flush 内重排速度快于扫描线程推进，队列撑爆。改为连接 `SceneTree::process_frame` 信号每帧重试一次（本 godot-cpp 生成的 SceneTree 无 create_timer/类型化信号 API，经基类 Object 按名连信号；新增 `_await_scan_then_generate_from_cmdline` 先断连再重入，扫描结束后自动收尾）。
    2. **空场景字典误判**：`SceneTSDGenerator::emit_scene_node_types` 把空 children 字典当失败，但基线 `Worker.nodes.gen.ts` 就是 `"path": {}`（旧 TS 仅对 undefined 抛错）——去掉空字典报错。
    3. **v8 isolate 未进入**（`HandleScope::CreateHandle` 致命错误）：helper 三处直调入口（`_request_codegen`、`get_resource_type_descriptor` 的 PackedScene 分支、`get_scene_nodes`）在未 Enter 的 isolate 上建 HandleScope；旧流程从桥接回调进入天然带作用域，C++ 直调必须自建 `JSB_ISOLATE_SCOPE`(+HandleScope/ContextScope)。修复后 `--generate-types` 全链路成功出产物。
  - 首轮基线 diff（57 处）定位三处移植偏差并修复：
    1. `make_copyright_header` 第 5 行行尾写成字面 `\n`（jsb_codegen_defs.cpp:157 转录笔误）——44 个 gen 声明文件全部因此差异；
    2. 注解表 `icon` 参数放错层（`_context` 应在内层函数参数表，annotations.cpp 转录错误）；
    3. Writer 的 Union/Intersection 分隔符运算符优先级转录错误：TS `` `${multiline ? "" : " "}| ` `` 的三元只包前导空格，C++ 写成整个加法在 false 分支 → 多成员联合/交叉类型的 `| `/`& ` 连接符整体丢失（typings 分片与 jsb.runtime.gen.d.ts 差异的主因之一）。
  - 注：generate-types 进程退出码非零（139/3221225477）发生在引擎关机拆除段（`[jsb] shutdown` 之后），与校验脚本对 dump 步容忍的「已知关机阶段崩溃」同类，脚本以产物落盘为准判定成功。
  - diff 迭代过程共修复 12 类移植偏差（57 → 22 → 12 → 11 → 7 → 4 → 2 → 0）：
    1. 版权头多余空行（TS join 语义）——44 个 gen 声明文件；
    2. 注解表 icon 参数放错层、signal 泛型放错层 + 内层参数/返回值缺失、rpc/onready 内层返回值应为 void；
    3. Union/Intersection 分隔符运算符优先级转录错误（多行时 | / & 连接符整体丢失）；
    4. ClassWriter::property_ 误委托 base_（__godotRPCMap/__godotNameMap 落到模块层）；
    5. NamespaceWriter 未携带类文档（枚举元素注释全丢）；emit_global 缺 @GlobalScope 常量注释；
    6. 场景子节点属性漏调 property_->finish()（全部输出空 {}）；
    7. 泛型合并语义错误（按键联合→整体替换，Node 的 Map 泛型泄漏进 AnimationMixer）+ ClassEmitOptions.generic_parameters 由 HashMap 改 LocalVector<Pair> 保插入序；
    8. 发射循环字母排序→装载序迭代（owned_* 有序访问器），分片边界恢复一致；
    9. 补 JSB_EXCLUDE_GETSET_METHODS（仅 index>=0 属性剔除访问器方法，注意与直觉相反）；
   10. 关键字表补 function→function_；rpc/rpc_id 的 ObjectRPCNames→GodotRPCNames；
   11. 全局常量经 v8 Number(double) 的精度痕迹（INT64_MAX/MIN=...776000）与枚举值 int32 截断（ARRAY_FLAG_*=0）按旧行为镜像保留；
   12. js_number_to_string 最短往返格式化；EDITOR_GET 文档改走 write_lines 多行分支。
  - 清单 9/10 完成：删除双侧 jsb_editor_utility_funcs.*、register_editor_types 注册调用、bridge loader 的 jsb.editor 暴露、jsb_environment.cpp dummy module 注释更新、scripts/jsb.editor/src/jsb.editor.codegen.ts。
  - 基线刷新：typings/jsb.editor.bundle.d.ts 按计划收敛为仅含 jsb.editor.main 模块（codegen 模块随 codegen.ts 删除而消失，D1 既定行为）。
  - **P1 验收结果**：
    - verify_codegen.py：✅ 校验通过，生成产物与基线一致（exit=0）；
    - runtime doctest：27/27 通过（443 断言），exit=0；
    - editor doctest：2/2 通过，exit=0；
    - 双套件均无资源泄漏警告。
  - 遗留观察项（不阻塞验收）：generate-types 进程在引擎关机段以 3221225477 退出（与 dump 步相同的既有现象，脚本以产物为准）；`[API Tool] initialize` 在 generate-types 进程中打印多次（幂等无害，来源为 has_generated_data()/get_api_data_files() 等惰性调用，每对日志对应一次检查）。

