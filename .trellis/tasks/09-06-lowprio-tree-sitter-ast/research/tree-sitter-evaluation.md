# tree-sitter 引入评估（代价 / 收益）

> 评估日期：2026-09-24。证据基线：本检出 + Godot 引擎源码（`D:/Dev/godot/godot`，4.8.dev）。
> 结论先行：**当前不建议引入 tree-sitter**；若未来确需真实解析用户 TS/JS，应限定为「编辑器专用 + 仅 JS 语法」，而非全面替换正则。

---

## 1. 解析点清单（全仓）

按「输入是什么」分类，只有 A 类才是 tree-sitter 的潜在目标。

| # | 位置 | 现用手段 | 用途 | 输入 | 类别 |
|---|---|---|---|---|---|
| 1 | `src/runtime/weaver/jsb_script_language.cpp:176` | `ts_class_name_matcher_` 正则 | 提取 `export default class X ... extends Y` 的类名/基类/`@tool` | **用户 TS 源码** | **A** |
| 2 | `src/runtime/weaver/jsb_script_language.cpp:174` | `js_class_name_matcher1_` | `exports.default = class X extends Y`（单行形式） | **用户 JS 源码** | **A** |
| 3 | `src/runtime/weaver/jsb_script_language.cpp:175` | `js_class_name_matcher2_` | `exports.default = X`（两行形式） | **用户 JS 源码** | **A** |
| 4 | `src/runtime/weaver/jsb_script_language.cpp:426` | 运行时拼装的 `base_matcher` | 上一步拿到类名后回查 `class X extends (\w+)` | **用户 JS 源码** | **A** |
| 5 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:489` | `type_regex` | 重写生成 `.d.ts` 里的类型标识符 | 机器生成的 `.d.ts` | B |
| 6 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:541-542` | `function_regex` + `parameter_regex` + 手写括号深度扫描 | 重命名生成 `.d.ts` 里的函数名/参数名 | 机器生成的 `.d.ts` | B |
| 7 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:596` | `reference_regex` | 删除 `/// <reference path=...>` 行 | 机器生成的 `.d.ts` | B |
| 8 | `src/runtime/internal/jsb_source_map_cache.cpp:42,60,61` | 3 条正则 | 解析 JS 栈帧 | 运行时栈字符串 | 出界 |
| 9 | `misc/build/static_binding_codegen.py:825,859,864` | `re.compile/re.match` | 解析 `extension_api.json` 的默认值 token | JSON 标量字符串 | 出界 |
| 10 | `misc/bench_matrix.py:44` | `BENCH_JSON_RE` | 抓 benchmark 输出行 | 日志文本 | 出界 |
| 11 | `src/editor/codegen/jsb_codegen_mutations.cpp:48-50` | 已改为**无正则**扫描器 | 重写 codegen 生成行 | 生成行 | 出界（已优化） |

**真正在界内 = #1–#7**，其中 #5–#7 的输入是**我方自己生成的、形状已知的文本**；只有 #1–#4 面对任意用户源码。

### 现用正则的具体脆弱点（#1–#4）

- `ts_class_name_matcher_`：`...class\s+(\w+)(\s*<)?[^\n]*(?:>|\s+)extends\s+(\w+)` —— `[^\n]*` 要求**类头必须单行**。跨行的泛型列表、类名与 `extends` 之间插注释/换行 → 匹配失败，脚本被判为无 Godot 基类的普通模块。
- `is_global_class_generic`（`jsb_script_language.cpp:143`）：以「`(\s*<)?` 分组是否非空」判定泛型全局类，即**只有类名后紧跟 `<` 才认**。泛型参数跨行即误判。
- `js_class_name_matcher2_`：`search` 取**首个**匹配，注释或嵌套作用域里出现的 `exports.default` 会抢先命中。
- #4 用 `jsb::internal::format` 把已提取的类名插进正则 —— 类名来自源码，属未转义拼接（`\w+` 限定使其不可注入，但仍是脆弱点）。

