# 设计与实施方案：函数与信号签名

> 依据：用户 2026-09-26 的 10 条要求 + 本轮追加「当前回退实现也改懒加载」。
> 所有 `file:line` 均为本轮实测/实读；未实测项集中在 §14。
> 本文取代 `research/signature-approach.md` §5.1 的别名结论（该节基于失效探针，已作废，见 §2.2）。

---

## 1. 范围

| 层 | 内容 | 本轮 |
|---|---|---|
| **清单层** | 编辑期提取 TS 签名 → 按脚本拆分的 sidecar → 运行时懒读 | 设计（本文），待实施 |
| **回退层** | 无清单时用函数源文本拿参数个数 | 已实施，**需改成懒加载**（§7） |
| **消费层** | `PropertyInfo` / `MethodInfo` 的填充与语义 | 设计（§5、§6） |

---

## 2. 证据摘要（本轮实测）

### 2.1 `p.type.getText()` 读 `.ts` 源 AST，不是编译后的 `.js`

同一份源码，左 = AST，右 = `ts.transpileModule` 产物：

| 方法 | `.ts` AST `p.type.getText()` | 编译后 `.js` |
|---|---|---|
| `full(a: int32, b: string = "z", c?: number, ...rest: boolean[])` | `"int32"` / `"string"` / `"number"` / `"boolean[]"` | `full(a, b = "z", c, ...rest) { return null; }` |
| `nested(cb: (x: number) => void = () => {}, obj: { p: number[] } = { p: [1] })` | `"(x: number) => void"` / `"{ p: number[] }"` | `nested(cb = () => { }, obj = { p: [1] }) { }` |
| `garr(x: GArray<number>)` | `"GArray<number>"` | `garr(x) { return x; }` |

默认值在**另一条 API** 上（`p.initializer`），与类型分列：

| 源码 | `p.type.getText()` | `p.initializer.getText()` | `questionToken` | `dotDotDotToken` |
|---|---|---|---|---|
| `b: string = "z"` | `"string"` | `"\"z\""` | false | false |
| `b = { p: [1] }` | **null** | `"{ p: [1] }"` | false | false |
| `c?: number` | `"number"` | null | **true** | false |
| `...rest: boolean[]` | `"boolean[]"` | null | false | **true** |
| `untyped(a, b = 2)` | **null**（不是 `"any"`） | `"2"` | false | false |

⇒ 四个正交信息可分列取到；**未写类型 = `null`，不伪造**。

### 2.2 别名（回答「真实类型 vs 别名」）—— 我上一轮的结论是错的

| 写法 | `p.type.getText()`（AST） | `checker.typeToString()`（类型检查器） |
|---|---|---|
| `x: int32` | **`"int32"`** | `"number"`（别名丢失） |
| `x: StringName` | **`"StringName"`** | `"string"` |
| `x: MyInt`（`type MyInt = int32`） | `"MyInt"` | `"number"` |
| `x: Vector2` | `"Vector2"` | `"Vector2"` |

别名链可从 **AST** 走通（不需要 checker）：`checker.getSymbolAtLocation(typeName)` →
`declarations.find(isTypeAliasDeclaration)` → `decl.type`，实测 `MyInt` → `["MyInt","int32"]` → 取 `int32`。

⇒ **`research/signature-approach.md` §5.1「必须查已知别名表、`aliasSymbol` 是别名名」的记载作废**：
那份探针的 `probeInProgram=false`（探针文件没进 program），结论不可信。正确做法见 §5。

### 2.3 `getText()` 的两个真实缺陷（必须处理）

| 输入 | `getText()` 原文 | `printer.printNode(removeComments:true)` |
|---|---|---|
| `Map<string, /* inner */ number>` | `"Map<string, /* inner */ number>"` ⚠️ 注释进来了 | `"Map<string, number>"` ✅ |
| `Map<\n string,\n number\n>` | 原样带换行 ⚠️ | `"Map<string, number>"` ✅ |
| `{ p: number; q: number }` | `"{ p: number; q: number }"` ✅ | `"{\n p: number;\n q: number;\n}"` ⚠️ 反而更糟 |

⇒ **类型节点用 printer 清洗；object 类型字面量不能用 printer**，需分治。

### 2.4 成本（本项目 119 个源文件）

| 路线 | 耗时 |
|---|---|
| `createProgram` + checker | **839 ms** + 遍历 3 ms |
| 只 `createSourceFile`（无 checker） | **313 ms** |

⇒ 实测差异 526 ms。但 §5 的设计**不需要 checker**（别名走 AST 链），故取 313 ms 档。

---

## 3. 数据流与文件布局

