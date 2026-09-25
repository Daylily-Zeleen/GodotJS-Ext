# Q1：`Object.freeze` 能否让非基础类型成为真常量？Q2：参数/返回值/信号参数有无 tree-sitter 以外的方案？

> 2026-09-25 用户提出两条备选，本轮取证。探针：`.agent_tmp/q1_freeze.mjs`、`.agent_tmp/q2_params.mjs`、
> `.agent_tmp/tsmeta*/`（TS 元数据发射试验）。引擎侧对照 `array.cpp` / `dictionary.cpp` /
> `extension_api-4-7.json`；本仓对照 `jsb.inject.ts` / `jsb_v8_class_builder.h`。

---

## Q1 结论先行

| 值类别 | `Object.freeze` 能否保证 JS 侧不可改 | 是否有别的两侧不可变路径 |
|---|---|---|
| Godot **容器**（`GArray`/`GDictionary` 包装） | ❌ 对现有 proxy **直接抛 TypeError** | ✅ **有** —— 共享 `_p` ⇒ `make_read_only()` 同时冻两侧 |
| 原生 JS **数组/对象**（`[1,2,3]` / `{a:1}`） | ⚠️ freeze 有效，但**转不成 Variant**（`js_to_gd_var` 返回 `false`） | ❌ 无（转换会造**新** `_p`，冻它保护不了 JS 原件） |
| Godot **值类型**（`Vector2`/`Color`/…） | ❌ **完全无效**（freeze 实例与 freeze prototype 都挡不住） | ❌ 无（引擎无值类型只读概念） |

⇒ **「注解 + freeze」不能作为通用机制**；只有「容器 + `make_read_only()`」这一格成立。

### 1.1 `Object.freeze` 对现有容器 proxy 抛异常（实测）

`jsb.inject.ts` 的 `GArray`/`GDictionary` handler 同时声明：

```js
isExtensible()      { return true;  },   // GArray:211 / GDictionary:314
preventExtensions() { return true;  },   // GArray:221 / GDictionary:325
```

这两个 trap **互相矛盾**（声称"目标不可扩展"，却报告 `isExtensible() === true`），违反 Proxy 不变式。

| 探针 | 结果 |
|---|---|
| `Object.freeze(proxy)`（镜像现有 handler） | **`TypeError: 'preventExtensions' on proxy: trap returned truish but the proxy target is extensible`** |
| 把 `isExtensible` 改成 `false` 再 freeze | **同样 TypeError** —— 因为真实 target 本身仍 extensible |
| 先 `Object.preventExtensions(target)` 再 freeze | freeze 成功；`set` trap 仍执行，写入被挡（`targetValue` 保持原值），严格模式写入抛错 |

⇒ 要 freeze 容器，必须先改 `jsb.inject.ts` 的 handler（把 `isExtensible` 改 `false` **且**对真实 target
调 `preventExtensions`）。那是**所有容器访问的热路径**，为常量特性改动它，代价与风险都不成比例。

### 1.2 值类型：freeze 挡不住（实测，这条是决定性的）

Godot 值类型成员的形态：`x`/`y`/`r`/`g`… 是 **prototype 上的 accessor**
（`jsb_v8_class_builder.h` 的 `Property()` → `prototype_template_->SetAccessorProperty`），
可变状态在实例的 `IF_Pointer` 指向的 Variant 里。

| 探针 | 结果 |
|---|---|
| `Object.freeze(inst)` 后 `inst.x = 42` | **store 变成 42 —— 未挡住**（freeze 只作用于 own property，碰不到 prototype accessor） |
| `Object.freeze(prototype)` 后 `inst.x = 42` | **store 变成 42 —— 未挡住**（accessor 的 setter 仍在） |
| `defineProperty(inst, "x", {writable:false})` 遮蔽后再写 | **挡住**（store 保持 1，写入抛错） |

⇒ 唯一有效的机制是**给每个实例加 own 不可写属性**——那要改写用户自己的对象（且 `static readonly V = new Vector2(1,1)`
的实例是模块求值时创建的，注解时机在类定义期，顺序上也不成立）。
**值类型若坚持"两侧都不可变"，只能继续排除**；否则就得接受"JS 侧可写、GDScript 侧是快照"。

### 1.3 freeze 是浅层的（实测）

`Object.freeze([[1]])` 之后内层数组仍可 `push(9)` 成功。要做就得递归——而递归冻结用户对象是更大的侵入。

### 1.4 容器有一条真正可行的路径（用户方案的正确形态）

**不需要 `Object.freeze`。** `GArray`/`GDictionary` 包装经 `js_to_gd_var` 的
`InternalFieldCount == IF_VariantFieldCount` 分支转换时，`r_cvar = *(Variant *)...` 只拷贝 Variant 外壳
⇒ **`_p` 是同一份**。因此对那份 Variant 调 `make_read_only()` 会同时影响两侧：

