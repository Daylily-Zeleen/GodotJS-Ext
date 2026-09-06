# GDExtension 架构约束

> 机制均经 godot-cpp 源码核实。

## 核心陷阱：Godot 以 `~` 前缀副本加载扩展 DLL（最重要）

Godot **不会**原地加载构建出的扩展 DLL。对 `.gdextension` 里每个条目，它把 DLL 拷贝为带 `~` 前缀的兄弟文件（`foo.dll` → `~foo.dll`）再加载副本。

推论：

- 若扩展 A 动态链接扩展 B 的产物，OS 加载器从**原始路径**解析 B，而 Godot 初始化的是 `~` 副本 → 进程内两份 B：一份被 Godot 初始化，另一份静态变量/godot-cpp 接口表全部未初始化
- 任何"把自己的实现注册进另一个库的静态变量"的模式（存于对方 DLL `.data` 段的函数指针注册表，如 `set_expose_impl` 式 setter）**静默失效**：注册落在永远不会运行的那份映像上。单 DLL 整体构建掩盖此问题；拆分成两个扩展立即显形（历史实例：`src/runtime/bridge/jsb_editor_utility_funcs.{h,cpp}` 的 `g_expose_impl`，已判定废弃删除）
- **铁律：绝不把一个 GDExtension 的产物链接进另一个 GDExtension**。共享代码/状态放第三个普通库

## 共享数据层：普通 DLL，不是第三个 GDExtension

两个扩展的共同依赖（共享层）要求：

- 必须是**普通动态库**。做成 GDExtension 会重蹈覆辙：只有 `~` 副本能收到入口回调初始化
- **单一 flavor**：每平台+架构只出一份产物，不带 `.editor` / `.template_*` 后缀变体——否则两侧各链各的 flavor，全局状态分裂
- **幂等生命周期**：导出 init/deinit，每个扩展在 entry 最开头调 init、finalization 最末尾调 deinit；引用计数；init 传 ABI 版本号，不匹配即报错。加载顺序无保证，任何逻辑不得要求"runtime 先于 editor 初始化"
- **非 GDExtension 库内的 godot-cpp 默认没有接口表**（godot-cpp `src/godot.cpp`：接口表由 `GDExtensionBinding::init()` 填充）。扩展 entry 拿得到 `get_proc_address`，转发即可；动手前先 spike 验证。兜底：共享层完全去 godot-cpp 化（纯 C ABI），引擎 API 调用推回扩展侧
- **MSVC 数据符号**：导出全局变量/数据必须显式 `__declspec(dllexport)` 式宏；`.def` 文件无法可靠导出数据符号

## editor ↔ runtime 桥接

- editor 不链接 runtime 而要触达 runtime 状态：runtime 在 ClassDB 注册内部桥接类，editor 经引擎中转调用；尽早 spike 跨扩展 ClassDB 可见性（`GDREGISTER_INTERNAL_CLASS` 注册的类能否被另一扩展调用并无保证）
- 桥接面保持最小、结果类型化（`Dictionary{ok, error}`），让 JS/C++ 异常浮出而不是消失
- 纯文件操作留在本地；只有真正驻留引擎的操作才走桥
- 定义桥接口前先枚举真实调用点；"就一个方法"的估计必然偏小

## 拆分单体扩展的方法

1. 动手移动任何东西之前先盘点跨侧依赖：单例 getter、对引擎/脚本引擎的直接访问、内部 util 命名空间——按依赖 × 文件统计调用点数量
2. 分类：无状态纯工具 → 同一份源码编入两侧（免跨 DLL 调用开销）；可变全局状态持有者（StringName 缓存、注册表、日志器、内嵌预设 blob）→ 共享 DLL；仅单侧使用的留在本侧
3. 构建生成的产物跟随消费者：SConstruct 生成的 blob 若双侧消费，生成输出迁入共享层
4. TOOLS_ENABLED/editor 职责与其所属代码一起迁入 editor 扩展
5. 重构任何生成器之前先固定基线并证明当前构建可确定性再生（见 [../test/codegen-baseline.md](../test/codegen-baseline.md)）
6. 阶段验收标准必须机械可查：`dumpbin /dependents` / `nm -D` 显示零个来自兄弟扩展产物的未定义导入

## 引用既有"接缝"前先审问

引用仓库里既有的"接缝"前，先问它是否依赖进程级单例或"本会成为另一个扩展"中的可变静态状态——此类模式在 `~` 副本模型下是**设计性损坏**，不得引为可行先例。

## 其他陷阱

- 目录搬迁时散落在源码树的陈旧 `.obj` 会被基于 Glob 的 SCons 脚本误收——同一变更里清理干净
- 两份 `.gdextension` 清单注册重叠类会引发重复注册错误；拆分时审计类注册表
- Headless 运行跳过部分编辑器流程（确认对话框自动确认）；驱动安装/codegen 路径时要刻意触发，不要按 GUI 行为假设
- Linux 自动加 `lib` 前缀；共享层命名避免荒诞形态（`liblibdata.so`），优先 `<product>-shared` 风格

## 验收标准（机械可查）

拆分/链接类改动：`dumpbin /dependents` / `nm -D` 显示零个来自兄弟扩展产物的未定义导入。