```
project/**/*.ts ──tsc──► res://.godot/godotjs_ext/**/*.js       ← 运行时执行（类型已擦除）
        │
        └──提取器(node)──► res://.godot/godotjs_ext/**/*.jsig   ← 运行时懒读 ← 本方案
```

- **sidecar 与编译产物同目录同名**，只换扩展名：`…/foo.js` ↔ `…/foo.jsig`。
  与 `.paths_mapping` 同目录（`get_jsb_out_res_path()`，`jsb_paths_mapping.cpp:77-79`）。
- **按脚本拆分**（用户要求 4）：只为查 `foo.ts` 的一个方法就去读全部脚本的清单是不合理的。
- **键 = 编译后 `.js` 路径**：`script_class_info_.module_id` 实测就是这个
  （`jsb_script.cpp:660` 的 `convert_typescript_path(get_path())`）。

---

## 4. 清单格式（二进制，体积优先）

不做人类可读（用户要求 6）。整文件头 + 字符串池 + 成员记录；类型名走池索引（重复率极高）。

```
magic      "JSIG"                      4 B
version    u8                          1
flags      u8  bit0=has_signals        1
source_md5 16 B                        ← 源 .ts 的 md5（用户要求 4 的增量依据）
pool_count varuint
  [len varuint][utf8]  × pool_count     ← 类型名/参数名/成员名都进池
member_count varuint
  kind       u8   0=method 1=signal
  name       varuint → pool
  flags      u8   bit0=has_return bit1=is_vararg bit2=is_overloaded
  ret_type   varuint → pool             (仅 has_return)
  param_count varuint
  default_count varuint                 ← = 可选参数个数（§6）
  param 记录 × param_count:
    flags    u8   bit0=optional bit1=rest
    name     varuint → pool
    type     varuint → pool  (0xFFFFFFFF = 无标注)
```

估算：2 方法的脚本 ≈ 100 B；100 方法的脚本 ≈ 2–3 KB。**每脚本一个文件**，读取即整脚本。

`source_md5` 是增量的唯一依据：提取器比对当前 `.ts` 的 md5，相同即跳过该脚本（用户要求 4）。

---

## 5. 类型映射：TS 类型文本 → `PropertyInfo`

**全部 Godot 知识留在 C++ 一侧**（提取器不做类型判定），避免 JS/C++ 两份表漂移。

### 5.1 提取器只做「AST 别名链解析」，不做判定

对每个类型节点：
1. 取 `p.type.getText()`（带注释/换行）；
2. 若节点是 `TypeReferenceNode`，沿 AST 别名链走，取**链条中第一个命中引擎别名集的名字**；否则取节点文本；
3. 用 §2.3 的规则清洗（printer 只用于非 object-literal 节点）；
4. 把结果名写进字符串池。

引擎别名集（`scripts/typings/godot.generated.d.ts:115-121` + `type.extension.d.ts:26`）：
`byte int32 uint32 int64 uint64 float32 float64 StringName`。

**这里回答了「用真实类型判定兼容」与「不要 checker」的冲突**（用户 2 与 3）：
「真实类型」由 **AST 别名链**给出，等价于 checker 的结果，但不需要 checker——
`type MyInt = int32` 链到 `int32`（可映射），`type Foo = Vector2` 链到 `Vector2`（可映射），
链到接口名（不可映射）就停在接口名 ⇒ 自然落 NIL。§2.2 已实测链走通。

### 5.2 C++ 分类（唯一判定点）

```
name == "" / 无标注                      → 参数：Variant::NIL（= 任意，见 §6.1）
别名集：int32/uint32/int64/uint64/byte   → Variant::INT
        float32/float64                  → Variant::FLOAT
        StringName                       → Variant::STRING_NAME
"number"                                 → Variant::FLOAT
"string"                                 → Variant::STRING
"boolean"                                → Variant::BOOL
"void"                                   → 无返回值（§6.2）
其余：查「Variant 内建类型名 → Variant::Type」反向表（启动时一次构建）
        for (int i = 0; i < Variant::VARIANT_MAX; ++i)
            map[Variant::get_type_name((Variant::Type)i)] = (Variant::Type)i;
      → 命中即用（Vector2/Color/GArray/GDictionary/Callable/Signal…）
```

**必须先反查原始类名，不能拿 JS 暴露名直接查表**（2026-09-26 用户指出）：

- 暴露给 JS 的类名**不一定等于** Godot 侧的原始类名 —— 实测 `NamingUtil::get_class_name`
  （`src/internal/jsb_naming_util.h:48-59`）在 `camel_case_bindings` 开启时做
  `pascal_to_pascal_case`，且硬编码两条改名：`Dictionary → GDictionary`、`Array → GArray`。
  双向映射登记在 `jsb::internal::StringNames`（`jsb_string_names.h:54-55` 的
  `replacements_` / `replacements_inv_`，由 `jsb_script_language.cpp:875-925` 的
  `populate_string_names_replacements()` 按**全部暴露类**逐个 `add_replacement` 填入）。
