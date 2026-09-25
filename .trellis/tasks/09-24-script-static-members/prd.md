# GodotJSScript 静态（常量）成员解析与跨语言访问

> 目标：让 GodotJSScript 在**常量 / 枚举 / 静态变量**三类成员上对齐 GDScript，使 GDScript 能以
> `SomeScript.CONST`、`SomeScript.my_static` 的形式直接访问，并让编辑器（inspector、远程调试器、文档）
> 看到这些成员。**静态函数不做**（用户决策 Q1=(c)，见 R5 / R8.5）。
> 当前状态：全部缺失（`_get_constants`/`_get_members` 返回空、`_has_static_method` 恒 false、
> `_get`/`_set`/`_get_property_list` 未实现、`Environment::call_script_method` 明确注明不支持静态调用）。

## Background

### 引擎侧 GDScript 静态成员面（已实读源码）

| GDScript 能力 | 引擎 API | JSB 现状 |
|---|---|---|
| `const X = 1` | `Script::get_constants()`（`core/object/script_language.h:196`） | `_get_constants()` 返回空 `Dictionary`（`jsb_script.h:196-199`） |
| `enum E { A }` | 同上，值为 `Dictionary{name: int}`（`gdscript_compiler.cpp:2949`） | 同上 |
| `static var v` | `GDScript::_get`/`_set`/`_get_property_list`（`gdscript.cpp:966-1081`） | 未实现 |
| `static func f()` | `has_static_method()`（`script_language.h:173`）+ `callp()`（`gdscript.cpp:930`） | `_has_static_method()` 恒 false（`jsb_script.cpp:359-364`） |
| `class Inner:` | 作为常量暴露 `Ref<GDScript>`（`gdscript.cpp:999-1005`） | 不支持（一个文件一个脚本） |
| 成员清单（远程调试器） | `Script::get_members()`（`script_language.h:197`） | `_get_members()` 返回空 |
| 成员行号 | `Script::get_member_line()`（`:194`） | 恒 -1 |

### GDScript 侧跨语言解析路径（两条，都已核实）

1. **静态分析期**：`GDScriptAnalyzer::reduce_identifier_from_base`（`gdscript_analyzer.cpp:4348-4402`）对非
   GDScript 的 `Ref<Script>` 依次探测 `get_script_property_list` → **`get_method_info`（无 `has_method`
   守卫）** → `get_script_signal_list` → **`get_constants`**。函数调用另经 `get_function_signature`
   （`:5962-6131`，内部走 `base_script->has_method()` + `get_method_info()`）与 `MethodInfo.flags` 的
   `METHOD_FLAG_STATIC`（`:3706`）判定。
2. **运行期**：`SomeScript.CONST` 编译为 `OPCODE_GET_NAMED`（`gdscript_compiler.cpp:838`）→
   `Variant::get_named`（`variant_setget.cpp:275-288`）→ `Object::get`（`object.cpp:268-289`）→
   **`GodotJSScript::_get`**；写入走 `Object::set`（`object.cpp:212-219`）→ `_set`。

⇒ `_get_constants()` 与 `_get()` **都必须实现**（前者供类型推导，后者供取值）。

### 分析期两条路线的关键差异（实测 + 源码，推翻早期推断）

| 路线 | 分析期 datatype | 后果 |
|---|---|---|
| `_get_constants()` 返回 `Callable` | `make_builtin_meta_type(CALLABLE)` ⇒ `is_meta_type = true`（`gdscript_analyzer.cpp:4391-4398`、`:226-233`） | `SomeScript.fn.call(...)` 命中 `gdscript_analyzer.cpp:3753`「Cannot call non-static function "call()" on the class "Callable" directly」**编译期失败**（实测探针 `p_u.gd`） |
| `_get_method_info()` 返回 `MethodInfo` | `make_callable_type(info)` ⇒ `is_meta_type = false`，带完整签名（`:4366-4373`、`:87-95`） | `SomeScript.fn.call(...)` **可用**（与 GDScript 静态函数同形，实测探针 `q2`） |

⇒ **静态函数名必须由 `_get_method_info()` 提供，且不得出现在 `_get_constants()` 里。**

### GDScript 静态成员调用形态（实测，探针在 `.agent_tmp/gdcallable/`（任务收尾已清理））

| 形态 | 结果 |
|---|---|
| `H.sfn(1,2)`（真 `static func`） | 可用（编译为 `OPCODE_CALL_METHOD_BIND`，走 `base_script->get_method_info`） |
| `H.sfn.call(1,2)` / `var f: Callable = H.sfn; f.call(1,2)` / `(H.sfn as Callable).call(1,2)` | 可用 |
| `H.sfn` 的 `typeof` | `TYPE_CALLABLE`；`get_method()` = `"sfn"`；`get_argument_count()` = `2` |
| `H.call("sfn", 1, 2)` | **parse error**：`Cannot call non-static function "call()" on the class "..." directly` |
| `H.get_script_constant_map()` 直连 | **parse error**（同上）；须 `load(...) as Script` 后再调 |
| `H.cb()`（`static var cb: Callable`） | **parse error**：`Member "cb" is not a function.` + `Name "cb" is a Callable. You can call it with "cb.call()" instead.` |
| `H.cb.call(4)` | 可用 |

### 常量形态（实测）

`H.N` / `H.S` / `H.V.x` 可读；`H.ARR` 的 `is_read_only()` = true；`H.D.A` / `H.D["A"]` / `H.D.keys()` 可读；
`H.E` 为 `Dictionary{ "EA": 0, "EB": 1 }`，`H.E.EA` 与 `H.E["EA"]` 均为 `0`；
`load(...) as Script` 的 `get_script_constant_map()` 返回全部常量。

> **这是 GDScript 自身的行为**（对标用）。我方支持面**更窄**：**JS 基础值 + enum + 容器
> （`GArray`/`GDictionary` 包装）**（R2.3）。上表 `H.V`（值类型）**我方不提供**；
> `H.ARR` / `H.D` 仅在**用包装构造**时提供（`GArray.create([...])` / `GDictionary.create({...})`，
> 纯 JS 字面量转不成 Variant）—— 理由见 R2.3。

GDScript `static var` 的容器**不是** read-only；`const` 的容器**经脚本对象取出时是深层冻结**
（内层容器与容器内嵌值一并只读，`p_ro4.gd`）。早期记的「只有顶层只读、内层可写」**仅**在同一脚本内
**裸标识符**形态下成立（`p_i.gd`），不是普遍事实 —— 见 `research/phase0-findings.md` §0.6。

## Requirements

### R1 成员解析（反射式，不改用源码解析）

- R1.1 在 `ScriptClassInfo::_parse_script_class_iterate`（`jsb_class_info.cpp:69-286`）中新增对
  `class_obj` **静态自有属性**的枚举（现仅枚举 `prototype`）。须用 `getOwnPropertyNames` +
  `getOwnPropertyDescriptor`：实测 TS 产物里 `static` **字段可枚举**（`static readonly X = 1`、
  `static arrow = () => {}` 均 `enumerable = true`）而 `static` **方法不可枚举**
  （`static sf() {}`、`static async asf() {}` 均 `false`），`Object.keys` 漏方法、`for...in` 带字段，
  两者都不完整；且须显式跳过 `length` / `name` / `prototype`。静态成员**不继承**
  （`class D extends B` 时 `getOwnPropertyNames(D)` 不含 B 的成员）⇒ 基类链靠 `base` 递归。
  证据：`research/phase0-findings.md` §0.1。
