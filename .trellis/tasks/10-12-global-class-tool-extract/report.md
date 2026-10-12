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

判定为**既有偶发、非本任务引入**（2026-10-13 复核，样本加大）：

- 干净树（stash 全部改动）`--generate-types` 连跑 10 次：rc 全 =5，0 次崩溃。
- 改动树连跑 14 次：rc 全 =5，0 次崩溃。
- 崩溃点在本任务**不触碰**的 JS 运行时收尾路径（`ScriptServer::finish_languages` →
  `ScriptLanguageExtension::finish`）；CI 早已对该非零退出有容忍口径。
- 崩溃样本与干净样本的日志**尾部完全一致**（都走到第 97 个工具请求、`Type generation
  complete`、`[editor-tools] terminated`），只在之后的 `_finish` 收尾处分岔 ⇒ 与工具请求无关。
- 早先"加钩子才崩 / 去掉钩子不崩"的 1 次 A/B 是**假相关**：随后同源二进制连跑 9 次全干净。
  该崩溃率约 <3%（本任务全程 ~30 次运行中出现 2 次），单样本对照没有鉴别力。

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


## 审查后追加（第二轮）：`_notification` 与属性 hook 的单次派发（已修，最终方案）

原始代码带着一条 TODO：

```cpp
//TODO find the method named `_notification`, cal it with `p_notification` as `argv`
//TODO call it at all type levels? @seealso `GDScriptInstance::notification`
callp(jsb_string_name(_notification), argv, 1, error);
```

**中途走过弯路**：一度照 `GDScriptInstance::notification` 实现成"沿脚本类链逐层调用"
（`Environment::call_script_method_chain`）。**该方案已被推翻并全部删除** ——
用户裁决：这是 JS，不是 GDScript，**不隐式调用父类同名函数**。

**最终语义（JS 原型链）**：`call_script_method(class_id_, …)` 内部的 `prototype.Get(...)`
从最派生类沿原型链往上找，**命中第一个即止**。派生类定义同名方法即替换基类那份；
要调父类得自己写 `super._notification(...)`。实测三态：

| 派生类 | GDScript（异类） | JS（本仓，正确语义） |
|---|---|---|
| 不定义 | `Base` | `Base` |
| 定义、不调 `super` | `Base`→`Derived` | **只有 `Derived`** |
| 定义、调 `super` | `Base`→`Derived`→`Base` | `Derived`→`Base` |

**C# 侧是佐证**：`csharp_script.cpp:1916-1954` 只调一次 `CSharpInstanceBridge_Call`，交给 C# 虚分派；
Godot 自己的 C# 代码写 `base._Notification(what)` 才会上溯。三条腿里 GDScript 是唯一异类。

### 顺带查出的真缺陷（同源）：属性 hook 被重复调用

`set` / `get` / `get_property_list` 在 C++ 侧手搓 `script_->base` 链，但循环内**每轮都用最派生类
`class_id_`** 调用同一个 JS hook ⇒ 同一函数被调 N 次（N = 链上声明了它的层数）。
实测（fixture 两层都定义）：`_get` / `_set` / `_get_property_list` **各被调 2 次**。

修法（见任务 `10-13-js-instance-hook-single-call`）：

- 循环内**无额外操作**的（`validate_property` / `property_can_revert` / `property_get_revert`）
  → 删掉遍历，单次 `call_script_method`。
- 循环内还有**逐层元数据**要收集的（`set`/`get`/`get_property_list`）→ 保留遍历，
  用 `*_hook_called` 标记把 JS hook 限定在**最派生的声明层**调一次。
- 纯查表的 `get_property_type` / `has_method` 不动。

### 验证

| 项 | 证据 |
|---|---|
| 通知三态 | 与 JS 语义一致（含显式 `super` 用例） |
| hook 调用次数 | 改后 `_set`/`_get`/`_get_property_list` 各 1 次（改前各 2 次） |
| 鉴别力 | 回退任一修复 → 对应 doctest 断言失败（实测 `values: 2 == 1`） |
| 全套 | doctest **96/96、1264/1264** |
| 构建 | `template_release` rc=0；`--headless --editor` 6 次全 rc=0 |
| spec | `architecture-constraints.md` 新增「栈虚拟函数按 JS 原型链解析」条目 |

**遗留**：`--generate-types` 退出期的 `HandleScope ... without proper locking` 崩溃为本仓**既有偶发**
（<3%，~30 次运行中出现 2 次；干净树 10 次、改动树 14 次均 0 崩溃）——与本轮改动无关。