- 因此每个类型名进来先走一次反查：

```cpp
const StringName original = jsb::internal::StringNames::get_singleton().get_original_name(name);
// GArray → Array, GDictionary → Dictionary, 以及 camel_case 下的暴露名 → 原始名；
// 表里没有的名字原样返回（jsb_string_names.h:72-75）⇒ 对普通名是无副作用的恒等映射。
```

- **顺序**：`get_original_name(name)` → 再查「Variant 内建类型名 → `Variant::Type`」反向表 / `ClassDB::class_exists`。
  这样 `GArray` 先还原成 `Array`，命中 `Variant::ARRAY`；`Vector2` 无改名，原样命中。
- **反向表本身**用 `Variant::get_type_name` 现构，不硬编码（godot-cpp 侧已确认有
  `static String get_type_name(Variant::Type)` 与 `VARIANT_MAX`，`variant.hpp:106/334`）：

```cpp
for (int i = 0; i < Variant::VARIANT_MAX; ++i)
    map[Variant::get_type_name((Variant::Type)i)] = (Variant::Type)i;
```

- **未命中**：`ClassDB::class_exists(original)`（godot-cpp `class_db_singleton.hpp:66`）
  ⇒ `Variant::OBJECT` + `class_name = original`（这里也要用**原始名**，inspector 按原始类名显示）；
  仍未知 ⇒ `Variant::NIL`（见 §6.1 / §6.2 的区分）。

**注意 `ClassDB::class_exists` 也要用原始名**：`GArray` / `GDictionary` 不是 ClassDB 类，
不先反查就会误判成未知。

反向表用 `Variant::get_type_name` 现构，**不硬编码**——引擎加类型自动跟上。
`GArray<T>` / `Map<...>` 只按名字匹配到 `GArray` / 未命中；泛型实参不参与映射
（`GArray` → `ARRAY`，`Map` → 未命中 → NIL）。

---

## 6. 三态编码（回答 2：可选参数如何与「不兼容」区分）

用户原提议是两种含义都用 `Variant::NIL`。**不必这样，引擎已有原生通道**，实测如下。

### 6.1 参数

| 语义 | 编码 | 引擎依据 |
|---|---|---|
| 无法映射的类型 | `type = Variant::NIL` | `GDScriptAnalyzer::type_from_property`（`gdscript_analyzer.cpp:5916`）：`type == NIL && (p_is_arg \|\| usage & NIL_IS_VARIANT)` ⇒ **参数位 NIL 一律解释为 VARIANT（任意值）**；`MethodBind::get_argument_info`（`method_bind.h:154`）给无类型参数正是 `Variant::NIL` + `PROPERTY_USAGE_DEFAULT \| PROPERTY_USAGE_NIL_IS_VARIANT` |
| **可选**（`?` 或带默认值） | **`MethodInfo::default_arguments` 追加一个 `Variant()`** | `get_function_signature`（`gdscript_analyzer.cpp:6123`）`r_default_arg_count = p_info.default_arguments.size()`；`validate_call_arg`（`:6143`）最小参数数 = `arguments.size() - default_arguments.size()`。GDScript 自己的 DEFVAL 就是这条路（`gdscript_compiler.cpp:1989`） |
| **剩余参数**（`...rest`） | 不进 `arguments` + `MethodInfo::flags \|= METHOD_FLAG_VARARG` | `validate_call_arg`（`:6146`）只在 **非** VARARG 时检查上限；不加会报 `Too many arguments`。值与 GDScript 一致：`_argument_count` 不含剩余参数 |

⇒ **两个正交维度各有原生通道，"不兼容"与"可选"天然可分**，不需要重载 NIL 的含义。
默认值本身取不到（TS 表达式无法求值）⇒ 只记"有默认值"这一事实（`default_count`），
`default_arguments` 里放 `Variant()` 占位。**计数才是 arity 的依据**，占位值够用。

### 6.2 返回值

| 语义 | 编码 | 依据 |
|---|---|---|
| 真 `void` | `return_val.type = NIL`，**不带** `NIL_IS_VARIANT` | `gdscript_analyzer.cpp:3524/3575`：取 void 调用的返回值 ⇒ `Cannot get return value of call to "…" because it returns "void"` |
| 无法映射（应为任意值） | `return_val.type = NIL` **带** `PROPERTY_USAGE_NIL_IS_VARIANT` | `:5916` 对非参数位要求该 flag 才判 VARIANT；`doc_data.cpp:83` 也只在该 flag 下把 NIL 渲染成 `Variant` |
| 具体类型 | 对应 `Variant::Type`（object 另带 `class_name`） | — |

