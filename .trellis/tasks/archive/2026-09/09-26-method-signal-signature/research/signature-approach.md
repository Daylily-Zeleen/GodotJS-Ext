# 函数与信号签名：方案裁决（tree-sitter / 正则 / 更好方案）

> 2026-09-26。**约束（用户当轮原话）**：「绝对不要写注解去反射」——即不允许让作者再写一遍
> 参数/返回值类型（否决前序任务 `09-24-script-static-members` 的 Q4「注解路线」）。
> 目标：方法参数名/类型/可选性/默认值/剩余参数、返回值类型、信号参数类型。

---

## 0. 结论先行

**推荐：编辑器侧用 TypeScript Compiler API 提取 → 落「签名清单」sidecar 文件 → 运行时读取。**

- **不引入 tree-sitter**（理由见 §3）
- **不写正则**（理由见 §2，实测脆弱点已具名）
- **不需要作者写任何注解**（满足约束）

**决定性事实**：类型信息**只存在于 `.ts`**，编译后的 `.js` 里**一个类型都没有**（实测，§1.1）。
且**导出的游戏只带 `.js`**（`.ts` 被显式跳过打包，§1.4）。⇒ 任何「运行时解析」的方案
（tree-sitter 或正则，都作用于运行期可见的文本）**在导出形态下拿不到类型**，只能拿到参数名。
这不是工具选择问题，是**输入不存在**的问题。

---

## 1. 实测：各层到底能拿到什么

### 1.1 tsc 编译产物：类型全被擦除，参数名保留

用真实项目 `tsconfig`（`target: es2022` / `module: node16`）编译
（探针 `.agent_tmp/sigprobe/`，已清理）：

```ts
// 源
complex(a?: number | null, b: Map<string, number> = new Map(), cb: (x: number) => void = () => {}): Pair
```

```js
// 产物 —— 类型全没了，名字/默认值/剩余参数还在
complex(a, b = new Map(), cb = () => { }, obj = { x: 1, y: 2 }) { return { x: 1, y: 2 }; }
```

### 1.2 `Function.prototype.toString()`（运行时，零依赖）

node 实测（同一份编译产物形态）：

| 方法 | `length` | `toString()` |
|---|---|---|
| `method(a, b = "z", ...rest)` | **1** | `method(a, b = "z", ...rest) {}` |
| `complex(a, b = new Map(), cb = () => {}, obj = { x: 1, y: 2 })` | **1** | 完整形参表（含嵌套箭头函数默认值） |
| `noArgs()` | 0 | `noArgs() {}` |
| `destructured({ a, b })` | 1 | `destructured({ a, b }) { return a + b; }` |

⇒ **能拿**：参数名、默认值、剩余参数、`length`（`length` 只数「默认值之前」的参数，不是总数）。
⇒ **拿不到**：任何类型、返回值类型。

**`length` 的语义坑**（决定了 `get_method_argument_count` 不能只靠它）：
`method(a, b = "z", ...rest)` 的 `length` = **1**，真实参数个数是 3。
`Function.length` = 「第一个带默认值/剩余参数之前的参数个数」，是**下界**不是个数。

### 1.3 tsc 的声明产物（`.d.ts`）：类型精确，但需额外一次发射

真实夹具 `--declaration --emitDeclarationOnly` 实测：

```ts
export default class StaticMembersTarget extends Node {
    static readonly N: number;
    static readonly F = 1.5;
    static readonly E: typeof Color2;
    accessor tag: number;
    static score: number;
    greet(): number;
}
```

`Probe` 探针（`.agent_tmp/sigemit/`，已清理）拿到完整签名：

```ts
method(a: number, b?: string, ...rest: boolean[]): void;
complex(a?: number | null, b?: Map<string, number>, cb?: (x: number, y: number) => void, obj?: Pair): Pair;
destructured({ a, b }: Pair): number;
```

