# 拆分方案 v1 与验证体系

> 注：本文为历史归档；文中提及的 TASK_STATUS.md、.本地文档/scripts/ 等路径已不存在，现行机制见 `.trellis/spec/`。

> 归档说明：本卷为第一版拆库路线（godotjs-shared 中间层）的完整设计与配套验证体系，
> 已被「双库拆分方案 v2」取代（见仓库根 TASK_STATUS.md）。文内交叉引用（如 4.3、D1、D6）
> 指本卷原始章节号；verify_codegen.py / 代码生成基线的使用说明另见《本地构建与测试.md》
> 与仓库根 AGENTS.md「代码生成测试基线位置」「重构校验脚本位置」两节。

---

## 一、现状核实结论（基于代码勘察）

1. 当前是单一 GDExtension（入口 `jsb_gdextension_init`，见 `project/addons/.../godotjs-ext.gdextension`），但 `src/editor/register_editor_types.cpp:66` 已存在独立的 `jsb_editor_library_init` 入口，且有 `godotjs-ext-editor.gdextension.backup` 备份，拆分有历史铺垫。
2. `EditorUtilityFuncs`（runtime 侧 `src/runtime/bridge/jsb_editor_utility_funcs.*` + editor 侧实现 `src/editor/weaver-editor/jsb_editor_utility_funcs.cpp`）的跨库注册模式**判定废弃、终态整体删除**：`g_expose_impl` 写入的是 editor 直接链接的那份 runtime DLL 副本的 .data 段，而 Godot 实际加载的是 `~` 前缀拷贝，注册根本到不了真正运行的实例（当前单 DLL 构建碰巧可用，因为只有一份）。重构终态下代码生成是纯 C++（直接调 api_tool），这组 utility 属纯编辑器功能且不再有 JS 消费方，连同 runtime 侧 throwing stub 一并删除，**不做**“editor 注册、runtime 读取”迁移；随 P1 的 codegen C++ 化直接删除，无需任何过渡数据面（见 4.3、D1）。
3. editor 对 runtime 的直接依赖点（拆分必须全部消除）：
   - `GodotJSScriptLanguage::get_singleton()`：7 个文件（editor_plugin / helper / utility_funcs / export_plugin / repl / statistics_viewer / docked_panel 间接）
   - 直接操作 v8 / `jsb::Environment`：5 个文件（集中在 `generate_types` / `generate_resource_types` / `generate_scene_nodes_types` 的 compile_function 调用、`jsb_export_plugin.cpp` 的 module children 遍历、`jsb_editor_helper.cpp:_request_codegen`）
   - `jsb::internal::` 工具：`Settings`(16 种 getter)、`PathUtil`(4 方法)、`Process`(tsc 启动)、`NamingUtil`、`StringNames`(少量)、`IConsoleOutput`(repl)、`GodotJSProjectPreset`(`get_source_rt`/`get_source_ed`)
   - `api_tool::`：editor 侧 35 处调用
4. codegen 链路：`try_install_project_files()` → `install_project_files()`（写预设文件）→ `load_editor_entry_module()`（load `jsb.editor.main`）→ `ensure_tsc_installed()` → `generate_types()`（compile_function 调 JS 里的 `TSDCodeGen.emit()`）。`scripts/jsb.editor/src/jsb.editor.codegen.ts` 约 4271 行。
5. 预设数据由 SConstruct 生成为 `src/runtime/jsb_project_preset.gen.cpp`（`get_source_rt` + TOOLS_ENABLED 下的 `get_source_ed`），**editor 的 `apply_file()` 同时消费两套**（tsconfig.json、package.json、*.d.ts 等属 rt 集）→ 拆分后预设数据必须移出 runtime。
6. `Environment::init()` 在 TOOLS_ENABLED 下无条件加载 `jsb.editor.bundle.js`（`jsb_environment.cpp:453-473`）→ editor bundle 的加载责任应移交 editor 库。
7. 测试二进制确认存在：`F:/godot/godot/bin/godot.windows.editor.x86_64.exe`。
8. codegen 测试基线已由 `project/original_codegen/` 迁出仓库至 `.本地文档/代码生成基线/`（含 gen/、typings/、tsconfig.json；`.本地文档/` 整体被 .gitignore 排除，不入库）。该目录不随 git 走、worktree 中不会自动出现：本 worktree（refactor/split-editor-runtime）已复制一份自己的 `.本地文档/` 副本，P0 校验脚本等所有对基线的引用一律通过当前检出根的绝对路径（支持环境变量覆盖）定位，即指向本检出自己的副本；若主检出基线日后更新，需手动同步本副本。