⇒ **返回值必须区分「void」与「不可映射」**：前者必然报错、后者应放过。这是 6.1 之外**另一处**
不能只用单个 NIL 的地方。

### 6.3 由此推翻一个早先决定

早先记过「**不要**填 `_get_method_info()` 的 `args`，否则 `validate_call_arg` 把每个参数当必填、
对外来脚本产生新的 GDScript 解析错误」。该结论**只在 `default_arguments` 为空时成立** ——
现在 `default_arguments` 会正确填充，最小 arity 变成 `arguments.size() - default_count`（`:6143`），
所以**填 `args` 是安全的**，而且是 inspector / 分析器看到真实签名的唯一途径。

**验收必须覆盖**：`add(a, b = 2, ...rest)` 上 GDScript 写 `obj.add(1)`（少参）
与 `obj.add(1, 2, 3)`（多参）**都必须照常解析**（见 §15 A3/A4）。

---

## 7. 懒加载（本轮核心变更）

### 7.1 现状（要改掉）

`jsb_class_info.cpp:559-562` 在**类解析期**对每个 prototype 方法都跑一次
`_count_declared_parameters(to_string_without_side_effect(...))`：

```cpp
ScriptMethodInfo method_info{};
method_info.argument_count = _count_declared_parameters(impl::Helper::to_string_without_side_effect(isolate, prop_val));
```

这是**急切**的：每个方法一次 source-text 物化 + 一次扫描，与"用不用"无关。
按用户要求改成「用到再解析」。

### 7.2 目标形态

- 解析期**不做任何签名工作**（删掉上面那行）。
- `_get_own_method_argument_count` / `_get_method_info` / `_get_script_method_list`
  **每次查询时按需解析**。
- `argument_count` 字段改为三态（**注释用中文写明含义**，用户 2026-09-26 要求）：

```cpp
	// 已声明参数个数，剩余参数不计。三态取值：
	//   k_ArgumentCountNotComputed —— 尚未解析（懒加载，首次查询时才去读函数源文本）
	//   k_ArgumentCountUnknown     —— 已尝试解析但无法判定（形参表闭合不了：正则字面量里的
	//                                 不平衡括号、模板字面量里嵌套的反引号、V8 把源文本截断到
	//                                 前 111 字符）。**不得当成 0** —— 那会把"读不出来"谎报成
	//                                 "没有参数"。消费端见负值即报 invalid，退回 get_method_info()。
	//   >= 0                       —— 真实个数。
	int argument_count = k_ArgumentCountNotComputed;
```

### 7.3 回退路径如何「用到再解析」（复用既有机制）

查询时重新进入环境拿函数对象，**复用 `call_script_method` 已有的 `method_cache`**
（`jsb_environment.cpp:1926-1944` 就是这么懒取并缓存 `v8::Global<v8::Function>` 的）：

```cpp
jsb::JSEnvironment env(get_path(), true);
JavaScriptModule *module = env->get_module_cache().find(script_class_info_.module_id);
ScriptClassInfoPtr ci = env->find_script_class(module->script_class_id);
// 名字映射同 _has_method（'_' 前缀 → NamingUtil::get_member_name）
// 从 ci->method_cache 取/插入 v8::Global<v8::Function>
// to_string_without_side_effect(isolate, fn) → _count_declared_parameters
```

- 必须像 `Environment::call_script_method` 那样进 isolate / handle / context scope。
- 环境析构后不影响：缓存的是 `int`。
- 若模块查不到 / 名字不是自有方法 ⇒ `k_ArgumentCountUnknown` ⇒ 报 invalid（保持既有不变量）。

### 7.4 缓存与刷新策略（对应要求 10）

| 场景 | 行为 |
|---|---|
| 导出后运行（非编辑器） | 解析一次并缓存（`argument_count` 落字段） |
| `JSB_TOOLS` 且 `Engine::get_singleton()->is_editor_hint()` | **不保留清单数据**，每次查询重读 sidecar（或走回退）；`_reload` 时清空 |

判据用既有先例 `Engine::get_singleton()->is_editor_hint()`（`jsb_settings.cpp:60`、
`jsb_resource_loader.cpp:91`、`jsb_script.cpp:174`）。

---

## 8. 提取器：实现与宿主（回答 8）

**结论：源码可以放 `scripts/jsb.editor/src/`，但必须另建一个 Node 目标的构建产物，不能并进引擎内嵌的 AMD bundle。**