⇒ **`b?: string` 把「默认值」规范化成了可选**（`b = "z"` → `b?`），这是 `.d.ts` 的语义损失，
需要在清单里同时保留「有默认值」与「可选」两个正交信息（用 TS API 而非读 `.d.ts` 即可避免，见 §1.5）。

### 1.4 导出形态：只带 `.js`，`.ts` 被显式跳过

`src/editor/weaver-editor/jsb_export_plugin.cpp:271-277`：

```cpp
if (p_path.ends_with("." JSB_TYPESCRIPT_EXT)) {
    const String compiled_script_path = ...convert_typescript_path(p_path);
    export_compiled_script(compiled_script_path, true);
    // always skip the typescript source from packing
    JSB_EXPORT_LOG(Verbose, "export source: %s => %s", p_path, compiled_script_path);
}
```

⇒ 发布出去的包里**没有 `.ts`**。运行期「解析源码」在导出形态下**无源码可解**。

### 1.5 TypeScript Compiler API：精确，且 TS 已是硬依赖

`project/node_modules/typescript` = **6.0.3**（`project/package.json` devDependency），
`lib/typescript.js` 存在。真实夹具实测（探针 `.agent_tmp/tsast/`，已清理）：

```json
{ "name": "method", "returns": "void",
  "params": [ {"name":"a","type":"number","optional":false,"rest":false,"default":null},
              {"name":"b","type":"string","optional":false,"rest":false,"default":"\"z\""},
              {"name":"rest","type":"boolean[]","optional":false,"rest":true,"default":null} ] }
{ "name": "complex", "returns": "Map<string, number>",
  "params": [ {"name":"a","type":"number | null","optional":true,"rest":false,"default":null},
              {"name":"cb","type":"(x: number) => void","optional":false,"rest":false,"default":"() => {}"} ] }
{ "name": "destructured", "returns": "number",
  "params": [ {"name":"{ a, b }","type":"{ a: number; b: number }", ...} ] }
```

⇒ 参数名、类型（含联合/泛型/函数类型/解构模式）、可选性、默认值、剩余参数、返回值**全部精确**，
且 `optional` 与 `default` **分列**（不丢 §1.3 的信息）。这是 §1.3 的 `.d.ts` 路线拿不到的一档。

### 1.6 信号：类型写在 `Signal<...>` 里，运行时完全不可见

真实用法（`project/test_01.ts:15-19`）：

```ts
@bind.signal()
accessor no_arg!: Signal<() => void>;
@bind.signal()
accessor test_signal!: Signal<(value: number) => void>;
```

现有实现只登记**名字**（`ScriptSignalInfo` 是空结构 `jsb_class_info.h:155-156`；
信号集合只存字符串数组，`jsb_class_info.cpp:203-217`）。参数类型只存在于 TS 类型注解里
⇒ 与 §1.1 同理，运行时不可得。

---

## 2. 为什么不用正则

本仓**已有**一处正则 + 手写括号深度扫描做参数重命名（`jsb_editor_plugin.cpp:540-592`），
它**能工作**，因为输入是**我方自己生成的 `.d.ts`**（形状已知）。用户 `.ts` 不是这种输入。

具名脆弱点（对**用户源码**）：

| 输入 | 正则为何错 |
|---|---|
| `cb: (x: number) => void = () => {}` | 形参表里含**嵌套箭头函数与嵌套括号**；必须做括号/尖括号/字符串/注释状态机才能切对参数边界 |
| `a?: number \| null` | `?` 与 `\|` 与「可选」的语义要分别处理 |
| `Map<string, number>` | 泛型里的**逗号不是参数分隔符**（`split(",")` 必错） |
| `destructured({ a, b }: Pair)` | 解构模式的 `,` 同样不是参数分隔符 |
| `method(a: number /* , b: string */)` | 注释里的逗号 |
| `x: string = "a,b"` | 字符串字面量里的逗号 |
| 重载 / 装饰器 / `#private` / getter-setter | 需区分声明种类 |

