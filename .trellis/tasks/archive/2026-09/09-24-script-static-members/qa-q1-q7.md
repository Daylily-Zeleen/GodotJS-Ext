# Q1–Q7 质询答复（2026-09-26）

用户当轮原话：7 个质询，要求逐条给细节；落成文档的须给**文档路径与描述位置**。

证据全部为用户指定的本仓源码路径（上帝视角 Godot 源码）与实机探针，非推断。

---

## Q1 — A11 归因是什么？文档路径和描述位置？

**结论：此前文档写的归因方向是反的。**

| 断言 | 修正前 | 修正后（实测） |
|---|---|---|
| 基线输入 | 「官方 4.7.2 dump 生成」 | **基线由 4.8.dev 引擎建**，live 才是真官方 4.7.2 |
| live 输入 | （模糊） | `project/extension_api.json` 头部自证 `version_minor:7, version_patch:2, version_status:stable, version_build:official`（11.87 MB） |

**文档位置**：

- 归因描述：**`.trellis/tasks/09-24-script-static-members/prd.md`** → `Acceptance Criteria` → **A11** 条目，其中「归因纠正（2026-09-25 复查）」段
- 同一还显式列出了 **基线-only 的 15 个具名实体**：`FuzzySearch` / `AnimationNodeObserver` / `StreamedTexture2D` / `TextureStreaming` / `Trail3D` / `Line3D` / `ScenePaint2DEditor` / `VisualShaderGroup` / `BoneSpreader3D` / `ResourceImporterStreamedTexture` / `CompressProfile` / `SensorOrientation` / `get_preferred_locales` / `ui_toggle_fullscreen` / `RENDER_STREAMING_TEXTURE_MEM_USED`
- 方法论依据（「分片输出时文件级 diff 会说谎」）：**`.trellis/spec/godotjs-ext/test/codegen-baseline.md:72`** 第 4 条

**为什么不能只看文件级 diff**：11 个 `godot*.gen.d.ts` 分片的字节差由**分片布局抖动**主导
（`godot10` 基线 108598 B → live 25661 B，近乎掏空；`godot1` 585764 → 653181 B）。
必须做语义级比较（归一化去注释/空白后按实体名取并集）。

**本轮脚本复检的诚实交代**：本轮跑的 pandas 交叉比对得到 baseline-only 423 行 / live-only 50 行 ——
样本偏小，**不构成区分 4.7/4.8 的确证**，只能作为「确有单向差异」的指示。
真正的确证来自源码实读：`D:/Dev/godot/godot/version.py` = **4.8.dev**，上述符号在其源码中存在。
⇒ 「基线是用错引擎版本建的」这一结论用的是**源码实读**，不是本轮脚本。

---

## Q2 — 具名枚举在 `get_constants()` 里是 `Dictionary` 还是摊平成 `Named.A`？

**实际情况：具名枚举 = 一个 Dictionary 挂在枚举名下（`ret["Named"] = {"A":0,"B":1}`）。**
你猜的 `ret["NamedEnum.A"] = 1` **不成立**。

两条规则：

- **具名枚举**：`ret[枚举名] = Dictionary{成员名: int}`
- **匿名枚举**：成员**摊平成独立常量**（`UA` / `UB` 直接进 `ret`）

**GDScript 自己点访问能用靠的不是这个**：是 parser AST 的 `DataType::ENUM` + `enum_values`
（`modules/gdscript/gdscript_analyzer.cpp:4132-4147`），**不是** `get_constants()`。
外来脚本（我方）经 `get_constants()` 拿到 Dictionary ⇒ 分析器给
`make_builtin_meta_type(DICTIONARY)` ⇒ 走 meta 分支，该分支只查 `has_constant`/enum、
不查 `get_property_list` ⇒ **点访问失败、索引访问可用**。

实测（A15）：`SomeScript.E.Red` 报 `Cannot find member "Red" in base "Dictionary"`；
`SomeScript.E["Red"]` 得 `0`。

⇒ 我方「GDScript 侧枚举成员须用索引访问」的结论**正确**，已写进
`scripts/jsb.runtime/src/godot.annotations.ts` 的 `exposed.const()` JSDoc。

---

## Q3 — 两个注解用错方式会在写的时候 / 编译的时候报错吗？

**答复：原状态有 3 个误用缺口（全部静默通过）。成员形态已修；类级点名的名字清单无法静态校验（已知剩余缺口）。**

### 原状态（探针 `.agent_tmp/current_misuse.ts`）