---

## 二、目标架构

```
┌─────────────────────────────┐   ┌─────────────────────────────┐
│  editor.gdextension (DLL)   │   │  runtime.gdextension (DLL)  │
│  入口 jsb_editor_library_init│   │  入口 jsb_gdextension_init   │
│  依赖: godot-cpp + godotjs-shared  │   │  依赖: godot-cpp + godotjs-shared  │
│  内容:                       │   │  内容:                       │
│   - EditorPlugin/Dock/REPL  │   │   - Script/ScriptLanguage   │
│   - ExportPlugin            │   │   - bridge/ weaver/ impl/   │
│   - codegen 编排(原api_tool/ │   │   - Environment/模块加载     │
│     editor + 触发逻辑)       │   │   - ClassDB 桥接接口(新增)    │
└──────────────┬──────────────┘   └──────────────┬──────────────┘
               │      动态链接（符号导入）           │
               └───────────────┬───────────────────┘
                               ▼
                    ┌──────────────────────┐
                    │  godotjs-shared (普通 DLL)   │
                    │  非 GDExtension       │
                    │  - api_tool core      │
                    │  - StringNames        │
                    │  - Logger             │
                    │  - IConsoleOutput 数据层│
                    │  - ProjectPreset 嵌入数据│
                    └──────────────────────┘
```

- **godotjs-shared**：普通动态库（**不能是 GDExtension**——editor 直接链接的副本不会是 Godot 实际加载并初始化的那份，`~` 拷贝机制同理适用），持有跨库全局数据；对外符号用显式导出宏（仿 `JSB_RUNTIME_API`，新增 `JSB_SHARED_API`），因为含数据符号（MSVC .def 无法可靠导出 data symbol，`jsb.config.h:32-45` 注释已论证）。
- **非对称生命周期（加载顺序无关）**：godotjs-shared 导出 `jsbl_initialize()` / `jsbl_deinitialize()`。`jsbl_initialize()` 由 **editor 与 runtime 两个 gdextension entry 在各自初始化最开头** 调用——二者顺序无保证，内部幂等：首次调用装载 godot-cpp 接口表并构造全局数据，后续调用仅递增计数。**反初始化只由 runtime 负责**：runtime entry 在自身反初始化的最末尾调用 `jsbl_deinitialize()` 执行真正清理；editor 反初始化时不调用（不触发任何清理），确保共享层不会因 editor 先行卸载而被提前析构、runtime 功能保持完整。接口带 ABI 版本号参数，不匹配即报错。“runtime 先于 editor 卸载”的边界情形当前引擎生命周期不可达（两插件同生共死），记入 R10。
- **C1 约束（单一副本）**：godotjs-shared 产物按 平台+架构 只出一份，**不带 target 味道**（不得出现 `.editor`/`.template_*` 后缀变体），否则编辑器进程内 editor 与 runtime 各链各的 flavor，会被加载成两份副本、全局状态再次分裂——正是本次要消灭的 bug 类别。TOOLS_ENABLED 相关内容（如 `get_source_ed` 预设数据）无条件编入，由消费方自行取舍。
- **共用无状态源文件**（第 8 条）：`PathUtil`、`NamingUtil`、`Settings`、`VariantUtil` 等不持跨库可变全局的工具，以共享源文件形式分别编入 editor 与 runtime（放 `src/common/`），不进 godotjs-shared，避免为无状态代码付出跨库调用成本。
- **判据**：有全局可变状态 / 单例 → godotjs-shared；纯函数工具 → src/common 双份编译；仅单侧使用 → 留在本侧。

---

## 三、目录结构重组（语义化）

