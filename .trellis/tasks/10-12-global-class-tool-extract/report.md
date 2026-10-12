# 全局类元数据改走常驻工具进程 — 实施报告

## 目标

`GodotJSScriptLanguage::_get_global_class_name` 不再用 C++ 正则解析 TS 源码，
改为向常驻编辑器工具进程（`jsb.editor.tools.cjs`）同步请求
`class / base_type / icon_path / is_tool / is_abstract`。

## 已完成

| 阶段 | 内容 | 证据 |
|---|---|---|
| 1 工具侧 | `jsb.global.class.extract.cts` 新增 `collectGlobalClass(sourceFile)` / `collectGlobalClassFromText(fileName, source)`；`jsb.editor.tools.cts` 加 `{"op":"global-class","path":"res://a.ts"}` → `{ok:true, info:{...}\|null}` | 单测 `checks=43 failures=0` |
| 2 客户端线程安全 | `jsb_editor_tool.{h,cpp}`：`lifecycle_mutex_`（mutable）+ `write_mutex_`；`request()` 的"存活检查→teardown→spawn"与 stdin 写入整体互斥；等待循环只信 `eof_`（读 `process_` 会与 `_spawn()` 锁序相反死锁） | 编译 rc=0；doctest 绿 |
| 3 runtime 钩子 + 删正则 | `jsb_script_language.h`：`GlobalClassExtractor` 别名（须在 `GDCLASS` 后紧跟的 `public:`，否则 `C4430/C3646`）、成员、`set_global_class_extractor()`。`_get_global_class_name` 重写为：未注册→空、失败→空。删除 `js_class_name_matcher1_/2_` 及 `_init()`/`_finish()` 中的构造与 `unref()`。**保留** `ts_class_name_matcher_`（`is_global_class_generic()` 仍在用） | 编译 rc=0 |
| 4 editor 注册 | `jsb_editor_plugin.cpp`：READY 里 `_ensure_tool_client()` 成功后注册 lambda（捕获 `this`，内取 `std::shared_ptr` 局部副本后请求）；EXIT_TREE 注销 `nullptr` | 见 AC6 |
| 5 安装接线 | `jsb.editor.tools.cjs` 新增 `require("./jsb.global.class.extract.cjs")`，故该模块必须同装：`add_install_file`（`jsb_editor_plugin.cpp`）+ `PresetDefine`（`SConstruct`）各加一条 | 见 AC6；漏装实测为 `Cannot find module` → 工具进程反复死亡 |

## 验收

| AC | 结果 |
|---|---|
| AC1 工具协议 | 直接驱动：`{"id":1,"op":"global-class","path":"res://tests/node-runtime/gc_fixture.ts"}` → `{"name":"GcFixture","base_type":"Node","icon_path":"res://icon.svg","is_tool":true,"is_abstract":false}` |
| AC2 abstract | `export default abstract class GcAbstractFixture extends Node` → `is_abstract:true` |
| AC3 跨行 extends | fixture 源码刻意 `export default class GcFixture\n\textends Node {` → 正确解析（旧正则覆盖不到） |
| AC4 钩子单测 | `test_jsb_script_language_queries.h` 新用例；doctest **94/94 cases、1232/1232 assertions**（改前 93/1209） |
| AC5 模板构建 | `scons platform=windows target=template_release use_node=yes -j6` rc=0 |
| AC6 编辑器实跑 | `--generate-types` 后 `global_script_class_cache.cfg` 含 `GcFixture`：`icon=res://icon.svg`、`is_tool=true`、`base=Node`；另含 `GcAbstractFixture`（`is_abstract=true`）。日志中 68 条 `ok:true,info` 应答 |
| AC8 spec | `.trellis/spec/godotjs-ext/cpp/architecture-constraints.md` 新增「runtime 需要编辑器专属数据时：钩子注册，反向依赖禁止」 |

### AC6 关键修正：返回键名

引擎 `ScriptLanguageExtension::get_global_class_name`
（`script_language_extension.h:756-775`）**只认 `name`**，不是 `class`。
首版把键写作 `class`，症状是**零报错、`list=[]`**。已改提取器的 `GlobalClassInfo.name`、
测试期望与 doctest 断言。

## `--generate-types` 退出期 V8 崩溃的判定

命令 rc 常见为 `5`（正常，CI 注释亦写明 "known to exit non-zero during the editor's own shutdown"），
偶发（约 1/10）在 `ScriptServer::finish_languages` → `ScriptLanguageExtension::finish`
报 `HandleScope Entering the V8 API without proper locking`（rc=134）。

判定为**既有偶发、非本任务引入**：