| 误用形态 | 原结果 |
|---|---|
| 成员形态挂在 instance field | **clean**（编译通过） |
| 成员形态挂在 instance accessor | **clean** |
| 成员形态挂在 instance method | 报 TS1241（纯因签名不匹配，`() => number` vs `(...args)=>Value`，**不是**位置错误） |
| 成员形态挂在 static accessor / static getter | **clean** |
| 类级形态点名不存在的名字 | **clean** |

这些错位在 Godot 侧**全部静默丢弃**：解析期只读类对象的**自有属性**（`jsb_class_info.cpp` 的
`ClassConstants` 读取路径），错位成员不进任何集合、无告警。用户拿到的是「写了注解但不生效」。

### 已修：成员形态收窄

`scripts/jsb.runtime/src/godot.annotations.ts:708`

```ts
export type StaticMemberDecoratorContext<This, Value> = ClassFieldDecoratorContext<This, Value> & { static: true };
```

两处零参重载 + 实现返回类型收窄：`:745` / `:749` / `:788` / `:792`。

**`& { static: true }` 是承重部分**（`.agent_tmp/necessity_probe.ts` 三者对照）：裸
`ClassFieldDecoratorContext` 的 `static` 是 `boolean`、容纳 `static: false` ⇒ **不报错**；只有并到
`true` 才让 instance 形态失败。

**收窄后效果**（`.agent_tmp/fieldonly_probe.ts`，`rc=2`）：合法形态（static field + 类级点名）通过，
**5 种误用全部报错**——instance field / instance accessor / instance method / static accessor /
static getter，各 TS1240/1270 或 TS1241/1270，共 10 条。

**不影响运行期**（`.agent_tmp/assign_narrow.ts`）：收窄后的重载集仍可赋给宽叶子
（`const check: Leaf = exposed_const;` ⇒ `rc=0`）。

### 用户实际拿到的是哪份类型（修复必须同步的另一半）

用户写 `.ts` 时是 `createClassBinder(): ClassBinder`，其 `exposed.const` 类型来自 **codegen 产出的
`project/typings/jsb.runtime.gen.d.ts`**，不是运行时函数。所以同时改了发射端：

- `src/editor/codegen/jsb_codegen_annotations.cpp:428-432`：零参叶子由
  `make_func(Array(), make_godot("ClassMemberDecorator"))` 改为
  `make_godot_args("ClassMemberDecorator", { make_godot("StaticMemberDecoratorContext") })`
- 产出核对：`project/typings/jsb.runtime.gen.d.ts` **139**（`const` 叶子）/ **145**（`shared` 叶子）
- `scripts/typings/godot.generated.d.ts:1289` / `:1292`：同一声明的独立检出镜像，已同步
- `project/typings/jsb.runtime.bundle.d.ts:216`：`StaticMemberDecoratorContext` 本体

### 仍然无法静态校验的一类（已知剩余缺口，已文档化）

**类级点名 `@bind.exposed.const("X")` 的 `"X"` 是否真实存在** —— 无法校验。

两条路都试过并失败（`.agent_tmp/keyof_probe.ts` / `keyof_probe2.ts`）：`keyof InstanceType<T>` 拿到
`Object` 原型键；`keyof T`（`T extends Ctor`）拿到 `Function` 键 ⇒ 类级名字列表无法在类型层枚举。
现状只有运行期守卫（`exposed_const` 类级分支检查 `context.kind !== "class"`，`:757` 抛错；
`exposed_shared` 同型在 `:797`）。该类级守卫保护的是**装饰目标**错位，不是名字存在性。

---

## Q4 — 改 `jsb_codegen_annotations.cpp` 的含义是什么？消费者在哪？它和生成 ClassDB 的类型有关系吗？

**答复：和 ClassDB 完全无关。它的唯一消费者是 `.d.ts` 发射器，喂的是用户 `.ts` 的 `tsc` 类型检查器。**

链路（已核实到行）：

```
src/editor/codegen/jsb_codegen_annotations.cpp   get_annotation_types()
  ↓ 声明   jsb_codegen_generator.cpp:53   const HashMap<String, Dictionary> &get_annotation_types();
  ↓ 使用   jsb_codegen_generator.cpp:555  annotation_types = &get_annotation_types();
  ↓ 发射   jsb_codegen_generator.cpp:538-569  emit_runtime_gen()
  →  out_dir_ + "/jsb.runtime.gen.d.ts"   ⇒  declare module "godot.annotations" { type ClassBinder = ... }
```

即：**注解类型表 → `jsb.runtime.gen.d.ts` → 用户 `.ts` 编译期的 `bind.*` 补全与类型检查**。
它**不**进 ClassDB、**不**影响运行期行为。