⇒ 要写对，就得写出一个**带状态的 TS 形参解析器**。那已经不是"写正则"，
是**自研 mini-parser**——而本仓的既有教训正是「不要自己造这个轮子」
（`09-06-lowprio-tree-sitter-ast` 的评估已否定自研解析器）。

---

## 3. 为什么不用 tree-sitter

`09-06-lowprio-tree-sitter-ast/research/tree-sitter-evaluation.md` 已实测评估（结论：不引入），
本节只补一条**本轮新增的决定性理由**：

**tree-sitter 解析的是运行期可见的文本，而导出形态下没有 `.ts`（§1.4）**
⇒ 它同样拿不到类型。它只能在**编辑器**里替代 §2 那个 mini-parser，
而 §1.5 的 TS Compiler API 在编辑器里**已经**能给出更准的结果（且 TS 已是硬依赖）。

原评估的代价数据仍然成立（若仍要引）：`tree-sitter-typescript/src/parser.c` = **8.34 MiB**
（JS 语法 2.72 MiB）、`unpackedSize` 37 MiB、需六平台构建接线 + MSVC 巨型 TU 特例
（`/bigobj`）、web 腿 wasm 加载另设计。

**收益侧**：唯一真实收益是「替换 `jsb_script_language.cpp` 那 4 处类名提取正则」
（原评估的 #1–#4）。**与本轮的「函数/信号签名」目标无关**——那是另一个议题，且优先级低
（失效面窄：类头必须单行）。

---

## 4. 推荐方案：签名清单 sidecar

### 4.1 形态

```
.ts 源  ──(tsc)──►  .js 产物（类型已擦除）        ← 运行时执行这个
   │
   └──(TS Compiler API，编辑器侧)──►  .type_manifest  ← 运行时读取这个
```

- **提取器**：编辑器侧跑一次 node 脚本（用 `project/node_modules/typescript`），
  遍历项目 `.ts`，用 Compiler API 取类/方法/信号签名，写清单文件。
- **清单位置**：与编译产物同目录（`res://.godot/godotjs_ext/`），
  **与 `.paths_mapping` 同型**（§4.2）。
- **运行时**：`GodotJSScript` 加载模块时读对应清单条目，填 `ScriptMethodInfo` / `ScriptSignalInfo`。
- **缺失时降级**：清单不存在（纯 JS 项目、或未跑提取器）⇒ 退回
  `Function.length` + `Function.prototype.toString()`（§1.2）拿参数名/下界，**类型留空**。
  不报错、不阻塞——这是"尽力而为"的诚实边界。

### 4.2 为什么这在本仓是**既有模式**，不是新发明

`.paths_mapping` 就是同一个形状，逐条对应：

| 环节 | `.paths_mapping`（既有） | 签名清单（本方案） |
|---|---|---|
| 编辑器生成 | `PathsMapping::generate_from_tsconfig`（`jsb_paths_mapping.cpp`） | 新增提取器 |
| 落盘位置 | `get_paths_mapping_path()` = 编译产物同目录（`:77-79`） | 同 |
| 运行时读取 | `jsb_script_language.cpp:196-199` `PathsMapping::refresh()` | 同 |
| 导出打包 | `jsb_export_plugin.cpp:140-145` `export_raw_file(mapping_path, false)` | 同 |
| 缺失时行为 | 静默跳过（`:145-147`） | 降级（§4.1） |

⇒ 接线成本已知且小，不引入新依赖、不碰 `third/`、不动构建矩阵。

### 4.3 为什么提取器用 TS Compiler API 而不是 `--declaration`

| | TS Compiler API | 读 `--declaration` 产物 |
|---|---|---|
| 依赖 | 已有（typescript 6.0.3） | 需再跑一次 tsc，产物要另落盘 |
| `optional` vs `default` | **分列**（§1.5） | 合并成 `b?`（§1.3，丢信息） |
| 参数名 | 精确 | `.d.ts` 会丢实现签名里的细节（如默认值表达式） |
| 覆盖 `.js` 项目 | 否（无类型可提） | 否 |
| 额外产物 | 清单一份 | `.d.ts` 一棵树 + 清单 |

