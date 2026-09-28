# 解析注释，为 Godot 编辑器提供脚本文档

## Goal

作者不再需要为了写说明而调用 `@bind.help("...")`：TypeScript/JavaScript **源文件**里的
`/** ... */` 文档注释自动成为脚本类与成员的文档，在编辑器的「帮助」/ inspector 里与 GDScript 的
`##` 文档注释同形态呈现。

同时交付注释解析方式的调研（各候选方案的实测判定），落档 `research/comment-parsing-options.md`。

## Background（已核实的事实）

### 用户拍板的四条约束（2026-09-28，原话见 `research/comment-parsing-options.md` §0/§2）

- B1 **解析源文件**，不是编译产物：「没有编译后丢失注释的问题，因为我们要解析的是源文件」。
- B2 **源文件不一定是 TS**：「用户完全可以直接用 js 进行开发」⇒ `.js` 是并列的一等输入。
- B3 **注释从声明往上找**（不是从文件头往下找）；注释与声明之间有**空行** ⇒ 视为没写文档
  （否则会把文件里其它说明当成这个类的说明）。
- B4 **类注释分段：首行 = brief，全文 = description**。
- B5 **成员注释与注解（装饰器）的关系都要处理**：注释可能紧贴成员声明，也可能在**所有注解之上**。

### 现状链路（`@bind.help()` → 引擎文档）

| 环节 | 位置 |
|---|---|
| binder 内装饰器 | `scripts/jsb.runtime/src/godot.annotations.ts:1338-1362`（类级 `set_script_doc(target, undefined, 2, msg)`；成员级 `help_map[name]` → `:897-899`） |
| legacy 导出 | 同文件 `:613-631`（`help` / `Help`，标 `@deprecated Use createClassBinder() instead.`） |
| JS→C++ 桥 | `src/runtime/bridge/jsb_bridge_module_loader.cpp:267-332`（`_set_script_doc`；`#ifdef TOOLS_ENABLED`） |
| 解析期读取 | `src/runtime/bridge/jsb_class_info.cpp:59-64`（`_parse_script_doc` → `ScriptBaseDoc::brief_description`）；调用点 `:407` 类 / `:450` 方法 / `:575` 属性 |
| 数据模型 | `src/runtime/bridge/jsb_class_info.h:129-141`（`ScriptBaseDoc` + 三个空派生） |
| 消费 | `src/runtime/weaver/jsb_script.cpp:288-330`（`_get_documentation()`；**`methods[]` 只填 `name`**，有 `// TODO: 填充完整函数文档`） |
| 引擎消费 | `script_language_extension.h:91` → `DocData::ClassDoc::from_dict`（`core/doc_data.h:698-860`；`MethodDoc` `:97-263`） |

**引擎可消费的字段面**：`ClassDoc{ brief_description, description, keywords, tutorials, methods, signals, constants, enums, properties, is_deprecated+deprecated_message, is_experimental+experimental_message, is_script_doc, script_path }`；`MethodDoc{ description, return_type, qualifiers, arguments[].{name,type,default_value}, deprecated, experimental }`。

### 调研结论（详见 `research/comment-parsing-options.md`）

| 方案 | 判定 | 实测依据 |
|---|---|---|
| 读**编译产物** `.js` 的注释 | ❌ | `@bind()` 类的类级注释在 `target: es2022` 下被 tsc 丢弃；且 JS 项目没有这一层 |
| `Function.prototype.toString()` | ❌ | Node 24/V8 实测：返回的函数源文本不含注释 |
| **C1 编辑器侧 TS Compiler API → `.sig` sidecar** | ✅ **推荐** | `node.jsDoc` 完整；`allowJs` 下 `.js` 同套逻辑实测可取；09-26 已落地整条分发链路 |
| C2 运行时读源文件自解析 | ⚠️ | 可行（编辑器有源文本、`GodotJSScript` 已持有），但要在 C++ 自建第二套解析逻辑 |
| 额外 emit `.d.ts` | ❌ | 实测保留 JSDoc，但多编译 + 多产物 + 第二个解析器 |
| `emitDecoratorMetadata` | ❌ | 前序实测：强制 legacy 装饰器，与本仓现代装饰器冲突 |
| tree-sitter / 自研解析器 | ❌ | 前序 `09-06-lowprio-tree-sitter-ast` 实测否决 |

**解析路线（推荐）**：编辑器侧 TS Compiler API（一个解析器同时覆盖 `.ts`/`.js`，均实测），
载体改为**常驻 Node 工具进程**（R5）——复用既有 `jsb::internal::Process`
（`src/internal/jsb_process.{h,cpp}`，已具备 stdout 读取线程；stdin 天然继承，只需补 `write_stdin`）。
既有可复用件：提取器源码 `scripts/jsb.editor/src/signature/jsb.signature.extract.cts`
（`SConstruct:658` 嵌入安装）、摘要门控 `_regenerate_signatures()`（`jsb_editor_plugin.cpp:1367+`）、
sidecar `<outDir>/<rel>.sig` 与运行时读取 `src/runtime/bridge/jsb_signature.{h,cpp}`（**签名路径不变**）。

## Requirements