- R1.2 分类规则：静态属性值先按 `typeof` 预筛（只放行 `boolean`/`number`/`string`/`bigint`/`object`）
  —— 无类型提示路径对 `symbol` 会打 `JSB_LOG(Error, "js_to_gd_var: unhandled type")`，直接调用会
  污染日志，违反「静默忽略」。再走 `TypeConvert::js_to_gd_var`，**转换成功 ≠ 纳入**：还须过
  **R2.3 的白名单**（`NIL`/`BOOL`/`INT`/`FLOAT`/`STRING`/`STRING_NAME` + `ARRAY`/`DICTIONARY`；
  枚举走 R1.3 的独立分支）。转换失败（纯 JS 对象、函数、`undefined`、`symbol`）→ 忽略，不报错、
  不产生成员。注意 `Vector2`/`Array`/`Dictionary`/`Packed*Array`/对象包装**都会转换成功**（命中
  `InternalFieldCount == IF_VariantFieldCount` 分支原样返回 Variant）⇒ 不能只靠「转换失败」过滤。
  证据：`research/phase0-findings.md` §0.2、R2.3。
- R1.3 TS `enum` 的产物（带反向映射的 IIFE 对象）须被识别并归一化为 `Dictionary{name: int}`，与 GDScript
  枚举的 `Variant` 形态一致。**字符串枚举**（`enum E { X = "x" }`，产物无反向映射）作为普通
  `Dictionary` 常量暴露（值类型 `String`），不做枚举归一化 —— 规则与理由见 `design.md` §4.3。
  归一化产物由我方**新建**（不经 `js_to_gd_var`）并 `make_read_only()` —— 它是 R2.3 两类容器常量中
  "不与任何 JS 可达对象共享 `_p`"的那一类（另一类是 `GArray`/`GDictionary` 包装，**共享 `_p`**
  但正因此才能两侧同时只读）。
- R1.4 常量 / 静态变量的区分见 R4（运行期无法区分，需注解）。

### R2 常量与枚举的跨语言暴露

- R2.1 实现 `GodotJSScript::_get_constants()`：返回**自有**常量（含枚举 Dictionary），
  **不合并** `base` 链 —— 对齐 `GDScript::get_constants`（`gdscript.cpp:911-917`），
  因为 `Script::get_script_constant_map()`（ClassDB 绑定）dump 的就是这个返回值。
  （2026-09-26 修正：原实现沿 `base` 合并，实测使派生脚本的该 API 偏离 GDScript。）
- R2.2 枚举的 Dictionary 形态须与 GDScript 一致，使 `SomeScript.get_script_constant_map()["MyEnum"]`
  得到相同结构（该 API 已由引擎绑定，`core/object/script_language.cpp:181`）。