```
src/
  godotjs_shared/          # 公共数据层（独立 DLL，产物名 godotjs-shared）
    api_tool/              #   现 api_tool/*.cpp + core/*（非 editor 部分）
    string_names/
    logger/                #   现 internal/jsb_logger.*
    console/               #   现 internal/jsb_console_output.*（IConsoleOutput 注册表）
    preset/                #   SConstruct 生成的 jsb_project_preset.gen.cpp 移到这里
    shared_api.h             # 导出宏 JSB_SHARED_API + jsbl_initialize/deinitialize
  common/                  # 无状态工具源码（双侧各编译一份，不跨库调用）
    path_util/ naming_util/ settings/ variant_util/ ...
  runtime/                 # runtime GDExtension
    bridge/ impl/ weaver/ internal/(剩余)/ compat/ js_type_extension/ node_helper/ tests/
    bridge_runtime.h       #   ClassDB 桥接接口（新增）
  editor/                  # editor GDExtension
    plugin/ dock/ repl/ export/
    codegen/               #   现 api_tool/editor/*（parser/generator/store_writer）
    presets/               #   安装编排（现 InstallFileInfo/apply_file 等）
third/ bin/ scripts/ project/ （不变）
```

注：源码树中混有大量 `*.windows.editor*.obj` 编译产物，目录迁移时同步清理，避免 SConstruct Glob 误收。

---

## 四、关键技术设计

### 4.1 godotjs-shared 的 godot-cpp 初始化【最高风险】
godotjs-shared 不是 GDExtension，没有 entry 回调，其静态链接的 godot-cpp 副本默认拿不到 GDExtensionInterface 函数指针表，直接调用 `String/FileAccess` 等会崩溃。
- 方案：godotjs-shared 导出 `jsbl_initialize(get_proc_address, library)`，由 **runtime 与 editor 的 gdextension entry 在自身初始化最开头** 调用（幂等，首次生效）。
- 实施前必须先读 `third/godot-cpp/src/godot.cpp` 的初始化序列，确认手动初始化的最小调用面（对应 `GDExtensionBinding::InitObject` 内部步骤），写一个 spike 验证。
- 已核实 `third/godot-cpp/src/godot.cpp`：接口表装载核心是 `GDExtensionBinding::init()` → `internal::load_gdextension_interface(p_get_proc_address)`（godot.cpp:115-184），两个 extension 的 entry 均能拿到 `get_proc_address`，转发给 godotjs-shared 即可；spike 需确认除接口表外是否还有最小必需步骤。
- 兜底方案 F2：godotjs-shared 完全不依赖 godot-cpp（纯 C ABI + POD + 不透明句柄），所有 godot API 调用推回两个 extension 侧。代价大（api_tool 重度使用 godot-cpp），仅在 spike 失败时启用。

### 4.2 editor ↔ runtime 桥接面（替代直接访问 Environment/v8）
经核对，"只留 `eval_source()`" 不够，最小桥接集合建议（runtime 侧新建一个注册进 ClassDB 的内部类，如 `GodotJSRuntimeBridge`）：

| 桥接接口 | 消费方 | 替代的现实现 |
|---|---|---|
| `eval_source(code) -> Variant` | REPL | `GodotJSScriptLanguage::eval_source`（已存在） |
| `ensure_environment() -> bool` | editor_plugin 启动 | 直接 `lang->get_environment()` |
| `load_module(name) -> Error` | 编辑器侧残余 JS 入口加载（P1 后审计，可能整行删除） | `env->load("jsb.editor.main")` |
| `gc()` | REPL GC 按钮 | `jsb::Environment::gc()` |
| `get_statistics() -> Dictionary` | statistics viewer | `env->get_statistics()` |
| `get_module_dependencies(path) -> PackedStringArray` | export_plugin 依赖收集 | v8 遍历 module.children |
- **codegen 不经过桥接**（D1 修订）：代码生成在 P1 即改为纯 C++、不依赖 JS 环境；桥接面仅服务 REPL / 统计 / 导出依赖收集。
- `load_module` 是否保留视 P1 后的残余 JS 入口审计结论。
- `GodotJSScriptLanguage` 已 `GDREGISTER_INTERNAL_CLASS`，跨 GDExtension 经 ClassDB 调用的可行性需 spike 验证（R9）。
- export_plugin 的 `export_compiled_script`（读编译产物文件）可保留为纯文件操作；仅 children 枚举走桥接。
- `jsb.editor` 命名空间（唯一消费方是 `jsb.editor.codegen.ts`：`get_classes`/`get_primitive_types`/`get_singletons`/`get_global_constants`/`get_utility_functions`/`VERSION_DOCS_URL`）：**终态整体删除**（路线 A 纯 C++ codegen 直接调 api_tool，两侧 `jsb_editor_utility_funcs.*` 与 stub 一并无）。~~路线 B ClassDB 过渡方案~~ **已废除**（D1 修订）：不再为 TS codegen 保留任何跨库数据面；`jsb.editor` 命名空间与两侧 `jsb_editor_utility_funcs.*` 在 P1 随 codegen C++ 化直接删除。

