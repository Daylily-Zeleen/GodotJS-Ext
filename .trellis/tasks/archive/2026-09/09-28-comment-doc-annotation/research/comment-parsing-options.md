# 注释解析方式调研（取代 `@bind.help()`）

> 2026-09-28。结论均由本检出的**实测**或**源码行号**支撑，探针与命令逐条附上。

## 0. 前提（两条用户约束，决定整个选型）

用户 2026-09-28 明确：

1. 「这个功能只是为了给 godot 编辑器提供说明信息，**没有编译后丢失注释的问题，因为我们要解析的是源文件**。」
2. 「源文件**不一定是 ts**，用户完全可以直接用 js 进行开发」

由此确定的三条边界（后面所有判定都建立其上）：

- **输入 = 源文件**（`res://**.ts` 或 `res://**.js`），**不是**编译产物 `.js`（TS 项目的产物另有其人）。
- **只在编辑器需要**：文档管线（`_set_script_doc` → `_parse_script_doc` → `_get_documentation`）全在
  `JSB_TOOLS` / `TOOLS_ENABLED` 内；而**编辑器中源文件就在磁盘上**
  （`jsb_resource_loader.cpp:91`：`is_editor_hint()` 时才校验源文件存在）。
  ⇒「导出包里没有源文件」这类约束**只对游戏运行成立，对本功能不成立**。
- **JS 是并列的一等输入**，不是 TS 的降级路径：本仓既有 JS 项目形态就是
  `jsconfig.json` + 直接跑 `res://**.js`（`scripts/presets/jsconfig.json.txt`；
  `jsb_script_language.h:250-252` 按 `JSB_USE_TYPESCRIPT` 切换脚本扩展名）。

⇒ 真正的选型问题不是「怎么解析」，而是「**解析放在哪一侧**」：

| | C1 编辑器侧提取 → sidecar | C2 运行时按需解析源文件 |
|---|---|---|
| 解析器 | Node 里的 TypeScript 编译器（现成） | C++ 里新写 |
| 分发 | 复用 09-26 已落地的 `.sig` 链路 | 无需分发 |
| 每次查询成本 | 一次文件读 + 查表 | 解析（或缓存） |
| 首次触发成本 | 依赖提取器已跑过（有摘要门控与 4 个触发点） | 无（读到即解析） |

---

## 1. 方案逐个判定

### A. 运行时读**编译产物 `.js`** 的 JSDoc —— ❌ 取错了输入源

**实测（`project/tsconfig.json` 原样，`tsc --noCheck`）：**

| 输入 | 产物 `.js` 是否保留注释 |
|---|---|
| 无装饰器的类 + 成员 | ✅ 都保留 |
| `@bind()` 类 + **无装饰器**成员 | ❌ **类级注释丢失**；成员注释保留 |
| `@bind()` 类 + 被装饰成员（`@bind.exposed.const()` / `@bind.deprecated(...)`） | ✅ 成员注释一律保留 |
| `target: esnext` + 原生装饰器 | ✅ 全部保留（类级也在） |

**根因（实测，非推断）**：`target: es2022` 时 tsc 把被装饰的类**下编译**成
`var X = (() => { ... class extends ... })()` 表达式，类声明前的注释落在被丢弃的语法位置上。

**判定：❌ 取错输入源。** 这不是"信息在编译中丢了所以做不成"，而是**不该读产物**：
需求要的是源文件里的注释，而 §0 已确认源文件就在编辑器磁盘上。
（另：JS 项目根本没有"编译产物"这一层，本方案对它们无从谈起。）

### B. `Function.prototype.toString()` 取注释 —— ❌

**实测（Node 24 / V8）**：`function f(a,b=1){}`、`K.prototype.m.toString()`、`K.s.toString()`
返回的函数源文本**不含注释**（V8 按规范剥离）。

现有 `count_declared_parameters` 的源文本扫描能活下来，是因为它只要形参表；
注释不存在——这条路与 C1/C2 都不兼容（它读的是运行期函数对象，而 C2 读的是磁盘文本）。

### C1. 编辑器侧提取（TS Compiler API，同一份 AST）→ `.sig` sidecar（**推荐**）

**实测（tsc 6.0.3，`ts.createSourceFile`）**：`node.jsDoc` 直接给出

- `jsDoc[].comment`：正文（保留 markdown 与换行）；
- `jsDoc[].tags[]`：`@deprecated` / `@experimental` / `@param` / `@returns` / `@see` …；
- 被 `@bind.xxx()` 装饰的类与成员**一视同仁**。