- R2.3 **常量类型准入规则**（2026-09-25 用户拍板）：**JS 基础值 + enum + 容器
  （`GArray`/`GDictionary` 包装）**。
  判据用用户给出的那条、可直接验证的：**这个值在 JS 侧能不能被原地改？**
  —— JS 的 `const` 只冻结**绑定**，不冻结对象：`const v = new Vector2(1, 1)` 之后 `v.x = 0`
  合法且生效。容器是唯一能把这条判据**反转**的一类：它两侧共享 `_p`，施加只读即两侧都改不动。

  | 值 | 准入 | 依据 |
  |---|---|---|
  | JS 基础值 `number` / `string` / `boolean` / `bigint` / `null`（→ `FLOAT`/`INT`/`STRING`/`STRING_NAME`/`BOOL`/`NIL`） | ✅ | 载荷即值本身，JS 侧**没有任何可变内容** |
  | `enum` | ✅ | 产物是可改的普通对象，但我们**不用它本身**：从它的自有可枚举属性**新建** `Dictionary` 并 `make_read_only()`（R1.3 / `design.md` §4.3）⇒ 暴露出去的那份由我们独占，无人能改 |
  | `GArray` / `GDictionary` **包装对象**（`GArray.create([...])` / `GDictionary.create({...})`） | ✅ | 与 JS 侧**共享 `_p`** ⇒ 递归 `make_read_only()` 后**两侧**都改不动（GDScript 与 JS 的写入命中同一 guard）⇒ 真常量。详见下方「容器」段 |
  | 其余一切带对象身份的 JS 值 —— `new Vector2(1,1)` / `new Color(…)` / **JS 字面量** `[1,2,3]` / `{a:1}` / `new Node()` / `Packed*Array` / `Callable` / `Signal` | ❌ | **JS 侧可原地改**（`v.x = 0`、`arr[0] = 9`、`obj.a = 1` 全部合法且生效）⇒ 我们保证不了它是常量，就不叫它常量。字面量另有一条：**转不成 Variant**（§0.2）⇒ 必须改写成包装构造 |
  | `undefined` / `symbol` / 函数 | ❌ | **`typeof` 预筛阶段即剔除**（R1.2）。`undefined` 另有一条必要理由：`static X;` 未初始化时值就是 `undefined`，放行会让**每个只声明未赋值的静态字段**都变成 NIL 常量。`symbol` 若直接进 `js_to_gd_var` 会打 `Error` 日志（污染日志，违反静默忽略） |

  **共享 `_p` 是容器得以成立的原因**（`Variant::is_type_shared()` 为 `true`，
  `core/variant/variant.cpp:3473-3493`；`js_to_gd_var` 的 `IF_VariantFieldCount` 分支只拷贝 Variant
  外壳，内层引用计数对象是同一份）：① 不施加只读 ⇒ JS 改它 = 改我们的"常量"（这是 v1 排除它的理由）；
  ② **施加只读 ⇒ 两侧同时冻住**（标志在共享的 `_p` 上）—— ② 正是我们要的效果，于是"共享"从缺陷
  变成机制。`PACKED_*` 缺 `make_read_only()` API、`OBJECT` 是引用语义 ⇒ 这两类没有 ② 可用，仍排除。

  **实现用白名单，不用排除清单**：转换成功后只放行 **8 种** Variant 类型 ——
  `NIL` / `BOOL` / `INT` / `FLOAT` / `STRING` / `STRING_NAME`（基础值）+ `ARRAY` / `DICTIONARY`
  （容器，见下）。`Vector2`/`Color`/`Packed*`/`Object`/`Callable`/`Signal` 全部落 `default:` 被剔除
  —— 这样不依赖任何"排除清单"的完备性。
  **注意 `ARRAY`/`DICTIONARY` 这一档只能来自 `GArray`/`GDictionary` 包装或我方新建的枚举
  `Dictionary`**：纯 JS 数组在无提示路径返回 `false`，纯 JS 对象在任何路径都返回 `false`
  （`research/phase0-findings.md` §0.2）⇒ 类型白名单本身就等价于"必须是包装对象"，无需额外判别。

  **TS 侧写法**：
  - 基础值常量 → 字面量：`static readonly MAX = 100`
  - 容器常量 → **必须**用包装构造：`static readonly ARR = GArray.create([1, 2, 3])` /
    `GDictionary.create({a: 1})`（`new GArray([...])` / JS 字面量都**不**行）
  - 对象（`Node`/`Resource`）/ `Vector2` 等值类型 / `Packed*` → 仍走
    `@bind.exposed.shared()` 静态变量（R3/R6）；语义上它是**静态变量**而非常量，不承诺不可变
  转换规则统一走 `TypeConvert`，不在 C++ 侧另造一套类型判定。

  **必须写明的边界**：TS 的 `readonly` 编译后被擦除 ⇒ **JS 侧 `MyClass.MAX = 999` 仍合法**。
  我们的常量是**解析期值快照** ⇒ JS 侧改了，GDScript 侧 `SomeScript.MAX` 仍是解析那一刻的值
  （不联动、不报错）。**不可变性只对 GDScript 侧成立**，必须文档写明，避免作者以为两侧联动。

  **`NODE_PATH` / `RID` 为何也排除**：它们同为值类型（`is_type_shared == false`）且 Godot 侧
  **没有**原地修改 API ⇒ 按"JS 侧能否原地改"这条判据其实**满足**。仍排除的理由：必须由 JS 包装
  对象构造（`new GNodePath(...)` / `new GRID()`），实用价值低，而每多一类就多一条要文档化的边界
  ⇒ 保持白名单最小。需要时走 `@…shared()`。

  **容器：纳入（2026-09-25 用户拍板；取证见 `research/q1q2-freeze-and-signatures.md` §Q1）**。
  用户原提议是「注解 `Object.freeze` 住 JS 对象 + 容器再 `make_read_only()`」；取证结论是
  **只需后半句**：
  - **值类型保持排除**：`Object.freeze(实例)` 与 `Object.freeze(prototype)` **都挡不住**
    `inst.x = 42`（实测 store 被改）—— `x`/`y` 是 prototype accessor，可变状态在 `IF_Pointer`
    指向的 Variant 里，freeze 只作用于 own property。唯一有效的是给实例加 own 不可写属性，
    但那要改**用户自己的对象**、且注解时机早于实例创建 ⇒ **不可行**。
  - **`GArray` / `GDictionary` 包装纳入常量**：它们经 `js_to_gd_var` 只拷贝 Variant 外壳
    （`IF_VariantFieldCount` 分支）⇒ 与 JS 侧**共享 `_p`** ⇒ 对该 Variant 调 `make_read_only()`
    **同时**挡住两侧写入（GDScript `Array::set`/`push_back`… 与 JS proxy `set` trap →
    `target.set()` 命中同一 `_p->read_only` guard）。**不需要 `Object.freeze`**
    —— 对现有容器 proxy 调 freeze 会直接抛 `TypeError`（`isExtensible→true` 与
    `preventExtensions→true` 不自洽，违反 Proxy 不变式）。
  - **作者写法**：`static readonly ARR = GArray.create([1, 2, 3])` /
    `GDictionary.create({a: 1})`。**不能**用 JS 字面量 —— 纯 JS 数组走无提示路径返回 `false`，
    走 `Variant::ARRAY` 提示重载则 `try_convert_array_any` **新建** `Array`（新 `_p`）⇒
    冻它保护不了 JS 原数组。
  - **代价（须文档写明）**：① 施加后 **JS 侧写该容器会开始报 engine error**（行为突变 ——
    这是"两侧真不可变"的必然代价）；② **深层需递归施加**（`make_read_only()` 是浅层）；
    ③ `Packed*Array`（无 `make_read_only` API）与 `OBJECT`（引用语义）**仍排除**。
  - **GDScript 侧保护分两级（已实测，A14/A15）**：**直接赋值** `SomeScript.ARR = other` 由
    `get_constants()` 命中保证**编译期**报错（`:4396` 置 DataType 的 `is_constant`，**不依赖
    我方 `_get`**）；**索引赋值** `SomeScript.ARR[0] = 7` **编译期不报**，只在**运行期**被
    `make_read_only()` 拒绝。根因见 R2.5 第 3 步（折叠路径因基是 metatype 而整体跳过）。
    **运行期 guard 无论如何都在**（`make_read_only()` 已施加）。
