# 设计：GodotJSScript 静态成员

> 本文件记录**技术设计、契约、取舍**；需求与验收见 `prd.md`，执行顺序见 `implement.md`。
> 引擎侧结论在 `D:/Dev/godot/godot`（4.8.dev）实读源码核实；GDScript 语义结论用
> `Godot_v4.7.2-stable_win64_console.exe` 实跑核实（探针在 `.agent_tmp/gdcallable/`（任务收尾已清理））。
> `[实测]` = 有实跑证据；`[INFERENCE]` = 未实测推断。

---

## 0. 已拍板决策（用户原话，不再重议）

| # | 用户答复（原话摘录） | 本设计的落实 |
|---|---|---|
| **D1** | 「做常量和注解的静态属性。另外常量的难点不是检测出所有支持的 godot 类型，而是非 js 基础类型你要怎么判定真的是不可变的常量。」 | 范围 = 常量 + **注解门控**的静态变量；不可变性判定规则见 §4.2 |
| **D2** | 忽略（未注解的静态属性不解析为成员） | §5.1：未标记的静态自有属性不产生成员，静默跳过 |
| **D3** | 「constant 和 shared。但是不能直接在 binder 上用这两个命名。binder 对标的是 gdscript 的注解，这两个不属于该体系，需要有更特殊的命名」 | 语义 = `constant` / `shared`；命名走**子命名空间**（§8.1） |
| **D4** | 「只是 `Object::callp` 对应在 GDScript 里应该是 `SomeScript.call("static_method", ...)` 的形式吧？这种没办法只能在文档里明确说明禁止。`_get()` 返回一个静态函数的 Callable 还是可行的吧，`SomeScript.static_method(...)` 这样的应该还是没问题吧。关键还是函数参数的问题。」 | §6：分析期 + `_get()` 返回 Callable 可行；**`SomeScript.static_method(...)` 直连不可行**（§6.1/§6.2 实测），唯一形态是 `SomeScript.static_method.call(...)`。`SomeScript.call("f", ...)` 是**编译期**错误。**调用形态待用户确认（§6.5）** |
| **D5** | 「不做你说的全量」 | 不把所有静态函数注册成 `GodotJSScript` 的 ClassDB 方法（§6.3 选项 B 仅限注解子集，且需用户再确认） |

---

## 1. 现状与缺口（精确到符号）

| 能力 | 现状符号 | 缺口 |
|---|---|---|
| 类信息容器 | `jsb::StatelessScriptClassInfo`（`src/runtime/bridge/jsb_class_info.h:198-231`）：`methods` / `signals` / `properties` | 无 `constants` / `static_variables`（**不**加 `static_methods`，R8.5） |
| 解析入口 | `ScriptClassInfo::_parse_script_class_iterate`（`jsb_class_info.cpp:69-286`）只枚举 `prototype`（`:88-185`）、`ClassSignals`（`:203-217`）、`ClassProperties`（`:229-282`） | 从不读 `class_obj` 的自有属性；`clear()` 在 `:100-104` |
| 脚本资源快照 | `GodotJSScript::script_class_info_`（`jsb_script.h:90`），由 `jsb_script.cpp:499-501` 从「首次加载该模块的环境」拷贝 | 快照语义 = 常量安全，静态变量不安全（§5.2） |
| 常量 | `_get_constants()`（`jsb_script.h:196-199`）返回空 `Dictionary` | 全空 |
| 成员清单 | `_get_members()`（`:200-203`）返回空 `TypedArray<StringName>` | 全空 |
| 静态方法 | `_has_static_method()`（`jsb_script.cpp:359-364`）恒 `false`（带 TODO） | 保持不动 —— 语义正确（确实没有静态函数），见 R5.2 |
| 参数个数 | `_get_script_method_argument_count()`（`:459-461`）恒返回空 `Variant` | 全空 |
| `_get` / `_set` / `_get_property_list` | **未声明** | 全缺 |
| 静态函数调用 | `Environment::call_script_method` 明确写着「TODO: 支持静态函数的调用。 static calls are not supported」（`jsb_environment.cpp:1874-1877`） | 全缺 |

GDScript 对照实现（已核实）：`_get`（`gdscript.cpp:966-1013`）、`_set`（`:1023-1053`）、
`_get_property_list`（`:1055-1081`，首项 `script/source` 在 `:1057`）、`get_constants`（`:911-916`）、
`get_members`（`:919-926`）、`has_static_method`（`:366-368`）、
`get_script_method_argument_count`（`:370-382`）、`callp`（`:930-960`）。

---

## 2. 引擎契约（逐条核实）

### 2.1 `_get` / `_set` — 可达 ✅

```
GDScript: SomeScript.CONST
  → OPCODE_GET_NAMED (gdscript_compiler.cpp:838 write_get_named；VM gdscript_vm.cpp:1261-1287)
  → Variant::get_named (core/variant/variant_setget.cpp:275-288)   [OBJECT 分支]
  → Object::get (core/object/object.cpp:268-289)
  → _extension->get(...)          ← GDExtensionClassGet
  → godot-cpp get_bind (wrapped.hpp:298-326)
  → GodotJSScript::_get
```

`Object::get` 的 `_extension->get` 分支位于 `get_native` **之前**（`object.cpp:276-283`），
脚本资源自身的 GDType 成员不会抢先。写入路径对称：`Variant::set_named` → `Object::set`
（`object.cpp:212-219`）→ `_extension->set` → `set_bind` → `_set`。

godot-cpp 注册条件（`wrapped.hpp:224-229,298-326`）：`GDCLASS` 用
`m_class::_get_get() != m_inherits::_get_get()` 判定，**只要 `GodotJSScript` 声明了
`_get`/`_set`，`get_bind`/`set_bind` 就会真的转发**；否则退化为 `false`。

### 2.2 `_get_property_list` — 可达 ✅

`Object::get_property_list`（`object.cpp:570-588`）在扩展链上调用
`current_extension->get_property_list(...)`；godot-cpp 用
`T::has_get_property_list() ? T::get_property_list_bind : nullptr` 注册（`class_db.hpp:273`，
判定在 `wrapped.hpp:324-326`）。⇒ 声明 `_get_property_list` 即生效。

### 2.3 `_get_constants` / `_get_members` / `_get_method_info` — 已注册，只是返回空 ✅

`ScriptExtension` 侧（`core/object/script_language_extension.h`）：

- `:200-214` `GDVIRTUAL0RC_REQUIRED(Dictionary, _get_constants)` /
  `(TypedArray<StringName>, _get_members)`；`get_constants` 用 `r_constants->insert(kv.key, kv.value)` 逐项插入。
- `:126-131` `GDVIRTUAL1RC_REQUIRED(Dictionary, _get_method_info)` → `MethodInfo::from_dict`
  （`core/object/method_info.cpp:54-88`，认 `name` / `args` / `default_args` / `return` / `flags`）。
- `:113-123` `GDVIRTUAL1RC(Variant, _get_script_method_argument_count)`，返回 `INT` 即 `r_is_valid = true`；
  否则回退 `Script::get_script_method_argument_count`（`core/object/script_language.cpp:122-135`，
  取 `get_method_info().arguments.size()`）。

godot-cpp 已绑定（`script_extension.hpp:195-199`），`GodotJSScript` 已 override
（`jsb_script.h:196-203`）⇒ **只需填充返回值**。

消费方（决定行为，不是可选）：

- 静态分析期：`GDScriptAnalyzer::reduce_identifier_from_base`（`gdscript_analyzer.cpp:4348-4402`）
- `Script::get_script_constant_map()`（`core/object/script_language.cpp:106-114`，绑定在 `:181`）
- 占位实例：`PlaceHolderScriptInstance::update`（`script_language.cpp:768`）→ inspector
- 远程调试器：`SceneDebuggerObject::_parse_script_properties`
  （`scene/debugger/scene_debugger_object.cpp:100-104,158-176`）

### 2.4 静态函数的分析期路径 — **关键不对称** ⚠️

`reduce_identifier_from_base` 对**非 GDScript** 脚本（`base_class == nullptr && script_type.is_valid()`，
`gdscript_analyzer.cpp:4346-4402`）按**固定顺序**探测四个来源：

```cpp
script_type->get_script_property_list(&property_list);                    // ① 属性
MethodInfo method_info = script_type->get_method_info(p_identifier->name); // ② 方法（无 has_method 守卫！）
if (method_info.name == p_identifier->name) {
    p_identifier->type_constraint = make_callable_type(method_info);       //    → is_meta_type = false
    p_identifier->function_source_is_static = method_info.flags & METHOD_FLAG_STATIC;
    return;
}
script_type->get_script_signal_list(&signal_list);                         // ③ 信号
script_type->get_constants(&constant_map);                                 // ④ 常量
if (constant_map.has(p_identifier->name)) {
    p_identifier->type_constraint = make_builtin_meta_type(constant.get_type()); // → is_meta_type = true
    return;
}
```

两点必须记住：

1. **② 没有 `has_method` 守卫**（`:4366`）。只要 `_get_method_info(name)` 返回 `name` 匹配的
   `MethodInfo`，分析期就把 `SomeScript.fn` 当作 Callable，**与 `_has_method` 无关**。
2. **④ 产出 `make_builtin_meta_type`（`is_meta_type = true`）**，② 产出 `make_callable_type`
   （`is_meta_type = false`，且带完整 `method_info` 签名）。这个差异决定 `.call()` 能否通过分析（§6.2）。

### 2.5 静态函数的运行期直连 — **不可达** ❌

```
GDScript: SomeScript.fn(args)
  → OPCODE_CALL_RETURN (gdscript_compiler.cpp:719 write_call)
  → Variant::callp (core/variant/variant_call.cpp:1390-1413)  [OBJECT 分支]
  → Object::callp (core/object/object.cpp:851-910)
      ├─ script_instance->callp(...)          ← 脚本资源没有 script_instance
      ├─ get_gdtype().members().getptr(...)   ← 只认 MethodBind；无 _extension 钩子
```