⇒ API 路线信息更全、产物更少。

### 4.4 清单格式（草案）

线性文本，对齐 `.paths_mapping` 的「可增量 diff、易手查」风格，而非 JSON 大文件：

```
# <module_id>\t<member_kind>\t<name>\t<ret>\t<param>;<param>;...
res://a.ts\tmethod\tgreet\tnumber\t
res://a.ts\tmethod\tcomplex\tMap<string, number>\ta:number|null?;cb:(x: number) => void=
res://a.ts\tsignal\ttest_signal\t\tvalue:number
```

字段：`name:type[?][=][...]`（`?` 可选、`=` 有默认值、`...` 剩余参数）。
**类型字符串原样保留 TS 写法**（不在此处归一化成 Variant 类型——那是消费端的职责，
且 TS 类型到 Variant 的映射需要一个显式表，见 §5 未决项）。

---

## 5. 裁决（2026-09-26）

### 5.1 TS 类型别名（`int32` 等）如何映射 —— 实测

TS Compiler API **保留别名名**，**不**透明给出底层 `number`。探针
（`project/.agent_tmp_alias/`，已清理；`node .agent_tmp_alias/walk.mjs tsconfig.json .agent_tmp_alias/alias.ts`）：

| 源码写法 | `checker.typeToString(t)` | `t.aliasSymbol.name` | `aliasSymbol.declarations` |
|---|---|---|---|
| `x: int32` / `uint64` / `float64` / `byte` / `StringName` | 别名本身（`"int32"` …） | `"int32"` … | **`[]`**（空） |
| `type MyInt = int32` 后 `x: MyInt` | `"int32"`（**不是** `"number"`） | `"int32"` | `[]` |
| `type MyNum = number` 后 `x: MyNum` | `"number"` | `null` | — |

⇒ 别名声明位于 `scripts/typings/godot.generated.d.ts:115-121`，**不在程序自身的源文件里**，
故 `declarations` 为空、拿不到 `underlying`。**类型映射必须查已知别名表**
（`byte`/`int32`/`uint32`/`int64`/`float32`/`float64` → 数值，`StringName` → STRING_NAME），
或把 typings 的别名声明一并纳入 program 再读 `declarations`。

### 5.2 重载 —— 引擎自身的先例（C#）

`D:/Dev/godot/godot/modules/mono/csharp_script.cpp`（只读参考）：

| 环节 | 行为 | 位置 |
|---|---|---|
| 收集 | `p_script->methods` 是**扁平向量**，同名多条目 | `:2278-2305` |
| `get_method_info` | 遇同名**第二条**即判定有重载，返回**空 `MethodInfo()`**（源码注释：*the best we can do is return an empty MethodInfo*） | `:2557-2576` |
| `get_script_method_argument_count` | 返回**首个**匹配的 `arguments.size()` | `:2534-2555` |
| `get_script_method_list` | **全部**条目都 push | `:2505-2518` |
| `CSharpInstance::get_method_argument_count` | 沿 `base_script` 链，首个匹配胜 | `:1666-1692` |

⇒ **采纳同形**：清单记录全部重载；`get_method_info` 遇同名多条退化为空；参数个数取首个匹配。

TS 侧补充（探针 `project/.agent_tmp_sig/`，已清理）：TS **擦除**重载 —— 3 条声明
`d(x:number)` / `d(x:string)` / `d(x:any){}` 经 `checker.getSignaturesOfType(..., SignatureKind.Call)`
得 **2** 个签名，emit 后只留**实现体**（`d(x) { … }`）。
⇒ 重载集必须在清单里**显式记录**（AST 侧 `declCount = 3` 可检测），不能从产物反推。

### 5.3 提取器触发时机 —— 与 `.paths_mapping` 同节奏（用户决策）