- R2.5 **GDScript 如何在静态分析期识别常量（源码实读 + 实机取证；回答"能否让编辑器也报错"）**：
  `Script::get_constants()` 是**唯一**入口。命中后**直接赋值**必被拒（编译期）；
  **嵌套形式永不折叠** ⇒ 只有运行期 guard：
  1. `reduce_identifier_from_base` 的常量分支（`gdscript_analyzer.cpp:4390-4399`）→
     `make_builtin_meta_type(constant.get_type())` ⇒ `is_constant = true` + `is_meta_type = true`
     （`:226-233`）。
  2. 赋值检查（`:2992-2994`）：`assignee_type.is_constant` ⇒
     **`Cannot assign a new value to a constant.`**
  3. **嵌套形式**（`SomeScript.ARR[0] = 7`、`SomeScript.E.EA`）**不参与常量折叠**，根因是
     `reduce_subscript` 的折叠路径**进不去**：属性分支 `:4889` 的前置是
     `p_subscript->base->is_constant && !base_type.is_meta_type`，而 `const S := preload(...)`
     的基类型**就是 metatype** —— `reduce_preload` 把 `p_preload->type_constraint` 置为
     `type_from_variant(resource)`（`:4850-4852`），该函数对有效脚本置 `is_meta_type = true`
     （`:5841-5846`，`is_meta_type = scr.is_valid()`）⇒ `!base_type.is_meta_type` 为**假**
     ⇒ **整个折叠块被跳过**。
     于是落到 `:4949` 的 else 分支 → `reduce_identifier_from_base(attribute, &base_type)`
     → 非 GDScript 分支 `:4390-4399` **只把 DataType 置 `is_constant`**
     （`make_builtin_meta_type`），**不置节点级 `p_identifier->is_constant`**；
     `:4962-4963` 随即执行 `p_subscript->is_constant = p_subscript->attribute->is_constant`
     ⇒ **节点级保持 `false`**。
     而节点级 `is_constant` 才是折叠的唯一开关（索引分支 `:4992` 要求
     `p_subscript->base->is_constant && p_subscript->index->is_constant`）
     ⇒ **嵌套形式永不折叠**。

     > **早期推断已被实测推翻**：曾据 `:4912-4920` 的 `get_named`（即 `Object::get` ⇒ 我方 `_get`）
     > 推测「`_get` 在分析期返回该值 ⇒ 嵌套形式也会折叠」，并据此把本节的结论写成
     > 「取决于 `_get`」。**实测为否**（A15）—— 那条路径**根本进不去**（前置
     > `!base_type.is_meta_type` 不成立），`get_named` 从未被调用，`_get` 是否实现**无关**。
     > `_get` 只服务**运行期**读取。这是「源码实读的推断必须由实测裁决」的又一例。
  4. 第二条独立机制 `is_read_only`（`:3001`、`:3009`）**只对 ClassDB 原生属性**在 `:4417`
     置位（`ptype.is_read_only = !has_setter`）；脚本属性走 `type_from_property`（`:5913`）
     **不置位**。

  ⇒ **结论（实测）：编译期保护只覆盖「直接赋值」** —— 由 `:4396` 的 DataType `is_constant`
  保证，**不依赖** `_get`；**嵌套形式只能靠运行期 `make_read_only()` guard**。

  **实测矩阵（A14 / A15，2026-09-25 实机取证）**：

  | GDScript 写法 | 实测结果 |
  |---|---|
  | `SomeScript.N = 5`（直接赋值） | ✅ **编译期** `Cannot assign a new value to a constant.`（脚本根本不加载） |
  | `SomeScript.ARR[0] = 7`（索引赋值） | ❌ **编译期不报**；运行期 `Invalid assignment on read-only value (on base: 'Array')` |
  | `SomeScript.E.Red`（点访问枚举成员） | ❌ **编译期报** `Cannot find member "Red" in base "Dictionary"` |
  | `SomeScript.E["Red"]`（索引访问枚举成员） | ✅ 可用，得到 `0` |

  探针与日志：`.agent_tmp/acc/_acc_w1.gd`（直接赋值）、`_acc_w2.gd`（索引赋值）、`_acc_w3.gd`
  （点访问枚举）、`_acc_w4.gd`（索引访问枚举）；日志 `.agent_tmp/acc_probes.log`。

  「枚举点访问」的两种候选落法中，实测落在**后者**：未折叠 ⇒ `base_type` 仍是
  `make_builtin_meta_type(DICTIONARY)`（**meta**）⇒ 落 `:4150-4193` 的 meta 分支，
  该分支只查 `has_constant`/enum、**不查 `get_property_list`** ⇒ 报错。
  ⇒ **文档必须写明：GDScript 侧枚举成员须用索引访问**（`SomeScript.E["EA"]`），点访问会报错。

  **另一条前提：基表达式必须是编译期已知的脚本引用**。若写成 `var h = load("res://x.ts")`
  （无类型），`base_type.is_variant()` 先命中（`:4926-4940`）⇒ 只 `mark_node_unsafe`、
  不设 `is_constant` ⇒ **编译期不报，退化为运行期**（探针 `w*`/`z*` 实测一致）。
  故文档应写「`const S := preload("res://x.ts")` 形态才有编译期保护」。

  静态变量（R3）保持**可写**（不置 read-only），符合语义。
  注意 `:3009` 的 `!Variant::is_type_shared(...)` 条件：嵌套只读检查**跳过**共享类型
  （`ARRAY`/`DICTIONARY`/`PACKED_*`/`OBJECT`）。

  **取证强度**：上表已由**实机实测**确认（A14/A15，2026-09-25），不再仅是源码推导。
- R2.4 **不可变性判定规则**：运行期无法自动判定 `const` vs `static`（`static readonly` 的
  `writable` 仍为 `true`），因此「是不是常量」由注解声明（R4）；C++ 侧按 R2.3 的**白名单**做类型准入。
  **`make_read_only()` 用于两处**：① 枚举 `Dictionary`（我方新建、不与 JS 共享）；
  ② **容器常量**（`ARRAY`/`DICTIONARY`）—— 对它们**递归**施加，从而把 JS 侧与 GDScript 侧**同时**
  冻住（共享 `_p`，见 R2.3）。递归是为了闭合与 GDScript 的深度差：GDScript `const` 经脚本对象取时
  是**深层**冻结（`research/phase0-findings.md` §0.6），浅层施加只冻顶层。
  基础值常量的保护来自 **`_set` 拒绝常量名** + **常量不进 `_get_property_list()`**：实测
  `H.N = 5` 在 `const H := preload(...)` 下是**编译期** `Cannot assign a new value to a constant.`，
  在 `var h = load(...)` 下是**运行期** `Invalid assignment of property or key 'N' …`，
  两种都改不动存储值（探针 `y5` / `z2`）。规则见 `design.md` §4.2 / §5.3。

### R3 静态变量

- R3.1 实现 `_get` / `_set` / `_get_property_list`（`Object::get`/`set`/`get_property_list` 均已确认可
  到达 GDExtension 的这三个虚函数，见 `design.md` §2.1/§2.2）。
- R3.2 `_get` 的查找顺序：常量 → 静态变量 → 基类链（`base`）递归。**无静态函数分支**（R8.5）。
- R3.3 `_get_property_list` 输出静态变量的 `PropertyInfo`，并保留 `script/source`（对齐
  `GDScript::_get_property_list` 的第一项，`gdscript.cpp:1057`）。
- R3.4 跨环境语义见 R6。

### R4 注解（区分常量与静态变量）

运行期 JS 无法区分 `static readonly X`（TS 编译后仍可写）与 `static X`，也无法区分模块级 `const` 与 `let`。
因此需要注解显式声明意图：

- R4.1 新增注解标记**常量**（进 `constants` / `_get_constants`，可被 GDScript 静态分析期取值）。
- R4.2 新增注解标记**跨环境静态变量**（见 R6.2）。
- R4.3 注解须通过既有 `jsb.internal.*` 桥接机制注册（形态对齐
  `scripts/jsb.runtime/src/godot.annotations.ts` 的 `createClassBinder()` 与
  `src/runtime/bridge/jsb_bridge_module_loader.cpp` 的 `_add_script_*` 系列，绑定表在 `:542-548`），
  并在 `src/editor/codegen/jsb_codegen_annotations.cpp` 的注解表中登记。
- R4.4 **未注解的 Variant 兼容静态属性一律忽略**（用户决策 D2）。**注解命名已定（Q2）**：
  `@bind.exposed.const()`（常量）/ `@bind.exposed.shared()`（跨环境共享静态变量）。
  选它的理由：不能是 binder 顶层的裸 `constant`/`shared`（D3）；`static` 已被否决
  （静态与常量不是包含关系）；`exposed` 是**真上义词**（两者都是"向 Godot 与其它环境暴露"），
  两叶子并列。待选方案与取舍见 `design.md` §8.1。