证据：

- `Object::callp` 全文（`object.cpp:851-910`）无 `_extension` 引用；注释明写
  「extension does not need this, because all methods are registered in MethodBind」。
- `GDExtensionClassCreationInfo6`（`third/godot-cpp/gen/include/gdextension_interface.h`）
  字段为 `set_func / get_func / get_property_list_func / … / get_virtual_func /
  call_virtual_with_data_func`，**没有 call 钩子**。
- `Variant::callp` 只转发到 `obj->callp`（`variant_call.cpp:1401`）。
- godot-cpp 的 `Wrapped` 不暴露 `callp`。

对照：`GDScript::callp`（`gdscript.cpp:930-960`）能工作，是因为它是引擎内 C++ 类对
`Object::callp` 的虚函数覆盖；GDExtension 拿不到这条。

**但 `Object::callp` 的第二条分支查 `get_gdtype().members()`，而对脚本对象
`_gdtype_ptr` 指向 `GodotJSScript` 的 GDType**（`object.cpp:1405-1413` 的 `_reset_gdtype`）。
⇒ 若在 `GodotJSScript` 的 GDType 上注册了同名 MethodBind，`write_call` 路径就能命中（§6.3 选项 B）。

---

## 3. 数据模型

### 3.1 `StatelessScriptClassInfo` 扩展

```cpp
// src/runtime/bridge/jsb_class_info.h
namespace ScriptConstantKind {
enum Type : uint8_t { Value, Enum, Container };   // Container 需递归 make_read_only（§4.2）
}

struct ScriptConstantInfo {
    StringName name;
    Variant value;                    // 已转 Variant；枚举已归一化为 Dictionary{name:int}
    ScriptConstantKind::Type kind = ScriptConstantKind::Value;
};

struct ScriptStaticVariableInfo {
    StringName name;
    PropertyInfo details;             // 类型由首次求值决定（§5.2）
};

struct StatelessScriptClassInfo {
    ...
    HashMap<StringName, ScriptConstantInfo> constants;
    HashMap<StringName, ScriptStaticVariableInfo> static_variables;   // 仅注解标记的（D2）
    ...
};
```

**不新增 `static_methods`、不动 `ScriptMethodInfo`**：静态函数不做（Q1=(c) / R8.5），数据模型里不为其留位置
（也就不存在要不要加 `argument_count` / `is_vararg` 的问题）。

### 3.2 与既有结构的边界

- `properties` = `@export` 实例属性（`ClassProperties` 符号驱动），**不动**。
- `static_variables` = 类对象上的静态自有属性，**且被注解标记**（D2）。
- 二者不互相转换：实例属性在 `prototype` 上，静态属性在 `class_obj` 上，JS 语义天然区分。

### 3.3 `GodotJSScript` 侧

- `_get_constants()`：由 `script_class_info_.constants` 展开为 `Dictionary`，**只报自有、不合并
  `base` 链**（对齐 `GDScript::get_constants`，`gdscript.cpp:911-917`）。
  **不含函数名**（§6.2 的强制约束）。
  **2026-09-26 修正**：原实现沿 `base` 链合并（理由「对齐 `get_script_property_list` 的递归形态」），
  但 `Script::get_script_constant_map()`（`script_language.cpp:106-112`，**ClassDB 绑定** `:181`）
  dump 的就是这个返回值 ⇒ 合并会让该 API 对派生脚本偏离 GDScript（实测派生 GDScript 的 map 只有
  自有常量）。**`Script` 层四个反射钩子（`_get_constants`/`_get_members`/`_has_method`/
  `_get_method_info`）一律只报自有**，基类链由调用方按需自己走（`has_method`/`get_method_info`
  的实例侧例外见 `jsb_script_instance.cpp`：`Object::has_method` 不走链，故实例层自己走）。
  实测边界（`const` 基 vs `var` 基）见 `qa-q1-q7.md` Q6 与 `prd.md` A7。
- `_get()` 查找顺序（对齐 `GDScript::_get`，`gdscript.cpp:975-1010`）：
  `constants` → `static_variables` → 基类链（`base`）递归。**无静态函数分支**（R8.5）。
- `_set()` 只查 `static_variables`（对齐 `gdscript.cpp:1023-1053`）。
- `_get_property_list()` 输出 `script/source` + 各 `static_variables` 的 `PropertyInfo`
  （对齐 `gdscript.cpp:1055-1081`；`script/source` 项见 `:1057`）。
- `_get_members()`：实例成员名集合（R7.1）。
- `_get_method_info()`：**只对 `script_class_info_.methods` 中的名字返回 `MethodInfo`，其余返回空
  `Dictionary`**（R5.1）。这是 R2 的前置 —— 现有实现对任意名字都返回非空，会让常量与信号永远解析不到。

---

## 4. 成员识别规则

### 4.1 枚举源：`class_obj` 的自有属性

`Object.getOwnPropertyNames(class_obj)` + `Object.getOwnPropertyDescriptor(class_obj, name)`。

必须用 `getOwnPropertyNames` 而非 `Object.keys` / `for...in`（`[实测]` 阶段 0.1，
`.agent_tmp/tsprobe/`（任务收尾已清理））：TS 产物里 **`static` 字段是可枚举的**（`static readonly X = 1`、
`static arrow = () => {}`、`static BIG = 123n`、`static UNDEF = undefined` 均 `enumerable = true`），
而 **`static` 方法是不可枚举的**（`static sf() {}`、`static async asf() {}` 均 `enumerable = false`）。
`Object.keys` 会漏掉全部方法，`for...in` 会带上字段 —— 两者都不完整。

处理规则：

1. **显式跳过 `length` / `name` / `prototype`**（前两者 `enumerable = false` 但
   `getOwnPropertyNames` 会返回；`prototype` 是实例原型，另有枚举路径）。`[实测]`
2. descriptor 无 `get`/`set`（data 属性）才处理；accessor 属性跳过。
3. **静态成员不继承**：`class D extends B` 时 `getOwnPropertyNames(D)` 只含 D 自己的成员
   （`[实测]`：`hasOwn(D, "bs") === false`）⇒ 基类链只能靠 `base` 递归（§3.3），
   不能指望 `getOwnPropertyNames` 取全链。
4. 私有静态字段（`static #priv`）不出现在 `getOwnPropertyNames`，`getOwnPropertySymbols` 为空
   ⇒ 天然被忽略。`[实测]`
5. 枚举对象与 JS 数组的区分：枚举产物 `Array.isArray` = **false**、`constructor.name` = `"Object"`
   ⇒ **不能**用 `Array.isArray` 判枚举（§4.3）。`[实测]`

### 4.2 「真的是不可变常量」怎么判定（回答 D1）

**结论先行：运行时无法自动判定。** TS 的 `static readonly X` 编译后仍是可写 data 属性；
模块级 `const` 与 `let` 产物同形。因此：

**(a)** 「是不是常量」由作者用注解声明（D1/D3）；
**(b)** C++ 侧只做「准入判定 + 能施加的只读」，不假装能自动判定；
**(c)** 文档明确说明我们冻结到什么程度。

**准入规则（2026-09-25 用户拍板）：JS 基础值 + enum + 容器（`GArray`/`GDictionary` 包装）。**

判据用用户给出的那条、可直接验证的：**这个值在 JS 侧能不能被原地改？**
JS 的 `const` 只冻结**绑定**，不冻结对象 —— `const vec2 = new Vector2(1, 1)` 之后 `vec2.x = 0`
合法且生效。凡带对象身份的 JS 值，作者都能在 JS 侧原地改它；把这类值叫"常量"就是把可变性
卖成不可变性 ⇒ **一律不支持**。

（`Variant::is_type_shared(t)` 是引擎侧的**机制说明**、不是准入判据：它对
`OBJECT`/`DICTIONARY`/`ARRAY`/全部 `PACKED_*_ARRAY` 返回 `true`（`core/variant/variant.cpp:3473-3493`），
因为这些类型的 Variant 载荷是指向引用计数对象的**指针**，而值类型是内联 8 字节载荷
（`variant.h:255-267`）。它只用来解释「容器/对象为何与 JS 侧共享 `_p`」。）

| 值 | 准入 | 理由 |
|---|---|---|
| JS 基础值 `number`/`string`/`boolean`/`bigint`/`null` → `NIL`/`BOOL`/`INT`/`FLOAT`/`STRING`/`STRING_NAME` | ✅ | 载荷即值本身，JS 侧**没有任何可变内容**；`_get` 返回副本 |
| `enum` | ✅ | 产物是可改的普通对象，但我们**不用它本身**：从它的自有可枚举属性**新建** `Dictionary`（§4.3）并 `make_read_only()` ⇒ 暴露出去的那份由我们独占 |
| `GArray` / `GDictionary` **包装对象** | ✅ | 与 JS 侧**共享 `_p`** ⇒ 递归 `make_read_only()` 后**两侧**都改不动 ⇒ 真常量（详见下方「容器」段） |
| 其余一切带对象身份的 JS 值 —— `new Vector2(1,1)` / `new Color(…)` / **JS 字面量** `[1,2,3]` / `{a:1}` / `new Node()` / `Packed*Array` / `Callable` / `Signal` | ❌ | **JS 侧可原地改**（`v.x = 0`、`arr[0] = 9`、`obj.a = 1` 全部合法且生效）⇒ 保证不了常量性，就不叫它常量。字面量另有一条：**转不成 Variant**（§0.2）|
| `undefined` / `symbol` / 函数 | ❌ | **`typeof` 预筛阶段即剔除**（R1.2，`js_to_gd_var` 根本不会被调用）。`undefined` 必要理由：`static X;` 未初始化即 `undefined`，放行会让每个只声明未赋值的静态字段都变成 NIL 常量 |