`.paths_mapping` 的重生成点：窗口获焦（`jsb_editor_plugin.cpp:164`）、编辑器就绪（`:214`）、
安装完成（`:979` / `:998`），每处由 MD5 空操作门控（`_regenerate_paths_mapping`，`:1275-1299`；
桥字段 `refresh_paths_mapping`，`src/runtime/internal/jsb_bridge_abi.h:114`，实现
`bridge_refresh_paths_mapping`，`jsb_bridge_table.cpp:525`）。签名清单**沿用同一节奏与同一 no-op 门控**。

### 5.4 参数个数（Q4）—— 本轮已实现

`get_method_argument_count` 曾恒 0（`jsb_script_instance.h:188` 的 `return 0; // TODO`）。

**语义对齐 GDScript**：`GDScriptFunction::_argument_count` 计**全部已声明参数**
（`gdscript_byte_codegen.cpp:35-39` 每参数 +1），剩余参数另以 `_vararg_index` 标记
（`gdscript_compiler.cpp:2351-2353`，`gdscript_function.h:481`）；引擎对
`Callable::get_argument_count()` 的文档是 *including optional arguments*（`doc/classes/Callable.xml:153`）。

⇒ **`Function.length` 不可用**：它是**下界**（`method(a, b = "z", ...rest)` ⇒ `length === 1`，§1.2 实测）。

**实现**：解析期扫描函数源文本的形参表（`Function.prototype.toString` 语义，经
`impl::Helper::to_string_without_side_effect`），计全部声明参数、**剩余参数不计**，
结果存入 `ScriptMethodInfo::argument_count`。已知盲点：正则字面量中的不平衡括号
（形如 `f(x = /a(b/) {}`）——属 best effort，注释已写明。

**消费端两层**（对齐 GDScript）：`GodotJSScript::_get_script_method_argument_count` 报**自有**方法
（同 `GDScript::get_script_method_argument_count`，`gdscript.cpp:370-383`）；
`GodotJSScriptInstance::get_method_argument_count` **自己走 `base` 链**
（`Object::get_method_argument_count` 先问实例且**不**走链，`object.cpp:772-781`，
同 `GDScriptInstance::get_method_argument_count`，`gdscript.cpp:1919-1931`）。

**清单落地后的关系**：清单在时用清单的精确个数（含 optional/default 分列），
清单缺失时退化为本扫描 —— 即 §4.1「缺失时降级」那条路径的实体，**不是一次性脚手架**。

---

## 6. 证据留档

| 结论 | 证据 |
|---|---|
| 类型在 `.js` 中被擦除 | `.agent_tmp/sigprobe/`（已清理）；本文 §1.1 原文 |
| `toString()` 给名字/默认值/剩余，不给类型 | node 实测输出；§1.2 表 |
| `Function.length` 是下界不是个数 | `method(a, b="z", ...rest)` ⇒ `length=1`（§1.2） |
| `.d.ts` 把「默认值」合并成「可选」 | `b?: string`（§1.3） |
| TS API 给出精确且 optional/default 分列 | §1.5 JSON |
| 导出只带 `.js` | `jsb_export_plugin.cpp:271-277` |
| TS 是硬依赖（6.0.3） | `project/package.json` devDependencies + `node_modules/typescript/lib/typescript.js` |
| `.paths_mapping` 是既有同型模式 | `jsb_paths_mapping.cpp:77-79`、`jsb_script_language.cpp:196-199`、`jsb_export_plugin.cpp:140-145` |
| 信号只存名字 | `jsb_class_info.h:155-156`、`jsb_class_info.cpp:203-217` |
| 既有正则只面对我方生成的 `.d.ts` | `jsb_editor_plugin.cpp:540-592`（注释自述「Regex obviously isn't the best tool」） |
| tree-sitter 代价 | `09-06-lowprio-tree-sitter-ast/research/tree-sitter-evaluation.md` §2 |