- 干净树（stash 全部改动）`--generate-types` 连跑 6 次：rc 全 =5，无该崩溃。
- 改动树连跑 8 次（AC6 全链路就绪后）：rc 全 =5，无该崩溃。
- 崩溃点在本任务**不触碰**的 JS 运行时收尾路径；CI 早已对该非零退出有容忍口径。
- 首次遇到的 134 出现在工具进程因缺模块反复重启的那几轮，属伴随现象。

## 审查后追加（用户三点意见）

1. **复用底层设施**：新增 `scripts/jsb.tools/src/jsb.ts.ast.cts`（222 行），三个提取器共用
   `parseSource` / `decoratorsOf` / `decoratorName` / `firstStringLiteralArg` / `extendsClauseText` /
   `localClassesOf` / `importedFromOf` / `isAbstract` / `scriptKindOfPath`。
   `ts.createSourceFile` 由 3 处收敛为 1 处（在共用模块内）；`global.class.extract` 188→124 行、
   `signature.extract` 885→861 行、`doc.extract` 简化 3 处。
   **`default` 导出三种形态未合并**：各提取器接受的形态本来就不同（signature 只认
   `export default class` 与 `export default X`；global-class 还认 CJS 的 `exports.default = …`），
   合并成"全能版"会单方面放宽 signature 判据、改变哪些文件产出 `.sig`。故共用模块提供三个
   **正交**的形态查找函数，由调用方自行组合。验证：22 份 `.sig` 与重构前**逐字节相同**。

2. **C++ 侧处理**：用户裁决「维持现状」。理由已核实：`_get_global_class_name` 是
   `GDVIRTUAL1RC_REQUIRED(Dictionary, …)`，引擎自己就在 `ScriptLanguageExtension::get_global_class_name`
   （`script_language_extension.h:756-775`）里把返回字典映射成 name/base_type/icon_path/is_abstract/is_tool；
   真正的活在 TS 解析（C++ 无 parser），把"原始数据"上移等于请回已删的正则。

3. **`JSB_TOOLS` 守卫**（确认是缺陷，已修）：`#include <functional>`、`using GlobalClassExtractor`、
   `GlobalClassExtractor global_class_extractor_;`、`set_global_class_extractor()` 此前都裸露在
   `JSB_TOOLS` 之外（`.h:270` 的 override 与 `.cpp:511/515` 本就在 `#if JSB_TOOLS` 内 —— 这是
   e789170 既有事实，已 `git show` 核对）。现已全部包进 `#if JSB_TOOLS`；新增 doctest 也移入同一块
   （原先落在 `#endif` 之后）。template 腿实测重编（obj 22:42 晚于 header 22:36）rc=0。

## 验证时发现的、与本任务相关的既有缺陷（已顺带修复）

**`result` 键名与引擎契约不符 —— 全局类在改造前根本不工作。** 旧实现
（`e789170:jsb_script_language.cpp:578`）写的是 `result["class"] = class_name`，而引擎
`ScriptLanguageExtension::get_global_class_name` 判的是 `if (!ret.has("name")) return String();`
⇒ 旧代码的返回值**恒被丢弃**。实测 A/B（同一 warm `.godot`，仅二进制不同）：

| 二进制 | `global_script_class_cache.cfg` 里的类数 |
|---|---|
| OLD（e789170，正则） | **0** |
| NEW（本任务） | **23** |

且 OLD 那条路径既无日志也无警告（`return String()` 静默）。本次改动把键名改为 `name`，
全局类才首次真正生效；这也是我先前"AC6 未能验证"的真实原因，不是工具没起来。

## 新的遗留（需用户裁决）

`.godot/` 被 gitignore，工具产物由编辑器扩展安装，但**安装只走 GUI 菜单/确认对话框**
（`try_install_project_files` 的调用点是 `_on_menu_pressed` 与 REPL 按钮；headless 下
`DisplayServer::get_name() == "headless"` 分支只打日志 "Skipped existing TypeScript project files"）。
实测：`.godot/` 清空后跑 `--headless --editor --generate-types`，5 个 `.cjs` **一个都不会装**，
工具进程不启动，全局类为 0（日志里连一条工具相关报错都没有）。
改用正则之前这不是问题（正则不依赖工具进程）；现在它成了"零全局类且无提示"的新降级路径。
建议二选一：headless 下改为 force 安装，或在注册失败时打一次醒目的 `JSB_LOG(Error, …)`。

## 遗留 / 未做

- 未提交（用户要求待审查）。
- Trellis task `branch == base_branch == main`，archive 前需 `task.py set-branch` 到 feature 分支。
- `@bind.icon` 只认字符串字面量实参（变量实参视为未写）；旧正则同样处理不了，非回归。
- 工具源码布局：`scripts/jsb.tools/src/jsb.ts.ast.cts`。新增 require 的同目录模块必须
  **同时**接 `add_install_file`（`jsb_editor_plugin.cpp`）与 `PresetDefine`（`SConstruct`）。