**Godot 值类型（`Vector2`/`Color`/…）为什么也排除**（最反直觉的一条，必须写明）：
`is_type_shared == false` 确实让我们拿到**独立的值快照**（JS 侧改 `V.x` 不会改到我们那份），
但**那个 JS 成员本身仍可写**：TS 里 `MyClass.V.x = 0` 合法且生效，而 GDScript 侧
`SomeScript.V` 永远停在解析那一刻 ⇒ **同一成员两种可变性、且静默分叉**。
这不是"常量"，是"两个不同的东西同名"。要跨语言暴露这类值 → `@bind.exposed.shared()` 静态变量
（R3/R6）：语义上它是**静态变量**、不承诺不可变。

**共享 `_p` 是容器得以成立的原因**：`ARRAY`/`DICTIONARY`/`PACKED_*`/`OBJECT` 的 Variant 与 JS 侧
那个对象**共享 `_p`** ⇒ ① 不施加只读则 JS 改它 = 改我们的"常量"（v1 据此排除）；② **施加只读则
两侧同时冻住**（标志在共享 `_p` 上）—— ② 正是我们要的效果。`PACKED_*` 缺 `make_read_only()` API、
`OBJECT` 是引用语义 ⇒ 这两类没有 ② 可用，仍排除。

**实现用白名单，不用排除清单**：`js_to_gd_var` 成功后只放行 **8 种** Variant 类型 ——
`NIL` / `BOOL` / `INT` / `FLOAT` / `STRING` / `STRING_NAME`（基础值）+ `ARRAY` / `DICTIONARY`
（容器；枚举走 §4.3 的独立分支）。`Vector2`/`Color`/`Packed*`/`Object`/`Callable`/`Signal` 全部落
`default:` 被剔除 —— 这样不依赖任何"排除清单"的完备性。**注意这些包装都会转换成功**（命中
`InternalFieldCount == IF_VariantFieldCount` 分支原样返回 Variant）⇒ 不能只靠「转换失败」过滤。
**`ARRAY`/`DICTIONARY` 只能来自 `GArray`/`GDictionary` 包装或我方新建的枚举**：纯 JS 数组在无提示
路径返回 `false`、纯 JS 对象在任何路径返回 `false`（§0.2）⇒ 白名单本身即等价于"必须是包装对象"。

> `NODE_PATH` / `RID` 亦落 `default:`。它们同为值类型、Godot 侧无原地修改 API ⇒ 按判据其实**满足**，
> 但必须由 JS 包装对象构造、实用价值低，为保持白名单最小而一并排除（理由同 `prd.md` R2.3）。

**容器：纳入（2026-09-25 用户拍板；取证见 `research/q1q2-freeze-and-signatures.md` §Q1）**：
- **值类型保持排除**（实测）：`Object.freeze(实例)` / `Object.freeze(prototype)` 都挡不住
  `inst.x = 42` —— `x` 是 prototype accessor（`jsb_v8_class_builder.h` 的 `Property()` →
  `prototype_template_->SetAccessorProperty`），可变状态在 `IF_Pointer` 指向的 Variant 里；
  freeze 只作用于 own property。唯一有效的是给实例加 own 不可写属性 ⇒ 要改用户对象、且注解时机
  早于实例创建 ⇒ 不可行。
- **`GArray`/`GDictionary` 包装纳入常量**：它们经 `js_to_gd_var` 的 `IF_VariantFieldCount` 分支
  **只拷贝 Variant 外壳** ⇒ 与 JS 侧共享 `_p` ⇒ 对转换结果调 `make_read_only()` 即**两侧同时**只读：
  - GDScript 侧：`Array::set`/`push_back`/`clear`/`insert`/`erase`/`fill`/`sort`/`resize`… 全部
    `ERR_FAIL_COND_MSG(_p->read_only, …)`（`array.cpp:123,281,288,305,324,339,346,503,516,732,742,768,773,780,791,801,884`）；
    `Dictionary` 同构（`dictionary.cpp:203,251,305,311,328,333,641`）。
  - JS 侧：容器 proxy `set` trap → `target.set(num, proxy_unwrap(v))`（`jsb.inject.ts:227`）→
    同一 `Array::set` → 同一 guard。
  - **不需要 `Object.freeze`**；对现有容器 proxy 调 freeze 反而抛 `TypeError`
    （`isExtensible→true` + `preventExtensions→true` 不自洽）。
  - 限制：① 须 `GArray.create`/`GDictionary.create` 构造（字面量转不成 Variant；提示重载新建 `_p`，
    保护不了原数组）；② JS 侧写入开始报 engine error（行为突变）；③ 深层须**递归**施加；
    ④ `Packed*`（无只读 API）、`OBJECT`（引用）仍排除。

**`PACKED_*` 的三条附加取证**（与上述规则同向、独立成立，均已实测）：

1. **GDScript 自身就不允许作 const**：`const P := PackedInt32Array([1,2,3])` 直接 parse error
   （探针 `p_p3.gd`：`Assigned value for constant "P" isn't a constant expression.`）⇒ 无参照行为可对标。
2. **原地改拦不住**：`is_type_shared() == true` ⇒ GDScript 索引写是原地改、不走写回，`_set` 拦不到。
   实测 `p_sv.gd`（`static var` 容器、未施加只读）：`h.P[0] = 99` 让存储从 `[1, 2, 3]` 变成 `[99, 2, 3]`。
3. **没有只读可施加**：`Packed*Array` 无 `is_read_only()` / `make_read_only()`（`extension_api-4-7.json` 核对）。

**触发条件只有注解**（D2 / R4.4）：未注解的 Variant 兼容静态属性**一律忽略**，不存在「靠
`Object.isFrozen` 自动识别」的第二条路径 —— 那会与注解门控形成两套并行约定，且
`Object.isFrozen` 只反映运行期状态，不表达作者意图。

**基础值常量为什么是诚实的常量**：`NIL`/`BOOL`/`INT`/`FLOAT` 载荷即值本身；`STRING`/`STRING_NAME`
虽走堆（`needs_deinit` 为真）但 `String`/`StringName` **没有任何原地修改 API** ⇒ JS 侧没有可改的
东西。GDScript 侧写入被 `_set` 拒绝（`v1.gd`：`h.N = 5` 后 `BEFORE`/`AFTER` 完全相同）。

**必须写明的边界**（基础值也躲不掉的一条）：TS 的 `readonly` 编译后被擦除，所以
`MyClass.MAX = 999` 在 **JS 侧仍合法**。我们的 `constants` 是**解析期的值快照** ⇒ JS 侧改了，
GDScript 侧 `SomeScript.MAX` **仍是解析那一刻的值**（不会跟着变，也不会报错）。
即：**不可变性只对 GDScript 侧成立**；JS 侧那个属性是普通可写字段，我们管不到（运行期无法拦截
`class_obj` 上的属性写入而不破坏 JS 语义）。这与"暴露出去的是常量"不冲突（GDScript 侧读到的值
恒定），但必须文档写明，避免作者以为两侧联动。要"JS 改、GDScript 跟着变"→ `@…shared()` 静态变量。

**只读标记的唯一用途**：枚举 `Dictionary`（我方新建、不与任何 JS 可达对象共享）。

支撑实测事实：

1. `Variant::is_read_only()` 只对 `ARRAY`/`DICTIONARY` 返回有意义值
   （`core/variant/variant.cpp:3500-3510`）；`Array::make_read_only()` 是**浅层**（`array.cpp:936`）。
   `[实测]`（探针 `.agent_tmp/gdcallable/ro.gd`，`make_read_only` 运行期语义）：

   | 操作 | `is_read_only()` |
   |---|---|
   | `var a := [1,2,3]` 初始 | `false` |
   | `a.make_read_only()` 后 | `true` |
   | `a.duplicate()` | **`false`** ← **只读标记不随副本传递** |
   | `a.duplicate(true)` | **`false`** |
   | 浅层：`nested.make_read_only()` 后 `nested` / `nested[0]` | `true` / **`false`** |
   | `Dictionary` 同上（`make_read_only` → `true`；`duplicate()` → `false`） | 同 |
   | 只读 Array 放进 `Dictionary` 后取出 | 仍 `true`（标记随 `_p` 走） |

   ⇒ `_get_constants()` 返回的 Variant 副本与存储共享 `_p`，**只读标记保留**
   （这正是枚举 `Dictionary` 需要的：我方新建那份被冻结后，取出的副本仍是只读）；
   GDScript 侧若显式 `duplicate()` 则拿到可写副本（与 GDScript 自身 `const` 行为一致）。
   `make_read_only` 在本仓目标 API 4.7 内可用（`third/godot-cpp/gdextension/extension_api-4-7.json`
   的 `Array`/`Dictionary` 均含该方法；godot-cpp 绑定 `array.hpp:224-225`、`dictionary.hpp:148-149`）。
2. GDScript `const` 容器的冻结深度**取决于取值形态**（`[实测]`）：
   - **同脚本内裸标识符**：`const NESTED := [[1,2],[3]]` 的 `NESTED[0].append(99)` **成功**，
     内层 `is_read_only()` = false（`p_i.gd`）—— 编译器把它当编译期常量字面量，内层不冻结。
   - **经脚本对象取常量**（`preload(...)` / `load(...) as Script`）：`p_ro4.gd` 实测
     `NESTED ro=true`、`NESTED[0] ro=true`、`D["k"] ro=true`，内层 `append` **失败** ⇒ **深层冻结**。
   见 `research/phase0-findings.md` §0.6。
3. GDScript 的 `static var` 的 `Array`/`Dictionary`/`Packed*` **都不是** read-only
   （`[实测]` `p_e.gd` rc=0：`H.A[0]=111`、`H.A.append(4)`、`H.D["a"]=222`、`H.P[0]=333` 全部成功）。