| 侧 | 写入路径 | 是否被挡 |
|---|---|---|
| GDScript | `Array::set` / `push_back` / `clear` / `insert` / `erase` / `fill` / `sort` / `resize` … 全部 `ERR_FAIL_COND_MSG(_p->read_only, "Array is in read-only state.")`（`array.cpp:123,281,288,305,324,339,346,503,516,732,742,768,773,780,791,801,884`） | ✅ |
| JS | 容器 proxy 的 `set` trap → `target.set(num, proxy_unwrap(v))`（`jsb.inject.ts:227`）→ 同一个 `Array::set` → 同一个 guard | ✅（写失败并打 engine error） |

`Dictionary` 同构（`dictionary.cpp:203,251,305,311,328,333,641`）。

**限制（必须写明）**：
1. **只有 `GArray`/`GDictionary` 包装**享受两侧冻结。纯 JS 数组字面量 `[1,2,3]` 走 `js_to_gd_var`
   返回 `false`（§0.2）；改走 `Variant::ARRAY` 提示重载则经 `try_convert_array_any` **新建**一个
   `Array`（新 `_p`）⇒ 冻它**保护不了 JS 原数组**。作者须写 `GArray.create([...])` / `GDictionary.create({...})`。
2. **浅层**：内层容器要**递归**施加。若内层也是 GArray/GDictionary 包装，其 `_p` 同样与 JS 共享，
   递归即可得到深层两侧冻结。
3. **这是故意改变 JS 侧行为**：施加后作者在 JS 里写该容器会开始报 engine error。属"显式标注即接受"，
   但必须在文档里写明，否则是静默的行为突变。
4. `Packed*Array` **无 `make_read_only()`/`is_read_only()`**（api json 核对：`PackedInt32Array` 只有
   `set`/`push_back`，无只读 API）⇒ 无法两侧冻结，仍排除。
5. `OBJECT`（Node/Resource/脚本）是引用，无法"冻结对象内容"，仍排除。

---

## Q2 结论先行

**有备选，而且多数比 tree-sitter 便宜。** 三类需求分开处理：

| 需求 | 可行方案 | 成本 |
|---|---|---|
| 方法**参数名 / 元数** | `Function.prototype.toString()` + **单函数参数表小词法器** | 低（实测可行，边界见 2.1） |
| 方法**参数类型 / 返回值类型** | **只能显式注解**（TS 自动元数据路线被本仓装饰器模型排除，见 2.2） | 低（注解机制已存在） |
| **信号参数** | 扩注解 + `ScriptSignalInfo` 字段；引擎侧 `add_user_signal(name, [{name,type}])` 已绑定 | 低 |

### 2.1 参数名/元数：`toString()` 可行，但朴素 split 不够

实测（`.agent_tmp/q2_params.mjs`，真实 tsc 产物形态）：

| 形态 | `String(fn)` | 朴素 split 结果 |
|---|---|---|
| `static sf(a, b) {}` | `sf(a, b) { return a + b; }` | `["a","b"]` ✅ |
| `static sf0() {}` | — | `[]` ✅ |
| `static sfa(...args) {}` | — | `["...args"]` ✅ |
| `static sfd(a, b = 2, ...r) {}` | — | `["a","b","...r"]` ✅ |
| `static arrow = (a) => a` | `(a) => a` | `["a"]` ✅ |
| `static async asf(a) {}` | `async asf(a) { return a; }` | `["a"]` ✅ |
| `static destructured({a, b}) {}` | — | `["{ a","b }"]` ❌ |
| `static commented(/* x */ a, b /* y */) {}` | — | `["/* x */ a","b /* y */"]` ❌ |
| `static defaultStr(a, b = "x,y)") {}` | `defaultStr(a, b = "x,y)") { ... }` | `["a","b","y"]` ❌（字符串内逗号） |

`Function.length`（元数下限）：`sf=2`、`sf0=0`、`sfa=0`、`sfd=1`、`arrow=1`、`asf=1`、`destructured=1`
—— 对 rest/默认值截断，只能当**下限**。

⇒ 需要一个**只吃"一个函数的参数表字符串"**的小词法器（跟踪 `()`/`{}`/`[]` 深度 + 字符串/注释状态），
成本量级：百行内。**不需要**解析整个 TS 文件。输入来自 `Function.prototype.toString()`，
对用户 TS 方法有效（native 函数会得到 `[native code]`，不适用）。

### 2.2 类型信息：TS 自动元数据路线在本仓**不可用**（实测）

两条自动路线都被排除：

