# P2 godotjs-shared 共享库：实施与终止回退

> 注：本文为历史归档；文中提及的 TASK_STATUS.md、.本地文档/ 等路径已不存在，现行机制见 `.trellis/spec/`。

> 归档说明：已终止（2026-08-24）。数据层进独立 DLL 后出现双 godot-cpp 副本全局态冲突
> （崩点漂移：String/CowData、ProjectSettings 双重析构、GDExtension 卸载），无法根治，
> 经用户拍板整体回退单库。前半卷为实施记录，后半卷为方向调整与回退执行记录。
> 存档价值结论（跨任务有效）：① 函数内 static 持引擎堆对象在静态析构期有二次析构风险
> （get_variant_operator_name 字面量表修复已保留）；② 双 godot-cpp 副本共存时跨模块
> Variant/String 传递不安全；③ DLL atexit 相对引擎 teardown 顺序不可假设。

---

## 十三、P2 godotjs-shared 实施（2026-08-24 启动，当日完成 ✅ 含 R1 降险调整）

> 目标（见第五章 P2 行）：建共享库目标与导出宏（幂等初始化 + 仅 runtime 反初始化的非对称生命周期 + ABI 版本检查）；迁入 api_tool core、StringNames、Logger、IConsole 数据层、preset gen；完成 4.1 spike。验收：三库链接成功；现有功能冒烟通过。

### 13.1 勘察结论（2026-08-24，基于代码事实）

1. **godot-cpp 构建形态**：`third/godot-cpp/SConstruct` 以 SConscript 方式编入主构建，产出**静态库** `bin/libgodot-cpp{suffix}.lib` 并 `env.AppendUnique(LIBS=[...])`（godotcpp.py:607-620）。当前单 extension 与它静态链接。
2. **4.1 手动初始化最小调用面已核实**（godot.cpp:115-196）：接口表装载 = `internal::load_gdextension_interface(p_get_proc_address)`（gdextension_interface_loader.hpp:291 公开声明）；随后 `Variant::init_bindings()`（variant.hpp:54）+ `internal::register_engine_classes()`（wrapped.hpp:180）。三者均为 godot-cpp 静态库内符号，godotjs-shared 静态链同一份 godot-cpp 后即可自行调用——**无需触碰 GDExtensionBinding::init**（其需要 InitData/r_initialization，不适用）。每个 DLL 有独立的接口表/初始化标志副本，shared 库需自带幂等守卫。
3. **迁移目标的依赖闭包**：
   - api_tool core：仅依赖 godot-cpp + `runtime/jsb.config.h`（→ jsb.gen.h，SConstruct:612 在编译前生成）；
   - StringNames/Logger/IConsole：依赖 `jsb_internal_pch.h`/`jsb_macros.h` 链 → `../compat/jsb_compat.h` + config/gen/version 头，均为头文件级依赖；
   - IConsoleOutput 注册表消费方：runtime essentials（注册）、editor REPL（P3/P4 改走桥接后仍注册）；Logger 写 console 注册表；
   - preset gen：`src/runtime/jsb_project_preset.gen.cpp`（330KB 生成物，`get_source_rt`/`get_source_ed`），消费方 Environment::init（rt）与 editor apply_file（ed）。