⇒ 这三条自 2026-09-25 起降级为**参照记录**：容器常量已整体取消（R2.3），我方不再暴露任何容器
   常量，"浅层 vs 深层"的分歧面随之消失（枚举值只有 `int`/`String`，无内层容器可写）。

### 4.3 枚举识别

TS `enum E { A, B }` 编译为带反向映射的 IIFE 对象（`[实测]` 阶段 0.1：
`Object.getOwnPropertyNames(E)` = `0,1,A,B`；`typeof E` = `object`；`Array.isArray(E)` = **false**；
`E.constructor.name` = `"Object"`）。识别规则：

1. 值是对象，且**不是** Godot 包装对象（无 `ProxyTarget` 符号，`InternalFieldCount() != IF_VariantFieldCount`）
   —— 判据必须是**内嵌字段数**，不能用 `Array.isArray`（枚举产物并非数组）；
2. 全部自有可枚举值为 `number` 或 `string`；
3. 存在「数字键 → 字符串值」且「字符串键 → 相同数字值」的反向映射对
   （`E[E["A"] = 0] = "A"` 产生 `E[0] === "A"` 且 `E["A"] === 0`）。

数字枚举有反向映射、**字符串枚举没有**（`[实测]`：`enum ES { X = "x", Y = "y" }` 只产 `X,Y`）
⇒ 规则 3 对字符串枚举不成立。**定稿处置**：字符串枚举（全部自有可枚举值为 `string`、键全为标识符）
**作为普通 `Dictionary` 常量暴露**（值类型 `String`），不做枚举归一化。理由：
① 信息不丢失；② `_get`/`_get_constants` 的取值路径与普通 Dictionary 常量完全一致，无额外代码；
③ 不做额外识别就退化为「普通对象」而被 R1.2 静默忽略，反而更差。
GDScript 侧 `SomeScript.ES.X` 得到 `"x"`（`String`），与 GDScript 的 `Dictionary{name:int}` 枚举形态
不同 —— 文档说明。混入（既有 `number` 又有 `string` 值）的对象按 R1.2 忽略。

> 归一化产物是 R2.3 两类容器常量之一（另一类是 `GArray`/`GDictionary` 包装）：它由我方从 JS 枚举
> 对象的自有属性**新建**，不与任何 JS 可达对象共享 `_p` ⇒ 既是真常量，也可安全 `make_read_only()`。

> **GDScript 侧的取值形态限制（R2.5，已实测）**：**直接赋值** `SomeScript.E = other` /
> `SomeScript.N = 5` 有**编译期**保护（`get_constants()` 命中即经 `make_builtin_meta_type` 置
> `is_constant`，`gdscript_analyzer.cpp:4396`）。但**嵌套形式永不折叠** —— `reduce_subscript`
> 的折叠路径因基是 metatype 而整体跳过（见 R2.5 第 3 步），节点级 `is_constant` 保持 `false`
> ⇒ **只**剩运行期 guard。**实测结论**：`SomeScript.ARR[0] = 7` **编译期不报**（运行期
> `Invalid assignment on read-only value`）；**枚举成员必须用索引访问** ——
> `SomeScript.E.EA`（点访问）**编译期报** `Cannot find member "EA" in base "Dictionary"`，
> 须改用 `SomeScript.E["EA"]`。属**引擎侧**机制，扩展侧无法修正。

满足则归一化为 `Dictionary{ "A": 0, "B": 1 }`（丢弃数字键，与 GDScript 一致 ——
`gdscript_compiler.cpp:2949` 的 `constants.insert(name, enum_n->dictionary)` 只含名字键；
`[实测]` `H.E` 打印 `{ "EA": 0, "EB": 1 }`，`H.E.EA` 与 `H.E["EA"]` 均为 `0`）。

`const enum` 被编译期内联擦除，运行期不存在对象 ⇒ **无法支持**（R8.1）。

### 4.4 静态函数识别（**本任务不实施**，仅存档）

`class_obj` 自有 data 属性且 `typeof value === "function"` ⇒ 静态函数。无需注解（运行期可判定）。
参数个数取 `Function.prototype.length`（§6.4）。

> Q1=(c) 后本任务**不识别、不暴露**静态函数：枚举 `class_obj` 自有属性时，
> `typeof value === "function"` 的值**直接忽略**（R1.2），既不进 `constants` 也不进
> `static_variables`。本节保留为将来实施的依据。

### 4.5 备选（未采用）

- **读源码正则/解析器区分 const/static**：被 `09-06-lowprio-tree-sitter-ast` 评估否决；
  且 `_get_constants` 会被 `EditorFileSystem` 后台线程调用，源码解析引入线程与缓存复杂度。
- **TS 类型信息（`static readonly` 标记）**：编译后丢失，不可靠（§4.2）。

---

## 5. 常量与静态变量（注解门控）

### 5.1 门控规则

| 情形 | 处置 |
|---|---|
| 值可转 Variant，且类型属 **§4.2 准入面 ✅**（JS 基础值 `NIL`/`BOOL`/`INT`/`FLOAT`/`STRING`/`STRING_NAME` + 容器 `ARRAY`/`DICTIONARY` + 我方新建的枚举 `Dictionary`） | **常量**，进 `constants` 与 `_get_constants()`；容器**递归** `make_read_only()` |
| 值可转 Variant，但类型属 **§4.2 排除面 ❌**（`Vector2`/`Color` 等值类型、`PACKED_*` / `OBJECT` / `CALLABLE` / `SIGNAL`） | **忽略**。这是**按类型**的硬排除，**注解也救不回来**（JS 侧那个成员可原地改，且无两侧冻结手段 ⇒ 保证不了常量性） |
| 值可转 Variant，类型为 OBJECT（**即使**被 `@…const()` 标记） | **忽略**。对象恒可变（`SomeScript.obj.x = 1` 合法）⇒ 保证不了常量性，**注解也救不回来**；需要 → `@…shared()` 静态变量 |
| 值可转 Variant，被 `@…shared()` 标记 | **静态变量**，进 `static_variables`，走 §5.2 的 C++ 权威存储 |
| 值可转 Variant，但未标记（D2） | **忽略**，不产生成员 |
| 值不可转 Variant | 忽略（R1.2/A5） |

### 5.2 跨环境静态变量：方案 A（C++ 权威存储）

**问题**（已核实）：`script_class_info_` 是**首次加载该模块的环境**的快照
（`jsb_script.cpp:499-501` 的拷贝赋值）；各 JS 隔离区独立 import 同一模块，同名类是完全无关的对象；
环境有线程归属（`EnvironmentStore::access()` 按当前 isolate / 线程解析，
`jsb_environment.cpp:83-172`）。⇒「一个静态变量在所有环境里自动一致」在架构上不存在。

**方案 A（采用）**：被 `@…shared()` 标记的静态属性，在类解析时**替换为 JS 访问器**
（`class_obj` 上 `defineProperty` 的 get/set），其实现调用桥接函数
`jsb.internal.get_static_shared(module_id, name)` / `set_static_shared(module_id, name, value)`。
C++ 侧持有**进程级、互斥保护**的
`HashMap<StringName /*module_id*/, HashMap<StringName, Variant>>`：

- 初值：首次解析该类时从该环境读到的 JS 值转换后写入；后续环境解析**不覆盖**，只装访问器。
- 于是 JS 内读写 → 访问器 → C++ → 所有环境读到同一个值；GDScript `SomeScript.x` →
  `_get`/`_set` → 同一份 C++ 存储。读写不要求目标环境在运行，也不需要跨线程推送。
- **转换路径待定（阶段 3 必须先解决，§10 已记）**：静态变量**不**受 R2.3 白名单约束（它本就是
  可变值），但跨桥的 `Variant` 转换仍走 `js_to_gd_var` ⇒ 若声明成 `Variant` 参数则走**无提示**路径，
  而该路径对 **JS 原生数组返回 `false`**（`research/phase0-findings.md` §0.2）⇒
  `@…shared() static ARR = [1,2,3]` 会**装不进去**。需要走带 `Variant::ARRAY` 提示的重载，
  或要求作者用 `GArray.create` / `GDictionary.create` 包装。**纯 JS 对象字面量在任何路径都转不成
  `Dictionary`** ⇒ 那类静态变量必须由作者用 `GDictionary.create({...})` 构造。方案在阶段 3 定，
  且必须实测。

**代价与风险**（必须处理）：

1. **Orphan StringName**：进程级 map 持有 `StringName` 键，必须在
   `GodotJSScriptLanguage::_finish()` 显式 `clear()` —— 这正是
   `.trellis/spec/godotjs-ext/test/index.md`「缺陷 A」的形态（`_finish` 里已有
   `GodotJSScriptInstanceBase::free_temporary_property_list_pool()` 的同型先例，
   `jsb_script_language.cpp:256-259`）。纳入 A9。
2. **可观察的行为改变**：`Object.getOwnPropertyDescriptor(MyClass, "x")` 从 data 变 accessor。文档说明。
3. **初值时机**：初值 = 首次类解析时求值；文档约束「被标记的静态属性初值必须能在类定义处静态确定」。
4. **锁**：读写需一把互斥锁；与 `GodotJSScriptLanguage::mutex_` 的嵌套顺序需避免死锁（§10）。

#### 5.2.1 值生命周期：刻意独立于 `GodotJSScript`（2026-09-26 用户拍板）

**决策：保持进程级存储、生命周期独立于 `GodotJSScript` 对象。** 曾评估"把槽位生命周期绑到脚本对象"
（`~GodotJSScript` 时释放），**否决**，两条理由：