| 路线 | 实测结果 |
|---|---|
| `emitDecoratorMetadata`（legacy 装饰器） | **必须** `experimentalDecorators`：单开报 `error TS5052: Option 'emitDecoratorMetadata' cannot be specified without specifying option 'experimentalDecorators'`；两者同开则**确实**发射 `__metadata("design:paramtypes", [Number, String])` / `"design:returntype"` |
| `context.metadata` / `Symbol.metadata`（现代装饰器） | tsc **6.0.3** 在现代装饰器下**完全不发射** metadata（产物中无任何 `metadata`/`Symbol` 行） |

而本仓**强制现代装饰器**：`createClassBinder()` 在检测到 legacy decorators 时**直接抛错**
（`godot.annotations.ts:721,853,999,1071,1121,1142,1165,1190,1215` 共 9 处
`"The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators)…"`）。

⇒ **类型必须显式写进注解**。好消息是机制已存在且成熟：
`jsb.internal.add_script_property(prototype, {name, type, hint, hint_string, usage})`
（`godot.minimal.d.ts:120`；`export_` 已在用，`godot.annotations.ts:375+`），
`type` 取 `Godot.Variant.Type`，`hint_string` 可表达数组元素/嵌套类型（`get_hint_string`，`:299-327`）。

### 2.3 信号参数：引擎已有 API，只差注解

- 引擎侧：`Object.add_user_signal(name, [{name, type}])` 已绑定（`godot.generated.d.ts:846`）。
- 本仓现状：`ScriptSignalInfo{}` 是**空结构**（`jsb_class_info.h:155-156`），信号集合只存名字
  （`jsb_class_info.cpp:203-217`）；TS 侧 `signal_names: string[]`（`godot.annotations.ts:695`）。
- 要参数：注解带 `{name: Variant.Type}` 表 → 存入 `ScriptSignalInfo.arguments` →
  `get_script_signal_list()` 输出（`jsb_script.h` 模板已就位）。
- **不需要**任何源码解析。

### 2.4 若真要"不写注解自动推断类型"

tree-sitter 不是唯一选择，但另一个主流选择在本架构下**不可用**：

| 方案 | 可行性 |
|---|---|
| **TypeScript Compiler API**（`ts.createProgram` / ts-morph） | ❌ 解析点在 **runtime 库**、被 `EditorFileSystem` **后台线程**调用（`jsb_script_language.cpp:390-394` 注释已记录该约束）；TS 编译器是 JS 实现，**无法在 C++ 后台线程里跑**；Node 侧也不在该调用路径上 |
| **自研「仅注解语法」子解析器** | ✅ 注解语法是**我们自己定的**，不是任意 TS ⇒ 解析面远小于 tree-sitter。但仍是新解析器，且**注解路线已能覆盖**（2.2） |
| tree-sitter | ❌ 已评估否决（`09-06-lowprio-tree-sitter-ast`）：~9MB 语法源码 + 六平台接线 + MSVC 巨型 TU 特例，收益仅"4 个正则的健壮性" |

⇒ 顺序建议：**注解 → 单函数参数表小词法器 → 自研注解子解析器**；tree-sitter 与 TS Compiler API 都不必走。

---

## 对三件套的影响（待用户拍板）

1. **Q1 容器**：可选「`GArray`/`GDictionary` 包装 + 递归 `make_read_only()`」把容器纳入常量
   （两侧都冻，且是用户设想的正确形态）。**值类型仍只能排除**（freeze 实测无效）。
2. **Q2**：可作为**独立后续任务**（不属于本任务范围）—— 本任务已声明不做静态函数、不含参数签名。

> 未实测项：① 递归 `make_read_only()` 对"内层 GArray 包装"是否确实随 `_p` 共享（推断成立，未实跑）；
> ② `add_user_signal` 的 `[{name, type}]` 在 GDExtension 路径上的实际效果未验证。

---

## Q3 GDScript 如何识别常量？（用户追问：能否让编辑器也报错）

> 用户实测：GDScript 里 `const A := Vector2()` 之后 `A.x = 1.0` **在脚本编辑器里直接报错**。
> 问能否让 GodotJSScript 的常量也获得同样待遇。结论：**能，但只有直接赋值形式**。

### 3.1 唯一入口是 `Script::get_constants()`

`reduce_identifier_from_base`（`gdscript_analyzer.cpp:4349-4399`）对非 GDScript 脚本按**固定顺序**
探测四个来源，`get_constants()` 是最后一个（也是常量性的唯一来源）：

```
① get_script_property_list  → 属性
② get_method_info           → 方法（无 has_method 守卫 ← R5.1 要修）
③ get_script_signal_list    → 信号
④ get_constants             → 常量 ★
```

命中 ④ 后：`make_builtin_meta_type(constant.get_type())`（`:4396`）⇒ 该 DataType 带
`is_constant = true` + `is_meta_type = true`（`:226-233`）。赋值检查
（`:2992-2994`）读 `assignee_type.is_constant` ⇒ 报 `Cannot assign a new value to a constant.`