- R4.5 **两种声明形态并存（2026-09-25 用户追加要求）**：注解须同时支持
  ① **成员形态** —— 类内 `static readonly` + 逐成员注解 `@bind.exposed.const()`；
  ② **类级形态** —— 类外**同名命名空间**声明 + 类级点名 `@bind.exposed.const("A", "B")`。
  用户原话：「他们实际上没有区别呀不是吗。最终都是要显示点名通过装饰器让他们不能改绑定的值，
  毕竟 `static readonly` 运行时也只是 `static`，不是真的不能换成另一个值。」
  - **两形态在运行期完全同形**（实测 `Object.getOwnPropertyDescriptor` 逐字段相等）⇒ 最终都归到
    同一个桥接调用 `jsb.internal.add_script_constant(target, name)`，**C++ 解析侧零改动**。
  - **唯一改动点** = 注解按**实参个数**分派（无参 → 成员装饰器；有参 → 类装饰器）。
  - **为何类级形态必须点名**：TS1206 —— 装饰器**不能**挂在命名空间成员上（实测）。
  - **为何类级装饰器读不到值也不影响**：类装饰器执行时命名空间成员**尚未挂上**（实测装饰期
    own props = `["length","name","prototype"]`）⇒ 类级装饰器**只记名字**，值由解析期按名到
    `class_obj` 上取，与成员形态通道完全一致。
  - **TS2652 陷阱**：类与同名命名空间合并时类**不能**带 `export default` 修饰符
    （`Merged declaration 'Target' cannot include a default export declaration`），
    须写成 `class Target {}` + `namespace Target {}` + 独立 `export default Target;`。
    已写进 `exposed.const()` 的 JSDoc 示例。
  - **类级形态校验装饰目标**：两个类级分支均断言 `context.kind === "class"`，否则抛错。
    必要性：错位的 `@bind.exposed.const("X")` 会把名字记到 **prototype** 上，而解析期只读类对象的
    **自有属性**（`jsb_class_info.cpp` 的 `ClassConstants` 读取），结果是**静默丢弃、无任何告警**。
    TS 本就拒绝该写法（故为类型守卫生效后的 misuse 路径），此守卫把「静默丢弃」变成「明确报错」。

### R5 不做静态函数（用户决策 Q1=(c)），但必须修一个挡住 R2 的既有缺陷

用户点名的两个语义问题，逐条给结论（完整证据见 `design.md` §6.0）：

1. **该定位到哪个 JS 环境的静态函数** —— **架构上没有答案**。`script_class_info_` 是
   `StatelessScriptClassInfo`（`jsb_script.h:90`），**刻意与环境解耦**：它不含 `js_class`
   （只在 `ScriptClassInfo` 里，`jsb_class_info.h:233-236`），也不含任何 `EnvironmentID`。
   `load_module_immediately()` 建的是 `jsb::JSEnvironment env(get_path(), true)` 的**临时环境**
   （`jsb_script.cpp:475`；`JSEnvironment` 构造即 `Environment::_access()` 取"当前环境"，
   `jsb_script_language.cpp:72-80`），函数返回后即析构。要调静态函数只能**按策略挑一个环境**
   （调用者线程环境 / 主环境 / 首次加载环境），而任一策略都会在 worker / ShadowRealm /
   纯 Godot 冷调用场景给出不符合直觉的结果。`JSCallable`（`jsb_callable.h:33-70`）的 `env_id_`
   是**实例方法**的先例（天然绑定对象所在环境），静态函数没有对象可绑，**不能直接套用**。
2. **它改变的数据是否跨环境** —— **默认不会，且无法检测**。被 `shared` 注解的静态变量走 R6.2 的
   C++ 权威存储，**会**跨环境；但普通模块级状态（模块 `let`、闭包捕获、未标注的 `static`）
   **每个 isolate 一份**，静态函数改它只改自己那份 ⇒ 跨环境静默分叉，我们**没有任何手段发现**。

① 只是"策略不唯一"，② 是"语义本身不确定"。按用户给出的判据（「这些你都没有好的方案的话静态函数
就不做了」），**采纳 (c)：不做**，仅文档说明（R8.5）。

但**必须**修一个**挡住 R2** 的既有缺陷：

- R5.1 **`_get_method_info()` 现在对任何名字都返回非空**（`jsb_script.cpp:365-372`：release 下
  `jsb_check` 是空操作，恒返回 `{"name": p_method}`）⇒ `reduce_identifier_from_base` 的 ② 分支
  （`gdscript_analyzer.cpp:4366-4373`，**无 `has_method` 守卫**）会对**任意**标识符命中，
  把 `SomeScript.ANYTHING` 解析成 Callable 并 `return` —— ③ 信号与 ④ 常量**永远到不了**。
  修法：只对 `script_class_info_.methods` 中的名字返回 `MethodInfo`，其余返回空 `Dictionary`
  （`MethodInfo::from_dict` 对空字典得 `name == StringName()`，② 自然不命中）。
  **这是 R2 的前置，不是可选优化。**
- R5.2 `_has_static_method()` 保持 `false`、`_get_script_method_argument_count()` 保持空 ——
  语义正确（确实没有静态函数），且不动它们把风险面降到最小。
- R5.3 函数值（`typeof value === "function"`）按 R1.2 **忽略**，不产生任何成员；被注解标记却
  转换失败时打 `JSB_LOG(Warning, ...)`（用户显式要求过，静默丢弃不合理）。

### R6 跨环境静态变量语义

- R6.1 现状：`script_class_info_` 是**首次加载该模块的那个环境**的快照（`jsb_script.cpp:499-501`）；
  各 JS 隔离区独立 import 同一模块，静态变量天然不共享。`Environment::call_script_method` 注释
  「static calls are not supported」（`jsb_environment.cpp:1874-1877`）。
- R6.2 **采用方案 A（C++ 权威存储）**：被注解标记的静态变量在类解析时替换为 JS 访问器，
  读写落到进程级互斥保护的 `HashMap<module_id, HashMap<name, Variant>>`，使 GDScript 侧读写有
  唯一确定语义且对所有 JS 环境一致。**必须在 `GodotJSScriptLanguage::_finish()` 显式释放**
  （Orphan StringName 缺陷 A 的同型处理，`jsb_script_language.cpp:256-259` 已有先例）。
  方案 B（环境本地只读快照）保留为回退。
- R6.3 **值生命周期刻意独立于 `GodotJSScript` 对象（2026-09-26 用户拍板）**：曾评估把槽位绑到
  脚本对象（`~GodotJSScript` 时释放），**否决**。两条理由：① `GodotJSScript` 析构即丢静态值，
  比"多活一会儿"危险得多 —— `static var` 语义上是**类级别**状态；② **纯 JS import 是真实场景**
  （worker `jsb_worker.cpp:449`、编辑器桥 `jsb_bridge_table.cpp:137`/`:163` 均在**没有
  `GodotJSScript`** 时触发类解析），绑对象会让这些场景下无处可写。
  ⇒ 与 GDScript 的偏离（GDScript 随 `~GDScript()` 释放，`gdscript.cpp:1534`/`:1461-1462`）**接受**，
  理由与可观测性分析见 `design.md` §5.2.1。**本轮未改代码**（当前实现即此形态）。

### R7 编辑器与调试器可见性

- R7.1 实现 `_get_members()`：返回实例成员名集合。消费方是远程调试器的对象检查器
  （`scene/debugger/scene_debugger_object.cpp:104`），用于区分脚本成员与导出成员。
- R7.2 常量须能被 inspector / 文档看到：`PlaceHolderScriptInstance::update` 会拉取 `get_constants`
  （`core/object/script_language.cpp:768`）。

### R8 非目标（明确不做）