运行期走的是另一条链：`scripts/jsb.runtime/src/godot.annotations.ts` → `pnpm build` →
`scripts/out/jsb.runtime.bundle.js` → 由 `SConstruct` 嵌入 DLL → 真正的注解函数调用在这里。

⇒ 用户可见类型有**两条独立来源**，在 `project/typings/` 里声明合并：

| 面 | 来源 | 文件 |
|---|---|---|
| `ClassBinder` 叶子类型 | codegen | `project/typings/jsb.runtime.gen.d.ts`（`exposed.const` 在 `:139`） |
| `ClassMemberDecorator` 等基础别名 | `scripts/jsb.runtime` 的 tsc declaration | `project/typings/jsb.runtime.bundle.d.ts`（`StaticMemberDecoratorContext` 在 `:216`） |

**为什么必须两边都改**：只改 `godot.annotations.ts`，用户侧拿到的 `ClassBinder`（来自 gen.d.ts）
仍是宽类型 ⇒ Q3 修复对用户**不可见**。这是本次修复最容易漏的一步，也已一并改了
（`jsb_codegen_annotations.cpp:428-432` + `scripts/typings/godot.generated.d.ts:1289`/`:1292` 镜像）。

---

## Q5 — `_get_method_info()` 为什么把 `jsb_check()` 换成 `ensure_module_loaded()`？确认过它能被独立调用吗？

**答复：换对了。它确实可在 `loaded_ == false` 时被调用，且这是常态而非边缘情形。**

### 调用链路（实读，无条件依赖别人先加载）

```
Object::get_method_argument_count            (core/object/object.cpp:1964，ClassDB 绑定)
  → object.cpp:794-805                       scr->get_script_method_argument_count(...)
  → ScriptExtension::get_script_method_argument_count
                                             (script_language_extension.h:113-124，先试 GDVIRTUAL)
  → 我方返回 {} 空 Variant                   (jsb_script.cpp:590-592)
  → ret.get_type() != Variant::INT ⇒ 恒回退  Script::get_script_method_argument_count
                                             (script_language.cpp:122-136)
  → get_method_info(p_method)                ⇒ 命中我方 `_get_method_info`，此时 loaded_ 仍为 false
```

第二个独立调用点：`gdscript_analyzer.cpp:4366`（`reduce_identifier_from_base` 的 ② 分支，**无
`has_method` 守卫**）。

补充事实：`Script::get_method_info` **无 ClassDB 绑定**（`script_language.cpp:163-198` 绑定表逐条核对无它）；
`ScriptExtension::get_method_info` 是 `GDVIRTUAL1RC_REQUIRED`（`script_language_extension.h:126-131`）。

### 为什么 `jsb_check(loaded_)` 是错的

`jsb_check` 在 release 下**编译期整体移除**（`jsb_macros.h:59-65`，随 `dev_build=yes` 才编入）
⇒ release 下该断言是空操作 ⇒ 原实现对**任意名字**都返回非空 `{"name": p_method}`
⇒ `reduce_identifier_from_base` 的 ② 分支对任意标识符命中并 `return`，③ 信号与 ④ 常量**永远到不了**。
这正是 R5.1 缺陷本身。

所以这里要的不是「断言前置条件」，而是「**自己建立**前置条件」⇒ `ensure_module_loaded()`。

### 为什么我方的恒定回退让这条链路变成「必走」

`_get_script_method_argument_count` 恒返回空 Variant（`jsb_script.cpp:590-592`）⇒ 引擎**每次**都走回退
⇒ `_get_method_info` 在 `loaded_ == false` 下被调用是**常态**。

### 覆盖缺口已补

原 C++ 套件**没有**该前置条件的用例。`src/runtime/tests/test_jsb_static_members.h:408` 新增
`script method info: queried before the module is loaded`：用 `Ref<GodotJSScript>` +
`load_source_code(path)` + `set_path(path)`，**不碰资源缓存**（用 `ResourceLoader::load` 会依赖用例
执行顺序）⇒ `loaded_` 保持 false；断言 `_get_method_info("__not_a_method__").is_empty()`，
并对真实方法名断言非空。

> 注：`_has_method()`（`jsb_script.cpp:335`）仍保留 `jsb_check(loaded_)` —— 它在
> `ensure_module_loaded()` **之后**，此时 `loaded_` 已由前者建立，与 `_get_method_info` 的情形不同。

---

## Q6 — 四个函数都不该走基类链吗？（GDScript 那边都没向父级查找）

**答复：GDScript 确实四个都只查自己，但「我方要不要替它走链」取决于每个调用点怎么调。逐函数不同，不能一刀切。**

### GDScript 侧实读