### 4.3 codegen 接管方式【已定：直接 C++ 化，且先于物理拆分】
- ~~先 B 后 A~~ **已否决**（D1 修订）：为维持 TS codegen 所需的过渡衔接层——ClassDB 数据提供类 + expose 包装 + `_request_codegen` 双向调用重布线——成本与直接把 TS 翻译成 C++ 相当，且用后即弃，不如直接重写。
- **第一步（P1，仍在当前单一 GDExtension 形态内）**：用 C++ 重写 `TSDCodeGen`（约 4271 行 TS 的类型声明生成逻辑），数据源 = api_tool + ClassDB + 场景/资源解析；同步删除 `jsb.editor.codegen.ts` 消费路径与两侧 `jsb_editor_utility_funcs.*`（含 throwing stub）；`_request_codegen` 的 C++↔JS 双向调用链（`get_scene_nodes`/`get_resource_type_descriptor` 反向调 JS codegen 函数）随 codegen C++ 化自然消失，重写时对照 TS 原逻辑逐一落实。以 P0 校验脚本验证输出与 `.本地文档/代码生成基线` 基线一致后，方可进入物理拆分。
- **第二步（P2 起，物理拆分）**：拆分后的 codegen 是 editor 库内的纯 C++ 编排（类型数据经 godotjs-shared 取自 api_tool），全程不触碰 JS 环境。
- 排序收益：先换实现、再动结构，每个大阶段只引入一个变量；校验脚本失败时可立即定位回归来自哪一步。
- 实施期审计项：① 盘点 `jsb.editor.bundle.js` 中 codegen 之外的残余内容（若存在非 codegen 功能，归入 4.2 桥接 / REPL 面处理）；② 盘点 `project/` 安装的编辑器预设文件中哪些随 codegen.ts 一并退役。

### 4.4 预设数据归属
`jsb_project_preset.gen.cpp` 生成位置从 `src/runtime/` 改到 `src/godotjs_shared/preset/`，`get_source_rt`/`get_source_ed` 均带 `JSB_SHARED_API` 导出；editor 的 `apply_file` 改从 godotjs-shared 取数。editor bundle（jsb.editor.bundle.js/.d.ts/.map）的加载从 `Environment::init()` 移除，改由 editor 库启动时经 `load_module` 加载（TOOLS_ENABLED 判定留在 editor 侧）。

### 4.5 IConsole 改造（第 10 条）
`IConsoleOutput` 接口 + 全局 `outputs_` 注册表 + `internal_write` 下沉 godotjs-shared；runtime essentials 与 editor REPL 都链接 godotjs-shared：REPL 构造时注册、析构注销。REPL 其余功能（eval/gc/auto_complete）走 4.2 桥接。

### 4.6 构建系统（SConstruct）
- 新增 `godotjs-shared` 共享库目标（MSVC 显式导出宏，不走 .def）；runtime/editor 目标改为链接它。
- 产物布局：godotjs-shared 产物按平台+架构一份、**无 target 后缀**（见 C1）+ 两个 extension dll；`.gdextension` 拆为两份（editor 版仅保留 editor target 键），`[dependencies]` 声明 godotjs-shared。
- Windows 上 Godot 以 `~` 前缀副本加载 extension DLL → editor 绝不可链接 runtime DLL（本计划的出发点），链接 godotjs-shared 则因无重名副本问题而安全。
- 预设生成段（SConstruct:555-600）输出路径调整；`register_editor_types.windows.editor*.obj` 等遗留产物清理。
- CI 矩阵后续补 godotjs-shared 产物（web/iOS 的 wasm/xcframework 合并模式列为二期）。

---