依据：

| 事实 | 位置 |
|---|---|
| 编辑器 bundle 是 **AMD**，`outFile: ../out/jsb.editor.bundle.js` | `scripts/jsb.editor/tsconfig.json` |
| 它被 `PresetDefine(...)` 嵌进 `jsb_editor_preset.gen.cpp`，在**引擎自己的 V8** 里经 `require('jsb.editor.main')` 加载 | `SConstruct:617-620`、`jsb_editor_plugin.cpp:1175` |
| 引擎的模块解析搜索路径只有 `res://` 系（out 目录、`res://`、`res://node_modules`），**没有 Node 内建模块** | `jsb_environment.cpp:448-451` |
| 而 `typescript.js` 在**加载期/调用期**依赖 `require("fs"/"path"/"os"/"crypto"/"perf_hooks"/"inspector")` | 实测 grep `typescript.js`（:5089 / :5271 / :8273-8278 / :8424） |
| 它还需要 `ts.sys`（Node 的文件系统宿主）；浏览器场景要自备 host | TS 的既有设计 |

⇒ 在引擎 V8 里跑 TS Compiler API 需要自造 host 并绕开全部 Node 内建，脆弱且难验证。
**采用 spawn node**，与既有 `start_tsc_watch()` 同型（`jsb_editor_plugin.cpp:1204-1220`：
`Process::create("tsc", "node.exe", {"./node_modules/typescript/bin/tsc", "-w"})`）。

**落地形态**：
1. 源码住 `scripts/<pkg>/src/`（与编辑器代码同仓、同 typecheck），**独立 tsconfig**（CommonJS 目标），
   构建产物 `scripts/out/<tool>.cjs`；
2. 该产物作为 **preset 文件安装进项目根**（`add_install_file`，先例 `jsb_editor_plugin.cpp:426-433`）；
3. 编辑器 spawn：`node.exe <项目根>/<tool>.cjs --project <项目根>`；
4. 脚本位于项目根 ⇒ `require("typescript")` 从 `<项目根>/node_modules` 解析成功
   （与 `tsc` 同一条解析路径，已实测可用）。

**被否决**：把提取器并进 AMD bundle（理由同上）；用 `tsc --declaration` 产物代替（§13）。

---

## 9. 编辑器侧：生成、增量、清理、导出

### 9.1 触发时机（已定案）

与 `_regenerate_paths_mapping()` 同节奏，同样受 `JSB_USE_TYPESCRIPT` 门控（用户要求 1）：

| 点 | 行 |
|---|---|
| 窗口获焦 | `jsb_editor_plugin.cpp:164` |
| 编辑器就绪 | `:214` |
| 安装完成（两处） | `:979` / `:998` |

**但门控输入必须不同**（`.paths_mapping` 的门控只看 tsconfig，改 `.ts` 不改 tsconfig ⇒ 永不重跑）：

- 编辑器侧廉价门控（避免白 spawn）：`(全部 .ts 的数量, 最大 mtime, tsconfig md5)` 摘要比对；
- 提取器侧精细门控：逐脚本比对 sidecar 头部的 `source_md5`，相同即跳过。

### 9.2 清理接入（用户要求 4）

`collect_invalid_files`（`jsb_editor_plugin.cpp:756-773`）当前规则：
非 `.js/.cjs/.mjs`，或没有对应 `.ts` 源 ⇒ 判 invalid，由 `cleanup_invalid_files`（`:1014-1021`）删除。
**我们的 `.sig`/`.jsig` 会被它直接删掉**。必须加分支：

- 扩展名属签名 sidecar ⇒ **不判 invalid**；
- 但其对应 `.ts`（经 `convert_javascript_path` 反查）已不存在 ⇒ **判 invalid**（陈旧清单要清掉）。

即：清单的存在性跟随源脚本，而不是被无条件清除。

### 9.3 导出（用户要求 4）

`jsb_export_plugin.cpp:140-145` 已有 `export_raw_file(mapping_path, false)` 先例；
对每个 sidecar 同样 `export_raw_file`。注意 sidecar 与 `.js` 同目录，导出遍历 `.js` 时可顺带带上。

---

## 10. 回退与边界（回答 9）

| 情形 | 行为 |
|---|---|
| 构建未开 `JSB_USE_TYPESCRIPT` | 无 `.ts`、无清单。清单读取代码 `#if JSB_USE_TYPESCRIPT` 编掉；**回退（源文本扫描）不门控**，仍可用 |
| 开了 TS，但该脚本没有 sidecar（新脚本 / 提取器没跑） | 该脚本走回退；不报错 |
| 有 sidecar，但查的成员不在其中 | 该成员走回退 |
| sidecar 损坏 / 版本不符 | 视为不存在，打一次 `JSB_LOG(Warning)`，不刷屏 |
| 回退也拿不到（函数取不到 / 形参表闭合不了） | 报 invalid（`*r_is_valid = false`；Script 层返回空 `Variant`），**不报 0** |