1. **`GodotJSScript` 析构时清掉静态变量是更大的问题**。`GodotJSScript` 是普通 Resource，引用计数
   归零即析构（`jsb_script.cpp:47-56`，仅 `remove_from_list`）。而 `static var` 的语义是**类级别**
   的状态，作者会写「关掉这个场景再回来，值还在」。绑到对象上会让"最后一个引用释放"变成
   静默丢值，且没有任何办法观测到 —— 比"多活一会儿"危险得多。
2. **纯 JS import 是真实应用场景**：类解析发生在**没有任何 `GodotJSScript`** 的时候 ——
   worker 隔离区 `env->load(impl->path_)`（`jsb_worker.cpp:449`）、编辑器桥
   `env->load(path, &module)`（`jsb_bridge_table.cpp:137`/`:163`）、以及 JS 侧 `import` 触发的模块解析。
   那一刻**只有 `module_id`**，若槽位必须挂在脚本对象上，这些场景下的 `@…shared()` 成员
   将**没有可写的地方**。

**跨环境一致性不依赖"进程级"这个属性本身**，而是靠"所有环境 + GDScript 读同一个持有者"：
`GodotJSScript` 本身也是进程级共享的 Resource（REUSE 直接返回缓存对象，
`jsb_resource_loader.cpp:117-122`），把它当持有者同样能一致 —— 所以上面第 1、2 条才是真正的判据。
`script_class_info_` 不带环境信息（`jsb_script.h:84-90` 注释：*can be used without an environment,
because we want GodotJSScript can be shared between threads*）正是它能当共享持有者的**前提**。

**与 GDScript 的已知偏离（接受）**：GDScript 的 `static var` 值就是脚本对象的实例字段
（`gdscript.h:94-95`），随 `~GDScript()` → `clear()`（`gdscript.cpp:1534` / `:1461-1462`）释放；
我方值活到 `GodotJSScriptLanguage::_finish()`（`jsb_script_language.cpp:264`）。实测差异
（`.agent_tmp/svperobj.log`：同源两对象 `A_INIT=100 A_AFTER_SET=5 B_INIT=100 A_STILL=5`）。
**可观测性极低**：每路径实际只有一个 `GodotJSScript`（`CACHE_MODE_IGNORE`/`IGNORE_DEEP` 被
`jsb_resource_loader.cpp:110-116` 显式降级为 REUSE；实测 `REPLACE`/`IGNORE` 也返回同一对象，
`.agent_tmp/svobj.log`）。**取舍：宁可多活，不可静默丢值。**

备选方案 B（环境本地只读快照）：`_set` 恒 `false`，零新增进程级状态、无 Orphan 风险，
但 GDScript 只能读到「首次加载环境求值出的初值」。保留为回退路径。

### 5.3 保护机制（写入常量的后果与只读施加）

- **容器（`ARRAY`/`DICTIONARY`）作常量，靠递归 `make_read_only()` 两侧冻结**（R2.3）：
  它们与 JS 侧那个对象**共享 `_p`** ⇒ 施加只读**同时**挡住两侧写入。实测 `p_e.gd`（GDScript
  `static var` 容器，**未**施加只读）：`H.A[0]=111`、`H.A.append(4)`、`H.D["a"]=222`、
  `H.P[0]=333` **全部成功** ⇒ 不施加只读则容器内容可被任意改写，所以这一步是必需的。
  递归是因为 `make_read_only()` 是**浅层**（§4.2、`research/phase0-findings.md` §0.3/§0.6），
  而 GDScript `const` 经脚本对象取时是**深层**冻结。
- **常量支持面 = JS 基础值 + enum + 容器**（R2.3）：**Godot 值类型（`Vector2`/`Color`/…）不作常量**。
  它们 `is_type_shared == false`、我们的副本确是独立快照，但**JS 侧那个成员本身仍可写**：
  `MyClass.V.x = 0` 合法且生效，而 GDScript 侧 `SomeScript.V` 停在解析那一刻 ⇒ 同一成员两种可变性、
  静默分叉。这不是常量，是"两个不同的东西同名"。需要 → `@…shared()` 静态变量。
- **基础值常量的保护**：引擎对基础值/值类型**没有**只读概念（`Variant::is_read_only()` 只对
  ARRAY/DICTIONARY 有意义）。保护来自两条不变式：
  1. **常量不进 `_get_property_list()`**；
  2. **`_set` 对常量名返回 false** ⇒ GDScript 侧 `H.N = 5` 失败（见下表 `H.N` 行）。
- **`PACKED_*` / `OBJECT` 仍属排除面**（§4.2）：`PACKED_*` 与 JS 侧共享 `_p` **但无
  `make_read_only()` API**（`extension_api-4-7.json` 核对）⇒ 没有"两侧同时冻"的手段；
  `OBJECT` 是引用语义 ⇒ 只能冻引用、冻不了对象内容。`CALLABLE`/`SIGNAL` 另有 `.call()` 编译期
  被拒的理由（§6.2）。
- **实测的失败形态**（探针 `y1`–`y5`、`w1`–`w4`、`z1`–`z6`）：

  | 写法 | `const H := preload(...)`（const 基） | `var h = load(...)` / 未标注类型的参数 |
  |---|---|---|
  | `H.V.x = 9` | **编译期** `Cannot assign a new value to a constant.`（`y3`） | **运行期** `Invalid assignment of property or key 'V' …`（`w2`/`z3`） |
  | `H.ARR[0] = 7` | **编译期** 同上（`y4`） | **运行期** `Invalid assignment on read-only value (on base: 'Array')`（`z1`） |
  | `H.N = 5` | **编译期** 同上（`y5`） | **运行期** `Invalid assignment of property or key 'N' …`（`z2`） |

  两条路径**都改不动存储值** —— 探针 `v1.gd` 直接取证：同一个 `preload` 得到的脚本上连续执行
  `h.V.x = 9` / `h.ARR[0] = 7` / `h.N = 5`（各自放在独立辅助函数里，错只终止该函数），
  打印 `BEFORE V=(3.0, 4.0) ARR=[1, 2, 3] N=7` → `AFTER` **三项完全相同**，3 条脚本错误，
  `rc=0`、`STEP-OK` 打印。

  > 该表是**GDScript 侧参照行为**（用来定位失败形态）。按 R2.3，`H.N` 与 `H.ARR[0]` 两行
  > 对应我方暴露的常量类型；`H.V.*`（值类型）仅作机制参照。
  > 结论"改不动存储值"对基础值与容器同样成立，且**不需要**进程挂起来"保护"。
- **纠正早期结论**：先前记录的「写入只读容器会让进程**永久挂起**」是**探针假象** —— 那批探针把
  `quit(0)` 写在 `_initialize()` 末尾，而运行期脚本错误会**终止当前 GDScript 函数**，于是 `quit`
  永远没执行、进程一直空转。实测反证：`z1`–`z6` 把错误放进被 `_initialize` 调用的辅助函数，
  `_initialize` 继续执行并正常 `quit`（全部 rc=0、`STEP2-OK` 打印）；`y6`–`y8` 把错误放进 Node 的
  `_ready`，同样只终止 `_ready`（rc=0）。⇒ 真实后果是**打印一条脚本错误 + 终止当前函数**，
  与任何其它脚本错误同级，不是挂起。
- 处置：**基础值靠 `_set` 拒绝写常量 + 常量不进 `_get_property_list()`；容器与枚举靠
  `make_read_only()`**（容器**递归**施加）。**两侧冻结的代价必须文档写明**：容器常量施加后
  **JS 侧写该容器也会报 engine error** —— 这是"真不可变"的必然结果，不是缺陷。
  与 GDScript「`const` 深层冻结」的分歧因此**闭合**（递归施加后我方也是深层）。
  `research/phase0-findings.md` §0.6 的浅层/深层对照保留为机制取证。

### 5.4 GDScript 参照行为（实测汇总）

> 下表是 **GDScript 自身**的行为，用于对标；我方支持面**更窄**（JS 基础值 + enum + 容器，
> R2.3）：`H.V`（值类型）**我方不提供**；`H.ARR` / `H.D`（容器）仅在用 `GArray.create` /
> `GDictionary.create` 构造时提供。

| 探针 | 结论 |
|---|---|
| `H.N` / `H.S` / `H.V.x` | 基础类型与 `Vector2` 常量可读（`p_s.gd`、`t1`–`t4`） |
| `H.ARR.is_read_only()` | `true`（`p_s.gd`） |
| `H.D.A` / `H.D["A"]` / `H.D.keys()` | Dictionary 常量可读、可调内置方法（`t1`/`t2`/`r2`/`r3`） |
| `H.ARR.size()` | `3`（`t3`） |
| `H.E` / `H.E.EA` / `H.E["EA"]` | 枚举为 `Dictionary`，两种取值都可（`p_s.gd`、`t5`/`t6`） |
| `load(...) as Script` 后 `s.get_script_constant_map()` | `{ &"D": {...}, &"ARR": [1,2,3], &"V": (3.0,4.0), &"E": { "EA": 0, "EB": 1 }, &"N": 7 }`（`t7`） |
| `H.get_script_constant_map()` 直连 | **parse error**：`Cannot call non-static function "get_script_constant_map()" on the class "..." directly`（`get_function_signature` 在 ClassDB 上找到 `Script::get_script_constant_map`，其 flags 无 STATIC） |
| `H.ARR[0] = 7`（GDScript const） | **parse error**：`Cannot assign a new value to a constant.`（`s5`） |
| `H.cb()`（`static var cb: Callable`） | **parse error**：`Member "cb" is not a function.` + `Name "cb" is a Callable. You can call it with "cb.call()" instead.`（`r4`，来自 `gdscript_analyzer.cpp:3815`） |
| `H.cb.call(4)` / `var f: Callable = H.cb; f.call(5)` | 可用；`f.get_argument_count()` = 1、`f.is_valid()` = true（`r5`/`r6`） |