**这就是用户观察到的报错机制**，且它**不需要**我方做任何额外工作 —— 实现 `_get_constants()`
即自动获得。

### 3.2 两条保护机制，层级不同

**机制 ①（不依赖我方任何实现）**：`:4396` 的 `make_builtin_meta_type(constant.get_type())`
把 **DataType** 的 `is_constant` 置 true。赋值检查 `:2992-2994` 读的是 `assignee` 的
`type_constraint` ⇒ `SomeScript.N = 5` **必在编译期被拒**，与 `_get` 是否实现无关。

**机制 ②（嵌套形式）**：**不存在** —— `reduce_subscript` 的折叠路径**进不去**（见下）。

### 3.2.1 为什么嵌套形式永不折叠（实测裁决后的定论）

属性分支 `:4889` 的前置是
`p_subscript->base->is_constant && !base_type.is_meta_type`，而 `const S := preload(...)`
的基类型**就是 metatype**：`reduce_preload` 把 `p_preload->type_constraint` 置为
`type_from_variant(resource)`（`:4850-4852`），该函数对有效脚本置 `is_meta_type = true`
（`:5841-5846` 的 `is_meta_type = scr.is_valid()`）⇒ `!base_type.is_meta_type` 为**假**
⇒ **整个折叠块被跳过**。

于是落到 `:4949` 的 else 分支 → `reduce_identifier_from_base(attribute, &base_type)` →
非 GDScript 分支 `:4390-4399` **只把 DataType 置 `is_constant`**（`make_builtin_meta_type`），
**不置节点级 `p_identifier->is_constant`**；`:4962-4963` 随即执行
`p_subscript->is_constant = p_subscript->attribute->is_constant` ⇒ **节点级保持 `false`**。
而节点级 `is_constant` 才是折叠的唯一开关（索引分支 `:4992` 要求
`p_subscript->base->is_constant && p_subscript->index->is_constant`）⇒ **永不折叠**。

> **早期推断已被实测推翻**：曾据 `:4912-4920` 的 `get_named`（即 `Object::get` ⇒ 我方 `_get`）
> 推测「`_get` 在分析期返回该值 ⇒ 嵌套形式也会折叠」，并据此把结论写成「取决于 `_get`」。
> **实测为否** —— 那条路径**根本进不去**，`get_named` 从未被调用，`_get` 是否实现**无关**。
> `_get` 只服务**运行期**读取。教训：源码实读的推断必须由实测裁决。

### 3.3 实测矩阵（A14 / A15，2026-09-25 实机取证）

| GDScript 写法 | 实测结果 |
|---|---|
| `SomeScript.N = 5`（直接赋值） | ✅ **编译期** `Cannot assign a new value to a constant.`（脚本根本不加载） |
| `SomeScript.ARR[0] = 7`（索引赋值） | ❌ **编译期不报**；运行期 `Invalid assignment on read-only value (on base: 'Array')` |
| `SomeScript.E.Red`（点访问枚举成员） | ❌ **编译期报** `Cannot find member "Red" in base "Dictionary"` |
| `SomeScript.E["Red"]`（索引访问枚举成员） | ✅ 可用，得到 `0` |

探针：`.agent_tmp/acc/_acc_w1.gd`（直接赋值）、`_acc_w2.gd`（索引赋值）、`_acc_w3.gd`（点访问枚举）、
`_acc_w4.gd`（索引访问枚举）；日志 `.agent_tmp/acc_probes.log`。

「枚举点访问」的两种候选落法中，实测落在**后者**：未折叠 ⇒ `base_type` 仍是
`make_builtin_meta_type(DICTIONARY)`（**meta**）⇒ 落 `:4150-4193` 的 meta 分支，
该分支只查 `has_constant`/enum、**不查 `get_property_list`** ⇒ 报错。

**另一条前提**：基必须是**编译期已知的脚本引用**。`var h = load("res://x.ts")`（无类型）时
`base_type.is_variant()` 先命中（`:4926-4940`）⇒ 只 `mark_node_unsafe`、不设 `is_constant`
⇒ 编译期不报、退化为运行期（探针 `w*`/`z*` 实测一致）。

### 3.4 判定

- **直接赋值**的编译期保护由引擎保证，扩展侧实现 `_get_constants()` 即自动获得。
- **嵌套形式**没有编译期保护，扩展侧**无法修正**（根因在引擎的折叠前置，不在我方返回值）
  ⇒ 只能靠**运行期** `make_read_only()` guard，并如实文档化。
- **枚举成员在 GDScript 侧必须用索引访问**（`SomeScript.E["EA"]`），点访问会报错 —— 文档必须写明。
- **取证强度**：本节结论已由**实机取证**确认（引擎 4.7.2 + 本扩展 dev 构建）。