**实测（JS 输入同样成立）**：`allowJs: true`（或 `jsconfig.json` 加同一开关）时
`ts.parseJsonConfigFileContent` 的 `fileNames` 收进 `.js`，`ts.createSourceFile(..., ScriptKind.JS)`
对 `export default class A extends Node { /** Member summary. */ greet(name) {...} }` 给出
类与成员的完整 JSDoc（探针实测：`CLASS A ["Class summary."]` / `MEMBER greet ["Member summary."]`）。
⇒ **同一套提取器同时覆盖 `.ts` 与 `.js`**，不需要两套逻辑。

**分发链路已是现成件**（09-26 任务落地）：

| 环节 | 位置 |
|---|---|
| 提取器进程 + 摘要门控 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:1400-1423`、`_regenerate_signatures()` |
| 工具产物 | `scripts/jsb.editor/src/signature/jsb.signature.extract.cts` → `scripts/out/jsb.signature.extract.cjs`，`SConstruct:658` |
| sidecar | `<outDir>/<rel>.sig`（`JSB_SIGNATURE_EXT`，`src/jsb.config.h`） |
| 运行时读取 | `src/runtime/bridge/jsb_signature.{h,cpp}`（`signature_load`，魔数 `JSIG` + 版本 + 懒加载 + 缺失静默回退） |
| 清理白名单 | `src/editor/tests/test_jsb_editor_cleanup.h` |

**新增量**：sidecar 格式加文档字符串段（版本 +1）；C++ 侧把字符串填进 `ScriptBaseDoc` / `MethodDoc::description`。
**新增覆盖**：提取器要同时枚举 `.js`（现只枚举 `.ts`，见 `jsb.signature.extract.cts` main 的 `endsWith(".ts")` 过滤）。

**判定：✅ 推荐。**

### C2. 运行时（C++）按需读源文件自己解析 —— ⚠️ 可行但更贵

**为什么这次它不再被"导出包没有源文件"排除**：文档只用编辑器（§0）；编辑器里源文件在磁盘上，
且 `GodotJSScript` **已经持有源文本**——`jsb_resource_loader.cpp:129-135` 在加载资源时
`load_source_code(p_path)` → `_set_source_code()`（`jsb_script.cpp:58-78`），
`_get_source_code()` 可直接取（`jsb_script.h:148`）。JS 项目的源就是 `.js` 本身，路径即 `get_path()`。

**代价**：需要在 C++ 里写一个"取声明前导 JSDoc + 关联到类/成员"的扫描器，
或引入真解析器。前者必须先解决注释与声明之间的**装饰器**（`@bind.xxx()`）间隔，
以及 TS 的 `@decorator` 行；后者已被前序任务实测否决（见 F）。**这是自建第二套解析逻辑。**

**判定：⚠️ 可行，不推荐。** 与 C1 相比没有省下任何产物，只多出一份必须与 TS 语料同步演进的解析器。

### D. 额外 emit `.d.ts` 再读 —— ❌

**实测**：`--declaration --emitDeclarationOnly` 的 `.d.ts` 完整保留 JSDoc（含 tag），包括被装饰成员。
**判定：❌** 多一次编译 + 多一棵产物树 + 多一种要打包的产物 + 运行时第二个解析器；
且对 `.js` 项目仍需 `allowJs`，与 C1 的前置条件相同却多出全部开销。

### E. `emitDecoratorMetadata` / `context.metadata` —— ❌

前序 09-24 实测：现代装饰器要求 `experimentalDecorators` 关闭，而 `emitDecoratorMetadata`
强制 legacy 装饰器，与本仓 9 处 `requires modern decorator support` 冲突；元数据里也不含注释正文。

### F. tree-sitter / 自研 TS 解析器 —— ❌

`09-06-lowprio-tree-sitter-ast` 实测否决（TS 语法 8.34 MiB 源码 + 六平台接线 + MSVC 巨型 TU 特例）；
`09-26-method-signal-signature` §2 列了 7 条具名脆弱点否决自研。
注意：这两条针对的是「运行时光靠文本解析」；C1 走**编辑器侧 + 真 AST**，与本否决不冲突。

---

## 2. 注释语法与关联规则（用户 2026-09-28 拍板）

用户原话（要点）：

> 「首行作为 brief，全文 description。因为 TS/JS 不像 GDScript 整个文件就是一个类，TS/JS 要显式定义类，
> 所以注释应该**从 class 声明的行开始往上找**，而不是从文件头部往下找，并且注释和 class 声明的那一行
> 往上一行是 空行 的话则认为没有为这个类写 description 或 brief，否则可能会把其他说明当作这个类的说明。」
> 「另外记得处理成员（属性/函数）上面有注解，和注释的情况，这个注释可能写在这个成员声明的上一行，
> 也可能写在这个成员的所有注解的上一行。」

⇒ **统一规则（类与成员同一套，无特例）**：

1. 取「声明之前的**最后一段** `/** ... */`」，其中「声明」包含其全部装饰器
   ⇒ 注释写在装饰器**之上**或装饰器**之下**、紧贴声明，都算（两种情况实测都能取到）。
2. 该注释与声明之间若有**空行** ⇒ 视为没有写文档（避免把文件里其它说明当成这个类的说明）。
3. 分段：**首行 = brief，全文 = description**（引擎 `DocData::ClassDoc` 两个字段都收）。

**实测验证（探针，ts 6.0.3 `ts.createSourceFile` + 源码文本扫描）**：

| 形态 | 结果 |
|---|---|
| `/** Doc */` 紧邻 `@bind()` 之上 | ✅ 取到 |
| `@bind.exposed.const()` 之后、成员之前紧贴 `/** Doc */` | ✅ 取到（`node.jsDoc` 为空，须靠"声明起点回溯") |
| 注释→成员之间有空行 | ✅ 判定为"无文档" |
| 方法 / 属性 / getter / `accessor` 信号 / `static` / 构造函数 | ✅ 全部取到 |
| 类级注释与文件头部的无关注释 | ✅ 互不干扰（只看声明紧邻那一段） |

**实现要点（实测细节）**：TypeScript **不会**在 AST 上暴露"装饰器之后的 JSDoc"
（`node.jsDoc` 与 `ts.getJSDocCommentsAndTags` 都为空；探针实测 `MEMBER Y jsDoc= []`），
所以两个位置必须用**同一条**源码扫描实现：
以 `getFullStart()` 为下界、`max(声明起点, 最后一个装饰器/modifier 的 end)` 为上界，
在原始文本里找最后一段 `/** ... */`，再检查其 `end` 到上界之间有无空行。

---

## 3. 关键约束（实施前必须满足）

1. **`TOOLS_ENABLED` 门控**：文档管线本来就在 `#ifdef TOOLS_ENABLED` / `#if JSB_TOOLS` 内，
   **游戏导出不应因此变重**。