---

## 6. 静态函数

### 6.0 结论：不做（用户决策 Q1=(c)）—— 用户点名的两个问题逐条作答

**(a) 该定位到哪个 JS 环境的静态函数？—— 架构上没有答案。**

`GodotJSScript` 是跨环境共享的 Resource，而它的成员快照 `script_class_info_` 是
`StatelessScriptClassInfo`（`jsb_script.h:90`），**刻意与环境解耦**：

- 它**不含** `js_class`（`v8::Global<v8::Object>` 只在 `ScriptClassInfo` 里，`jsb_class_info.h:233-236`），
  **也不含任何 `EnvironmentID`**；
- `load_module_immediately()` 里那句 `jsb::JSEnvironment env(get_path(), true)`
  （`jsb_script.cpp:475`）是**临时环境**：`JSEnvironment` 的构造函数就是
  `target_ = jsb::Environment::_access()`（`jsb_script_language.cpp:72-80`）—— 取不到"当前环境"
  且允许 shadow 时才新建 shadow 环境；函数在 `}` 处就把环境（乃至 shadow）析构掉，只留下
  "无状态"的类信息副本。

⇒ 调用静态函数时只能**按策略挑一个环境**，而三个候选各有反例：

| 策略 | 反例 |
|---|---|
| 调用者线程环境（`EnvironmentStore::access()` 无参版） | GDScript 从主线程冷调用时按 isolate → 线程回退解析（`jsb_environment.cpp:113-142`），命中哪个环境取决于当时谁在跑 |
| 主（Default）环境 | 脚本可能从未在主环境加载过；worker 里 import 过的模块状态与主环境不一致 |
| 首次加载环境 | `script_class_info_` 根本没记这个环境（见上），且它可能是**临时 shadow**，早已析构 |

`JSCallable`（`jsb_callable.h:33-70`）的 `env_id_` 之所以成立，是因为它绑的是**实例方法** ——
天然有"对象所在环境"。静态函数没有对象可绑，**不能照搬这个先例**。

**(b) 它改变的数据是否跨环境？—— 默认不会，且无法检测。**

- 被 `@…shared` 标记的静态变量走 §5.2 的 C++ 权威存储 ⇒ **写会跨环境**（这正是我们要的）；
- 但**普通模块级状态**（模块 `let`、闭包捕获、未标注的 `static` 字段）**每个 isolate 独立一份**
  （各环境各自 import 同一模块），静态函数改它只改自己那份 ⇒ 跨环境**静默分叉**；
- 我们**没有任何手段**在调用点判断"这个函数会不会碰非共享状态"。

⇒ (a) 只是"策略不唯一"，(b) 是"语义本身不确定"。按用户给出的判据（「这些你都没有好的方案的话
静态函数就不做了」），**采纳 (c)：不做**，仅文档说明（R8.5）。

> 以下 §6.1–§6.5 保留为「若将来要做」的实测记录与方案，本任务**不实施**。本任务在静态函数上的
> **唯一实施项**是 R5.1 —— 修 `_get_method_info()` 对任意名字都返回非空的既有缺陷（那是常量与
> 信号能被 `reduce_identifier_from_base` 解析到的前置，不属于静态函数功能本身）。


### 6.1 实测：GDScript 侧各调用形态

| 探针（`H` = `preload("res://h_k.gd")`，`sfn(a:int,b:int)` 为真 `static func`） | 结果 |
|---|---|
| `H.sfn(1, 2)` | **可用**（编译为 `OPCODE_CALL_METHOD_BIND`，走 `base_script->get_method_info`） |
| `H.sfn.call(1, 2)` | **可用**（`q2`） |
| `var f: Callable = H.sfn; f.call(1, 2)` | **可用**，返回 3（`q3`） |
| `(H.sfn as Callable).call(1, 2)` | **可用**（`q4`） |
| `H.call("sfn", 1, 2)` | **parse error**：`Cannot call non-static function "call()" on the class "res://h_k.gd" directly. Make an instance instead.`（`s1`） |
| `H.sfn` 的 `typeof` | `25` = `TYPE_CALLABLE`；`get_method()` = `"sfn"`；`get_argument_count()` = `2`（`q7`） |
| `Callable.call(null)`（裸类型调用） | **parse error**：`Cannot call non-static function "call()" on the class "Callable" directly.`（`p_u.gd`） |

⇒ 用户 D4 里「`SomeScript.call("static_method", ...)` 只能文档禁止」的判断**得到实测支持**，
而且比预想更早失败：不是运行期报错，而是**编译期就拒绝**（`get_function_signature` 在 ClassDB 上
找到 `Script::call`，其 `MethodInfo.flags` 不含 `METHOD_FLAG_STATIC`，于是命中
`gdscript_analyzer.cpp:3753` 的「Cannot call non-static function … directly」）。

### 6.2 分析期路线：走 `_get_method_info`，**绝不**走 `_get_constants`

由 §2.4 的两种 datatype 决定：

| 路线 | 分析期 datatype | `SomeScript.fn.call(1,2)` |
|---|---|---|
| `_get_constants()` 返回 `Callable` 值 | `make_builtin_meta_type(CALLABLE)`（`is_meta_type = true`） | **parse error**：命中 `!is_self && base_type.is_meta_type && !p_call->is_static` → 「Cannot call non-static function "call()" on the class "Callable" directly」（`p_u.gd` 实测） |
| `_get_method_info()` 返回 `MethodInfo` | `make_callable_type(info)`（`is_meta_type = false`，带完整签名） | **可用**（与 GDScript 静态函数同形，`q2` 已实测） |

⇒ **强制约束：静态函数名必须由 `_get_method_info()` 提供，且不得出现在 `_get_constants()` 里。**

> 此结论**推翻**了上一版 design/handoff 中「函数名必须同时进 `_get_constants()`」的推断 ——
> 那条推断忽略了 `make_builtin_meta_type` 会置 `is_meta_type = true`，从而让 `.call()` 被
> 「Cannot call non-static function … directly」拦下。实测（`p_u.gd`）已证伪。

副作用：`_get_method_info` 返回 `name` 会让 `reduce_identifier_from_base` 的 ② 分支命中，
因此 `SomeScript.fn`（作为值）在分析期就是 Callable —— 这正是我们要的。

`get_script_method_list()` 是否包含静态函数：GDScript 包含（`gdscript.cpp:304,1900` 遍历
`member_functions` 不筛静态），我们同样包含（仅影响 inspector/调试器呈现，低风险）。

### 6.3 运行期：两个选项

**选项 A（默认，不注册 MethodBind）**

- `_get()` 对 `static_methods` 命中时返回 `Callable(memnew(JSStaticFunctionCallable(...)))`。
  参考既有 `JSCallable`（`src/runtime/bridge/jsb_callable.h:33-70` + `.cpp:35-56`）；差异：需要按
  `module_id + 函数名` 定位，而不是 `ObjectCacheID`（或复用 `env->get_cached_function()`
  缓存函数对象 id，`jsb_environment.cpp:1034-1046`、`release_function` `:1591-1611`、
  `call_function` `:1968-2001`）。
- `_has_method()` 对静态函数返回 **false** ⇒ `get_function_signature` 的
  `base_script->has_method()` 失败 ⇒ `SomeScript.fn(...)` 在**编译期**报
  `Static function "fn()" not found in base "res://x.ts".`（`gdscript_analyzer.cpp:3844`）。
  干净的失败，无运行期意外。
- `_has_static_method()` 返回 true（语义正确；`Object::has_method` 靠它识别脚本资源的静态方法，
  `object.cpp:749-757`）。
- 用户可见形态：`SomeScript.fn`（取值）与 `SomeScript.fn.call(...)`（调用）。

**选项 B（运行期注册变参方法，D5 限定为注解子集）**

- 在 `GodotJSScript` 的 GDType 上运行期注册 MethodBind：
  `classdb_register_extension_class_method`（godot-cpp 已在 `third/godot-cpp/src/core/class_db.cpp:232`
  使用）→ `GDExtension::_register_extension_class_method`（`core/extension/gdextension.cpp:574-617`）
  → `ClassDB::bind_method_custom`（`class_db.cpp:1599-1621`）→ `GDType::bind_method`
  （`gdtype.cpp:150-153`，要求 `init_state == MUTABLE`）。
- 可行前提（已核实）：`init_state` 只在**出现子类注册**时转 FINALIZED（`gdtype.cpp:76-87`）；
  `GodotJSScript` 无扩展子类（全仓 `GDCLASS` 共 6 处，`jsb_script.h:58` 是唯一脚本类）。
  注册必须由注册 `GodotJSScript` 的那个 DLL 发起（runtime，`src/runtime/register_types.cpp:54`），
  因为 `_register_extension_class_method` 按 `p_library` 查 `extension_classes`
  （`gdextension.cpp:579`），且 `extension->is_reloading` 时早退（`:588-590`）。
- 分派：注册时**不加** `GDEXTENSION_METHOD_FLAG_STATIC` ⇒
  `GDExtensionMethodBind::call`（`gdextension.cpp:100-108`）传
  `extension_instance = p_object->_get_extension_instance()`，第一参数为 `method_userdata`。
  因此 `call_func(method_userdata /*= name StringName* */, instance /* GodotJSScript* */, …)`
  可据 `instance->script_class_info_.module_id` 定位模块。
- 用 `GDEXTENSION_METHOD_FLAG_VARARG`（`gdextension_interface.h` 的 flags 表：`VARARG = 16`）注册，
  绕过引擎侧元数校验，由我们的 `call_func` 自行报错。