不变量沿用已实施的：
Script 层未知名字返回**空 `Variant`**（否则 `ScriptExtension` 的 INT 分支会认下任意名字）；
实例层总 miss 置 `*r_is_valid = false`（否则遮蔽 ClassDB 第二腿）。

---

## 11. 重载（对齐 C#，用户要求 7）

C# 先例（`modules/mono/csharp_script.cpp`）：

| 环节 | 行为 | 行 |
|---|---|---|
| 收集 | `methods` 扁平向量，**每个重载一条** | `:2304` |
| `get_script_method_list` | 走 base 链，**全部** push | `:2511-2515` |
| `get_script_method_argument_count` | **首个**匹配的 `arguments.size()` | `:2534-2555` |
| `get_method_info` | 遇同名**第二条** ⇒ 返回**空 `MethodInfo()`**（注释原文 *the best we can do*） | `:2557-2576` |
| `CSharpInstance::get_method_argument_count` | 走 base 链，首个匹配 | `:1675-1690` |

**TS 侧的关键差异**：JS 的运行期**看不到重载**（emit 只留实现体，原型上同名只有一个函数），
所以「同名多声明」这一事实**只能来自清单**。⇒ sidecar 里同名成员存多条 + `is_overloaded` 标志；
运行时按 C# 同形处理。

### 11.1 重载信息存在 `ScriptMethodInfo` 内部，**不动** `HashMap`（2026-09-26 用户拍板）

**用户的裁定**：「`HashMap<StringName, ScriptMethodInfo>` 为什么要动，你不也说了 JS 运行时本身没有重载。
你改 `ScriptMethodInfo` 本身不就完了，在里面加个数组类型的字段存重载信息就行了。」

这个裁定是对的，我原先的「改扁平 `Vector`」是多此一举：

- **运行期不存在两个同名方法**：emit 只留实现体，原型上 `name` 唯一 ⇒
  `HashMap<StringName, ScriptMethodInfo>` 的键**天然唯一**，它就是正确的索引。
- **重载是"一个名字下的多个签名"**，属于值的内部结构，不属于键空间 ⇒ 放进 `ScriptMethodInfo`：

```cpp
// 一个重载签名（清单里 kind=method 的一条记录）
struct ScriptMethodSignature {
    PropertyInfo return_val;              // void 与"不可映射"的区分见 §6.2
    LocalVector<PropertyInfo> arguments;  // 剩余参数不入内
    int default_count = 0;                // = MethodInfo::default_arguments.size()（§6.1）
    bool is_vararg = false;
};

struct ScriptMethodInfo {
    // ...既有字段...
    // 重载集：无重载时大小为 1；有重载时按源码声明顺序存全部签名（§11 的 C# 同形处理）。
    // 运行期无法从 JS 侧观测到重载（原型上同名只有一个函数）⇒ 这一信息只能来自清单。
    LocalVector<ScriptMethodSignature> overloads;
};
```

⇒ **`StatelessScriptClassInfo` 的公开形态不变**，既有 5 个调用点（`_has_method` /
`_get_own_method_argument_count` / `_get_script_method_list` / `make_temporary_method_list` /
`_get_method_info`）的**键查找逻辑全部不动**，只是读值时多一个 `overloads` 维度：

| 消费点 | 行为 |
|---|---|
| `_get_script_method_list` | 每个名字**展开全部签名**（C# `:2511-2515` 的"全部 push"）|
| `_get_method_info` | `overloads.size() > 1` ⇒ 返回**空 `MethodInfo()`**（C# `:2557-2576`）|
| `_get_own_method_argument_count` / 实例层 | 取 `overloads[0].arguments.size()`（C# `:2534-2555` 的"首个匹配"）|

**没有清单时的退化**：`overloads` 为空数组 ⇒ 全部消费点按既有行为（只有名字与参数个数），
与今天完全一致 ⇒ 回退路径不受这个字段影响。

---

## 12. 信号

`ScriptSignalInfo` 目前是**空结构**（`jsb_class_info.h:155-156`），收集只存名字
（`jsb_class_info.cpp:627`）。清单里 `kind=1` 的记录给出参数表；
C++ 侧填 `MethodInfo{name, arguments}`（返回值恒 void，不带 `NIL_IS_VARIANT`）。