2. **`.ts` 不进导出包**：`jsb_export_plugin.cpp:95-98` 明确跳过 `.ts`。因此 C1 的 sidecar 必须
   像现在这样**显式打包**（`jsb_export_plugin.cpp:173-190`，挂在 `export_raw_file`）。
3. **提取器的输入枚举要覆盖 `.js`**：现有 main 只收 `.ts`（`jsb.signature.extract.cts` main 的
   `endsWith(".ts")` / `.d.ts` 过滤），且其 `isGodotScript` 判据基于 `import { X } from "godot"`
   与 `export default class`——JS 源码同形，但**放行后缀、`allowJs` 是否开启**要一并处理。
4. **摘要门控含工具 md5**（09-26 既有裁决）：升级扩展会换提取器而源文件不动，
   文档段并入同一份 sidecar ⇒ 不引入新的门控维度。
5. **sidecar 版本必须递增**：`jsb_signature.cpp` 已有"版本不符 ⇒ 按没有清单处理"的拒绝路径。
6. **缺失即静默**：无 sidecar（未跑提取器 / 纯 JS 项目且提取器未覆盖 / 版本不符）时文档为空，不报错。
7. **`_get_documentation()` 的 `methods[]` 是空壳**（`jsb_script.cpp:326-331`，`// TODO: 填充完整函数文档`）。

---

## 4. 结论

| 方案 | 判定 | 一句话理由 |
|---|---|---|
| A 读编译产物 `.js` | ❌ | 取错输入源；JS 项目也没有这一层 |
| B `Function.toString()` | ❌ | 实测 V8 返回的函数源文本不含注释 |
| **C1 编辑器侧 Compiler API → `.sig`** | ✅ **推荐** | 一个解析器同时覆盖 `.ts`/`.js`（均实测），整条分发链路已存在 |
| C2 运行时读源文件自解析 | ⚠️ | 可行（编辑器有源文本），但要在 C++ 里自建第二套 JSDoc+装饰器扫描逻辑 |
| D 额外 emit `.d.ts` | ❌ | 多编译 + 多产物 + 第二个解析器，收益为零 |
| E 装饰器元数据 | ❌ | 与"强制现代装饰器"冲突（前序实测） |
| F tree-sitter / 自研 | ❌ | 前序实测否决；C1 不需要 |