4. **api_tool/editor/*（parser/generator/store_writer/editor 编排）留在 editor 库**（P4 迁移），其 include 路径 `<api_tool/...>` 不变即可跨库引用 shared 导出的 api_tool core 符号。
5. **Windows 多 DLL 符号规则**：各 DLL 独立符号空间，无导出即无冲突；godotjs-shared 只导出 `JSB_SHARED_API` 标注的公共面（R2 对策）。

### 13.2 实施步骤

1. `src/godotjs_shared/shared_api.h`：`JSB_SHARED_API` 导出宏（MSVC dllexport/dllimport + GCC visibility）；`jsbl_initialize(get_proc_address, library)` / `jsbl_is_initialized()` / `jsbl_deinitialize()` / `jsbl_get_abi_version()`。
2. **4.1 spike**：`src/godotjs_shared/godotjs_shared.cpp` 实现幂等手动初始化（load_gdextension_interface → Variant::init_bindings → register_engine_classes，含 ABI 版本常量校验），先以最小 main 链接验证可加载（实际验证并入第 5 步冒烟）。
3. 目录迁移（git mv 保历史）：api_tool core → `src/godotjs_shared/api_tool/`；string_names/logger/console → `src/godotjs_shared/{string_names,logger,console}/`；preset 生成输出路径改到 `src/godotjs_shared/preset/`。include 路径统一为 `<godotjs_shared/...>`。
4. SConstruct：新增 `godotjs-shared` SharedLibrary 目标（产物名 `godotjs-shared` 无 target 后缀，C1）；extension 目标从源列表移除已迁文件并链接 shared 库；CPPPATH 增加 `src/godotjs_shared`；preset 生成段输出路径调整。
5. 扩展入口接线：runtime `jsb_gdextension_init` 与 editor `jsb_editor_library_init` 最开头调用 `jsbl_initialize`；runtime shutdown 调 `jsbl_deinitialize()`（D5 非对称生命周期）；ABI 不匹配则 entry 返回 false 拒载。
6. 冒烟验收：三库链接成功；双 doctest 套件通过；verify_codegen diff 全绿；TS 集成测试跑通。

### 13.3 执行记录

- **4.1 spike 结论（已实测）**：godotjs-shared 经 `jsbl_initialize()` 转发 `get_proc_address/library` 后调用 `GDExtensionBinding::init()`（自备 InitData + 本地 GDExtensionInitialization sink）可完整完成手动初始化，接口表/Variant 绑定/引擎类注册均可用——R1 的"手动初始化不可行"风险解除。**但**：双 godot-cpp 静态副本并存时，api_tool core 中跨模块传递的 godot::String/StringName 生命周期管理出现崩溃（generate-types 阶段 `try_load_compatible_method_bind` → 引擎侧 CowData 解引用），定位为双副本各自管理 Variant 引用计数的深层冲突，触及 F2 兜底的触发条件。
- **实施结果（含 R1 降险调整）**：
  - 已落库：`src/godotjs_shared/shared_api.h`（JSB_SHARED_EXPORT/IMPORT 宏 + jsbl_initialize/is_initialized/deinitialize/get_abi_version + JSBL_ABI_VERSION=1）、`godotjs_shared.cpp`（幂等手动初始化 + D5 非对称生命周期）；api_tool core、StringNames、Logger、IConsole、preset gen 源码已迁移至 `src/godotjs_shared/{api_tool,string_names,logger,console,preset}/`（include 路径统一 `<godotjs_shared/...>`）；SConstruct 新增 godotjs-shared 共享库目标（产物名无 target 后缀）；runtime/editor 入口已接入 jsbl_initialize + ABI 校验；preset 生成路径改到 shared 目录。
  - **降险调整（R1 触发）**：api_tool core / StringNames / Logger / preset gen 当前从新位置**双侧各编译一份**（不跨 DLL 导出符号），godotjs-shared.dll 仅承载模块边界（jsbl_*）+ IConsole 注册表；三库链接目标达成。数据层进 DLL 推迟到 4.1 双副本生命周期问题解决后（候选方案：F2 纯 C ABI 化 api_tool 查询面、或共享库与扩展共享同一份 godot-cpp 动态副本）。
- **验收结果（2026-08-24）**：
  - 三库链接成功：bin/windows 下 godotjs-shared.windows.editor.x86_64.dll/.lib 与 godotjs-ext.windows.editor.x86_64.dll 同时产出并安装到 addon bin；
  - runtime doctest：27/27 通过（443 断言），exit=0；
  - editor doctest：2/2 通过，exit=0；
  - verify_codegen.py：✅ 生成产物与基线一致（exit=0）；
  - 双套件无资源泄漏警告。
- 遗留观察项：generate-types/api-generate 进程在引擎关机段退出码非零（既有现象，脚本以产物为准）；`[API Tool] initialize` 多对打印为惰性调用所致（幂等无害）。

- **双副本冲突根因定位（A/B 实验，2026-08-24）**：
  - 实验 1：jsbl_initialize 完整执行 `GDExtensionBinding::init()`（重跑 Variant::init_bindings + register_engine_classes + 覆写本模块 token/library 全局态）→ generate-types 在 codegen 运行中崩（`String::operator=` → 引擎侧 `CowData::_unref`），或 cleanup 卸载扩展时崩（`GDExtension::~GDExtension → Object::~Object`）。两类崩点漂移 = 内存破坏型 UB。
  - 实验 2：jsbl_initialize 仅置标志（no-op，不加载接口表）→ 全流程绿（生成成功、cleanup 正常、verify ✅）。
  - **结论**：崩溃与 shared DLL 里 godot-cpp 副本的重复初始化直接相关——两份静态副本的绑定表/全局态在进程内共存时，跨模块 godot::String/StringName 的引用计数管理被破坏。4.1 的"手动初始化"技术可行，但**双副本共存**本身在当前 godot-cpp 静态库形态下不安全。
- **最终形态**：jsbl_initialize 当前为保守实现（置标志 + 预留 MINIMAL_INIT 分支），shared DLL 不触碰 godot-cpp 全局态；数据层双侧各编。恢复数据层进 DLL 的前置条件（任一）：① godot-cpp 改出动态库形态供多模块共享；② api_tool 查询面 F2 化（纯 C ABI，godot 类型不出边界）。

---

## 第十三章 P2（godotjs-shared）—— 已终止回退

### 13.1 方向调整决定（2026-08-24）

用户决定**终止 P2 拆库方向，回到单库架构**：保留 C++ codegen 功能（P1 全部成果），
撤销 godotjs-shared 共享库。直接动因：

1. 完整共享形态下 verify 的 api-generate 步骤存在退出阶段 fastfail 崩溃（0xC000027B），
   手动单独跑同一命令却成功；崩点漂移（String/CowData、ProjectSettings 双重析构、
   GDExtension 卸载三种签名），为双副本 godot-cpp 全局态冲突的典型表现。
2. 深挖需引擎级调试（ASan / 引擎 debug 构建逐帧追），成本与收益不匹配。

### 13.2 P2 期间有价值的结论（存档）

1. **函数内 static 持引擎堆对象的隐患**（该修复在单库形态下同样正确并已保留）：
   `api_tool_types.cpp` 的 `get_variant_operator_name` 原 `static std::array<String, OP_MAX>`
   在静态析构期晚于引擎 teardown 触发 CowData 二次递减。现改为
   `static const char* kNames[]` 字面量表 + 越界保护 + 按值返回 `godot::String`
   （头文件签名同步由 `const String&` 改为按值）。
2. 双 godot-cpp 副本共存时，「非入口模块构造 Variant/String 传引擎」的路径不安全：
   `UtilityFunctions::print` 多参打包实测必崩（ptrcall 的 Vector<Variant> 析构段），
   ERR_PRINT/vformat 类宏同属危险路径。
3. DLL atexit / 静态析构顺序相对引擎 teardown 不可假设，任何跨二进制的引擎托管句柄
   （String/StringName/Variant）传递都需要生命周期论证。

### 13.3 回退执行记录

- `git mv` 将 src/godotjs_shared/{api_tool,string_names,logger,console} 搬回原位
  （api_tool → src/api_tool，其余 → src/runtime/internal），保 rename 历史；
  删除 godotjs_shared.cpp / shared_api.h / preset 目录。
- api_tool.{h,cpp}、api_tool_types.h、string_names/logger/console 各文件内容回退到 HEAD
  （P2 的 JSB_SHARED_API 宏标注、jsbl_* 入口、g_shutdown latch 一并撤销）；
  仅 reapplied 13.2-1 的字面量表修复。
- consumer 侧 16+ 处 include 还原旧路径；runtime/editor register 的 jsbl_initialize/
  deinitialize/ABI 校验删除；jsb_project_preset.h 撤 JSB_SHARED_API 标注。
- SConstruct：preset 生成路径改回 runtime_dir；源收集恢复收编 src/api_tool/*.cpp 与
  core/*.cpp（保留 editor/codegen 收编行）；整段删除 godotjs-shared 目标、链接与 Install；
  CPPPATH 删 godotjs_shared 行。
- 回退后 diff 面貌 = 纯 P1 codegen + 字面量表修复；`git grep jsbl_|JSB_SHARED|godotjs_shared`
  于 src/SConstruct 清零。
- P2 工作区快照备份于 .agent_tmp/p2_backup/（src_godotjs_shared + SConstruct）。
- 回退后验收（全部真实执行）：
  - 构建：`scons platform=windows target=editor dev_build=yes tests=yes -j5` exit=0；
  - verify_codegen.py：✅ 校验通过、生成产物与基线一致（exit=0；api-generate 步骤干净退出，
    无 File-not-found 风暴、无 fastfail——P2 形态下的异常在单库形态下消失）；
  - runtime doctest：27/27 通过（443 断言），exit=0；
  - editor doctest：2/2 通过，exit=0；
  - 双套件关机段均正常走完 `[jsb] shutdown → api_tool finalize`。
- 遗留清理：bin/ 下旧 godotjs-shared.* 二进制已删（不入库）；api_tool_types.cpp 的
  字面量表修复保留（单库形态下同样正确，见 13.2-1）。