## 五、分阶段实施计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0 基线** | 固定现有单库构建；编写 `.本地文档/scripts/verify_codegen.py`（不入库，见 D6 与第十章 V2；删除 project/gen、typings → 用 F:/godot/godot/bin/godot.windows.editor.x86_64.exe headless `--generate-types` 触发生成 → 与当前检出根下的 .本地文档/代码生成基线 递归 diff，路径用绝对路径/环境变量定位（本 worktree 指向自己的 .本地文档 副本）；tsconfig.json 为 git 跟踪的预设文件不删，单独比对）；先跑通一轮证明基线可再生，并完成 V1 确定性预检（第十章） | diff 为空 |
| **P1 codegen C++ 化**（单库形态内） | 用 C++ 重写 TSDCodeGen（对照约 4271 行 TS 逐段迁移）；删除 `jsb.editor.codegen.ts` 消费路径、两侧 `jsb_editor_utility_funcs.*`（含 throwing stub）；`_request_codegen` 双向链随之消除；执行 4.3 审计项（bundle 残余 / 预设文件退役清单） | P0 校验脚本 diff 为空；编辑器内代码生成入口可用 |
| **P2 godotjs-shared** | 建共享库目标与导出宏（幂等初始化 + 仅 runtime 反初始化的非对称生命周期 + ABI 版本检查）；迁入 api_tool core、StringNames、Logger、IConsole 数据层、preset gen；完成 4.1 spike | 三库链接成功；现有功能冒烟通过 |
| **P3 runtime 桥接** | 新增 `GodotJSRuntimeBridge`（4.2 接口集，按 P1 后审计裁剪，如 load_module 去留）；Environment::init 移除 editor bundle 加载 | runtime 单独可编译可运行（游戏跑 template_debug） |
| **P4 editor 解耦** | editor 全部文件去除 runtime 符号引用：codegen 编排保持纯 C++、export_plugin/statistics/repl 改走桥接、api_tool/editor 编入 editor 库、jsb_editor_helper 按“多处复用才保留”原则精简或内联 | editor 库仅依赖 godot-cpp + godotjs-shared（dumpbin/nm 验证无 runtime 导入） |
| **P5 构建/清单拆分** | 两份 .gdextension、CI 矩阵、产物复制 | editor + runtime 双插件在测试工程正常装载，无双注册报错 |
| **P6 端到端校验** | P0 脚本全量跑通；连续 3 次删除-重生-diff 稳定；REPL/export/codegen 手工冒烟 | 校验零差异（或仅记录并解释的差异） |
| **P7 结构收尾** | 目录语义化调整到位、清理 obj 残留、更新 README/CONTRIBUTING 架构图 | 文档与实际一致 |

每阶段结束更新本文档勾选状态；阶段内不混合无关改动。

---

## 六、风险与对策

| # | 风险 | 对策 |
|---|---|---|
| R1 | godotjs-shared 的 godot-cpp 手动初始化不可行 | 先 spike；兜底 F2（godotjs-shared 去 godot-cpp 化，纯 C ABI） |
| R2 | MSVC 数据符号导出遗漏（零地址读取） | 强制 `JSB_SHARED_API` 标注审查清单；dumpbin 校验 |
| R3 | 双 extension 类注册冲突/遗漏 | 注册表审计（runtime: Script/ScriptLanguage/Loader/Saver；editor: Plugin/Helper/ExportPlugin/REPL…）；更新 NamingUtil omitted 列表 |
| R4 | codegen 输出不确定导致 diff 噪音 | P0 先量化基线确定性（连跑两次 diff）；必要时校验脚本白名单化已知不稳定项并记录原因 |
| R5 | export_plugin 对 module 树的深度依赖 | 桥接 `get_module_dependencies` 保持行为一致；导出冒烟用例 |
| R6 | 多引擎（v8/node/qjs/jsc/web）下桥接行为不一致 | 桥接面全部走引擎无关 API；CI 至少覆盖 v8 + qjs |
| R7 | 移动/Web 平台 godotjs-shared 形态（静态合并） | 二期处理，一期仅桌面三平台 |
| R8 | REPL `eval_source` 的异常回传丢失 | 桥接返回统一 Result(Dictionary{ok,error}) |
| R9 | godotjs-shared 出现多 flavor 副本导致全局状态分裂 | 强制 C1（平台+架构单一份）；CI 用 dumpbin/ldd 审计两个 extension 的导入表指向同一文件名 |
| R10 | 非对称反初始化的边界：若 runtime 先于 editor 卸载，共享层随 runtime 清理，editor 再访问会失效 | 当前引擎生命周期两插件同生共死（进程退出/工程关闭/reload 同步进行），实际不可达；若未来出现单侧热重载需求，再引入显式 retain/release 协商 |