**收益判定**：tree-sitter 在这 4 处是**真实的健壮性提升**（真正的语法树，天然处理跨行/注释/泛型）。但注意这 4 处的调用者是 `GodotJSScriptLanguage::_get_global_class_name` / `is_global_class_generic`，被 `EditorFileSystem` **后台线程扫描**调用；`jsb_script_language.cpp:390-394` 的注释明确记录了选正则的原因：

> `So, we can not load the script module in-place because get_global_class_name could be called from EditorFileSystem (background) scan. And for simplicity, we use regex to extract the class name from the source code instead of using ANTLR or similar.`

即：这里需要的是「**不加载模块、纯文本、任意线程**」。tree-sitter 满足该约束（纯 C，无全局状态依赖），但代价见下。

**#5–#7 判定**：输入是 `godot*.gen.d.ts` / `jsb.*.d.ts`，形状由本仓生成器决定（`src/editor/codegen/`）。tree-sitter 在这里**不带来健壮性收益**（形状已知），只带来依赖与体积。**不改。**

---

## 2. tree-sitter 集成代价（实测数据）

### 2.1 源码体积

| 组件 | 文件 | 字节 | 说明 |
|---|---|---|---|
| 核心库 | `tree-sitter/lib/src/*.c` | `parser.c` 79,954 B 等，`lib.c` 单 TU 聚合同名 `.c` | 约 20 个 C 文件 |
| `tree-sitter-javascript` | `src/parser.c` | **2,855,934 B（2.72 MiB）** | GitHub API `contents` 实测 |
| | `src/scanner.c` | 10,576 B | 外部扫描器 |
| `tree-sitter-typescript` | `typescript/src/parser.c` | **8,745,894 B（8.34 MiB）** | 实测 |
| | `typescript/src/scanner.c` | 573 B | |
| | `tsx/src/*` | 未取（另一份同量级） | TSX 变体 |

> npm 包 `tree-sitter-typescript@0.23.2` 的 `unpackedSize` = **38,844,097 B（37.0 MiB）**（含 node 绑定与预编译产物）。

### 2.2 二进制体积