**待验证（§14-3）**：填充信号 `arguments` 后，GDScript 的 `obj.sig.emit(...)` /
`connect` 是否受 arity 检查影响而报新错。信号参数不设 `default_arguments`、不加 `VARARG`
（除非实测表明需要）。

---

## 13. 被否决的备选

| 方案 | 否决理由 |
|---|---|
| 正则解析 TS | 具名脆弱点 7 条（嵌套箭头函数 / 泛型逗号 / 解构 / 注释与字符串里的逗号…），写对 = 自研 parser |
| tree-sitter | 解析的是运行期可见文本；导出形态无 `.ts` ⇒ 拿不到类型。唯一收益（替换类名提取正则）与本目标无关 |
| 读 `tsc --declaration` 产物 | `.d.ts` 把「有默认值」并成「可选」（`b = "z"` → `b?`），丢信息；且需多跑一次 tsc、多一棵产物树 |
| 提取器并进引擎内嵌 AMD bundle | §8：TS 需要 Node 内建与 `ts.sys` |
| 用 type checker | 成本 +526 ms，且**会把别名抹成 `number`/`string`**（§2.2）——比不用更差 |
| 「不可映射」与「可选」都用 NIL | §6：引擎已有 `default_arguments` / `NIL_IS_VARIANT` 两个原生通道，无需重载语义 |

---

## 14. 未验证清单（实施前必须先补实测）

1. ~~**本机 TS 提取器的 Node 落地形态**：把产物放进项目根后 `require("typescript")` 是否真解析成功。~~
   **已验证（第一轮，并持续复现）**：工具落 `res://.godot/jsb.signature.extract.cjs`，从无关 cwd
   以绝对路径 spawn、`--project` 传项目根 ⇒ `RC=0`、`ts.version = 6.0.3`。
   **两个致命细节已定位并规避**：① 源文件必须是 `.cts`（`scripts/package.json` 的 `"type": "module"`
   会让 tsc 产出的 CommonJS `.js` 被 Node 当 ESM，报 `exports is not defined`）；
   ② `require("typescript")` 只在**项目根**解析得到（`scripts/node_modules/typescript` 不存在，
   真实位置是 `scripts/jsb.editor/node_modules/typescript`）⇒ 故工具须装进**项目**数据目录。
   `--project` 参数值不加引号（尾反斜杠被引号包住后会被命令行转义吞掉）。
2. ~~**`default_arguments` + `VARARG` 的 arity 效果**：GDScript 对 `obj.add(1)` / `obj.add(1,2,3)`
   必须照常解析（§6.3 的推论依赖此）。~~ **已验证（第六轮）**：`add()` 报
   `Too few arguments … Expected at least 1`（= arguments 2 − default_count 1）⇒ 分析器**确实读了**
   `default_arguments`；`add(1,2,3,4,5)` 不报 `Too many` ⇒ `VARARG` 生效；对照夹具 `h(a, b = 2)`
   （无 rest）的 `h(1,2,3,4,5)` 报 `Expected at most 2` ⇒ 上限检查确实由 VARARG 开关。
   **并修正一条错误归因**：曾据"派生实例上 `add(1)` 通过"推断「GDScript 对脚本实例方法不做 arity
   检查」，**错误** —— 真实原因是 `add` 由基类声明、`Script::get_method_info` 只报自有 ⇒ 分析器
   拿不到 `MethodInfo`。判据是"分析器能否为该方法拿到 `MethodInfo`"，不是"GDScript 检不检查"。
   见 `report.md` 第六轮。
3. ~~**信号填充 `arguments` 后是否引入新的 GDScript 解析错误**（§12）。~~
   **已验证**：`test_01.ts` 的 `test_signal` 经清单得到参数表后，GDScript 侧
   `connect(Callable)` → `0`（OK）、`has_signal` → `true`、`emit(3.5)` 回调**正常收到 3.5**，
   **零解析错误**（RC=0）。清单缺失对照下同一脚本的信号参数表为空 `()` ⇒ 填充确实生效且未引入新错。
4. ~~**懒回退路径的 scope 正确性**：`_get_own_method_argument_count` 里建 `JSEnvironment` +
   进 isolate/context scope + 读 `method_cache`，需在 worker/多线程场景实测不炸。~~
   **已验证（两条独立取证）**：
   - **回退路径确实被执行且结果正确**：移走全部 18 份 sidecar（强制无清单）后全量验收仍
     `GD-OK=1`，而 gdcheck 里 `get_method_argument_count("add") == 2` 的值此时**只能来自**函数源
     文本扫描 ⇒ 建 environment + 进 isolate/context scope 这条路径实际跑通并给出正确值（不是退化成 Unknown）。
   - **worker / 多线程不炸**：`-- --object-transfer-backend=worker` + 无清单 ⇒ `RC=0`、
     `COMPLETED=1`、`FAILED=0`、`orphan=0`，worker 四个对象用例与三轮会话全部 `done`，零脚本错误/崩溃。
   **跨线程安全由构造保证**：`resolve_declared_parameter_count` 入口即守卫
   `p_env->is_caller_thread()`，非属主线程返回 `k_ArgumentCountUnknown` 而**不**进 isolate。