- 风险（必须记录）：类命名空间污染（所有被标记的函数名都成为 `GodotJSScript` 的方法）；
  与 `Script`/`Object` 已有方法同名时 `GDType::bind_method` 失败（`gdtype.cpp:156-158`）需跳过并告警；
  注册时机（脚本加载后才知名字集合，需守卫 + 失败降级）；`ClassDB` **无注销 API**
  （`class_db.h` 只有 `unregister_extension_class`），热重载后无法清理已注册方法；
  跨环境调用落在哪个环境需定义（优先调用方环境，不可用则回退主环境或直接拒绝）。
- **本选项需用户再次确认后才做**（§6.5）。

### 6.4 函数参数问题（用户点名的核心难点）

引擎侧事实：

- `MethodInfo` 的 `arguments` 个数**在引擎的一般路径上**是分析期硬约束：`get_function_signature` →
  `function_signature_from_info`（`gdscript_analyzer.cpp:6121-6131`）逐项建 `par_types`，
  随后 `validate_call_arg`（`:6142-6189`）按 `arguments.size() - default_arguments.size()` 与
  `METHOD_FLAG_VARARG` 校验实参个数。
  **但我们的静态函数到不了这条路径**（`get_function_signature` 脚本分支要求
  `base_script->has_method()`，`:6076`；静态函数不进 `has_method`）——见下面的消费点表。
- `_get_script_method_argument_count()` 的返回只影响 `Object::get_method_argument_count`
  （`object.cpp:764-810`，被 `Object._get_method_argument_count_bind` 暴露，`:1964`），
  与 `Callable.get_argument_count()`（`core/variant/callable.cpp:186-193`）在 **custom callable**
  路径上会 override `CallableCustomBase::get_argument_count(bool &r_is_valid)`（`callable.h:163`）。

**先厘清「我们返回的 `MethodInfo` 到底被谁消费」**（`[实测]` 源码核对，避免过度设计）：

| 消费点 | 用到什么 | 我们的 `MethodInfo` 是否参与 |
|---|---|---|
| `reduce_identifier_from_base` ②（`:4366-4373`） | `method_info.name`、整体 → `make_callable_type()` | **参与**（这是 `SomeScript.fn` 取值的唯一来源） |
| `reduce_call` 的 `get_function_signature` 脚本分支（`:6076-6083`） | 需先过 `base_script->has_method()` | **不参与**：静态函数不进 `has_method` ⇒ 直接落空 |
| `reduce_call` 对 `.call(...)` 的解析 | `base_type.kind == BUILTIN`（Callable）→ 构造 `Callable` 哑值 → 取**内置** `call` 的 `MethodInfo`（`:5983-6006`） | **不参与**：用的是引擎 `Callable::call` 自己的签名与 VARARG |
| `validate_call_arg`（`:6142-6189`） | 上一行得到的 `par_types` / `default_args` / `vararg` | **不参与**（我们的 `args`/`flags` 到不了这里） |
| 编辑器签名提示（`gdscript_editor.cpp:2998-2999`） | `type.method_info.arguments` | **参与**（仅提示） |

⇒ 结论：`_get_method_info()` 的 `arguments` 长度与 `flags` **只影响取值类型与编辑器提示**，
不构成任何分析期参数校验。因此 `METHOD_FLAG_VARARG` 的取舍是低风险装饰性选择；
加它与否都不会改变 `.call(...)` 的可用性（`.call` 走引擎自己的 vararg 签名）。

方案：

1. `Function.prototype.length` 取 JS 定义参数个数（JS 语义保证不抛异常），写入
   `ScriptMethodInfo::argument_count`。
   **`[实测]` 阶段 0.1 的边界**（`.agent_tmp/tsprobe/desc.mjs`）：`length` 只数**首个默认值/rest 之前**
   的形参 —— `static sf(a, b)` → 2、`static sf0()` → 0、`static sfa(...args)` → **0**。
   故它是**下限**而非精确元数。因为它不进 `validate_call_arg`（上表），
   这一不精确**不会**导致任何误报；只是编辑器提示可能少几个参数位。
2. `_get_method_info()` 的 `args` 数组长度 = 该 `argument_count`；每项 `PropertyInfo` 的
   `type` 置 `Variant::NIL` 且 `usage |= PROPERTY_USAGE_NIL_IS_VARIANT`（`type_from_property`
   `:5916-5920` 对「NIL + is_arg」直接判 `VARIANT`），保证编辑器提示不产生类型错误。
3. `_get_method_info()` 置 `flags = METHOD_FLAG_STATIC`（让 `function_source_is_static` 正确，
   `:4371`）。**同时置 `METHOD_FLAG_VARARG`**：`length` 是下限，标 vararg 可让编辑器提示
   不因参数个数不足而显示错误；对分析期无副作用（见上表）。
4. `_get_method_info()` 对不匹配的名字必须返回**空 `MethodInfo`**（`name == StringName()`）——
   该函数在每次脚本标识符查找时都会被调用（`:4366`），返回带名字的 info 会污染解析。
5. `_get_script_method_argument_count()` 返回同一 `argument_count`（`Object::get_method_argument_count`
   会遍历 `Script` 链取第一个 valid 值，`object.cpp:794-805`；这是 `SomeScript.get_method_argument_count("fn")`
   的取值来源，`Object._get_method_argument_count_bind` 在 `:760-762`、暴露点 `:1964`）。
6. `JSStaticFunctionCallable::get_argument_count()` 返回同一值 ⇒ `f.get_argument_count()` 有真实值
   （对齐 `[实测]` `q7` 的 GDScript 行为：`H.sfn.get_argument_count()` = 2）。

### 6.5 状态（已拍板）

| # | 问题 | 结论 |
|---|---|---|
| **Q1** | 静态函数的调用形态 | **= (c) 不做**。理由见 §6.0（环境无解 + 数据跨环境不确定）；仅文档说明（R8.5） |
| **Q2** | 注解命名 | **= `@bind.exposed.const()` / `@bind.exposed.shared()`**（`@bind.static.const()` 已否决：静态 ⊉ 常量）。详见 §8.1 |
| **Q3** | 容器常量 | **已定案（2026-09-25 用户拍板）**：`GArray`/`GDictionary` **包装**作常量，靠**递归** `make_read_only()` 两侧同时冻结（共享 `_p`）；值类型仍排除（`Object.freeze` 实测无效）；`PACKED_*`/`OBJECT` 仍排除。见 §4.2 / §5.3 |
| **Q4** | 参数/返回值/信号参数的类型信息 | **本任务不做**（用户否决"每函数重写类型注解"）；取证留档 `research/q1q2-freeze-and-signatures.md` §Q2 |

---

## 7. 内部类（R8.2，明确不做）

用户设想的「同脚本内继承自 Godot 类型的其他类作为 `export default` 类的内部类型」，
对齐 GDScript 的 `class Inner:` —— 后者作为**常量**暴露 `Ref<GDScript>`
（`gdscript_compiler.cpp:3004`：`p_script->constants.insert(name, subclass)`；
取值见 `GDScript::_get` 的 `subclasses` 分支，`gdscript.cpp:999-1005`）。

JS 侧无法直接复用该形态，原因（已核实）：

1. 一个 `.ts` 文件对应一个 `GodotJSScript`，按**路径**寻址
   （`ResourceLoader::load(source_path, GodotJSScript)`，`jsb_class_info.cpp:289-291`；
   `load_module_immediately` 用 `get_path()`，`jsb_script.cpp:470-478`）。
2. `ScriptClassID` 只从 `exports.default` 派生（`jsb_class_info.cpp:318-330`）。
3. 因此同文件内的第二个类**没有**对应的脚本资源，无法被 GDScript 当作类型使用。

⇒ 前置条件是「非 default 导出的类也能获得自己的 `ScriptClassInfo` + `GodotJSScript`
（合成路径或内存态资源）」，属独立特性。**本任务完全不支持该形态**：`OBJECT` 一律不作常量
（R2.3 白名单外的硬排除，**注解也无效** —— 对象恒可变）⇒ 即使作者把 `Other = SomeScriptResource`
标成 `@…const()`，它也不会出现在 `_get_constants()` 里（会打 `JSB_LOG(Warning, …)`）。
需要跨语言引用脚本资源 → `@…shared()` 静态变量。

---

## 8. 注解设计

### 8.1 命名（**已定**：`@bind.exposed.const()` / `@bind.exposed.shared()`）

用户原话（D3）：「constant 和 shared。但是**不能直接在 binder 上用这两个命名**。binder 对标的是
gdscript 的注解，这两个不属于该体系，需要有更特殊的命名」。

上一版提议 `@bind.static.const()` / `@bind.static.shared()` **已被否决**：
「什么叫 static.const()，**静态 和 常量 就不是包含的关系**」—— 把 `const` 嵌在 `static` 下暗示
"常量是静态的一种"，而二者是**正交的两个轴**（静态 = 挂在类对象上；常量 = 不可变）；GDScript 里
`const` 与 `static var` 同样**并列**，不是包含关系。

待选方案必须同时满足：
1. 不能是 binder 顶层的裸 `constant` / `shared`（D3）；
2. 命名空间（若有）必须是**真正的上义词**，不制造假的包含关系；
3. 两个名字彼此**并列**。

| # | 方案 | 读法 | 评价 |
|---|---|---|---|
| **a**（推荐） | `@bind.exposed.const()` / `@bind.exposed.shared()` | 「暴露出去的：常量 / 共享变量」 | `exposed` 是真上义词（两者都是"向 Godot 与其它环境暴露"），两叶子并列；与 GDScript 注解族一眼可分 |
| b | `@bind.expose_const()` / `@bind.expose_shared()` | 扁平，无嵌套 | 不制造包含关系；但 binder 顶层已有 `export`，`expose*` 与之形近易混 |
| c | `@bind.script.const()` / `@bind.script.shared()` | 「脚本级的：常量 / 共享变量」 | 同为并列；但 `script` 未点出"跨语言/跨环境"这层含义，且易与 `Script` 资源混淆 |
| d | 用户自定 | — | 由用户给出最终形态 |