官方 PR [tree-sitter#5488](https://github.com/tree-sitter/tree-sitter/pull/5488)（CSR 压缩，尚未合入）给出了同编译器（MSVC /O2）的 **ABI 15（= 当前线上格式）** 实测值：

| 语法 | ABI 15 DLL |
|---|---|
| JSON | 106 KB（含最小 DLL 开销） |
| C | 735 KB |
| JavaScript | **505 KB** |

TypeScript 的 `parser.c` 约为 JS 的 3.06 倍，按同比例推算**约 1.5 MB**（`[INFERENCE]`，未实测）。

核心库本体：Ubuntu `libtree-sitter0.25` amd64 包 **安装后 207.0 kB**（包 93.3 kB）。

**相对本仓现状的量级**：`bin/windows/godotjs-ext.windows.editor.x86_64.dll` = 108 MiB，`godotjs-ext-editor...dll` = 5.1 MiB。加 0.5–1.5 MB 语法 + ~0.2 MB 核心库**不是**体积瓶颈。真正的成本在**构建接线与多平台覆盖**。

WASM（web 腿）另计：官方 issue [#1860](https://github.com/tree-sitter/tree-sitter/issues/1860) 报「TypeScript 单独 1.2 MB unpacked（含 `tree-sitter.wasm`）」。

### 2.3 构建接线成本

本仓第三方依赖只有两种既有模式（`SConstruct:100-200`）：

1. **源码编译**：`ThirdPartyDescriptor` + `ThirdPartyDetails(path, sources)`，例如 `quickjs_src_descs`（`SConstruct:173-176`）直接列 `.c` 文件，由 SCons 编译进库。
2. **预编译产物**：`LibraryDescriptor` + `download_dependency` 从自建 `GodotJS-Dependencies` release 拉取（`SConstruct:166-171`、`362-363`），`validate_library_support` 校验（`SConstruct:186-200`）。

tree-sitter 的合适归属是模式 1（`third/tree-sitter` + 语法仓），需新增：

- `third/` 下 2–3 个目录（核心库 + JS 语法 + TS 语法），或加进 `.gitmodules`（现仅 `godot-cpp`、`third/quickjs-ng` 两项）；
- 把 `parser.c` / `scanner.c` 加进源列表（与 `quickjs_src_descs` 同形）；
- `CPPPATH` 追加 `third/tree-sitter/lib/include`（供 `tree_sitter/api.h`）与各语法 `src/`（供 `tree_sitter/parser.h`）；
- **MSVC 特例**：`parser.c` 是巨型静态数组，MSVC 编译 8 MB 级 TU 需要放宽对象文件节数/编译时间（C1128 / 段超限是已知问题，社区做法是 `/bigobj`）。本仓已有 MSVC 特例区（`SConstruct:654` 起），需要新增条目。
- **平台覆盖**：Windows / Linux / macOS / iOS / Android / web(wasm) 六条腿都要过；web 腿还要处理 wasm 版语法加载（或把语法编进 wasm 主模块）。

### 2.4 Node 侧承载可行性

**不可行**，证据：

- 编辑器期 codegen **已迁到 C++**：`src/editor/codegen/jsb_codegen_defs.h:29-30` 自述 *"P1 rewrite of `scripts/jsb.editor/src/jsb.editor.codegen.ts`"*；`scripts/jsb.editor/src/` 现在只剩 `jsb.editor.main.ts`（`auto_complete` + `run_npm_install` 两个函数）。
- Node 侧无 AST 工具链：`scripts/package.json` devDeps 仅 `@changesets/cli`、`@types/node`；`scripts/jsb.editor/package.json` 与 `scripts/jsb.runtime/package.json` 仅 `typescript`。`scripts/pnpm-workspace.yaml` 只含 `scripts/` 与 web bridge。
- 解析点 #1–#4 位于 **runtime 库**（`src/runtime/weaver/`），在引擎进程内被 `EditorFileSystem` 后台线程调用 —— Node 进程不在该调用路径上。

⇒ 若引入 tree-sitter，**必须走 C/C++ 侧**，Node 侧（`tree-sitter` npm 包 + node-gyp 原生模块）在此架构下无用武之地。

---

## 3. 后续需求：函数参数/返回值、信号参数 —— 需要解析还是反射？

这是评估中**最关键的一条**，结论是：**大部分不需要源码解析**。

### 3.1 现状：类型/成员信息管线是**反射式**的

- 类、方法、属性、信号来自 **运行时枚举活体 JS 类对象**：`ScriptClassInfo::_parse_script_class_iterate`（`src/runtime/bridge/jsb_class_info.cpp:69-286`）读 `class_obj` / `prototype` 的 own property names、以及 `@tool`/`@icon`/`@signal`/`@export` 写入的符号集合（`Symbols::ClassSignals` / `ClassProperties` / …，`src/runtime/bridge/jsb_environment.h:73-91`）。
- 属性类型来自注解产出的描述字典（`jsb.internal.add_script_property`，`scripts/jsb.runtime/src/godot.annotations.ts:375,397,454,493`）。
- 编辑期 codegen 的类型库来自 `api_tool`（引擎 API 反射），见 `src/editor/codegen/jsb_codegen_type_db.cpp`（`load_classes()` / `load_primitive_types()`）。

⇒ 本仓**没有**任何「读源码得出类型」的既有管线，唯一的源码读点是 §1 的类名提取。

### 3.2 逐项判定

| 需求 | 能否反射 | 依据 |
|---|---|---|
| **方法名集合** | ✅ 已实现 | `_parse_script_class_iterate` 枚举 prototype |
| **方法参数个数** | ✅ 可反射 | `Function.prototype.length`（JS 标准）。注意含默认值/剩余参数时语义有偏，但可给上界 |
| **方法参数名** | ⚠️ 反射 + 文本 | 需 `Function.prototype.toString()` 后解析形参表 —— 仍是文本解析，但**输入是单个函数体字符串，不是整个 TS 文件** |
| **方法参数类型** | ❌ 不可反射 | JS 运行时无类型信息。只能靠**注解**（既有机制）或**源码解析** |
| **返回值类型** | ❌ 不可反射 | 同上 |
| **信号参数** | ❌ 不可反射 | 信号目前只有名字（`ScriptSignalInfo{}` 是空结构，`src/runtime/bridge/jsb_class_info.h:155-156`；信号集合只存字符串数组，`jsb_class_info.cpp:203-217`）。要参数必须新增注解或解析 |
| **常量/枚举/静态成员** | ✅ 可反射 | 类对象上的 own property + 值类型检查（Variant 兼容性可运行时判定） |

### 3.3 判定结论

- **类型信息**（参数类型、返回值类型、信号参数类型）**本质无法反射**，必须来自**注解**或**解析**。注解是既有且已成熟的机制（`@export_*`/`@signal`/`@rpc` 已全走这条路），扩展成本远低于引入 tree-sitter。
- **参数个数/名字**用反射 + 单函数 `toString()` 即可，无需完整语法树。
- 因此：**tree-sitter 不是「完善成员信息」的必要前提**。若注解路线已能覆盖，解析路线可以完全不做。

---

## 4. 建议

### 决策

**不引入 tree-sitter。** 分项理由：

| 目标 | tree-sitter 是否值得 |
|---|---|
| 替换 #5–#7（生成 `.d.ts` 重写） | ❌ 输入形状已知，零健壮性收益，纯增依赖 |
| 替换 #1–#4（用户源码类名提取） | ⚠️ 健壮性确有提升，但 4 个正则的失效面很窄（类头必须单行）；代价是 ~9 MB 语法源码 + 六平台构建接线 + MSVC 巨型 TU 特例 |
| 支撑「参数/返回值/信号参数」 | ❌ 类型信息不可反射也**不必解析** —— 注解路线更契合本仓既有架构 |

### 若未来仍要做（触发条件）

触发条件应是「出现**必须**解析用户源码才能满足、且注解无法表达的需求」，例如：跨语言**类型检查**、编辑器内跳转定义/重命名重构、把 TS 源码结构作为一等公民暴露给 GDScript。

此时的最小方案：

1. **仅 JS 语法**（2.7 MB 源码 / ~0.5 MB 二进制），放弃 TS 语法 —— 用户 `.ts` 会被编译成 `.js` 产物，解析产物即可覆盖类结构；类型注解本就在 `.ts` 里，属于注解路线的领域。
2. **仅编辑器腿**（`JSB_EDITOR_LIB_BUILD`），不进 runtime/模板腿，避免六平台矩阵与运行时体积。
3. **仅替换 #1–#4**，保持「不加载模块、纯文本、任意线程」约束。
4. 验收用**语义等价对比**：以现有正则的结果集为基线，对 `project/tests/**` 全量 `.ts/.js` 跑双实现比对。

### 与另一任务的边界

`09-24-script-static-members` 明确**不依赖**本评估结论：其静态成员解析走**运行时反射**（活体类对象）+ **注解**（跨环境静态变量），不需要源码解析器。

---

## 5. 未决 / 未实测项

- TypeScript 语法编译后的二进制体积为按比例推算（`[INFERENCE]`），未在本机编译实测。
- MSVC 对 8.7 MB `parser.c` 的实际编译时间与 `/bigobj` 必要性未实测。
- web(wasm) 腿引入 tree-sitter 的具体接线方式未评估（`JSB_WITH_WEB` 当前用 quickjs；wasm 语法加载路径需另设计）。
- 未验证 tree-sitter 在 Godot 后台线程（`EditorFileSystem` 扫描线程）中的线程安全性（库本身无全局可变状态，但语法对象生命周期需按线程管理）。