5. ~~**`Variant::get_type_name` 反向表**：确认 `Variant::VARIANT_MAX` 与 `Variant::get_type_name`
   在 godot-cpp 下可得。~~ **已验证**：`jsb_signature.cpp:129-144` 即用二者**现构**反向表
   （遍历 `Variant::VARIANT_MAX`、跳过 NIL）⇒ 编译通过即证明二者可得。运行期证据：
   `add(): number` 的返回值被解析为 `Variant::FLOAT`（测试断言 `return.type == FLOAT` 绿），
   且该断言在"禁用清单加载"的负向控制中**失败**（`values: -1 == 3`）⇒ 非空转。
6. ~~**`collect_invalid_files` 改动的副作用**：确认加白名单后，真正的陈旧 `.js` 仍会被清理。~~
   **已验证（永久用例，双向）**：`src/editor/tests/test_jsb_editor_cleanup.h` 同时断言四个方向 ——
   `paired.sig`（有 `.ts` 源）**保留**、`orphaned.sig`（无源）**收集**、`live.js`（有源）**保留**、
   `dead.js`（无源）**收集**。第三轮负向控制（把 `.sig` 分支改成 `false && …`）
   得 `TESTS_RC=1`、失败项 `!invalid.has(...paired.sig)` ⇒ 守卫非空转。

---

## 15. 交付阶段（每阶段独立可验证）

| 阶段 | 内容 | 验收 |
|---|---|---|
| **0** | 把已实施的回退改**懒加载**（§7），删掉解析期扫描 | 现有 C++/GD 断言全绿；新增「解析期不再调用扫描」的负控；构建期扫描调用为 0 |
| **1** | sidecar 读取器（C++）+ 格式（§4）；`_get_method_info` 填 args/defaults/return/flags（§6） | A1/A2/A3/A4（§16） |
| **2** | 提取器（Node）+ 构建接线 + 安装进项目 + 触发/增量（§8、§9.1） | 生成的 sidecar 与手工期望一致；md5 未变则跳过 |
| **3** | 重载结构改造（§11）+ 信号（§12） | A6/A7 |
| **4** | 清理接入 + 导出打包（§9.2、§9.3） | A8 |

---

## 16. 验收标准

- [ ] **A1** 有 sidecar 时，`_get_method_info("m")` 返回的字典含 `name` / `args` / `default_args` /
      `flags` / `return`；`args[i].type` 与 TS 标注一致（`int32`→INT、`float64`→FLOAT、
      `StringName`→STRING_NAME、`Vector2`→VECTOR2、Godot 类→OBJECT+class_name）
- [ ] **A2** 无法映射的类型（接口、函数类型、`Map<>`、联合）⇒ `args[i].type == NIL`；**返回值**
      无法映射 ⇒ NIL **带** `NIL_IS_VARIANT`，而真 `void` ⇒ NIL **不带**（§6.2）
- [ ] **A3** `add(a, b = 2, ...rest)`：`args.size()==2`、`default_args.size()==1`、
      `flags & METHOD_FLAG_VARARG`；GDScript `obj.add(1)` 与 `obj.add(1,2,3)` **都照常解析**
- [ ] **A4** 参数个数：`greet`=0、`add`=2；未知名字 Script 层返回空 `Variant`、实例层 invalid
- [ ] **A5** 无 sidecar / 未开 `JSB_USE_TYPESCRIPT` ⇒ 走回退，不报错，类型留空（§10）
- [ ] **A6** 重载：清单多条 ⇒ `get_script_method_list` 列全部、`get_method_info` **空**、
      `method_argument_count` 取首个（对齐 C#）
- [ ] **A7** 信号：sidecar 的 `kind=1` 记录填进 `get_script_signal_list` 的 `arguments`
- [ ] **A8** `cleanup_invalid_files` 不删有源脚本的 sidecar、删掉源已消失的 sidecar；
      导出包内含 sidecar
- [ ] **A9** 懒加载：解析期零签名工作；`is_editor_hint()` 下不保留清单数据（§7.4）
- [ ] **A10** 验收判据：Orphan StringName = 0、`GODOTJS_TEST_PROJECT_COMPLETED` = 1、
      `GODOTJS_TEST_PROJECT_FAILED` = 0、`verify_codegen.py` 通过