- R8.1 **不引入源码解析器**（tree-sitter 或自研）来区分 const/static —— 结论见
  `09-06-lowprio-tree-sitter-ast` 的评估；`const enum`（编译期擦除）因此**不支持**，需在文档中说明。
- R8.2 **内部类（`Outer.Inner`）本任务不实现**：一个 `.ts` 文件对应一个 `GodotJSScript`（按路径寻址），
  无法为同文件内的第二个类产出脚本资源。原因与替代形态见 `design.md` §7。
- R8.3 不改 `_get_member_line()`（需源码位置，属 R8.1 的范畴）。
- R8.4 **不把全部静态函数注册成 `GodotJSScript` 的 ClassDB 方法**（用户决策 D5）。
- R8.5 **不做静态函数的暴露与调用**（用户决策 Q1=(c)）：原因见 R5；`SomeScript.fn` /
  `SomeScript.fn.call(...)` / `SomeScript.call("fn", ...)` 三种形态全部不支持，文档中写明。
  仅修 R5.1 的 `_get_method_info` 缺陷 —— 那是常量与信号能被解析到的前置。

## Constraints

- 遵循 `.trellis/spec/godotjs-ext/cpp/` 全部规范（头文件无 `using namespace`、临时文件入
  `./.agent_tmp/`、不直接编辑 `*.gen.*`/`*.def.*`）。
- 构建只用 `.trellis/spec/godotjs-ext/build/scons-build.md` 的规范命令，禁止 `scons --clean`。
- `src/runtime/` 属 runtime 库、`src/editor/` 属 editor 库；两侧共享的数据结构改动需注意
  `JSB_RUNTIME_API` 数据符号导出约束（`src/jsb.config.h` 顶部注释）。
- 目标引擎 API：`API_VERSION = "4.7"`（`SConstruct:69`），引擎源码为 4.8.dev。
- 验收标准遵循 `.trellis/spec/godotjs-ext/test/index.md`：Orphan StringName = 0、
  `GODOTJS_TEST_PROJECT_COMPLETED` 哨兵。

## Acceptance Criteria

- [x] **A1** GDScript 侧 `SomeScript.CONST`（JS 基础类型：`int` / `float` / `String` / `bool` / `null`；
      以及 `GArray.create` / `GDictionary.create` 构造的容器）可读，值与 JS 侧一致
      —— **已验证**（`static-members-gdcheck.gd` 断言 GDScript 侧读值；`test-static-members.ts`
      经 `get_script_constant_map()` 断言 Godot 侧同一批值）
- [x] **A2** GDScript 侧 `SomeScript.MyEnum` 得到 `Dictionary{name: int}`，与 GDScript 枚举形态一致；
      `SomeScript.get_script_constant_map()` 含全部常量与枚举
      —— **已验证**（gdcheck 断言 `E.size()==3` / `E["Blue"]==2` 与常量表内容）
- [x] **A3** GDScript 侧可读写被注解标记的静态变量；写值在**所有 JS 环境**（主环境 + worker）可见
      （跨环境一致性有实测证据）
      —— **已验证**：gdcheck 写 `S.score=77` 读回 77；`test-static-members.ts` 断言 worker 读到主环境写的
      `11`、worker 写 `2000` 后主环境读到 `2000`。**负向控制**：把期望改成 `0`（per-isolate 初值）后
      `failed=1` 且失败项正是该断言 ⇒ 非空转
- [x] **A4** `_get_method_info()` 只对 `script_class_info_.methods` 中的名字返回非空 `MethodInfo`，
      其余返回空 `Dictionary`；`_has_static_method()` 恒 false、`_get_script_method_argument_count()`
      返回空（R5.1 / R5.2）
      —— **已验证**：`jsb_script.cpp` 已按 `script_class_info_.methods.has()` 守卫；守卫的**回归检测**
      由 `static-members-gdcheck.gd` 承担 —— 该文件的 `S.N` / `S.score` 解析走
      `reduce_identifier_from_base` 的 `get_method_info` 分支（**无 `has_method` 守卫**），守卫一旦回归
      这些访问全部解析失败、场景加载即报错。后两项语义未改动（恒 false / 返回空）
- [x] **A5** 纯 JS 对象类型的静态成员在解析阶段被忽略，不产生成员、不报错、不崩溃
      —— **已验证**：C++ 单测断言 `LIT_ARR`/`LIT_OBJ`/`UNDEF`/`FN`/`ANON` 均不在 `constants`；
      JS + Godot 侧用例断言 `VEC`/`LIT`/`FN`/`PLAIN` 均不在常量表；解析期仅打 `JSB_LOG(Warning)`
- [x] **A6** `_get_members()` 使远程调试器对象检查器能区分脚本成员与导出成员
      —— **已验证**：C++ 单测 `script members: own members only` 直接驱动
      `GodotJSScript::_get_members()`（该函数无脚本绑定，消费方 `scene_debugger_object.cpp:104`
      只从 C++ 触达 ⇒ 这是唯一可测入口）。
      **语义（2026-09-25 质询轮修正）**：**只报自有实例成员**，对齐 `GDScript::get_members`
      （`gdscript.cpp:919-925`）—— 唯一调用方自己走 `get_base_script()` 链并把每个脚本的成员挂到该
      脚本名下（`scene_debugger_object.cpp:111-122`）⇒ 合并基类会让同一成员列两次。原实现合并了
      基类链，属**多余且有害**。
      断言：基类脚本列表含 `tag` / `baseOnly`、不含静态 `score`；派生脚本列表 `tag` 恰好 **1** 次、
      **不含**基类独有的 `baseOnly`。
      **负向控制（本轮重做）**：把 `_get_members()` 改回「合并基类链 + `has` 去重」后重编 ⇒
      `test_jsb_static_members.h(394): ERROR: !members.has(StringName("baseOnly"))`、`TESTS_RC=1`、
      `53 passed | 1 failed`；还原后 `54/54`、`698/698` 绿。
      （历史负控：早前删掉 `!inserted.has(member)` 守卫曾得
      `tag_count == 1 values: 2 == 1` —— 那是**合并形态下**的 `HashSet::insert` 缺陷形状，该形态
      已随本轮删除合并而消失；`HashSet::insert` 返回 `Iterator` 的教训仍记在 spec）
      。独立静态审查后（2026-09-25）：C++ 套件扩到 **53/53 cases、684/684 assertions**（新增
      `_shared_static_setter` 的 NIL 回归断言 —— 审查判定该修复此前**无任何永久断言**，属覆盖缺陷）
- [x] **A7** 继承链语义（2026-09-26 修正为「自有优先 + 运行期可达」）
      —— **已验证**：`static-members-derived.ts` 断言 `N=22`（自有常量遮蔽基类）、
      `score=2`（自有静态变量槽位）；`get_script_constant_map()` **只含自有常量**
      （`N` 在、继承的 `F` **不在**，与 GDScript 一致）；继承的 `F` 经 `_get` 在**运行期**
      仍取到 `1.5`（`var` 基形态，见下）；成员清单无重复。
      **实测边界（官方 4.7.2，`.agent_tmp/probe_dconst.log`）**：
      `const D := preload(…)` + `D.F` ⇒ **解析期硬失败**（`Cannot find member "F"`，脚本不加载）；
      `var d := preload(…)` + `d.F` ⇒ 不报错，退化为运行期 `_get`（其自身走 `base`）⇒ 取到 `1.5`。
      原「合并基类」实现让 `get_script_constant_map()` 偏离 GDScript，已删除。