- `GDScript::get_constants`（`gdscript.cpp:911-917`）/ `get_members`（`:919-925`）：**只 dump 自己的**。
- `GDScript::has_method`（`:362-363`）/ `get_method_info`（`:385-392`）：**只查自己的**。

### 实测（`inherit` / `inherit2` 探针，官方 4.7.2）

- `derived.gd extends base.gd` 的 `get_script_constant_map()` = **只有 `{D}`**（无 `B`、无 `BE`）
- `get_script_property_list()` 名字 = `["base.gd","bv","derived.gd","dv"]`
- `DER.B` / `DER.bs` / `DER.BE` 在 GDScript 里**全部可解析** —— 但那是**分析器自己走链**
  （`gdscript_analyzer.cpp:6076-6082` 的 `while (base_script.is_valid() && base_script->has_method(fn))`）

### 关键：为什么 GDScript 能而我们不能被照样委屈

`type_from_script`（`gdscript_analyzer.cpp:5755-5804`）对 GDScript 返回 `kind = CLASS`（有
`class_type` 链可走），对我方返回 **`kind = SCRIPT`**（`base_class == nullptr`）⇒ 非 GDScript 分支
（`:4350-4400`）**只处理 `base.script_type` 这一个脚本，内部没有任何 `get_base_script()` 调用**。

⇒「GDScript 能解析 `DER.B`」**不**证明被调者会走链 —— 那条链是分析器为 GDScript **自己**走的。

### 逐函数结论（2026-09-26 定案：**四个都只报自有**）

本节表格**原为「逐函数不同、`_get_constants`/`_has_method`/`_get_method_info` 必须走链」**，
已被**用户当轮裁决 + 负向控制实测**推翻（裁决原话：「干脆去掉查基类脚本得逻辑，解析不到就解析不到
得了，你特么 `_get_script_constant_map()` 行为都变得不一致了」）。修正后的定案：

| 函数 | 结论 | 依据 |
|---|---|---|
| `_get_constants()` | **只报自有**（已删 `base` 合并） | `Script::get_script_constant_map()`（ClassDB 绑定，`script_language.cpp:181`）dump 的就是这个返回值 ⇒ 合并使该 API 对派生脚本偏离 GDScript（实测派生 GDScript 的 map 只有 `{D}`）。对齐 `GDScript::get_constants`（`gdscript.cpp:911-917`） |
| `_get_members()` | **只报自有** | 唯一调用方 `scene_debugger_object.cpp:111-122` 自己 `while (base.is_valid())` 逐个 script 调并把成员挂到该脚本名下 ⇒ 合并会让同一成员在派生与基类下**各列一次** |
| `_has_method()` | **只报自有** | 需要走链的调用方自己走：`GDScriptAnalyzer::get_function_signature`（`gdscript_analyzer.cpp:6076-6083`）的 `while (base_script->has_method())`。**实例侧例外**：`Object::has_method`（`object.cpp:738-758`）**不**走链 ⇒ `GodotJSScriptInstance::has_method` 自己走（对齐 `GDScriptInstance::has_method` `gdscript.cpp:1906-1917`），否则继承方法在实例上回归 |
| `_get_method_info()` | **只报自有** | 同上；`Object::get_method_argument_count`（`object.cpp:794-805`）自己 `while (scr) { …; scr = scr->get_base_script(); }` |

**「解析不到会怎样」实测边界（这是"删合并是对的"的唯一授权来源，官方 4.7.2，`.agent_tmp/probe_dconst.log`）**：

| GDScript 写法 | 实测结果 |
|---|---|
| `const D := preload("…derived.ts")` + `D.F`（继承常量） | **解析期硬失败**：`Parse Error: Cannot find member "F" in base "…static-members-derived.ts"`，脚本不加载（外来脚本无 `class_type` 链，非 GDScript 分支 `:4350-4400` 内部无 `get_base_script()`） |
| `var d := preload(…)` + `d.F` | **不报错**，退化为运行期 `OPCODE_GET_NAMED` → `_get`，而 `_get` 自身走 `base`（`jsb_script.cpp:465-467`）⇒ 仍取到 `1.5` |
| `get_script_constant_map()`（关掉合并后） | 只剩 `["N"]` —— **这才与 GDScript 一致** |

⇒ 上一轮「不合并就分析失败/加载失败」的结论**被证伪**：失败的只是 JS 侧那条**编码了旧行为**的断言
（`derived_map.get("F") == 1.5`），已随夹具同步删除。**鸭子类型只覆盖 `var`（非 const 基）那一半**。