---

## 七、决策记录

- **D1（修订）**：~~先路线 B（TS 随 editor 库 + 桥接驱动）后路线 A（C++ 重写）~~ → **放弃路线 B，codegen 直接 C++ 化，并在单库形态内先行完成（P1），再做物理拆分**。理由：TS 过渡衔接层（ClassDB 数据提供类 + expose 包装 + `_request_codegen` 重布线）成本≈重写且用后即弃；“先换实现、再动结构”让每阶段只有一个变量，P0 校验脚本可精确归属回归。详见 4.3。
- **D2（已定）**：公共数据层命名 **godotjs-shared**（源码目录 `src/godotjs_shared/`）。理由：① 与 `godotjs-ext` 产品命名同族；② “libdata” 在 Linux 会叠加自动 `lib` 前缀变成 liblibdata.so；③ “shared” 语义准确，避免 “core” 与引擎核心混淆、“data” 读起来像数据文件。
- **D3（已定）**：`Settings`/`NamingUtil` 等无状态工具放 `src/common/` 双侧各编译一份，不进 godotjs-shared。（目录由原 src/shared 更名为 src/common，避免与库名混淆）
- **D4（已定）**：`EditorUtilityFuncs` 相关代码（runtime dispatcher + editor 侧实现 + throwing stub）**终态整体删除**，不做“editor 注册 / runtime 读取”迁移；删除时机提前至 P1（随 codegen C++ 化一并执行，见 4.3、D1）。
- **D5（已定）**：godotjs-shared 生命周期非对称——editor 与 runtime 入口均幂等初始化；反初始化仅由 runtime 触发、editor 不参与，防止 editor 先卸载破坏 runtime（见 二）。
- **D6（已定）**：重构校验脚本（verify_codegen.py 等）放 `<当前检出根>/.本地文档/scripts/`，不入 git——git 中本无基线内容，校验脚本离开基线无意义；每个检出自持一份、随基线一同维护。详见 10.2。
- **补充结论**：godotjs-shared 不能是 GDExtension（直接链接的副本未经 Godot 初始化，`~` 拷贝机制对其同样适用）。

---

## 八、历史任务归档

- CI 系统改进（多平台矩阵、node/jsc 引擎、macOS universal、xcframework 等）：已完成并修复多轮 CI 问题，详见 git 历史（daa1081 及此前提交）。原详细记录已被本计划替换，可在 git 历史中查阅。


---
---

## 十、闭环验证手段（重构验收的触发与校验设计）

> 目标：重构各阶段（P1 codegen C++ 化 → P7 收尾）每一步都有可重复执行的自动化验证闭环，不依赖手工点编辑器菜单。

### 10.1 现有触发入口盘点（勘察结论，多数已存在，无需新造）

| 入口 | 位置 | 行为 | 重构验证中的角色 |
|---|---|---|---|
| `--generate-types` | `jsb_editor_plugin.cpp:166` → `_generate_types_from_cmdline()`（:194） | 编辑器插件 READY 后延迟触发全量类型生成；headless 下完成后自动 `SceneTree::quit(0)` | **codegen 端到端触发的现成入口**：P0 校验脚本直接用它做「删产物→重生→diff」循环；P1 后同一参数触发的是 C++ 版生成器，前后行为可直接对比 |
| `--godotjs-api-generate <json>` | `jsb_editor_plugin.cpp:176-186` → `api_tool::generate_api_tool_data()` | 纯 C++ 从 extension_api.json 构建 api store（`project/.godot/.api_dumping/*.capi`），headless 下完成即退出码 | api store 可再生性检查；CI 已在用（ci.yml「Generate API data」步，含 4.7.1 关机崩溃重试逻辑）；P2 迁移 api_tool core 后用它冒烟 |
| `--jsb-run-tests` | `register_types.cpp:113-127` | doctest 套件 + SceneTree 退出码 | C++ 单测入口（第九章拆分方案） |
| JS 集成测试 | `project/tests/`（Start.tscn 主场景 + test-status.ts 哨兵 `GODOTJS_TEST_PROJECT_COMPLETED` / `GODOTJS_TEST_PROJECT_FAILED:`）+ CI 的 run-runtime-matrix.mts | 运行时行为集成测试，哨兵字符串判定成败 | 每阶段回归兜底：脚本运行时（模块加载/桥接/REPL 底层）未被重构破坏 |
| 编辑器菜单 | 「项目」→ GodotJS：Generate API Data / Install Project Files / Generate Types / Config Enabled TS Classes / Generate All Scene Nodes Types / Generate All Resource Types / Cleanup Invalid Files（`jsb_editor_plugin.cpp:322-336`） | 手工功能面 | 仅手工冒烟用；自动化一律走命令行参数 |