- [x] **A8** 热重载后静态成员集合正确刷新（不残留旧成员）
      —— **已验证**：C++ 单测 `script shared static store`（更窄注解重解析 ⇒ `GONE` 从
      `static_variables` 与存储同时消失、`SHARED` 活值 `7` 保留）；实机探针 `scr.reload(true)` 后
      `after-reload-props=["score"]`、`dupes=[]`、活值 `55` 保留、常量与只读标记完好
- [x] **A9** 验收判据：Orphan StringName = 0；测试项目 `GODOTJS_TEST_PROJECT_COMPLETED` 且无 FAILED
      —— **已验证**：`RC=0`、`orphan=0`、`COMPLETED=1`、`FAILED=0`、`scenes=11`
- [x] **A10** 新增注解在 `project/` 测试项目中有实际用例，且 TS 类型检查通过
      —— **已验证**：`project/tests/static-members/`（5 个 TS + 1 个 GD + 2 个场景，已登记进
      `start.ts`）；`tsc` `RC=0`（不加 `--noCheck`，先删 `.godot/.tsbuildinfo`）。
      追加轮（2026-09-25）：`static-members-namespaced.ts` 覆盖**类级点名形态**，`pnpm gen:types`
      （内含强制 `tsc`）`RC=0` ⇒ 经重生成的 `ClassBinder` 类型检查通过
      。独立静态审查后复验（2026-09-25）：类级分支加 `context.kind` 守卫、参数类型放宽为
      `ClassDecoratorContext | ClassMemberDecoratorContext` 后，`pnpm gen:types` 仍 `RC=0`、
      **tsc error 数 = 0**
- [x] **A11** `misc/verify_codegen.py` 全量校验通过（typings 基线仅预期变化）
      —— **已通过（2026-09-26，用户批准 `--update-baseline` 后）**：`--update-baseline` → `RC=0`，
      紧接一轮全流程重跑（不 update）→ `RC=0`、`✅ 校验通过: 生成产物与基线一致`（双轮确定性成立）。
      基线已吸收那 3 处（`.codegen-baseline/`，mtime 2026-09-26 06:34；`typings/` 20 文件、`gen/` 71 文件），
      三处产物与 live 逐字节相等（实测 `baseline==live ? True`）。
      日志：`.agent_tmp/vc_update_baseline.log`、`.agent_tmp/vc_confirm_baseline.log`。
      **以下为刷新前的归因记录（保留，勿再当作"未通过"）**：
      当时差异 = **10** 处本轮新增
      测试文件产生的 `[gen] 多余`（全在 `gen/godot/tests/static-members/` 下）+ 1 处
      `Operators.tscn.gen.ts` 内容漂移（前序已归因）+ **13** 处 `[typings]`：其中
      `jsb.runtime.gen.d.ts`（本轮 `exposed` 交集声明，**+14 行，逐行核对无其他变化**）与
      `godot.minimal.d.ts`（`BINDING_MODE` 来自晚于基线的 commit + 本轮 `add_script_*`）为预期，
      其余 11 处 `godot*.gen.d.ts` 为**引擎版本输入漂移**。
      **归因纠正（2026-09-25 复查）**：此前写「基线由官方 4.7.2 dump 生成」**方向相反**。实测：
      基线 typings **含 4.8 期实体**（`FuzzySearch` / `AnimationNodeObserver` / `StreamedTexture2D` /
      `TextureStreaming` / `Trail3D` / `Line3D` / `ScenePaint2DEditor` / `VisualShaderGroup` /
      `BoneSpreader3D` / `ResourceImporterStreamedTexture` / `CompressProfile` / `SensorOrientation` /
      `get_preferred_locales` / `ui_toggle_fullscreen` / `RENDER_STREAMING_TEXTURE_MEM_USED`），
      live 全部不含；这些符号**只**存在于 `D:/Dev/godot/godot`（4.8.dev）源码，而
      `project/extension_api.json` 自证 `version_minor:7, version_patch:2, version_status:stable,
      version_build:official`（官方 4.7.2）。⇒ **基线输入是 4.8.dev dump，live 才是官方 4.7.2**；
      该 11 处差异属「基线建立时用错引擎版本」的输入漂移，非生成器回归。
      又：11 个分片的**文件级** diff 由**分片布局抖动**主导（`godot10` 基线 108598 B → live 25661 B、
      `godot1` 585764 → 653181，其余 ±2~8%）—— 按 `codegen-baseline.md:72` 第 4 条，必须做语义级比较：
      以归一化（去注释/空白）行取并集后，**baseline-only 423 行 / live-only 50 行**（后者含真实
      4.7↔4.8 签名差异，如 `ScriptEditor extends PanelContainer` vs `EditorDock`、
      `compress(..., astc_format?: Image.ASTCFormat)` vs `profile?: Image.CompressProfile`）。
      追加轮复核（2026-09-25）：20 → 24 的**增量全部**是新增夹具的生成产物（新目录 10 个文件，
      基线建立于该目录不存在时）⇒ **本轮未引入新的差异类别**。
      独立静态审查后复核（2026-09-25）：`jsb.runtime.gen.d.ts` 的 diff 仍**仅** `exposed` 一处
      （逐行核对），与审查结论一致。
      **归因已完成，`--update-baseline` 待用户确认**（会掩盖真实回归）
      质询答复轮复核（2026-09-25）：因 Q3 修复（生成的 `exposed` 叶子加
      `ClassMemberDecorator<StaticMemberDecoratorContext>` 实参），`jsb.runtime.bundle.d.ts` 也进入差异表
      ⇒ 24 → **25 处**（`jsb.runtime.gen.d.ts` 仍是同一处 `exposed` 块，两个叶子各多一个泛型实参）。
      质询答复轮**固定引擎重建基线**（2026-09-26）：用同一 `Godot_v4.7.2-stable_win64_console.exe`
      回退到改动前再重建基线 ⇒ 那 11 处 `godot*.gen.d.ts` 分片差异**全部消失**，坐实「基线建立时
      用错引擎版本」的输入漂移定性（**这一步才是 Q1 的实证**，此前只是源码符号比对推论）。
      恢复改动后重跑校验，差异稳定为 **3 处**：
      ① `[typings] jsb.runtime.gen.d.ts` —— 本轮 `exposed` 块，两个叶子各多
      `ClassMemberDecorator<StaticMemberDecoratorContext>` 泛型实参（**预期，Q3 修复的产出**）；
      ② `[typings] jsb.runtime.bundle.d.ts` —— `StaticMemberDecoratorContext` 声明本身
      （**预期**，同上 agent 链的另一端）；
      ③ `[gen] godot/tests/static-members/StaticMembers.tscn.gen.ts` —— 新增测试场景产物
      （**预期**，基线建立时该夹具不存在）。
      ⇒ 目前的 3 处**全部**可由本轮改动逐条归因，不存在"未归因差异"。
      **仍未 `--update-baseline`**：一旦吸收就丧失对回归的检出能力，需用户拍板。