**已定案：a**（用户选定 `@bind.exposed.const()` / `@bind.exposed.shared()`）。

```ts
@bind.exposed.const()          // 常量：进 constants / _get_constants
static readonly MAX_SPEED = 100;

@bind.exposed.shared()         // 跨环境静态变量：进 static_variables + C++ 权威存储
static score = 0;
```

binder 上新增 `exposed` 子对象，两个叶子**并列**，与 GDScript 注解族（`export`/`signal`/`rpc`/
`tool`/`icon`/`onready`/`deprecated`/`experimental`/`help`）一眼可分，满足 D3。

### 8.2 桥接契约

```ts
// godot.minimal.d.ts — declare module "godot-jsb" { export namespace internal {
function add_script_constant(target: GObjectConstructor, name: string, kind: number): void;
function add_script_shared_static(target: GObjectConstructor, name: string): void;
function get_static_shared(module_id: string, name: string): unknown;
function set_static_shared(module_id: string, name: string, value: unknown): boolean;
```

C++ 实现放在 `jsb_bridge_module_loader.cpp` 的既有 `_add_script_*` 家族旁
（绑定表在 `:542-548`），用既有 `Symbols` 机制（`jsb_environment.h:73-91`）新增两个符号
`ClassConstants` / `ClassSharedStatics`（追加在 `MemberDocMap` 之后、`ClassModuleId` 之前）。
`Symbols::Type` 是 `symbols_[Symbols::kNum]` 的下标（`jsb_environment.h:179`），
插入位置会改变既有符号索引，但同一次编译内一致，无 ABI 兼容问题。

编辑期类型：在 `src/editor/codegen/jsb_codegen_annotations.cpp` 的注解表登记新注解，
使生成的 `jsb.runtime.gen.d.ts` 含其类型（对齐既有 `annotation_types` 表的组织方式，
生成入口 `jsb_codegen_generator.cpp:548` 的 `ModuleWriter(&runtime_gen, "godot.annotations")`）。

> **命名分层**：`@bind.exposed.const()` / `@bind.exposed.shared()` 是 **TS 面向用户的名字**；
> `jsb.internal.*` 的桥接函数沿用既有 `_add_script_*` 家族命名（`add_script_constant` /
> `add_script_shared_static` / `get_static_shared` / `set_static_shared`），**不要求与用户面同名** ——
> 内部名对齐既有家族更好维护，用户面名按「真上义词 + 并列叶子」的原则定。

---

## 9. 热重载（A8）

`GodotJSScript::load_module_immediately` 重载时重新 `_parse_script_class` 并覆盖
`script_class_info_`（`jsb_script.cpp:499-501`）。`_parse_script_class_iterate` 已对
`methods`/`signals`/`properties` 做 `clear()`（`jsb_class_info.cpp:100-104`），
新增的两个容器（`constants` / `static_variables`）**必须同样 `clear()`**，否则残留旧成员。

方案 A 的进程级存储需额外处理：重载后注解集合可能变化 ⇒ 在类解析时**重建**该类的
shared-static 键集合（保留仍存在的键的值，删除已移除的键），逻辑必须幂等。

---

## 10. 未实测 / 未决清单

### 已实测（阶段 0 前置，见 `research/phase0-findings.md`）

- ✅ TS 产物形态（`static` 字段可枚举 / 方法不可枚举 / 不继承 / `const enum` 擦除 /
  `static readonly` 仍 writable / `Function.length` 截断）。
- ✅ `js_to_gd_var` 两个重载的判定表（含「JS 原生数组与纯 JS 对象在无提示路径返回 `false`」、
  「纯 JS 对象在**任何**路径都转不成 `Dictionary`」、「`symbol` 会打 Error 日志」）。
- ✅ `Array`/`Dictionary::make_read_only()` 的运行期语义（浅层、`duplicate()` 不继承、随 `_p` 传播）。
- ✅ `_get_method_info()` 返回的 `MethodInfo` 的**消费点清单**（见 §6.4 表）——只在
  `reduce_identifier_from_base` 与编辑器签名提示处被读，不进 `validate_call_arg`。
- ✅ **常量准入判据**（`Variant::is_type_shared`，`core/variant/variant.cpp:3473-3493` 实读）：
  返回 `true` 的集合恰为 `OBJECT` / `DICTIONARY` / `ARRAY` / 全部 `PACKED_*_ARRAY`；
  **`CALLABLE`/`SIGNAL` 不在其中**（须单独剔除）。`Variant` 的载荷是 8 字节 union
  （`variant.h:255-267`），值类型内联、共享类型放指针 + 引用计数。
  2026-09-25 用户拍板：常量支持面 = **JS 基础值 + enum + 容器（`GArray`/`GDictionary` 包装）**
  （判据是「JS 侧能否原地改」，见 §4.2）。值类型虽有独立快照，但 JS 侧那个成员本身仍可写
  （`MyClass.V.x = 0` 合法），且 `Object.freeze` 实测挡不住 ⇒ 不作常量。
- ✅ **`Object.freeze` 对值类型无效**（本轮实测，探针 `.agent_tmp/q1_freeze.mjs`）：
  `freeze(实例)` 与 `freeze(prototype)` 都挡不住 `inst.x = 42`（store 被改）—— `x`/`y` 是
  prototype accessor（`jsb_v8_class_builder.h` 的 `Property()` → `SetAccessorProperty`），
  可变状态在 `IF_Pointer` 指向的 Variant 里；freeze 只作用于 own property。
  对容器 proxy 调 freeze 则直接抛 `TypeError`（`isExtensible→true` + `preventExtensions→true`
  不自洽）。⇒ 容器改走"共享 `_p` + `make_read_only()`"，**不需要 freeze**。
- ✅ **`PACKED_*` 为什么不能作常量**（§4.2 下方说明块，本轮取证）：① GDScript 自身就不允许
  `const P := PackedInt32Array([1,2,3])`（`p_p3.gd` parse error：`isn't a constant expression`）；
  ② `is_type_shared() == true` ⇒ 索引写是原地改、不走写回，`_set` 拦不到 —— `p_sv.gd` 实测
  `h.P[0] = 99` 让存储从 `[1, 2, 3]` 变成 `[99, 2, 3]`；③ `Packed*Array` 无 `is_read_only()` /
  `make_read_only()`（`extension_api-4-7.json` 核对）⇒ 无只读可施加。三条合起来 ⇒ 排除。

### 仍未实测 / 未决

- ~~§6.3 选项 B 的运行期方法注册路径未实机验证~~ **已取消**：用户选定 Q1=(c)（静态函数整体剔除，
  R8.5），该路径不再是待办，`implement.md` 阶段 0.4 已标为「不做」。
- ✅ **GDScript 侧常量识别机制**（本轮取证，`research/q1q2-freeze-and-signatures.md` §Q3）：
  `Script::get_constants()` 是唯一入口；命中后 `make_builtin_meta_type`（`:4396`）置 DataType 的
  `is_constant = true` ⇒ **直接赋值** `SomeScript.N = 5` **编译期**报错（**不依赖 `_get`**）。
  **嵌套形式**的保护等级**取决于我方 `_get` 是否在静态分析期返回值** —— `reduce_subscript` 的折叠走
  `p_subscript->base->reduced_value.get_named(name, valid)`（`:4912-4920`）⇒ `Object::get` ⇒ `_get`：
  能返回则折叠 ⇒ 索引赋值也编译期报错；不能返回则只剩运行期 guard。
  **实现后必须实测**（A14/A15）；**实测前不得承诺**枚举点访问行为（两种落法都可能）。见 R2.5。
- ✅ **容器常量的构造与冻结**（本轮取证）：`GArray.create([...])` / `GDictionary.create({...})` 是
  作者侧唯一入口（纯 JS 数组无提示路径返回 `false`；纯 JS 对象在任何路径都返回 `false`）。
  经 `js_to_gd_var` 的 `IF_VariantFieldCount` 分支转换后与 JS 侧**共享 `_p`** ⇒ 递归
  `make_read_only()` 即两侧同时只读。**代价**：JS 侧写该容器会报 engine error（须文档写明）。
- **待定（阶段 3 阻塞项）**：`@…shared()` 静态变量的跨桥转换路径。静态变量不受 R2.3 白名单约束，
  但 `Variant` 参数走**无提示** `js_to_gd_var`，而该路径对 **JS 原生数组返回 `false`**（§0.2）⇒
  `static ARR = [1,2,3]` 装不进去。选项：① 桥接函数声明成具体类型/带提示重载（数组走
  `Variant::ARRAY` 提示）；② 要求作者用 `GArray.create` / `GDictionary.create` 包装。
  **纯 JS 对象字面量在任何路径都转不成 `Dictionary`** ⇒ 对象类静态变量必须走 ②。阶段 3 定案并实测。
- `[INFERENCE]` 方案 A 的跨线程读写（GDScript 主线程读、JS worker 环境写）需要一把互斥锁，
  锁的粒度与是否可能死锁**未评估**；`_get` 是 `const` 方法且可能被非 JS 线程调用，
  `Environment::_access` 的线程归属是风险点（`jsb_environment.cpp:83-172`）。
- `[INFERENCE]` `Variant` 存于进程级 map 的引用计数 / StringName 生命周期需按
  `jsb::internal::VariantUtil` 的既有约定核对。
- `[INFERENCE]` `_get_constants()` 的调用线程（`EditorFileSystem` 后台线程）与
  `ensure_module_loaded()` 的加载时机是否安全，未评估。
- `[INFERENCE]` `_get_property_list()` 对**无实例**的脚本资源（inspector 选中 `.ts` 文件时）是否
  会被调用、以及 `script/source` 项在 GDExtension 路径上的实际呈现，未实测。