- R1 **注释即文档**：类的 `/** */` 成为该脚本类的 `brief_description` + `description`；成员的注释成为对应条目的描述。正文保留 markdown 与换行。
- R2 **关联规则按 B3/B5**：从**声明点（含其全部装饰器）向上**取最后一段紧邻的 `/** */`；与声明之间出现空行即判为"无文档"。类与成员**同一套规则**，无特例。
- R3 **分段按 B4**：首行 = brief，全文 = description。
- R4 **输入覆盖 `.ts` 与 `.js`（B1/B2）**：提取器同时枚举两种源文件；判据基于源文件本身，不依赖编译产物。
- R5 **载体 = 编辑器侧常驻 Node 工具进程**（用户 2026-09-28 裁决）：签名提取与帮助文档提取由**同一个长驻进程**提供，宿主经 **NDJSON over stdin/stdout** 触发；不再每次 `spawn` 短命进程。
- R5.1 **帮助文档直接写进 `ScriptClassInfo`**（不落盘、不新增产物、不改打包与清理白名单），并**实现刷新机制**（源 md5 / 类信息重建 / 工具重启三层判据）。
- R5.2 **`typescript` 包是硬前置**（用户裁决）：`.js` 项目同样要求项目内已装该包（与现状同一前置），不退回自研 JS 解析器。
- R6 **保留 `@bind.help()`（用户明确）**：「旧的注解仍然有效」，且**显式声明优先于注释**（`@help` 非空时不覆盖）。
- R7 **门控不放松**：文档代码仍在 `JSB_TOOLS` / `TOOLS_ENABLED` 内；导出包不含源文件这一既有限制不变（签名 sidecar 照旧显式打包）。
- R8 **失败不静默**：工具崩溃 / 请求超时 / 启动失败时**必须**打印错误信息，并按裁决处理
  （崩溃：告知即将重启 → 重启 → 重试本次请求；超时：5s 上限、放弃并报出 `op`/文件/超时值）。
  仅在 `JSB_USE_TYPESCRIPT=0`（工具根本不启动）时才无输出。
- R9 **调研落档**：`research/comment-parsing-options.md`（已完成）。

## Acceptance Criteria

- [ ] **A1** 类注释 → 该类的 `brief_description`（首行）与 `description`（全文）；`description` 保留空行分段与 markdown。
- [ ] **A2** 方法 / 属性 / 信号 / 常量的注释 → 对应条目的描述；`_get_documentation()` 的 `methods[]` 不再是空壳（`jsb_script.cpp:326-331` 的 TODO 消除）。
- [ ] **A3** 关联规则可验证：注释在装饰器**之上**、装饰器**之下**都取到；中间隔空行则不取；注释与声明之间有注解时同样取到（四种形态各有断言）。
- [ ] **A4** `.js` 源脚本同样产出文档（与等价 `.ts` 的结果一致）。
- [ ] **A5** `@bind.help(...)` 行为**不变**；同时写注释与 `@help` 时 **`@help` 胜出**（R6）。
- [ ] **A6 常驻进程**：工具启动一次后，多次触发（签名 + 文档）**不重复启动进程**；进程死亡后能重启且后续请求恢复正常。
- [ ] **A7 刷新机制**：改源文件 ⇒ 下一次查询拿到新文档（L1）；类信息重建 ⇒ 文档重新填充（L2）；工具重启 ⇒ 请求重发且不把"未取到"记成"已同步"（L3）。
- [ ] **A8 故障处理不静默**：kill 掉工具进程 ⇒ 有错误输出 + 明确的重启提示，且**重启后本次请求被重试并可成功**；制造 >5s 的请求 ⇒ 有包含 `op`/文件/超时值的错误输出，且该请求不被记为"已同步"（下次重问）；`JSB_USE_TYPESCRIPT=0` ⇒ 工具不启动、无错误刷屏；全量验收 `FAILED = 0`、`Orphan StringName = 0`。
- [ ] **A9** 签名产物契约不变：`.sig` 仍按摘要门控增量产出、仍被打包、仍受 `collect_invalid_files()` 白名单保护（既有测试保持绿）。
- [ ] **A10** 新增断言**非空转**（负向控制实测可失败）：C++ 套件（`--jsb-run-tests`）与 GDScript 侧检查脚本。
- [ ] **A11** codegen 基线 `python misc/verify_codegen.py` 与基线一致。

## Out of Scope

- 不引入 tree-sitter / 自研 TS 解析器 / 任何运行时（C++）侧源文件解析器（R5、R5.2）。
- **不删除** `@bind.help()` 及其任何一环（R6，用户明确保留）。
- 帮助文档**不落盘**（R5.1）：不新增扩展名、不动 `export_raw_file` 打包、不动 `collect_invalid_files()` 白名单。
- 不改 `_get_member_line()`（源码行号，`jsb_script.h:197` 仍 `-1`）；本任务只做文档文本。
- 不改 `_get_constants()` 的常量值来源（注释只贡献描述）。
- 不把现有 `tsc` 监听进程（`jsb_editor_plugin.cpp:1285-1289` 的 `tsc_`）并入本工具进程。
- 不做类型检查 / 编辑器跳转定义。

## Decisions（本轮已由用户拍板，见 `design.md` §9）

- `op:"doc"` 应答**阻塞宿主，上限 5s**；超时放弃该请求并**输出错误**告知 `op`/文件/超时值。
- 工具进程崩溃：**不静默** —— 打印错误 + 告知即将重启 → 重启 → 重试本次请求。
- `@deprecated` / `@experimental` 的 JSDoc tag **暂不做**。