- [x] **A12** 文档写明：静态函数三种调用形态均不支持及其原因（R8.5）；常量**禁止写入**（R2.4）
      —— **已验证**：`godot.annotations.ts` 的 `exposed.const()` / `exposed.shared()` JSDoc 已写明
      常量是解析期快照 / TS `readonly` 被擦除 / 不可变性只对 Godot 侧成立 / 容器冻结的代价 /
      枚举须索引访问 / `const enum` 不支持 / 静态函数三种形态均不支持及原因
- [x] **A13** R2.3 白名单可验证：只有 `NIL`/`BOOL`/`INT`/`FLOAT`/`STRING`/`STRING_NAME`
      （基础值）+ `ARRAY`/`DICTIONARY`（**来自 `GArray`/`GDictionary` 包装或我方枚举**）类型的
      静态属性出现在 `_get_constants()` 与 `get_script_constant_map()` 中；
      `Vector2`/`Color`/`Packed*Array`/`Object`/`Callable`/`Signal` 类型的静态属性**一个都不出现**
      （这些包装都会**转换成功** ⇒ 必须按 Variant 类型白名单显式剔除，不能只靠 `js_to_gd_var`
      失败）；且枚举与容器常量的 `is_read_only()` 为 `true`，**容器内层也**为 `true`（递归施加）
- [x] **A14** GDScript 静态分析期识别（R2.5，**仅直接形式**）：`SomeScript.N = 5` 在 GDScript
      **脚本编辑器里直接报** `Cannot assign a new value to a constant.`（编译期，非运行期）
      —— **已实测通过**（探针 `_acc_w1.gd`，日志 `.agent_tmp/acc_probes.log`）
- [x] **A15** 实测并回写**嵌套形式的实际保护等级**（R2.5 实测矩阵）：实测结果 ——
      ① `SomeScript.ARR[0] = 7` **编译期不报**，运行期 `Invalid assignment on read-only value
      (on base: 'Array')`（探针 `_acc_w2.gd`）；② `SomeScript.E.Red`（点访问）**编译期报**
      `Cannot find member "Red" in base "Dictionary"`（`_acc_w3.gd`）；③ `SomeScript.E["Red"]`
      可用，得到 `0`（`_acc_w4.gd`）。⇒ 嵌套形式**永不折叠**，与 `_get` 无关（根因见 R2.5 第 3 步）

## Open Questions

（无。Q1=(c)、Q2= `@bind.exposed.const()` / `@bind.exposed.shared()` 已拍板；
**Q3 已定案 = JS 基础值 + enum + 容器（`GArray`/`GDictionary` 包装）**，见下方「已拍板」段。）

**已拍板**：

- **Q1 = (c)**：不做静态函数（R5 / R8.5）。
- **Q2 = `@bind.exposed.const()` / `@bind.exposed.shared()`**：binder 上挂 `exposed` 子对象，
  两个叶子并列（见 R4.4）。注：这是 **TS 面向用户的名字**；`jsb.internal.*` 侧的桥接函数名仍沿用
  既有 `_add_script_*` 家族命名（`design.md` §8.2），两者无需同名。
- **Q3 最终（2026-09-25 用户拍板，经两轮收窄）**：常量类型面 = **JS 基础值 + enum +
  容器（`GArray`/`GDictionary` 包装）**。
  - **Godot 值类型（`Vector2`/`Color`/…）不作常量**：JS 侧那个 wrapper 是可改的（`v.x = 0` 合法），
    且 `Object.freeze` 实测**挡不住**（`x` 是 prototype accessor，freeze 只作用于 own property）
    ⇒ 我们不把注解当"不可变性"卖。要跨语言暴露 → `@bind.exposed.shared()` 静态变量。
  - **容器作常量**（用户原提议「freeze + `make_read_only()`」→ 取证后**只需后半句**）：
    `GArray`/`GDictionary` 与 JS 侧**共享 `_p`** ⇒ 递归 `make_read_only()` **同时**冻住两侧
    （JS 侧写容器也开始报 engine error —— 这是"两侧真不可变"的必然代价）。
    作者须写 `GArray.create([...])` / `GDictionary.create({...})`；`Packed*`（无只读 API）与
    `OBJECT`（引用语义）仍排除。
  - 基础值常量的保护：**`_set` 拒绝常量名** + **常量不进 `_get_property_list()`**。
    实测：`H.N = 5` 在 `const H := preload(...)` 下是**编译期**
    `Cannot assign a new value to a constant.`；在 `var h = load(...)`（非 const 基）下是**运行期**
    `Invalid assignment of property or key 'N' …`，两种都**改不动**存储值（探针 `y5` / `z2`）。
  - 写入常量的后果已实测澄清：**打印脚本错误并终止当前 GDScript 函数**，不是进程永久挂起
    （早期「挂起」是探针把 `quit()` 写在 `_initialize` 里造成的假象，见 `design.md` §5.3）。
  - **GDScript 编辑器识别**（R2.5，**已实测**）：**直接赋值** `SomeScript.N = 5` 在**编译期**即报错
    （实现 `_get_constants()` 即得，`:4396` 置 DataType 的 `is_constant`）。**嵌套形式**
    （`SomeScript.ARR[0] = 7`、`SomeScript.E.EA`）**编译期不折叠** —— 折叠块的前置
    `!base_type.is_meta_type` 不成立（`const S := preload(...)` 的基类型**就是** metatype，
    `reduce_preload` 经 `type_from_variant` 置 `is_meta_type = true`）⇒ 整块被跳过，
    `get_named`/`Object::get`/我方 `_get` **从未被调用**，与 `_get` 是否实现**无关**。
    故嵌套形式只有**运行期** `make_read_only()` guard。**早期"取决于 `_get`"的推断已被实测推翻**，
    详见 R2.5 第 3 步的「早期推断已被实测推翻」段。
    取证见 `research/q1q2-freeze-and-signatures.md` §Q3。

## Notes

- 关联任务：`09-06-lowprio-tree-sitter-ast`（已完成评估，结论：不引入；见其
  `research/tree-sitter-evaluation.md`）。本任务**不依赖** tree-sitter 结论。
- 交付物为**阶段 0–3**（见 `implement.md`），每阶段可独立验证；原「阶段 4：静态函数」随 Q1=(c) 删除。
- 关键证据与取舍集中在 `design.md`；未实测项已在 `design.md` §10 逐条标注。
- 实测探针：`.agent_tmp/gdcallable/`（任务收尾已清理）（`h_*.gd` = helper；`p_*` / `q*` / `r*` / `s*` / `t*` = 早期形态探针；
  `w*` / `y*` / `z*` = 「写入常量」失败形态探针），经 python `subprocess` 调
  `Godot_v4.7.2-stable_win64_console.exe --headless --script`。TS 产物探针在 `.agent_tmp/tsprobe/`（任务收尾已清理）。