缺口：场景/资源 d.ts 生成（scene_nodes_types / resource_types）、cleanup_invalid_files、export_plugin 导出链没有独立命令行触发——见 10.4 V5/V6，按需以最小改动补参数。

### 10.2 校验资产归属（D6）

- 校验脚本统一放 `<当前检出根>/.本地文档/scripts/`（如 `verify_codegen.py`），与基线同库维护、不入 git；每个检出自持一份。
- 一次性调试脚本/日志仍放 `.agent_tmp/`；只有长期复用的校验手段才进 `.本地文档/scripts/`。

### 10.3 验证手段分层

| 层 | 验证什么 | 手段 | 适用阶段 |
|---|---|---|---|
| L1 端到端 diff | codegen 最终产物与基线一致 | V2 校验脚本（删 `project/gen`、`project/typings` → headless `--generate-types` → 与 `.本地文档/代码生成基线/` 递归 diff；`tsconfig.json` 是 git 跟踪的预设文件不删、单独比对；路径经检出根绝对路径/环境变量定位） | P0 起每阶段必跑 |
| L2 C++ 单测 | codegen 生成器单元级断言（与 L1 互补：L1 只看终态，L2 定位内部回归） | `src/editor/tests/` doctest 套件（第九章 T1），触发 `--jsb-run-editor-tests` | P1 起 |
| L3 运行时回归 | 脚本运行时行为未被破坏 | 既有 JS 集成测试 + `--jsb-run-tests`（CI 已有流程照跑） | 每阶段必跑 |
| L4 功能冒烟 | 编辑器功能面（REPL/export/dock/统计面板） | 手工清单（P6）；能参数化的逐步转自动化（V5/V6） | P3–P6 |

### 10.4 待建项（按需排入对应阶段）

| # | 内容 | 时机 |
|---|---|---|
| V1 | 基线确定性预检：P0 先连跑两次「删产物→重生→diff」，量化 R4（输出不确定性）；若有噪音，先白名单化并记录原因，再谈基线可信 | P0 内先行 |
| V2 | `.本地文档/scripts/verify_codegen.py`：实现 L1 全流程（环境变量定位基线、清理→触发→diff、退出码报告） | P0 |
| V3 | editor doctest 套件骨架（第九章 9.2/T0）：第二个 startup callback + `--jsb-run-editor-tests` | 随 P0/P1 |
| V4 | api store 再生校验：删 `project/.godot/.api_dumping` → `--godotjs-api-generate` → 检查关键 capi 文件存在（复用 ci.yml 判据） | P2 前 |
| V5 | 场景/资源 d.ts 生成触发参数：仿 `--generate-types` 加最小命令行开关（或并入 V2 脚本用 `--script` 驱动），覆盖 gen_dts_test 场景集 | P1 验收前按需 |
| V6 | export_plugin 冒烟自动化：headless 导出一次测试预设 + 产物存在性断言（R5/R6 的对策落点） | P4/P5 |

> 原则：优先复用既有入口（`--generate-types`、`--godotjs-api-generate`、JS 集成测试、`--jsb-run-tests`），确有功能面无参数可达时才新增最小触发参数；所有新增触发参数随第九章 editor 测试套件一并归入 editor 侧。

### 10.5 命令手册

常用构建 / 测试 / 触发命令已由用户整理在 `<当前检出根>/.本地文档/本地构建与测试.md`（不入库，每检出自持一份；含默认 v8 构建、其他 JS 引擎开关、`--jsb-run-tests`、两步 api 数据生成、`--generate-types` codegen 触发、TS 集成测试流程与「exit code == 0 + 无泄漏」验收注意项）。AGENTS.md 仅留指针不复制内容（单一事实源）；V2 的 `verify_codegen.py` 实现时以该手册命令为准。

---