`get_script_method_list` 侧「 Father not impl 」的 bug 子分支调用了 `_has_method`
（`gdscript_analyzer.cpp:6076-6082` 的基类分支，jsb druck 已 abort）⇒ 该 bug 子分支全橙、Assert 8/8 pass
（`der.A()` 橙，`_has_method` 调用 2 次 vs GDScript 1 次，多出的一次正是 parents chain 下沉探）。

---

## Q7 — 为什么覆写 `_get_script_property_list()` 而不是改那个模板？

**答复：模板有两个消费者，改模板会把静态变量泄漏进第二个（实例属性表）。**

模板 `get_script_property_list<ElemTy, ListTy, ConvertFn>`（`jsb_script.h:208-221`）的消费者：

1. `jsb_script.cpp` 的我方 `_get_script_property_list()` → 发 `Dictionary`（给 GDScript 分析器）
2. **`jsb_script_instance.cpp:375-379`** `GodotJSScriptInstanceBase::get_property_list(PropertyList*)`
   → 发 `const PropertyInfo` 给引擎的**实例**属性表

⇒ 把静态变量加进模板 ⇒ 泄漏到 ② ⇒ **每个实例**都多出静态属性（inspector + `get_property_state`
的 STORAGE 路径，`:381-393`）；且 `Object::get_property_list` 会把它算进节点持久化。

**实机取证（官方 4.7.2，`S.gd` = `static var sv` + `var iv` + `const C`）**：

实机取证（官方 4.7.2，`Godot_v4.7.2-stable_win64_console.exe`；探针产出留在
`.agent_tmp/q4_probe.log`，`S` = `static var sv: int = 100` + `var iv: int = 7` + `const C: int = 42`）：

```
RESULT_INSTANCE_HAS_sv=false                 ← 静态变量【不在】实例 get_property_list()
RESULT_INSTANCE_script_entries=script|u=1048590,iv|u=4096
                                             ← 实例表只有 script 槽与实例成员
RESULT_SCRIPT_RESOURCE_LIST=…,sv|u=4096,…    ← 静态变量在【脚本资源】_get_property_list，usage=4096
RESULT_GET_SCRIPT_PROPERTY_LIST=Built-in script,iv
                                             ← 分析器读的面【不含】sv
RESULT_INSTANCE_get_sv=100                   ← 但实例 get 触达得到静态变量
RESULT_INSTANCE_set_then_get_sv=555          ← 实例 set 也触达得到
```

四个面各不相同，三条 instr 结论：

1. **`sv` 不在实例 `get_property_list()`** ⇒ 模板第二消费者是实例表，塞静态变量进去 = 偏离 GDScript。
2. **`sv` 在脚本资源面且 usage = 4096**（只有 `SCRIPT_VARIABLE`，无 `STORAGE`/`EDITOR`）
   ⇒ `jsb_class_info.cpp:777` 改 4102 → 4096 是**对齐**，不是拍脑袋。
3. **实例 `get`/`set` 能触达静态变量**（100 / 555）⇒ `jsb_script_instance.cpp:500-507` 补的
   常量 + 静态变量分支是**对齐 GDScript 的必要补充**，不是多此一举 —— 缺它则
   `n.get("sv")` 返回 null，而 GDScript 返回 100。

⇒ GDScript 静态变量**不出**实例 `get_property_list()`。塞进模板 = 偏离 GDScript。

**另外顺带纠正一个早前说错的理由**：此前拿「godot-cpp 无法覆写
`Script::get_script_property_list(List<PropertyInfo>*)`（纯虚、无 GDVIRTUAL）」当「不能改模板」的
决定性理由是**错的** —— 那条只解释「为什么必须走 `_get_script_property_list()` 这个面」，
**不**解释「为什么不能改模板」。真正的理由就是上面的**两个消费者**。

---

## 由 Q7 暴露并已修复的 STORAGE 偏差

顺 Q7 查 usage 时发现：我方静态变量的 `PropertyInfo` 原用
`PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE`（= 4102），而 GDScript 侧
`DataType::to_property_info` 起始 `PROPERTY_USAGE_NONE`（`gdscript_parser.cpp:5418`）+ 编译器补
`SCRIPT_VARIABLE`（`gdscript_compiler.cpp:2902`）= **4096**，不带 STORAGE/EDITOR。

**已改**：`src/runtime/bridge/jsb_class_info.cpp:777` 只留 `PROPERTY_USAGE_SCRIPT_VARIABLE`。

该位**不**影响 `.tscn` 持久化 —— `packed_scene.cpp:938-939` 读的是 `p_node->get_property_list()`
（实例属性表），脚本资源的 `_get_property_list` 从不参与。故这是「报告出来的 usage 数值偏差」，
不是持久化缺陷；按 parity 改。
