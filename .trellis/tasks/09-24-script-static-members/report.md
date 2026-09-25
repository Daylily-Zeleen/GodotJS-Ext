# 进度：GodotJSScript 静态成员

任务目录：`.trellis/tasks/09-24-script-static-members/`
状态：**planning**（未 `task.py start`）。Q1=(c) 不做静态函数、Q2 注解命名、**Q3 = 常量类型面（JS 基础值 + enum + 容器 `GArray`/`GDictionary` 包装）**、Q4 不做签名类型信息 —— **全部拍板**，三件套已同步；当前等用户对三件套的审查与开工批准。

## 日志

- 2026-09-24 规划三件套完成（v1）：`prd.md`（R1–R8 / A1–A11）、`design.md`（§1–§10）、`implement.md`（阶段 0–4 + C1–C5）。
- 2026-09-24 引擎侧契约逐条核实（写入 `design.md` §2）：`_get`/`_set`/`_get_property_list` 可达；
  `_get_constants`/`_get_members`/`_get_method_info` 已注册只是返回空；静态函数运行期直连不可达
  （`Object::callp` 无 `_extension` 钩子）。
- 2026-09-24 tree-sitter 评估结论（前序任务）：不引入。报告见
  `09-06-lowprio-tree-sitter-ast/research/tree-sitter-evaluation.md`。本任务 R8.1 据此声明 `const enum` 不支持。
- 2026-09-24 用户拍板 D1–D5（原话见 `design.md` §0）。
- 2026-09-24 **GDScript 侧语义实测**（探针 `.agent_tmp/gdcallable/`（任务收尾已清理），经 python `subprocess` 调
  `Godot_v4.7.2-stable_win64_console.exe --headless --script`）：
  - 静态函数各形态：`H.sfn(1,2)` ✓、`H.sfn.call(1,2)` ✓、`var f: Callable = H.sfn; f.call(1,2)` ✓、
    `H.call("sfn",1,2)` ✗（**编译期** `Cannot call non-static function "call()" on the class "..." directly`）。
  - `H.sfn` 的 `typeof` = `TYPE_CALLABLE`(25)、`get_method()` = `"sfn"`、`get_argument_count()` = 2。
  - 常量形态：`H.N`/`H.S`/`H.V.x` 可读；`H.ARR.is_read_only()` = true；`H.D.A`/`H.D["A"]`/`H.D.keys()` 可读；
    `H.E` = `{ "EA": 0, "EB": 1 }`；`load(...) as Script` 后 `get_script_constant_map()` 返回全部常量；
    直连 `H.get_script_constant_map()` 是编译期错误。
  - GDScript `static var` 的容器**不是** read-only；`const` 容器的冻结深度**随取值形态变**：同脚本内
    裸标识符只冻顶层（内层 `append` 成功，`p_i.gd`），经脚本对象取则是**深层**（内层一并只读，`p_ro4.gd`）。
    早期记的「只有顶层只读」是前者，不是普遍事实 —— 见下方 §0.6 条目。
  - ~~GDScript `const ARR` 的 `ARR[0] = x` 在 `_initialize()` 里**永久挂起进程**~~ → **已证伪，见下方 2026-09-24 条目**：那批探针把 `quit(0)` 写在 `_initialize()` 末尾，而运行期脚本错误会**终止当前 GDScript 函数**，于是 `quit` 永不执行、进程空转。真实后果是「打印一条脚本错误 + 终止当前函数」。
- 2026-09-24 **关键修正**：`_get_constants()` 返回 `Callable` 会让分析期得到
  `make_builtin_meta_type`（`is_meta_type = true`），从而让 `SomeScript.fn.call(...)` 在**编译期**被拒
  （实测 `p_u.gd`）。⇒ 静态函数名**只能**由 `_get_method_info()` 提供，且**不得**进 `_get_constants()`。
  这条推翻了 v1 design/handoff 的推断。
- 2026-09-24 三件套重写（v2）：`prd.md` 过收敛（R1–R8 / A1–A11 / Q1–Q3）；
  `design.md` 重写为实测版（§4.2 不可变性判定规则回答 D1；§6 静态函数改为 `_get_method_info` 路线；
  §6.5 三问；§8.1 注解命名走子命名空间）；`implement.md` 重写阶段 0–4。

- 2026-09-24 **阶段 0 前置实测提前完成**（结论落 `research/phase0-findings.md`，探针 `.agent_tmp/tsprobe/`（任务收尾已清理））：
  - 0.1 TS 产物 dump ✅：`static` **字段可枚举**、`static` **方法不可枚举**；`length`/`name`/`prototype`
    必须显式跳过；静态成员**不继承**；`const enum` 被完全擦除；`static readonly` 的 `writable` 仍为 `true`
    （⇒ 运行期无法判定 const，D1 的结论得到实测支撑）；`Function.length` 对 rest 参数为 0。
  - 0.2 `js_to_gd_var` 判定表 ✅（源码实读）：无提示路径对 **JS 原生数组 / 纯 JS 对象 / symbol /
    function 全部返回 false**；JS 数组必须走 `Variant::ARRAY` 提示重载；**纯 JS 对象在任何路径都
    转不成 `Dictionary`**（`FALLBACK_TO_VARIANT` 要求 `is_variant`）⇒ 枚举必须手工构造 Dictionary；
    `symbol` 会打 `Error` 日志 ⇒ 必须先按 `typeof` 预筛才能「静默忽略」。
  - 0.3 `make_read_only()` ✅（GDScript 探针 `ro.gd` 实跑）：`Array`/`Dictionary` 均可用；
    **浅层**（`nested[0]` 仍可写）；**`duplicate()` 不继承只读标记**；只读标记随 `_p` 走
    （放进 Dictionary 再取出仍只读）。目标 API 4.7 内含该方法。
  - 0.4 运行期 MethodBind 注册探针：**未做**（仅 Q1=(b) 需要）。
- 2026-09-24 据实测修正 `design.md`：§4.1（枚举理由改写 + 跳过 length/name/prototype + 不继承）、
  §4.2（read-only 实测表 + `duplicate()` 不继承）、§4.3（枚举判据改为内嵌字段数，不能用
  `Array.isArray`；字符串枚举无反向映射）、§6.4（新增「`MethodInfo` 消费点」表，纠正「参数个数是
  分析期硬约束」的适用范围；`_get_method_info` 对不匹配名字必须返回空 `MethodInfo`）、
  §5.1（OBJECT 常量的告警改为文档明示）。`prd.md` R1.1/R1.2 同步。
- 2026-09-24 **用户答复四问**：
  - Q1：「不做静态函数了，文档说明就行」，但要求先回答「该定位到哪个 JS 环境的静态函数」与
    「它改变的数据是否也跨环境」两点。
  - Q2：**否决** `@bind.static.const()` —— 「静态 和 常量 就不是包含的关系」；最终**选定**
    `@bind.exposed.const()` / `@bind.exposed.shared()`（在 4 个待选里选了推荐项 a）。
  - Q3：容器作常量 + 浅层 `make_read_only()`；并追问非容器类型（Vector2 等）用 const 声明是否仍可改。
- 2026-09-24 **Q1 两点逐条作答（结论：采纳 (c)）**，写入 `design.md` §6.0 / `prd.md` R5：
  - ① 该定位到哪个环境 —— **架构上没有答案**。`script_class_info_` 是 `StatelessScriptClassInfo`
    （`jsb_script.h:90`），**不含 `js_class`、不含任何 `EnvironmentID`**；
    `load_module_immediately()` 用的是 `jsb::JSEnvironment env(get_path(), true)` **临时环境**
    （`jsb_script.cpp:475`；构造即 `Environment::_access()`，`jsb_script_language.cpp:72-80`），
    函数返回即析构。三个候选策略（调用者线程环境 / 主环境 / 首次加载环境）在 worker / ShadowRealm /
    纯 Godot 冷调用下各有反例。`JSCallable` 的 `env_id_` 是**实例方法**的先例，静态函数无对象可绑。
  - ② 数据是否跨环境 —— **默认不会，且无法检测**。仅 `shared` 注解的静态变量走 C++ 权威存储会跨环境；
    普通模块级状态（模块 `let`、闭包、未标注 `static`）**每个 isolate 一份**，改它只改自己那份 ⇒
    静默分叉，我们没有任何手段发现。
  - ⇒ ① 只是"策略不唯一"，② 是"语义本身不确定"。按用户判据**采纳 (c)**，仅文档说明（R8.5）。
- 2026-09-24 **发现并写入 R5.1：一个挡住 R2 的既有缺陷** —— `_get_method_info()`
  （`jsb_script.cpp:365-372`）对**任意**名字都返回非空（release 下 `jsb_check` 为空操作），
  而 `reduce_identifier_from_base` 的 ② 分支（`gdscript_analyzer.cpp:4366-4373`）**无 `has_method`
  守卫** ⇒ 常量（④）与信号（③）**永远解析不到**。修法：只对 `script_class_info_.methods` 返回
  `MethodInfo`，其余返回空 `Dictionary`。这是 R2 的前置。
- 2026-09-24 **Q3 追问实测澄清（容器 / 非容器两类保护机制）**，写入 `design.md` §5.3：
  - 非容器（值类型）**没有** `make_read_only()` 可用（`Variant::is_read_only()` 只对
    ARRAY/DICTIONARY 有意义）；保护来自「常量不进 `_get_property_list()` + `_set` 拒绝写常量」。
  - `const H := preload(...)`（const 基）下 `H.V.x = 9` / `H.ARR[0] = 7` / `H.N = 5` 均为
    **编译期** `Cannot assign a new value to a constant.`（探针 `y1`–`y5`）。
  - `var h = load(...)` / 未标注类型的参数下走**运行期**：值类型报
    `Invalid assignment of property or key 'V' …`，容器报
    `Invalid assignment on read-only value (on base: 'Array')`（探针 `w1`–`w6`、`z1`–`z6`）。
    两条路径**都改不动存储值**。
- 2026-09-24 用户指出我在收尾汇报里把**已拍板**的 Q1/Q2/Q3 又当未决复述了一遍。根因：照搬 compaction
  摘要里过期的 `Next Steps`（`workflow-rules.md` 已沉淀该坑：「压缩摘要的 Next Steps 不是待办清单」）。
  核实后文档侧决策均已落盘，本轮只清掉 `report.md` 里三处仍写着"待拍板"的过期表述。
- 2026-09-24 **补齐 §5.3 最后一条未取证结论**（探针 `v1.gd`）：在 `preload` 得到的脚本上连续执行
  `h.V.x = 9` / `h.ARR[0] = 7` / `h.N = 5`，打印 `BEFORE V=(3.0, 4.0) ARR=[1, 2, 3] N=7` →
  `AFTER` **三项完全相同**，3 条脚本错误，`rc=0`。⇒ 「写入常量改不动存储值」由断言升级为实测。
- 2026-09-24 **用户追问「非 JS 基础类型 / Array / Dictionary 常量怎么没个说法」** ⇒ 补齐
  `prd.md` **R2.3 常量类型矩阵**（按 Variant 类型分档给结论，判据只有 `is_type_shared()` 与
  「有无只读 API」）：JS 基础值 ✅；Godot 值类型 ✅（靠写回被 `_set` 拒）；`ARRAY`/`DICTIONARY`
  ✅ + 浅层 `make_read_only()`；`PACKED_*ARRAY` ❌；`CALLABLE`/`SIGNAL` ❌；`OBJECT` ❌ 默认；
  纯 JS 对象/函数/`undefined`/`symbol` ❌（字符串枚举例外，手工构造 `Dictionary`）。
  并写明 TS 侧写法：**数组用字面量**（走 `Variant::ARRAY` 提示重载 ⇒ 转得成，产出 `Array`）、
  **Dictionary 必须 `GDictionary.create({...})`**（`new GDictionary({...})` 无效，纯 JS 对象字面量
  在任何路径都转不成 Dictionary）。`research/phase0-findings.md` §0.3 恢复（只读语义）+ §0.6 新增
  （冻结深度：GDScript `const` 经脚本对象取是**深层**、我方 `make_read_only()` 是**浅层**）。
  > **该矩阵已被同日用户指令推翻**（见下方 2026-09-25 条目）：Godot 值类型与容器**都不再作常量**，
  > 支持面收窄为 **JS 基础值 + enum**。此处保留为演进记录。
- 2026-09-25 **用户纠正准入判据（推翻上条矩阵）**：原判据「`is_type_shared()` + 有无只读 API」是
  **机制层**的，不是语义层的。用户给出的正确判据：**JS 的 `const` 只冻结绑定、不冻结对象** ——
  `const vec2 = new Vector2(1.0, 1.0)` 之后 `vec2.x = 0.0` **合法且生效**。凡带对象身份的 JS 值，
  作者都能在 JS 侧原地改它 ⇒ 把这类值叫"常量"就是把可变性卖成不可变性。
  ⇒ **常量支持面收窄为 JS 基础值 + enum**（enum 是唯一例外：不用它本身，而是从它的自有可枚举属性
  **新建** `Dictionary` 并 `make_read_only()`，那份由我方独占）。
  连带作废：Godot 值类型（`Vector2`/`Color`/…）也不作常量 —— 它们 `is_type_shared == false`、
  我们确能拿到独立快照，但 **JS 侧那个成员本身仍可写**（`MyClass.V.x = 0` 合法），
  而 GDScript 侧 `SomeScript.V` 停在解析那一刻 ⇒ 同一成员两种可变性、静默分叉。
  **实现改为白名单**（只放行 `NIL`/`BOOL`/`INT`/`FLOAT`/`STRING`/`STRING_NAME` 六种 Variant 类型
  + 枚举），不依赖任何"排除清单"的完备性。
  已回写：`prd.md` R1.2 / R2.3 / R2.4 / A1 / A13 / 「已拍板」Q3 段；`design.md` §3.1（删
  `ScriptConstantKind::Container`）/ §4.2 / §4.4 / §5.1 / §5.3 / §5.4 / §6.5 / §7 / §10；
  `implement.md` 头部 / 0.2 / 1.2 / 1.4 / 2.4 / C3；`research/phase0-findings.md` §0.2 / §0.3 / §0.6。
- 2026-09-24 **消除一处自相矛盾**：`design.md` §5.3 曾把 `PACKED_*` 混入「非容器（值类型）」并称靠
  `_set` 拒绝写回保护 —— 与 §4.2「`PACKED_*` 原地改、`_set` 拦不到、已排除」直接冲突。
  已拆成独立的「`PACKED_*` 不适用上一条」条目；`implement.md` 1.2 补**显式按类型剔除**步骤
  （`PACKED_*` 是 `GArray` 包装、无提示路径会转换成功，不能靠 `js_to_gd_var` 返回 false）；
  `prd.md` 补 **A13**（❌ 档可验证）。
- 2026-09-25 **用户提两条备选**（① 注解 freeze + `make_read_only()` 让非基础类型也能作常量；
  ② 参数/返回值/信号参数是否有 tree-sitter 之外的方案）⇒ 取证落
  `research/q1q2-freeze-and-signatures.md`（探针 `.agent_tmp/q1_freeze.mjs`、`.agent_tmp/q2_params.mjs`、
  `.agent_tmp/tsmeta*/`）：
  - **Q1 值类型不可行**（实测）：`freeze(实例)` 与 `freeze(prototype)` 都挡不住 `inst.x = 42`
    （x 是 prototype accessor，状态在 IF_Pointer 的 Variant 里）。
  - **Q1 容器可行，且比 freeze 更简单**：`GArray`/`GDictionary` 与 JS 侧**共享 `_p`** ⇒
    对转换结果调 `make_read_only()` **同时**冻两侧（GDScript `Array::set` 与 JS proxy `set` trap
    → `target.set` 命中同一 guard）；**不需要 freeze**（对现有容器 proxy 调 freeze 直接
    `TypeError`：`isExtensible→true` + `preventExtensions→true` 不自洽）。代价：须 `GArray.create`
    构造、JS 侧写入开始报错、深层须递归、`Packed*`/`OBJECT` 仍排除。
  - **编辑器识别只覆盖直接形式**（源码实读，`prd.md` R2.5 已回写）：非 GDScript 脚本走
    `reduce_identifier_from_base` 的 `script_type` 分支（`gdscript_analyzer.cpp:4390-4399`），
    它**只设 `type_constraint`、不设节点级 `is_constant`/`reduced_value`**（GDScript 类成员分支
    `:4273-4277` 两者都设）。后果：`SomeScript.N = 5` 编译期报错 ✅；`SomeScript.ARR[0] = 7`
    **不报**（索引折叠要求 base 的**节点**字段，为 false）→ 只落到运行期 guard；
    `SomeScript.E.EA` 反而报 `Cannot find member "EA" in base "Dictionary"`
    （落入 BUILTIN meta 分支，该分支不查 `get_property_list`）→ 须用 `SomeScript.E["EA"]`。
    这是**引擎侧**行为，扩展侧无法修正。A14/A15 拆开记录，实现后必须实测回写。
  - **Q3 定案（用户拍板「纳入这两个容器」）**：常量类型面 = JS 基础值 + enum +
    **容器（`GArray`/`GDictionary` 包装）**。实现要点：递归 `make_read_only()`；
    `ScriptConstantKind::Container` 恢复；白名单由 6 种扩为 **8 种** Variant 类型。
    已同步 `prd.md`（R2.3 / R2.4 / R1.2 / R1.3 / A1 / A13 / Q3 段）、`design.md`（§3.1 / §4.2 /
    §5.1 / §5.3 / §5.4 / §6.5 / §10）、`implement.md`（头部 / 0.2 / 1.2 / 1.4 / 2.4 / C3）、
    `research/phase0-findings.md`（§0.2 / §0.3 / §0.5 / §0.6）。
  - **Q4 定案（用户拍板「先不做，留取证文档」）**：方法参数/返回值/信号参数的类型信息不做 ——
    用户明确否决"定义一个函数就要用注解再写一遍参数和返回值类型"。取证留档，未来顺序：
    注解 → 单函数参数表小词法器 → 自研注解子解析器。
  - **新增 R2.5（回答"能不能让 GDScript 编辑器也报错"）**：源码实读 `gdscript_analyzer.cpp`。
    新增 A14（编译期覆盖项）与 A15（不覆盖项 + 回写要求）。
  - **R2.5 二次修正（复核时发现自相矛盾）**：初版 R2.5 的「第 3 步：链上传播 ⇒
    `SomeScript.ARR[0] = 7` 编译期被拒」与同节「关键不对称」表（说索引赋值**不报**）**互相矛盾**。
    重读引擎源码定位到根因：`reduce_subscript` 的折叠（属性 `:4889`、索引 `:4992`）**前置是节点级
    `is_constant`**，而 `:4912-4920` 的折叠实现走 `reduced_value.get_named(name, valid)` ——
    **即 `Object::get` ⇒ 我方 `_get`**。⇒ **决定因素是「我方 `_get` 在静态分析期是否返回该值」，
    不是固定的引擎不对称**。已把 R2.5 改写为「机制①（`:4396` 置 DataType，不依赖 `_get`）+
    机制②（嵌套形式，取决于 `_get`）」并给出**待实测矩阵**；枚举点访问明确标为「两种落法都可能，
    实测前不得承诺」。同步改写 `research/q1q2-freeze-and-signatures.md` §Q3.2–3.4、
    `design.md` §4.3 / §10。
  - **Q2 有备选且多数比 tree-sitter 便宜**：参数名/元数用 `Function.prototype.toString()` +
    **单函数参数表小词法器**（朴素 split 在解构/注释/字符串默认值上失败，实测）；
    参数类型/返回值类型**只能显式注解** —— `emitDecoratorMetadata` 强制 legacy 装饰器而本仓
    强制现代（`createClassBinder()` 9 处抛错），现代装饰器的 `context.metadata` 在 tsc 6.0.3
    **完全不发射**（实测）；信号参数走注解 + `ScriptSignalInfo` 字段（引擎 `add_user_signal`
    已绑定）。TS Compiler API 路线**不可用**（解析点在 runtime 库、被 EditorFileSystem 后台线程
    调用）。⇒ 顺序：注解 → 单函数小词法器 → 自研注解子解析器。
- 2026-09-24 同轮清掉第二处与矩阵冲突的旧表述：`design.md` §4.2 的「`Object.isFrozen` 逃生口」
  （v1 遗留）与 **D2/R4.4**「未注解一律忽略」直接冲突，且构成注解门控之外的第二套约定 ⇒ 删除，
  改写为「触发条件只有注解」。

- 2026-09-25 **三件套全部同步并自检通过**：R2.5 的矛盾已消除，全库 `grep` 无残留的
  「自动传播 / 不填充节点级 / 不对称表」旧表述；`task.py validate` 全绿
  （implement.jsonl 10 条 / check.jsonl 5 条）。改动文件：`prd.md`（R2.3 / R2.5 / A15 / Q3 段）、
  `design.md`（§4.3 / §6.5 / §10）、`implement.md`（C3）、
  `research/q1q2-freeze-and-signatures.md`（§Q3.2–3.4）。

## 阶段 1 实施（2026-09-25，用户批准「开始实施，注意不要提交」）

- **阶段 1.1 / 1.3 数据模型与符号**（`src/runtime/bridge/jsb_class_info.h`、
  `jsb_environment.h`）：新增 `ScriptConstantKind{Value,Enum,Container}`、`ScriptConstantInfo`、
  `ScriptStaticVariableInfo`；`StatelessScriptClassInfo` 加 `constants` / `static_variables`；
  `Symbols::Type` 在 `MemberDocMap` 后插入 `ClassConstants` / `ClassSharedStatics`。
- **阶段 1.2 解析**（`jsb_class_info.cpp`）：`clear()` 块同步清空两个新容器；`class_obj` 静态属性段
  按注解符号门控（未标注一律忽略），流程 = `typeof` 预筛 → 枚举识别 → `js_to_gd_var` →
  8 类型白名单 → 容器递归 `make_read_only()`。文件级 static 辅助：`_apply_read_only_recursive`、
  `_try_parse_enum`、`_is_constant_value_type`。
- **实施期修正（枚举识别逻辑）**：首版按「全部值为 number ⇒ 数字枚举」判定，**是错的** ——
  TS 数字枚举产物是**混合值**对象（数字键存成员名字符串、标识符键存数字）。改为按**键形态**判定：
  数字键必须存字符串（反向映射），标识符键存 number/string；数字枚举要求每个成员值的反向映射
  自洽，字符串枚举要求无数字键。混入对象返回 `None` 落 `js_to_gd_var` 后自然被忽略。
- **编译**：`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` 通过
  （修掉一处 `const v8::MaybeLocal<v8::Array>` 上调用非 const `ToLocalChecked()` 的错误）。
- **单测**：新增 `src/runtime/tests/test_jsb_static_members.h`（已登记进 `jsb_test_main.cpp`），
  覆盖基础值（INT/FLOAT/STRING/BOOL/INT(bigint)/NIL(null)）、数字枚举归一化（丢弃数字键 +
  `is_read_only`）、字符串枚举作普通 Dictionary、容器递归只读（顶层 + **内层**）、
  白名单剔除（`Vector2`）、JS 字面量数组/对象、`undefined`/函数、未注解成员、共享静态变量。
  `_parse_script_class_iterate` 为测试加了头文件声明（生产调用点仍是 `_parse_script_class`）。

- **阶段 1 验证（实测）**：`scons ... tests=yes` 通过；`godot --headless --path ./project --jsb-run-tests`
  → `RC = 0`、`Orphan StringName = 0`、51/51 cases、644/644 assertions。
  日志逐条印证各守卫分支真的执行：`UNDEF`/`FN` → "unsupported value type"；
  `VEC` → "type Vector2 is not allowed"；`LIT_ARR`/`LIT_OBJ` → "unconvertible value"。
- **负向控制（按 test/index.md 要求）**：把 `_is_constant_value_type()` 临时改成 `return true` →
  测试在 `!class_info->constants.has("VEC")` **恰好失败**（`RC=1`、643/644），证明白名单断言
  不是同义反复；已还原并重编复测全绿。

## 阶段 2 / 3.1 实施与端到端验收（2026-09-25）

- **阶段 2.0（R5.1 前置修复，必须先做）**：`_get_method_info()` 改为只对
  `script_class_info_.methods` 命中的名字返回 `MethodInfo`，其余返回空 `Dictionary`；
  并把 `jsb_check(_has_method(...))` 换成 `ensure_module_loaded()`
  （原断言在 dev 构建下会因分析器探测任意标识符而 abort）。
- **阶段 2.1–2.4**：实现 `_get_constants()`（**只报自有**，2026-09-26 修正） / `_get_members()` / `_get()`
  （沿 `base` 链合并，基类先合入以便子类遮蔽）。
- **阶段 3.1**：桥接 `jsb.internal.add_script_constant` / `add_script_shared_static`
  （载荷 = 名字数组，对齐 `ClassSignals` 写法；**kind 由 C++ 从值推导**，注解侧不声明）；
  TS 侧 `@bind.exposed.const()` / `@bind.exposed.shared()`（挂在 `exposed` 子对象下，
  校验必须为 `static`）；`scripts/typings/godot.minimal.d.ts` 补桥接声明；
  `src/editor/codegen/jsb_codegen_annotations.cpp` 登记 `exposed`。
- **编译**：`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` 通过。
- **端到端验收（GDScript 探针，`.agent_tmp/acc/_acc_*.gd` + `.agent_tmp/acc_probes.log`）**：
  - **A1 通过**：`N=42` / `F=1.5` / `S=hello` / `B=true` / `NUL=<null>` / `BIG=123`
    （bigint → INT）；容器 `ARR=[1, 2, 3]`、`D={ "a": 1 }`。
  - **A2 通过**：`E={ &"Red": 0, &"Green": 1, &"Blue": 2 }`（数字枚举归一化，反向映射键已丢弃）；
    `get_script_constant_map()` 含全部 10 个常量。
  - **A13 通过**：`MAP-keys` = `[E,N,F,D,B,NUL,BIG,NESTED,ARR,S]`；
    `VEC`/`LIT`/`FN`/`PLAIN`（未注解）**全部缺席**；`ARR-ro=true`、
    `NESTED-ro=true`、**`NESTED0-ro=true`**（递归只读生效）、`D-ro=true`。
  - **A14 通过**：`S.N = 5` → **编译期** `Cannot assign a new value to a constant.`（脚本不加载）。
  - **A15 实测并已回写 R2.5**：`S.ARR[0] = 7` **编译期不报**，运行期
    `Invalid assignment on read-only value (on base: 'Array')`；
    `S.E.Red`（点访问）**编译期报** `Cannot find member "Red" in base "Dictionary"`；
    `S.E["Red"]` 可用 = `0`。
    ⇒ **推翻了 R2.5 早先「取决于我方 `_get`」的推断**：嵌套折叠路径因基是 metatype 而整体跳过，
    与 `_get` 无关。R2.5 / R2.3 / design §4.3 §10 / implement C3 / research §Q3 已按实测改写。
- **C2 完整验收通过**：`godot --audio-driver Dummy --headless --path ./project --verbose`
  → `RC=0`、Orphan StringName = **0**、`GODOTJS_TEST_PROJECT_COMPLETED` = **1**、
  `GODOTJS_TEST_PROJECT_FAILED` = **0**（日志 `.agent_tmp/acc_full.log`）。

## 待用户拍板（无）

（无。Q1=(c)、Q2、Q3（基础值 + enum + 容器）、Q4（签名类型信息不做）均已拍板，
见 `prd.md` 的「已拍板」段。）

## 阻塞

- 未 `task.py start`：等用户对三件套的审查与开工批准。


## 阻塞定位与修复：`--generate-types` / `--dump-extension-api-with-docs` 崩溃（已解决）

**症状**：`misc/verify_codegen.py` 的清理步骤删掉 `project/gen`、`project/typings`、
`project/.godot/.api_dumping` 后，第 1 步 dump **两次**都以 `exit=3221225477`（`0xC0000005`）崩溃
且产物未落盘，脚本 `FATAL` 退出（日志 `.agent_tmp/vc.log`）。

**A/B 对照（决定性）**：

| 实验 | 结果 |
|---|---|
| 纯引擎项目（无扩展）跑 dump | ✅ 写出 11870277 字节 `extension_api.json`，无崩溃 |
| **main 检出**用其**改动前** DLL 跑 dump（同一 project 目录） | ✅ 写出产物，0 次 `CrashHandlerException`（`.agent_tmp/dump_main.log`） |
| 本 worktree 把 main 的**改动前 DLL** 换进来跑 dump | ✅ 写出产物，0 次崩溃（`.agent_tmp/dump_maindll.log`） |
| 本 worktree 用**本轮改动的 DLL** | ❌ 崩溃、无产物 |

⇒ **崩溃由本轮改动引入**，与 `project/` 状态、api store、引擎无关。

**根因（PDB 符号化调用栈）**：崩溃帧全部落在扩展 DLL，`llvm-symbolizer` + `ImageBase=0x180000000`
解析出完整链：

```
GodotJSScript::get_bind                     jsb_script.h:58
GodotJSScript::_get                         jsb_script.cpp:436   ← 本轮新增
GodotJSScript::ensure_module_loaded         jsb_script.h:255
GodotJSScript::load_module_immediately      jsb_script.cpp:559
jsb::Environment::load                      jsb_environment.cpp:1471
jsb::Environment::_load_module              jsb_environment.cpp:1154
jsb::Environment::find_module_resolver      jsb_environment.h:621
DefaultModuleResolver::get_source_info      jsb_module_resolver.cpp:432
DefaultModuleResolver::try_resolve_id       jsb_module_resolver.cpp:454
DefaultModuleResolver::check_search_path    jsb_module_resolver.cpp:471  ← 崩溃点
```

`check_search_path` 第 471 行是 `if (p_module_id[0] != '.')`。`p_module_id` 为**空**时
`String::operator[]` 返回 `_cowdata.get(0)` → 越界读 → `0xC0000005`。

**实测确认**：加临时诊断后日志逐条打印
`[DIAG] load_module_immediately path='' script_path=''` ×4（`.agent_tmp/dump_guarded.log`）——
即编辑器流程中存在**无路径的 `GodotJSScript`**，对其取属性即触发 `_get` → 强制加载 → 空模块 id
→ 越界。

**为何是本轮引入**：改动前 `GodotJSScript` 没有 `_get` override，`Object::get` 不会到达
`load_module_immediately`；本轮的 `_get`（阶段 2）让这条路径**首次可达**。

**修复**：`load_module_immediately()` 在空路径时提前返回（`jsb_script.cpp:552-560`）。
这与同文件既有 `load_module_if_missing()` 的空路径守卫**同型**（该函数早已认定"无路径脚本"
是合法状态），是最小且一致的修法。

**修复后实测（全绿）**：

| 项 | 结果 |
|---|---|
| dump | ✅ `extension_api.json` 11870734 字节，**0** 次崩溃 |
| `misc/verify_codegen.py` 全链路 | ✅ dump → api-generate → generate-types 全部完成，`project/gen`、`project/typings`、`.godot/.api_dumping` 均落盘 |
| `jsb.runtime.gen.d.ts` 含 `exposed` | ✅（本轮预期变化，A11 归因见下） |
| 项目 TS 类型检查 | ✅ `TSC_RC=0` |
| C++ 单测 | ✅ `TESTS_RC=0` |
| 完整验收 | ✅ `ACC_RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0** |

**A11 的 15 处基线差异归因**（`verify_codegen.py` 报 15 处，全部可归因）：

| # | 差异 | 归因 |
|---|---|---|
| 1 | `[gen] godot/tests/operators/Operators.tscn.gen.ts` | **测试项目内容漂移**：场景挂的 `test-operators.ts` 在基线与现工作区之间变了（基线生成时该 TS 未编译/未识别脚本类型，现为 `PackedScene<TestOperators>`）。与生成器无关 |
| 2 | `[typings] jsb.runtime.gen.d.ts`（+4 行 `exposed`） | **本轮预期变化**（阶段 3.1 的注解登记） |
| 3 | `[typings] godot.minimal.d.ts`（`STATIC_BINDING_ENABLED`→`BINDING_MODE`；+2 行 `add_script_*`） | `BINDING_MODE` 来自 commit `fcc1123`（2026-09-21，**晚于**基线 2026-09-20）；两行 `add_script_*` 为本轮预期变化 |
| 4 | `[typings] godot0..godot10.gen.d.ts`（11 个分片，共 1971 行） | **引擎版本输入漂移**：**基线输入是 4.8.dev dump**（基线含 `FuzzySearch`/`AnimationNodeObserver`/`StreamedTexture2D`/`TextureStreaming`/`Trail3D`/`Line3D`/`ScenePaint2DEditor`/`VisualShaderGroup`/`BoneSpreader3D`/`ResourceImporterStreamedTexture`/`CompressProfile`/`SensorOrientation`/`get_preferred_locales`/`ui_toggle_fullscreen`/`RENDER_STREAMING_TEXTURE_MEM_USED`，**这些符号只存在于 `D:/Dev/godot/godot`（4.8.dev）源码**），而现工作区 dump 自官方 4.7.2（`extension_api.json` 头部 `version_minor:7 version_patch:2 version_status:stable version_build:official`）⇒ 是**基线用错引擎版本**，非生成器回归。**此前写「基线由官方 4.7.2 dump 生成」方向相反，已纠正**。分片**文件级** diff 另由布局抖动主导（`godot10` 108598→25661 B），故按 `codegen-baseline.md:72` 第 4 条做语义级比较：归一化行并集 **baseline-only 423 / live-only 50** |

> 基线快照时间 2026-09-20 17:33；`BINDING_MODE` 引入于 2026-09-21 ⇒ 基线**早于**该 commit，
> 差异属输入漂移。**基线需要刷新**（`--update-baseline`，且须用**同一引擎**：官方 4.7.x，与 CI 的
> `GODOT_VERSION: tags/4.7.1-stable` 对齐）以吸收引擎漂移与本轮预期变化——
> 但这会掩盖真实回归，故先记录归因，刷新动作待用户确认。

---

## 阶段 3.2–3.5 实施（2026-09-25，本轮）

### 共享静态变量：进程级权威存储

- 新增 `src/runtime/bridge/jsb_shared_statics.{h,cpp}`：`jsb::SharedStatics` 命名空间
  （`ensure` / `get` / `set` / `retain` / `clear`）。存储为进程级
  `HashMap<StringName, HashMap<StringName, Variant>>`，`std::recursive_mutex` 保护。
  锁序恒为 `GodotJSScriptLanguage::mutex_` → 本锁（叶子锁），无环。
- `GodotJSScriptLanguage::_finish()` 调 `SharedStatics::clear()` —— 与 Orphan StringName
  缺陷 A 同型处理（进程级静态容器持有 StringName 必须有显式释放点）。

### `GodotJSScript` 成员面

| 成员 | 作用 |
|---|---|
| `_get_constants()` | **只报自有**（2026-09-26 修正；`get_script_constant_map()` 是 ClassDB 绑定，合并即偏离 GDScript） |
| `_get_members()` | 实例成员名集合（去重已修，见下） |
| `_get(name)` | 常量 → 共享静态变量 → base 链 |
| `_set(name)` | **只**查静态变量（常量故意不查 ⇒ 写常量返回 false） |
| `_get_property_list()` | `script/source`（internal）+ 静态变量，本脚本向上走（派生遮蔽基类） |
| `_get_script_property_list()` | 静态变量（**必须**，见下） |

**实测发现的关键缺口**：静态变量**必须同时**进 `_get_property_list` 与
`_get_script_property_list`。`GDScriptAnalyzer::reduce_identifier_from_base`
（`gdscript_analyzer.cpp:4351`）解析**非 GDScript** 脚本的成员时**只读
`Script::get_script_property_list`**（GDVIRTUAL `_get_script_property_list`），
`Object::_get_property_list` 是另一个（inspector 用）钩子。只实现后者时
`S.score` 在分析期报 `Cannot find member "score" in base "res://..."`。
GDScript 自己有 `STATIC_VARIABLE` 通道，外来脚本没有。

### `HashSet::insert` 去重缺陷（4 处，已修）

`HashSet::insert` 返回 **`Iterator`**，不是 `bool` —— 其 `operator bool` 只是
`_keys != nullptr`（恒真）⇒ `if (set.insert(k))` **永远成立**，去重静默失效。
A7 探针实测 `scriptlist-dupes=["shadowed"]` 暴露该 bug；同一缺陷也存在于
`_get_members()`。全部 4 处已改为 `if (!set.has(k)) { set.insert(k); ... }`。

### 空模块 id 越界崩溃（阻塞解除）

- **症状**：`--generate-types` / `--dump-extension-api-with-docs` 以
  `exit=3221225477`（`0xC0000005`）崩溃且产物未落盘。
- **A/B 对照**：纯引擎项目 dump ✅；main 检出用其改动前 DLL ✅；本 worktree 换入 main
  改动前 DLL ✅；用本轮改动 DLL ❌ ⇒ 崩溃由本轮改动引入。
- **根因（PDB 符号化调用栈）**：`_get` → `ensure_module_loaded` →
  `load_module_immediately` → `Environment::load` → `check_search_path` 第 471 行
  `if (p_module_id[0] != '.')` —— **空模块 id** 使 `String::operator[]` 越界读。
  编辑器流程中存在**无路径的 `GodotJSScript``（诊断日志逐条打印 `path=''` ×4）。
  改动前 `GodotJSScript` 没有 `_get` override，`Object::get` 不会到达该路径；
  本轮 `_get` 让它**首次可达**。
- **修复**：`load_module_immediately()` 空路径时提前返回（`jsb_script.cpp:552-560`），
  与同文件既有 `load_module_if_missing()` 的空路径守卫同型。

### 探针实测证据

| 探针 | 结果 |
|---|---|
| `_acc_s1.gd`（共享静态读写） | `score-init=0` → 写 `42` → 写 `7`；`has-score-prop=true`；`has-script-source=true`；`scriptlist-has-score=true`；`N=42`；`unknown=<null>` |
| `_acc_s2.gd`（A7 继承链） | `DERIVED_N=22`、`BASE_N=11`、`derived_score=2`、`base_score=1`、`shadowed=200`；写 `base_score=111`、`shadowed=999` 生效；**`scriptlist-dupes=[]`**；常量**不在**可写属性列表 |

### 构建与验收

| 项 | 结果 |
|---|---|
| `scons ... tests=yes` | `SCONS_EXIT=0`，dll md5 `74ab3f6ac30efcd2fe47c8bd67f09e8a`（06:50:33），已装到 addon（两处 md5 一致） |
| `verify_codegen.py` 全链路 | dump ✅（`extension_api.json` 11870734 字节，0 崩溃）→ api-generate → generate-types 全部落盘 |
| `jsb.runtime.gen.d.ts` 含 `exposed` | ✅ |
| 项目 `tsc` | `RC=0` |
| C++ 单测 | `RC=0`，51/51 cases、644/644 assertions |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0** |

### A3/A10 测试项目用例（新增）

- `project/tests/static-members/static-members-target.ts`（常量 / 枚举 / 容器 / 被拒形态 / 共享静态 `score`）
- `project/tests/static-members/static-members-peer.ts`（worker 侧，响应 `"read"` / `"write"`）
- `project/tests/static-members/test-static-members.ts`（`TestStaticMembers extends Node`，
  `async _ready()` + `beginAsyncTest`/`endAsyncTest` + `completeCallback`；断言 JS 侧常量值、
  容器只读（含嵌套）、未注解成员、共享静态本地读写、**跨环境**：worker 读到主环境写的 11、
  worker 写 2000 后主环境读到 2000）
- `project/tests/static-members/StaticMembers.tscn` + `project/tests/start.ts` 的 `scenes` 登记

---

## 阶段 3.5：测试项目用例、Godot 侧断言与清理（2026-09-25 续）

### A3 / A10 / A7：测试项目用例（永久覆盖）

`project/tests/static-members/`（已登记进 `project/tests/start.ts` 的 `scenes`，`START-DIAG scenes=10`）：

| 文件 | 作用 |
|---|---|
| `static-members-target.ts` | 常量（基础值 / enum / 容器 / 被拒形态）+ 共享静态 `score` |
| `static-members-derived.ts` | 派生类：`N` 遮蔽基类常量、`score` 自有槽位（A7 基类链合并） |
| `static-members-peer.ts` | worker 侧，响应 `"read"` / `"write"` |
| `test-static-members.ts` | `TestStaticMembers extends Node`，`beginAsyncTest`/`endAsyncTest` + `completeCallback` |
| `StaticMembers.tscn` | 场景（`ext_resource` 指向 `test-static-members.ts`） |

`test-static-members.ts` 断言分两半：

- **JS 侧**（`checkConstants`）：常量值、BigInt 保持 `123n`、容器只读（含嵌套递归）、未注解成员不收集
- **Godot 侧**（`checkGodotSide`，读 `Script::get_script_constant_map()` 与
  `get_script_property_list()`）：A1/A2/A13 的**引擎可见面** —— 常量值、enum 为
  `Dictionary{name:int}`、容器 `is_read_only()`、被拒形态（`VEC`/`LIT`/`FN`/`PLAIN`）**一个都不在**、
  共享静态**不**泄漏进常量表、成员清单含 `score` 且**无重复**（`HashSet::insert` 缺陷的回归守卫）、
  派生类遮蔽（`N=22`）且继承基类常量（`F=1.5`）
- **跨环境**（`checkCrossEnvironment`）：worker 读到主环境写的 `11`；worker 写 `2000` 后主环境读到 `2000`

### 负向控制（证明跨环境断言非空转）

把 `worker reads the main environment value` 的期望从 `11` 改成 `0`（= 每 isolate 各自跑初始化器的值）
后重跑：`completed=0`、`failed=1`，失败项正是
`GODOTJS_TEST_PROJECT_FAILED: worker reads the main environment value`
⇒ worker 实测读到 **11**（共享存储），不是 `0`（per-isolate 初值）。断言真实生效，已还原。

### A8：热重载

两条互补证据：

- **C++ 单测**（`script shared static store`）：同一 class 对象以更窄的注解数组重解析 ⇒ `GONE` 从
  `static_variables` 与存储中**同时消失**、`SHARED` 的**活值 `7` 保留**（`retain` 的幂等剪枝）。
- **实机探针**（`.agent_tmp/acc/_acc_a8.gd`（临时拷入 `project/` 运行，已清理））：
  ```
  before-props=["score"]        before-score=0
  after-write-score=55          reload-err=0
  after-reload-props=["score"]  after-reload-dupes=[]
  after-reload-score=55         after-reload-has-N=true
  after-reload-N=42             after-reload-ARR-ro=true
  ```
  ⇒ 重载后成员集**不重复、不残留**，活值 `55` 保留，常量与只读标记完好
  （重载路径会重新走 accessor 安装分支，此处证明该分支不破坏状态）。

### C4：spec 三条引擎事实

`.trellis/spec/godotjs-ext/cpp/architecture-constraints.md` 的「其他陷阱」新增：

1. `HashSet::insert` 返回 `Iterator` 不是 `bool` ⇒ `if (set.insert(k))` 恒真，去重静默失效
2. 非 GDScript 脚本的成员在 GDScript 分析期**只经 `Script::get_script_property_list`** 解析
   （`Object::_get_property_list` 是另一个钩子）⇒ 要暴露的成员必须同时进两者
3. 空 `StringName` 上 `operator[]` 越界崩溃（`0xC0000005`）；修法在 `load_module_immediately()` 入口

### A12 / C3：文档

`scripts/jsb.runtime/src/godot.annotations.ts` 的 `exposed.const()` / `exposed.shared()` JSDoc 补全：

- 常量是**解析期快照**；TS `readonly` 被擦除 ⇒ JS 侧仍可写但不联动；不可变性**只对 Godot 侧成立**
- 容器常量与 JS 侧共享存储 ⇒ 冻结后 JS 侧写该容器会报 engine error（行为突变是"两侧真不可变"的代价）
- 枚举成员 GDScript 侧**须用索引访问**（`SomeScript.E["Red"]`），点访问编译期报错
- `const enum` 不支持（编译期擦除）
- **静态函数三种调用形态均不支持**及原因（无环境可定位 + 跨环境状态静默分叉）

> 注：bundle `.d.ts` 不含 JSDoc（实测 `grep -c "Expose a" project/typings/jsb.runtime.bundle.d.ts` = 0）
> ⇒ 纯注释改动不改变任何生成产物，无需重编。

### 中间验收（dll `7a20d3026ea7a9171f54597dd9c794f8`，已被本文件末尾的最终验收取代）

| 项 | 结果 |
|---|---|
| `scons ... tests=yes` | `SCONS_EXIT=0`，两处 dll md5 一致 |
| C++ 单测 | `RC=0`、**52/52 cases、669/669 assertions** |
| 项目 `tsc` | `RC=0` |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=10` |
| `verify_codegen.py` | 24 处差异（探针未清理时的计数），**全部归因**（见下） |

**A11 差异归因**（清理前 24 处 = 前序 15 处 + 本轮 9 处；清理探针后为 20 处，见文末）：

| 类别 | 数量 | 归因 |
|---|---|---|
| `[gen] 多余` 探针/测试文件 | 10 → 清理后 6 | 本轮新增的 `project/tests/static-members/*`（6 处，保留）与已删探针（`_acc_*`、`test_static_members.ts`，4 处，清理后消失）——**输入漂移**，非生成器回归 |
| `[gen] 内容不同` `Operators.tscn.gen.ts` | 1 | 测试项目内容漂移（前序已归因） |
| `[typings] jsb.runtime.gen.d.ts` | 1 | 本轮预期（`exposed` 注解登记） |
| `[typings] godot.minimal.d.ts` | 1 | `BINDING_MODE`（commit `fcc1123`，晚于基线）+ 本轮 `add_script_*` |
| `[typings] godot0..10.gen.d.ts` | 11 | **引擎版本输入漂移**：基线输入是 **4.8.dev** dump（含 `FuzzySearch`/`AnimationNodeObserver`/`StreamedTexture2D`/`TextureStreaming`/`Trail3D`/`Line3D`/`ScenePaint2DEditor`/`VisualShaderGroup`/`BoneSpreader3D`/`CompressProfile`/`get_preferred_locales`/`ui_toggle_fullscreen`/`RENDER_STREAMING_TEXTURE_MEM_USED`，均只存在于 `D:/Dev/godot/godot` 4.8.dev 源码），现工作区 dump 自官方 4.7.2 ⇒ 基线用错引擎版本。**此前写「基线由官方 4.7.2 dump」方向相反，已纠正**。分片文件级 diff 另受布局抖动主导，语义级并集 = baseline-only 423 / live-only 50 |

> 基线刷新（`--update-baseline`）会吸收上述输入漂移，但也会掩盖真实回归 ⇒ **待用户确认**。

### GDScript 侧永久用例（A1/A2/A3/A7 + R5.1 守卫）

`.agent_tmp/acc/_acc_w*.gd` 探针已删除，其结论原本只存在于日志里 ⇒ 补成永久用例
`project/tests/static-members/static-members-gdcheck.gd` + `StaticMembersGd.tscn`
（登记进 `start.ts`，`START-DIAG scenes=11`）：

- **该文件的「解析」本身就是断言**：`S.N` / `S.score` 由 `GDScriptAnalyzer::reduce_identifier_from_base`
  解析，而它探测非 GDScript 脚本时**先查 `get_script_property_list`、再对每个标识符调
  `get_method_info()`（无 `has_method` 守卫）**。R5.1 一旦回归（任意名字返回非空 `MethodInfo`），
  这些访问全部解析失败、场景加载即报错 —— 这是 R5.1 的回归守卫
- **值断言**（A1/A2/A13）：基础值（`N`/`F`/`S`/`B`/`NUL`/`BIG`）、enum 为 `Dictionary` 且
  `E["Blue"] == 2`（**索引访问**，点访问编译期报错）、容器内容与 `is_read_only()`（含嵌套递归）、
  常量表含 `N` 而**不含** `VEC`/`LIT`/`FN`/`score`
- **A3 读写**：`S.score = 77` → 读回 `77`，随后还原为 `0` ⇒ 与 JS 侧用例的写值**不冲突**
  （顺序无关）
- **A7**：派生脚本 `D.N == 22`（遮蔽）、`D.F == 1.5`（继承）、`D.score == 2`（自有槽位）

**负向控制**（证明用例非空转）：把 `_check("int", n, 42)` 改成 `999` 后重跑 ⇒
`GODOTJS_TEST_PROJECT_FAILED: static-members(gd) int: expected 999, got 42`、
`STATIC-MEMBERS-GD-OK` 消失；还原后 `gdok=1`、`failed=0`。

### 收尾清理（C5）

- 删除 `project/` 下全部探针：`_acc_base.ts`、`_acc_derived.ts`、`_acc_s1.gd(.uid)`、
  `_acc_s2.gd(.uid)`、`_acc_a8.gd(.uid)`、`test_static_members.ts`（已被 `tests/static-members/` 取代）
  及其编译产物与 `project/gen/godot/` 下对应的陈旧 `.gen.ts`
- 删除 `.agent_tmp/` 重型目录：`dumpprobe/`、`dllbackup/`、`sym/`、`gdcallable/`、`tsprobe/`
  与若干 50~110MB 的 dump/反汇编中间产物（`.agent_tmp` 由 ~900MB 降至 ~23MB）
- 任务文档里指向已删探针的路径改为实际位置或标注「任务收尾已清理」
  （`prd.md` / `design.md` / `implement.md` / `report.md` / `research/*`）

### 最终验收（dll `f22e649456743df8f3d31043b8e18294`，两处部署位 md5 一致）

| 项 | 结果 |
|---|---|
| `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `SCONS_EXIT=0` |
| C++ 单测 `--jsb-run-tests` | `RC=0`、**52/52 cases、669/669 assertions**、`Status: SUCCESS` |
| 项目 `tsc`（不加 `--noCheck`，先删 `.godot/.tsbuildinfo`） | `RC=0` |
| 完整验收 `--audio-driver Dummy --headless --path . --verbose` | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| 负向控制 ×2 | 跨环境读（期望 `0` 而非 `11`）→ `failed=1`；GDScript 值（期望 `999`）→ `failed=1`；均还原后转绿 |

### A6 闭环：`_get_members()` 的直接单测

`_get_members()` 无脚本绑定（`Script::get_members` 不在 ClassDB），消费方是远程调试器
（`scene_debugger_object.cpp:104`）⇒ **C++ 单测是唯一可测入口**。新增用例
`[runtime] [jsb] script members: inherited, and without duplicates`：

- 夹具补 `@bind.export` 实例成员 `tag`（基类与派生类**都声明**，即 `HashSet::insert` 缺陷的触发形状）
- 断言：基类 `_get_members()` 含 `tag`、**不含** `score`（静态成员不是实例成员）；
  派生类 `_get_members()` 中 `tag` 恰好 **1** 次

**负向控制**：删掉 `!inserted.has(member)` 守卫 → 重编 → 
`test_jsb_static_members.h(352): ERROR: tag_count == 1 values: 2 == 1`、`TESTS_RC=1`；
还原后 **53/53 cases、679/679 assertions** 绿。

> 该 `tag` 重声明也影响 `get_script_property_list()`：实例属性路径**本来就不跨基类去重**
> （既有行为，与本轮无关）⇒ TS 侧断言已改为只针对 `score`（共享静态）计数，不误判既有行为。

### 最终验收（dll `89394e34620e79d8511417bfc2a2acfe`，两处部署位 md5 一致）

| 项 | 结果 |
|---|---|
| `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `SCONS_EXIT=0` |
| C++ 单测 `--jsb-run-tests` | `RC=0`、**53/53 cases、679/679 assertions**、`Status: SUCCESS` |
| 项目 `tsc` | `RC=0` |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| 负向控制 | 跨环境读（`11`→`0`）`failed=1`；GDScript 值（`42`→`999`）`failed=1`；`_get_members` 去重（删守卫）`TESTS_RC=1` —— 三者还原后全绿 |

**验收状态**：A1–A10、A12–A15 全部已验证；**A11 未通过**（20 处差异已逐项归因，`--update-baseline` 待用户确认）。

### 未纳入本轮的既有/附带变化（如实列出）

- `project/icon.svg.import`：被引擎 headless 编辑器流程**重写**（删了 3 行 `compress/high_quality_mode`、
  `mipmaps/preserve_alpha_test_coverage`、`mipmaps/alpha_test_threshold`）。**与本功能无关**，
  是引擎导入器写回；未还原（AGENTS.md 禁止未经确认还原文件），留待用户决定。
- `project/tests/static-members/` 下暂无 `.uid` 文件（其他测试目录有，被 git 跟踪）。功能不受影响
  （验收日志 `uid` 相关警告 = 0），如需入库一致性可由编辑器导入时生成。
- `.trellis/tasks/09-06-lowprio-tree-sitter-ast/research/`：前序任务遗留，非本轮产生。

---

## 追加轮（2026-09-25）：注解双形态（#1）+ 审查项修复

### 目标

用户当轮提出：注解必须支持**两种声明形态并存** —— ① 类内 `static readonly` + 逐成员注解（现状）；
② **类外同名命名空间声明 + 类级注解点名**。用户理由：「他们实际上没有区别呀……最终都是要显示点名
通过装饰器让他们不能改绑定的值，毕竟 `static readonly` 运行时也只是 `static`，不是真的不能换成另一个值。」

### 结论（先验证，后实现）

**两种形态在运行期完全同形，C++ 解析侧零改动。** 探针 `.agent_tmp/nsprobe/`（已清理）：

| 探针 | 结论 |
|---|---|
| `shape.mjs` | 类内 static 与命名空间成员**同值下描述符逐字段相等**（`own/enumerable/writable/configurable` 全同，`full equal: true`）⇒ 运行期无从分辨来源 |
| `probe3.ts` | `class X { enum Inner { A } }` → **TS1068**（类内不能声明枚举） |
| `probe4.ts` / `probe5.ts` | `@dec export const X` / `namespace H { @dec export const X }` → **TS1206**（装饰器不能挂在命名空间成员上） |
| `order.ts` | 类装饰器执行时命名空间成员**尚未挂上**（装饰期 own props = `["length","name","prototype"]`）⇒ 类级装饰器**只能记名字**，值由解析期按名到 `class_obj` 上取 |
| `coexist.ts` / `answer.ts` | 两形态并存，注册顺序与结果一致，全部归到同一通道 |

⇒ **唯一改动点** = `exposed.const` / `exposed.shared` 按**实参个数**分派（无参 → 成员装饰器；
有参 → 类装饰器点名）。**命名空间形态必须用类级名字列表**，因为 TS1206 不允许装饰命名空间成员。

**TS2652 陷阱（实测）**：类与同名命名空间合并时，类**不能**带 `export default` 修饰符 ——
`Merged declaration 'Target' cannot include a default export declaration`。必须写成
`class Target {}` + `namespace Target {}` + 独立语句 `export default Target;`（已写进 JSDoc 示例）。

### 改动

| 文件 | 改动 |
|---|---|
| `scripts/jsb.runtime/src/godot.annotations.ts` | `exposed` 子对象下新增两个**局部重载函数** `exposed_const` / `exposed_shared`（对象字面量方法无法承载重载签名）；`const` / `shared` 改为引用它们；JSDoc 写明两形态等价、TS1206/TS2652 限制、类级点名是解析期解析 |
| `src/editor/codegen/jsb_codegen_annotations.cpp` | `build_class_binder()` 的 `exposed` 叶子由 `() => ClassMemberDecorator` 改为**两个调用签名的交集**（`DescriptorType` 无 rest 支持，用 `make_intersection`）：`(() => ClassMemberDecorator) & ((...names: string[]) => (target, context) => void)`。**顺序敏感**：零参签名必须在前（rest 签名也接受零参，靠前者优先解析） |
| `scripts/typings/godot.generated.d.ts` | 手工维护的 `ClassBinder` 镜像同步补 `exposed`（该文件是 `godot.annotations.ts` 在独立检出时的编译前提） |
| `project/tests/static-members/static-members-namespaced.ts` | **新增**：类级点名形态夹具（`@bind.exposed.const("NS_N","NS_E","NS_ARR","NS_DICT","NS_MISSING")` + `@bind.exposed.shared("nsScore")` + 类内成员形态 `NS_INNER` 共存） |
| `project/tests/static-members/test-static-members.ts` | 新增 `checkNamespacedForm()` |
| `project/tests/static-members/static-members-gdcheck.gd` | 新增类级形态的 GDScript 侧断言 |

### 顺带修复的审查项（同轮，均为实测驱动）

| # | 位置 | 修复 |
|---|---|---|
| 1 | `jsb_class_info.cpp` `_shared_static_setter` | **静默写 NIL 缺陷**：原逻辑对不可转换的非数组 JS 值（如纯对象）会落到 `SharedStatics::set(..., 默认构造的 NIL)`，把共享值悄悄清成 NIL。改为按 `js_to_gd_var` 返回值判定，不可转换即丢弃并告警 |
| 2 | `jsb_class_info.cpp` 共享静态段 | **`const` + `shared` 双标矛盾**：同一成员同时标两种注解时，原先会**同时**进 `constants` 与 `static_variables` ⇒ `_get`（冻结快照）与 `_set`/访问器（可写存储）语义打架。新增 `annotated_constant_names` 集合（在常量段**取值前**登记，故常量被白名单拒绝时也照样报冲突），共享静态段命中即告警跳过 ⇒ **常量胜出**，`_get`/`_set`/`_get_property_list` 三方一致 |
| 3 | `jsb_class_info.cpp` `_try_parse_enum` | **文档化**（不改行为）：反向映射一致性检查会整体拒绝**重复值枚举**（`enum E { A=0, B=0 }`，TS 只发一条反向项 `{0:"B"}`）与浮点值枚举。两者都降级为「忽略并告警」，不产生错误常量 —— 写明这是不可表示枚举的既定失败模式 |
| 4 | `jsb_shared_statics.cpp` 锁注释 | **注释不实**：原文称锁序恒为 `GodotJSScriptLanguage::mutex_` → 本锁，实际 `load_module_immediately` 加载/解析期**不持** `mutex_`（`mutex_` 只在 `jsb_script.cpp` 的 rebind 循环取）。改为只声明「本锁是叶子锁」这一成立的事实 |

### 验证（全部实测）

| 项 | 结果 |
|---|---|
| `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `BUILD_EXIT=0` |
| C++ 单测 `--jsb-run-tests` | `RC=0`、**53/53 cases、681/681 assertions**、`Status: SUCCESS`（+2 断言 = 冲突守卫） |
| `pnpm gen:types`（含 `tsc` 强制类型检查，先删 `.godot/.tsbuildinfo`） | `RC=0` ⇒ 新夹具的**类级形态经重生成的 `ClassBinder` 类型检查通过** |
| 重生成的 `project/typings/jsb.runtime.gen.d.ts` | `exposed.const` / `exposed.shared` 均为交集形态（见下） |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| 两处部署位 dll md5 | 一致（`c3f10d8dc863eb18a139c1492eda88e9`） |

重生成产物（`project/typings/jsb.runtime.gen.d.ts`）：

```ts
exposed: {
    "const": (() => ClassMemberDecorator)
        & ((...names: string[]) => ((target: GObjectConstructor, context: ClassDecoratorContext) => void));
    shared: (() => ClassMemberDecorator)
        & ((...names: string[]) => ((target: GObjectConstructor, context: ClassDecoratorContext) => void));
};
```

**两条新守卫的负向控制（按 spec 要求，人为削减 → 确认 FAILED → 还原 → 确认绿）**：

| 削减 | 结果 |
|---|---|
| A：冲突守卫改成 `if (false && …)` | C++ 单测 `TESTS_RC=1`、`53 | 52 passed | 1 failed`，失败点**正是** `test_jsb_static_members.h(250): ERROR: !class_info->static_variables.has("CONFLICT")` |
| B：类级形态改为不注册名字（`void name;`） | 完整验收 `GODOTJS_TEST_PROJECT_COMPLETED=0`、`FAILED=1`，失败项 `GODOTJS_TEST_PROJECT_FAILED: class form int` |

两者还原（字节级复原，md5 核对）后全绿 ⇒ 守卫非空转。

### A11 复核（差异数 20 → 24，逐项归因完成）

`python misc/verify_codegen.py --godot <引擎>` → `VC_EXIT=1`，**24 处差异**。按目录逐文件实测分类：

| 类别 | 数量 | 归因 |
|---|---|---|
| `[gen] 多余` | **10** | 全部在 `gen/godot/tests/static-members/` 下，即本轮新增夹具（8 个源文件 → 10 个生成产物：4 个场景/节点 + 6 个脚本）。基线建立于该目录**尚不存在**时 |
| `[gen] 内容不同` | 1 | `godot/tests/operators/Operators.tscn.gen.ts` —— 前序已归因的既有漂移 |
| `[typings] 内容不同` | 13 | 见下 |

13 处 typings 中**只有 1 处与本轮改动相关**：

- `jsb.runtime.gen.d.ts` —— **本轮预期**：`+14 行`，即上表的 `exposed` 交集声明。已逐行核对，无其他变化。
- `godot.minimal.d.ts` —— 2 处：`STATIC_BINDING_ENABLED` → `BINDING_MODE`（来自 commit `fcc1123`，**晚于**基线）+
  本轮 `add_script_constant` / `add_script_shared_static` 两行声明。
- `godot0..godot10.gen.d.ts`（11 个分片）—— **引擎版本输入漂移**：**基线输入是 4.8.dev dump**
  （基线含 `FuzzySearch`/`AnimationNodeObserver`/`StreamedTexture2D`/`TextureStreaming`/`Trail3D`/
  `Line3D`/`ScenePaint2DEditor`/`VisualShaderGroup`/`BoneSpreader3D`/`ResourceImporterStreamedTexture`/
  `CompressProfile`/`SensorOrientation`/`get_preferred_locales`/`ui_toggle_fullscreen`/
  `RENDER_STREAMING_TEXTURE_MEM_USED` —— 经 grep 证实**只**存在于 `D:/Dev/godot/godot`（4.8.dev）源码；
  用 4.8.dev 引擎实跑 `--dump-extension-api-with-docs` 得到的 json 头部为
  `4.8.0 / dev / custom_build` 且**全部含**这些符号），而现工作区 dump 自官方 4.7.2
  （`project/extension_api.json` 头部 `version_minor:7 version_patch:2 version_status:stable
  version_build:official`，11.87 MB）⇒ **基线建立时用错了引擎版本**，非生成器回归。
  **此前写「基线由官方 4.7.2 dump 生成」方向相反，已纠正**。
  分片的**文件级** diff 另受**布局抖动**主导（`godot10` 基线 108598 B → live 25661 B、
  `godot1` 585764 → 653181，其余 ±2~8%）⇒ 按 `codegen-baseline.md:72` 第 4 条改用语义级比较
  （归一化行取并集）：**baseline-only 423 行 / live-only 50 行**；后者含真实 4.7↔4.8 差异
  （`ScriptEditor extends PanelContainer` vs 基线的 `EditorDock`、`compress(..., astc_format?)` vs
  `profile?`、`get_display_cutouts()` vs `(screen?: int32)`、`reset_bone_pose(bone_idx)` 少一个可选参）。
  基线刷新须用**与验证同一引擎**（官方 4.7.x，对齐 CI 的 `tags/4.7.1-stable`）。

⇒ **本轮未引入新的差异类别**；20 → 24 的增量**全部**是新增夹具的生成产物（10 − 6 = +4，因基线早于该目录存在）。
`--update-baseline` **仍未执行**（会掩盖真实回归），待用户确认。

### 未纳入本轮的既有/附带变化（如实列出）

- `project/icon.svg.import`：被引擎 headless 编辑器流程**重写**（删了 3 行 `compress/high_quality_mode`、
  `mipmaps/preserve_alpha_test_coverage`、`mipmaps/alpha_test_threshold`）。**与本功能无关**，
  是引擎导入器写回；未还原（AGENTS.md 禁止未经确认还原文件），留待用户决定。
- `project/tests/static-members/` 下暂无 `.uid` 文件（其他测试目录有，被 git 跟踪）。功能不受影响
  （验收日志 `uid` 相关警告 = 0）。
- `.trellis/tasks/09-06-lowprio-tree-sitter-ast/research/`：前序任务遗留，非本轮产生。
- **静态函数的暴露**仍未做（Q1=(c)）；`#2` 的「枚举成员补全」对本项目不可达（引擎把外来脚本的常量
  硬编码为 `BUILTIN`/`DICTIONARY` meta，补全分支要求 `DataType::ENUM` + ClassDB 类名），
  当前只有索引访问 `SomeScript.E["Red"]`，已写进 JSDoc。

### 已知边界（如实记录，未改代码）

- **`const` + `shared` 双标且常量被白名单拒绝时，该成员两种形态都不暴露**：`annotated_constant_names`
  在**取值前**登记（这是为了让冲突在常量被拒时也照样报出来），因此守卫对"双标 + 常量不可表示"
  （如值是 `Vector2`）同样生效 —— 结果是该成员**既不是常量也不是静态变量**，只打两条 Warning。
  这是"注解自相矛盾即整体忽略"的既定取舍，不是缺陷；若要改成"常量不可表示时回退为静态变量"，
  需要把登记点移到取值成功之后，并接受"双标 + 常量被拒时静默变成可写静态变量"的语义。
- **`exposed.const` / `exposed.shared` 的类级形态不校验名字是否存在于类对象**：不存在的名字在解析期
  按既有规则打 `Warning … ignored (not an own property of the class)` 并跳过（夹具的 `NS_MISSING`
  覆盖此路径）。TS 侧也无法静态校验，因为名字是字符串实参。

---

## 独立静态审查（`trellis-check`，5/6 PASS + 1 FAIL）与修复

审查限定了「**不重编、不重跑测试、不做 git 操作**」的预算边界（重跑已有绿证属明令禁止的浪费），
逐项给出 PASS/FAIL + file:line 证据。**未重新推导**已核实事实。

### PASS（4 项，直接读码取证）

| # | 结论 | 证据要点 |
|---|---|---|
| 1 | 重载分派正确 | 单一实现按 `names.length > 0` 分派；成员分支在触碰数组**前**校验 `context`/`name`/`context.static`，类上下文不可能静默注册名字；接线 `const: exposed_const` / `shared: exposed_shared` 正确 |
| 2 | 代码生成与实现一致 | 生成的交集零参签名在前，与运行期分派顺序一致；`project/typings/jsb.runtime.gen.d.ts` 逐行核对 |
| 3 | setter 不再写 NIL | 三条失败路径**全部**在 `SharedStatics::set` 之前 return；唯一 `set` 仅 `converted` 为真时到达；`null`→NIL 是有意保留 |
| 4 | 冲突守卫位置正确 | 登记在取值/白名单**之前**；守卫先于 `ensure` / `static_variables.insert` / `shared_static_names.insert` ⇒ `retain` 也无法保留该槽位；终态自洽 |
| 6 | 无负控残留 | 目标文件 grep `NEGATIVE CONTROL` / `false &&` / `void name;` 等全部无命中 |

### FAIL（1 项，真缺陷：覆盖不诚实）+ 2 项脆弱性

| 项 | 审查结论 | 处置 |
|---|---|---|
| **FAIL**：`_shared_static_setter` 的 NIL 修复**无永久断言** | 全仓无任何测试写入不可转换值后断言存储值未变 ⇒ **回退该修复后测试仍全绿** | **已修**：`test_jsb_static_members.h` 的 store 用例新增一段 —— 向已安装访问器写入纯 JS 对象，断言存储值仍为 `7`（`+3` 断言） |
| 脆弱性 A：类级分支不校验 `context.kind` | 错位的 `@bind.exposed.const("X")` 会记到 prototype 上，解析期只读类对象自有属性 ⇒ **静默丢弃、无任何告警** | **已修**：两个类级分支均加 `context.kind !== "class"` 抛错（参数类型放宽为 `ClassDecoratorContext \| ClassMemberDecoratorContext` 以便该检查有意义）。TS 本就拒绝该写法，此守卫把「静默丢弃」变成「明确报错」 |
| 脆弱性 B：共享槽位的绝对初值断言与场景顺序耦合 | `score == 0` / `nsScore == 3` 依赖「本场景是进程内首个写者」 | **不改，记为已知边界**：每轮验收是新进程 + `_finish()` 清空存储 ⇒ 初值由初始化器确定性播种；仅当新增场景在 static-members 之前写同名槽位才会失效 |

### 审查后复验（全部实测，dll 重新构建）

| 项 | 结果 |
|---|---|
| `scons … tests=yes -j5` | `BUILD_EXIT=0`；bundle 含新守卫 |
| C++ 单测 | `RC=0`、**53/53 cases、684/684 assertions**（+3 = 新增 setter 断言） |
| `pnpm gen:types`（含强制 `tsc`） | `RC=0`、**tsc error 数 = 0**（放宽后的类级参数类型仍被生成的 `ClassBinder` 接受） |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |


---

## 质询答复轮（Q1–Q7）与由此触发的修复（2026-09-25）

### 目标

回答用户对上一轮交付的 7 个质询（A11 归因细节 / 具名枚举在 `get_constants()` 的形态 / 注解误用是否报错 /
`jsb_codegen_annotations.cpp` 改动的含义与消费者 / `_get_method_info()` 去掉 `jsb_check` 的调用链 /
四个反射函数是否需要向父级查找 / 为何覆写 `_get_script_property_list()` 而不改模板），
并把其中暴露出的问题直接修掉。

### 由质询触发的改动（4 项，均已落盘并验证）

| # | 文件 | 改动 | 触发自 |
|---|---|---|---|
| ① | `src/runtime/weaver/jsb_script.cpp:412-426` | `_get_members()` 删掉父级合并，只返回自有 `properties`（对齐 `GDScript::get_members`；唯一消费方 `scene_debugger_object.cpp:111-122` 自己走 `get_base_script()` 链） | Q6 |
| ② | `scripts/jsb.runtime/src/godot.annotations.ts:692-710`、`:745`、`:749`、`:788`、`:792` | 新增 `StaticMemberDecoratorContext = ClassFieldDecoratorContext & { static: true }`；`exposed_const`/`exposed_shared` 的零参重载与实现返回类型收窄到它 | Q3 |
| ③ | `src/editor/codegen/jsb_codegen_annotations.cpp:423-432` + `scripts/typings/godot.generated.d.ts:1289-1294` | 生成的 `exposed.const`/`shared` 零参叶子由 `ClassMemberDecorator` 改为 `ClassMemberDecorator<StaticMemberDecoratorContext>`（codegen 与独立检出镜像同步） | Q3（只改 ② 对用户不可见） |
| ④ | `src/runtime/bridge/jsb_class_info.cpp:769-777` | 静态变量 `PropertyInfo` 的 usage 由 `PROPERTY_USAGE_DEFAULT \| SCRIPT_VARIABLE`（4102）改为只留 `PROPERTY_USAGE_SCRIPT_VARIABLE`（4096），对齐 GDScript | Q7 |

### 配套测试与文档

- `src/runtime/tests/test_jsb_static_members.h`
  - `_get_members()` 用例改写为「own members only」，并新增断言：派生脚本列表**不含**基类独有的 `baseOnly`。
  - 新增用例 `script method info: queried before the module is loaded`：以 `memnew` + `load_source_code` + `set_path`
    构造（`loaded_ == false` 为结构性事实，不依赖用例执行顺序），断言未知名字返回**空** `Dictionary`、
    `greet` 返回 `{"name": "greet"}`（Q5 的覆盖缺口）。
  - 新增静态变量 usage 断言（`SCRIPT_VARIABLE` 置位、`STORAGE|EDITOR` 均不置位）。
- 夹具：`static-members-target.ts` 新增基类独有实例成员 `baseOnly`；`static-members-namespaced.ts` 新增真实方法 `greet()`。
- 注释/文档纠正：`static-members-derived.ts:7-14`、`test-static-members.ts:139-146`、
  `prd.md:429-444`、`report.md:314/316-319/493`、`report.md:671-686`（A11 归因方向纠正）。

### 验证（全部实测，dll md5 `4e96ff50c3308f7099101598a0aa8575`（runtime，两处部署位一致）、`10173253fe4121223082ca2d33e4e276`（editor，两处一致））

| 项 | 结果 |
|---|---|
| `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `SCONS_EXIT=0` |
| C++ 单测 `--jsb-run-tests` | `RC=0`、**54/54 cases、698/698 assertions** |
| `pnpm gen:types`（含强制 `tsc`） | `RC=0`、**tsc error = 0**；`project/typings/jsb.runtime.gen.d.ts` 的 `exposed` 叶子已带 `<StaticMemberDecoratorContext>` |
| 项目 `tsc`（`--strict`，不加 `--noCheck`） | `RC=0`、0 error |
| 完整验收 | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| `misc/verify_codegen.py` | `VC_EXIT=1`、**25 处差异**（24 → 25，+1 = `jsb.runtime.bundle.d.ts` 因 ② 新增导出类型，属预期） |

**负向控制（Q3 收窄是否承重）**：在 `project/` 内用真实 `createClassBinder()` 写误用探针
（instance accessor / instance method / static accessor），`tsc` 报 **6 条**（TS1240/1270 ×2、TS1241/1270 ×1），
`rc=2` ⇒ 收窄经**用户实际拿到的类型链**生效，非仅运行时文件内部。

**负向控制（改动 ① 是否承重）**：临时把 `_get_members()` 改回「合并基类链 + `has` 去重」形态 → 重编 →
`test_jsb_static_members.h(394): ERROR: !members.has(StringName("baseOnly"))`、`TESTS_RC=1`、`53 passed | 1 failed`；
还原后 `54/54`、`698/698` 绿。

### 环境实测补充（Q1 / Q7 的证据）

- **Q1**：`project/extension_api.json`（11 870 734 B）头部 = `version_minor:7 version_patch:2 version_status:stable
  version_build:official version_full_name:"Godot Engine v4.7.2.stable.official"`；
  `third/godot-cpp/gdextension/extension_api-4-7.json`（4.7.0）**不含** 4.8 期实体；
  用 `D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.console.exe`（`version.py` = `4.8.dev`）
  实跑 `--dump-extension-api-with-docs` 得 12 040 422 B、头部 `4.8.0 / dev / custom_build`，
  **含全部** 4.8 期实体 ⇒ 基线输入 = 4.8.dev dump，live = 官方 4.7.2。**文档原归因方向相反，已纠正。**
  语义级比较（归一化行并集）：baseline-only **423** / live-only **50**；分片体积
  `godot10` 108598→25661 B、`godot1` 585764→653181（文件级 diff 由布局抖动主导）。
- **Q7**：GDScript 探针实测 —— `extends Node` + `static var sv` 的脚本资源
  `get_property_list()` 报 `sv|usage=4096`；我方静态变量改动前 4102、改动后
  `score|usage=4096`（引擎内探针 `.agent_tmp/` 临时脚本，已删）。
  另实测：`packed_scene.cpp` 保存与 `get_property_state` 都按 `STORAGE` 过滤，且
  **GDScript 的 `static var` 也不被持久化**（`sv` 改 77 后存盘，`out.tscn` 无 `sv` 行），
  说明该位对静态变量确属误标。

### 清理

`.agent_tmp/` 下本轮与前轮探针已删：`constkeys/`、`gdenum2/`、`inherit/`、`inherit2/`、`gd_enum/`、`acc/`、
`gdcallable/`、`dumpprobe/`、`tsprobe/`、`gdstatic_save*/`、`dump48/`、`q3probe/` 及
`static_narrow.ts`/`narrow_real.ts`/`keyof_probe*.ts`/`misuse_probe.ts`/`current_misuse.ts`/
`fieldonly_probe.ts`/`necessity_probe.ts`/`assign_narrow.ts`/`narrow_overload.ts`/`head_jsb_script.cpp`。
`project/` 根与 `project/tests/` 复查无 `_` 前缀残留。

### 遗留

- A11 的 `--update-baseline` 仍未执行（需用户确认；且**必须用与验证同一引擎**——官方 4.7.x，
  与 CI 的 `GODOT_VERSION: tags/4.7.1-stable` 对齐，否则会把 4.8 漂移再固化一次）。
- `#2` 枚举成员点访问/补全对外来脚本不可达（用户已否决「摊平成独立常量」）⇒ 现状（仅索引访问）为最终形态。
- `project/icon.svg.import` 的既有重写、`static-members/` 无 `.uid`：与本功能无关，未处理。

### 补做的负向控制（2026-09-25，Q5/Q7 两个新守卫）

`_get_method_info()` 的「未知名字返回空」与静态变量 usage 的「只留 `SCRIPT_VARIABLE`」两个新断言，
此前只有正向绿证。补做削减 → 确认 FAILED → 还原 → 确认绿：

| 削减 | 结果 |
|---|---|
| `_get_method_info()` 改回 R5.1 前的形态（无条件返回 `{"name": p_method}`） | `test_jsb_static_members.h(419): ERROR: script->_get_method_info(StringName("__not_a_method__")).is_empty() values: false`、`TESTS_RC=1`、`52 passed \| 2 failed` |
| 静态变量 usage 改回 `PROPERTY_USAGE_DEFAULT \| SCRIPT_VARIABLE` | `test_jsb_static_members.h(253): ERROR: (shared_info->details.usage & (PROPERTY_USAGE_STORAGE \| PROPERTY_USAGE_EDITOR)) == 0 values: 6 == 0` |

两者还原后（源码残留 grep `NEGATIVE CONTROL` / `HashSet<StringName> inserted` = **NONE**）：
`SCONS_EXIT=0`、**54/54 cases、698/698 assertions**、`TESTS_RC=0`；
完整验收 `RC=0`、orphan=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK`。
最终 dll md5 `4e96ff50c3308f7099101598a0aa8575`（runtime，两处部署位一致）、
`10173253fe4121223082ca2d33e4e276`（editor，两处一致）。

### 沉淀进 spec

`.trellis/spec/godotjs-ext/cpp/architecture-constraints.md`「其他陷阱」新增 3 条：
① `Script` 反射钩子「只报自有、走链在调用方/实例层」（2026-09-26 定案；含根因：`type_from_script`
对外来脚本给 `kind = SCRIPT`、`base_class == nullptr`，以及 `const` 基 vs `var` 基的实测边界）；
② `_get_method_info()` 可在 `loaded_ == false` 时被调用（含完整链路与"如何在 C++ 侧干净复现该前置条件"）；
③ 静态变量 usage 对齐 GDScript（含"该位不影响 `.tscn` 持久化"的实测反证）。
`.trellis/spec/godotjs-ext/cpp/generated-files.md` 的「注解类型表」段更新为收窄后的写法与同步要求。

### 独立核查（2 只 trellis-check，只读、带预算、无重编）

| 核查项 | 结论 | 要点 |
|---|---|---|
| `_get_members()` 只报自有成员 | PASS | `jsb_script.cpp:412-426` 无 `base`/父级合并残留；守卫在；注释 file:line 与引擎源码一致；全引擎 `get_members(` 调用点仅 `scene_debugger_object.cpp:104/115` ⇒ 单一消费方成立 |
| `StaticMemberDecoratorContext` 收窄 | PASS | 零参重载声明与实现返回类型一致；`& { static: true }` 确实收窄；运行期 `context.static` 守卫保留（`:773-779`/`:813-819`） |
| codegen 与镜像同步 | PASS | `jsb_codegen_annotations.cpp:428-432` 用 `make_godot_args`；`const`/`shared` **共用同一数组** ⇒ 不可能半改；镜像与 `project/typings/jsb.runtime.gen.d.ts:138-151` 逐 token 一致；零参在 rest 之前 |
| 静态变量 usage | PASS | 只留 `SCRIPT_VARIABLE`；`static_variables` 全部读点（`jsb_script.cpp:470-483`、`:508-521`）**无一按 STORAGE/EDITOR 过滤** ⇒ 删位对 `.tscn` 惰性，与实测一致 |
| 测试诚实性 ×4 | PASS | 双向断言、夹具真实、无 print-only 空转；负控 a/b/c 分别命中 `:394`/`:419`/`:253` |

**核查提出的唯一 UNVERIFIED**（`scripts/typings/godot.generated.d.ts` 的 ambient module 引用未同块声明的名字，
依赖与 `jsb.runtime.bundle.d.ts` 的 module 合并；project 侧已被 `gen:types` 绿证）—— 已由
`node scripts/jsb.runtime/node_modules/typescript/bin/tsc -p scripts/jsb.runtime/tsconfig.json --noEmit`
**实测关闭：`RC=0`、0 error**（该 tsconfig 的 `include` 覆盖 `../typings/**/*`）。

**核查提出的唯一措辞风险**（`test-static-members.ts` 的 NOTE 依据未实测）—— 已实测钉死并改写注释：
引擎内探针读 `get_script_property_list()`，target = `["tag","baseOnly","score"]`、
derived = `["tag","tag","baseOnly","score"]` ⇒ 该列表**确实**含实例属性且**不跨基类链去重**，NOTE 成立。

### 收尾复验（comment-only 改动后）

`project/tests/static-members/test-static-members.ts` 仅改注释 ⇒ 删 `.godot/.tsbuildinfo` 重编
（`TSC_RC=0`、0 error、编译产物已含新注释）后完整验收：`RC=0`、orphan=**0**、COMPLETED=**1**、
FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK`。C++ 侧未改动 ⇒ dll md5 仍为
`4e96ff50c3308f7099101598a0aa8575`，未重跑单测（无新信息）。

### 固定引擎的前后基线（2026-09-25 答复轮执行，此前「同引擎前后对比」一直没做，是缺口）

全程**同一引擎**：`D:/Dev/godot/godot/bin/Godod_v4.7.2-stable_win64_console.exe`（官方 4.7.2 stable / official，`project/extension_api.json` 头部自证）。

| 步骤 | 操作 | 结果 |
|---|---|---|
| ① | `git show HEAD:<三文件>` 回退到改动前（`jsb_codegen_annotations.cpp` `exposed=0`、`godot.annotations.ts` `exposed=0`）→ `scons …dev_build=yes tests=yes -j5` | `SCONS_EXIT=0`，dll md5 `4218d7d5230ca84b024c0d9dfa395718` |
| ② | `misc/verify_codegen.py --godot <4.7.2> --update-baseline` | `RC=0`。新基线：`jsb.runtime.gen.d.ts` **152 行 / exposed=0**、`jsb.runtime.bundle.d.ts` **210 行 / exposed=0**、`godot.minimal.d.ts` 392 行 |
| ③ | 双轮确定性：不 update，同引擎重跑全流程 | **`RC=0`，零差异** ⇒ 基线可信（规范要求的唯一授权来源） |
| ④ | 恢复改动后版本 → `scons` | `SCONS_EXIT=0` |
| ⑤ | 同引擎 `verify_codegen.py` | **3 处**（此前 25 处） |

**3 处差异逐条归因**：

| 差异 | 归因 |
|---|---|
| `typings/jsb.runtime.gen.d.ts` | **+14 行 = 整个 `exposed` 块**（产物行 138–151，两个叶子 `const`/`shared`）—— 本任务 R4.4 新增注解，预期内 |
| `typings/jsb.runtime.bundle.d.ts` | +19 行 = `export type StaticMemberDecoratorContext`（源 `godot.annotations.ts:708-710`）—— Q3 收窄，预期内 |
| `gen/godot/tests/static-members/StaticMembers.tscn.gen.ts` | `PackedScene<Node<…>>` → `PackedScene<TestStaticMembers>`——**测试项目 TS 产物**，与 codegen 逻辑无关（前序已归因） |

**决定性结论**：此前 25 处里的 **11 个 `godot*.gen.d.ts` 分片差异全部消失** ⇒ 那 11 处确证是「基线建立时用了 4.8.dev 引擎」的**输入漂移**，**不是本任务的生成器回归**。旧基线（2026-09-20 / 4.8.dev 产物）已备份至 `.agent_tmp/baseline_snapshot_2026-09-20`。

**另修正一条我此前的错误**：上一轮我在 shell 链里用 `cd ..`（相对每次重置的 ROOT），导致 `verify` 与 `acceptance` **根本未执行**（`can't open file '…\.worktrees\misc\verify_codegen.py` + `Invalid project path specified: "./project"`），我却把「grep 不到差异行」误读为通过。本次已改为 `cwd=ROOT` + 绝对路径，并对每步做**日志特征校验**（verify 需三步触发链齐全；acceptance 需 `START-DIAG` 存在）。`scenes=0 / COMPLETED=0` 的日志即「没跑」的判别信号。

### 第 3 问（基类逻辑包 JSB_TOOLS）——**执行后按证据撤回**

用户建议：只有 GDScript 脚本解析需要基类信息 ⇒ 把基类处理逻辑包进 `JSB_TOOLS`。

我先照做（`_get_constants()` 基类合并 + `_get_members()` 包 `#if JSB_TOOLS`），随后取证推翻：

- `GDScript::reload`（`gdscript.cpp:806-833`）：`GDScriptParser` / `GDScriptAnalyzer` **不在任何 `TOOLS_ENABLED` 块内**（该区间只有 808-812 与 869+ 是 TOOLS 段）⇒ **分析器在 runtime 构建里照样运行**。
  ⇒ 门控会让**运行时的 `DerivedScript.INHERITED_CONST` 解析失败**（分析器 `reduce_identifier_from_base` 的 `:4350-4400` 分支不调 `get_base_script()`，`:4366` 直接调 `get_method_info`、`get_constants` 亦单脚本探测）。已撤回，反证写进 `jsb_script.cpp:406-416` 注释。
- `_get_members()` 同样撤回：唯一消费方是远程调试器（`scene_debugger_object.cpp:104`），**调试会话不是编辑器专属**（远程调试器可附着到 debug/runtime 构建），且 `GDScript::get_members`（`gdscript.cpp:919-925`）自身无门控。

⇒ 结论：四个反射函数的基类处理**均不能**门控；这是「用户建议 → 取证否决」的一例，已留痕。

> **2026-09-26 后续修正（本段的两条表述已过期，勿再引用）**：
> ① 上段「门控会让**运行时的** `INHERITED_CONST` 解析失败」**不准确** —— 实测（`.agent_tmp/probe_dconst.log`）
> 只有 `const D := preload(…)` + `D.F` 是**解析期**硬失败（脚本不加载）；`var d := preload(…)` + `d.F`
> **不报错**，退化为运行期 `_get`（其自身走 `base`）⇒ 取到 `1.5`。鸭子类型只覆盖非 const 基那一半。
> ② 「四个反射函数的基类处理」这一前提**已不存在**：按用户当轮裁决，`_get_constants()` /
> `_has_method()` / `_get_method_info()` 的 `base` 走链**已从 `Script` 层全部删除**（只剩
> `_get_members()` 早已如此），走链下移到调用方/实例层。详见下文「Script 层去基类查找」段。
> ③ 「反证写进 `jsb_script.cpp:406-416` 注释」**已失效** —— 那段注释随合并逻辑一并删除。

### 第 4 问（GDScript 静态变量是否出实例 `get_property_list()`）——**已查证，结论推翻我原方案的理由**

实机探针（官方 4.7.2 `Godot_v4.7.2-stable_win64_console.exe`；**日志留存 `.agent_tmp/q4_probe.log`**，
`S` = `static var sv: int = 100` / `var iv: int = 7` / `const C: int = 42`）：

```
RESULT_INSTANCE_HAS_sv=false                  ← 【不在】实例 get_property_list()
RESULT_INSTANCE_script_entries=script|u=1048590,iv|u=4096
RESULT_SCRIPT_RESOURCE_LIST=…,sv|u=4096,…     ← 在【脚本资源】面，usage=4096
RESULT_GET_SCRIPT_PROPERTY_LIST=Built-in script,iv   ← 分析器读的面【不含】sv
RESULT_INSTANCE_get_sv=100                    ← 实例 get 触达得到
RESULT_INSTANCE_set_then_get_sv=555           ← 实例 set 也触达得到
```

> **更正**：本报告此前写 `usage=135168` 且探针文件名为 `S.gd`，**没有对应留盘日志**，不可采信。
> 上一轮也是因此未能向用户出示证据。以本次 `q4_probe.log` 为准：`sv` 的 usage 是 **4096**
> （仅 `SCRIPT_VARIABLE`），实例面**不含** `sv`。

- ⇒ **GDScript 静态变量不出实例 `get_property_list()`**。模板第二消费者（`jsb_script_instance.cpp:373-377`）是**实例**属性表，把静态变量塞进模板 = 推进实例表 = **偏离 GDScript**；当前做法（只进 `_get_property_list` 脚本资源面）**是对的**。
- 我上一轮拿「godot-cpp 无法覆写 `Script::get_script_property_list(List<PropertyInfo>*)`（纯虚、无 GDVIRTUAL）」当"不能改模板"的决定性理由是**错的** —— 那条只解释"为什么必须走 `_get_script_property_list()` 这个面"，不解释"为什么不能改模板"。
- **唯一必要偏离（须文档化）**：我方额外把静态变量塞进 `_get_script_property_list()`。GDScript 对应面**不含**静态变量（它走 parser 的 `STATIC_VARIABLE` 通道，外来脚本没有）⇒ 这是**必要妥协**，已在 `generated-files.md` / `architecture-constraints.md` 写明。

### 本轮改动的最终验证（同引擎、日志特征校验）

| 项 | 结果 |
|---|---|
| `scons …dev_build=yes tests=yes -j5` | `SCONS_EXIT=0` |
| C++ 单测 `--jsb-run-tests` | `RC=0`、**54/54 cases、698/698 assertions**（editor 3/3 + runtime 54/54） |
| `verify_codegen.py`（官方 4.7.2） | **3 处**，逐条归因见上 |
| 完整验收 `--verbose` | `RC=0`、orphan=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| 实例 `get`/`set` 补常量+静态变量 | 已实现并随上述验证通过（对齐 GDScript：派生实例 `n.get("bsv")` 取到基类 static var） |

**未做**：`--update-baseline` 吸收当前 3 处差异（会掩盖真实回归，需用户确认）；`git` 任何操作。

---

## Q1–Q7 答复落档与最终验证（2026-09-26）

**答复文档：`.trellis/tasks/09-24-script-static-members/qa-q1-q7.md`**（每条带 file:line 与实机/源码取证）。

由答复暴露并**本轮已修**的代码项：

| 项 | 位置 | 状态 |
|---|---|---|
| 成员形态收窄为用户可见类型（codegen 侧） | `jsb_codegen_annotations.cpp:428-432`；产出 `jsb.runtime.gen.d.ts:139/145` | 已改 |
| 镜像同步 | `scripts/typings/godot.generated.d.ts:1289/1292` | 已改 |
| 类级名字存在性 | —— | **无法静态校验**（keyof 两次探针失败），已知剩余缺口 |
| 静态变量 usage 对齐 GDScript | `jsb_class_info.cpp:777` 只留 `PROPERTY_USAGE_SCRIPT_VARIABLE`（4102 → 4096） | 已改 |
| `_get_members()` 不再合并基类 | `jsb_script.cpp:431-447` | 已改 |
| `_get_method_info()` 独立调用（`loaded_ == false`）用例 | `test_jsb_static_members.h:408` | 已补 |
| 实例 `get`/`set` 补常量+静态变量 | `jsb_script_instance.cpp:500-507` | 已实现；**负向控制已做**：削减后 `GET_N=<null>` / `GET_score=<null>` / `AFTER_SET_GET_score=<null>`，还原后 `42 / 0 / 99` |

### 本轮最终验证（全部重跑，同一引擎 `Godot_v4.7.2-stable_win64_console.exe`）

| 项 | 结果 |
|---|---|
| `pnpm gen:types`（含强制 `tsc`） | `RC=0`、**tsc error 数 = 0** |
| 完整验收 `--verbose` | `RC=0`、orphan=**0**、COMPLETED=**1**、FAILED=**0**、`scenes=11`、`STATIC-MEMBERS-GD-OK` |
| C++ 双套件 `--jsb-run-tests` | `RC=0`、editor **3/3**（12 assertions）、runtime **54/54**（698 assertions） |
| `format_src.py` | 388 files，RC=0 |

`verify_codegen.py` 差异树仍为 **3 处**（`jsb.runtime.gen.d.ts` 的 `exposed` 块 / `jsb.runtime.bundle.d.ts` 的
`StaticMemberDecoratorContext` / `StaticMembers.tscn.gen.ts` 测试产物），逐条归因在 `prd.md` A11。

**未做（需用户确认）**：`--update-baseline` 吸收这 3 处（会掩盖真实回归）；任何 `git` 操作。
**清理**：本轮探针（`.agent_tmp/` 下 11 个 ts + `inst_backup.cpp` + 5 个探针目录）已删；
`project/` 下无散落（glob 复查为空）。

---

## `Script` 层去基类查找（2026-09-26，用户当轮指令）

用户原话：「**干脆去掉查基类脚本得逻辑，解析不到就解析不到得了**，你特么 `_get_script_constant_map()`
行为都变得不一致了，就一个解析算什么」。已执行。

### 改动

| 文件 | 函数 | 改动 |
|---|---|---|
| `src/runtime/weaver/jsb_script.cpp` | `_get_constants()` | **删掉** `base->_get_constants()` 合并，只报自有（对齐 `GDScript::get_constants` `gdscript.cpp:911-917`） |
| 同上 | `_has_method()` | **删掉** `while (current) { … current = current->base.ptr(); }`，只查自有；保留 `_ready` 特判 |
| 同上 | `_get_method_info()` | **删掉**同一走链，只查自有 |
| 同上 | `_get_members()` | 保持只报自有（上一轮已改） |
| 同上 | `_get()` / `_set()` | **保持走 `base`** —— 运行期取值路径，与反射钩子职责不同 |
| `src/runtime/weaver/jsb_script_instance.cpp` | `GodotJSScriptInstance::has_method()` | **接手走链**（`while (sptr) { if (sptr->_has_method(…)) return true; sptr = sptr->base.ptr(); }`），对齐 `GDScriptInstance::has_method` `gdscript.cpp:1906-1917` |

**实例层走链是必需的**：`Object::has_method`（`object.cpp:738-758`）只调 `script_instance->has_method`，**不自己走链**；不接手则继承方法在实例上回归。

### 负向控制（「删合并是对的」的唯一授权来源）

把合并按 `#if 0` 关掉后重编（`.agent_tmp/negctl_constants_run.log`、`probe_dconst.log`）：

| GDScript 写法 | 实测 |
|---|---|
| `var d := preload("…derived.ts")` + `d.F`（继承常量） | **不报错**，退化到运行期 `_get`（其自身走 `base`）⇒ 取到 `1.5` |
| `const D := preload("…derived.ts")` + `D.F` | **解析期硬失败** `Parse Error: Cannot find member "F" in base "…static-members-derived.ts"`（`const` 基是编译期解析，不退化） |
| `get_script_constant_map()`（关掉合并后） | 只剩 `["N"]` —— **这才与 GDScript 一致** |

⇒ 上一轮「不合并就分析失败/加载失败」的结论**被证伪**；失败的只是 JS 侧那条**编码了旧行为**的断言。

### 夹具同步

- `test-static-members.ts`：`derived_map.get("N") == 22` + **新增** `!derived_map.has("F")`（原 `get("F") == 1.5` 删除）
- `static-members-derived.ts`：注释改为「`_get_constants()` 只报自有；继承值仍经运行期 `_get` 可读」
- `static-members-gdcheck.gd`（A7 段）：① `D.N == 22`；② map 含 `N` 不含 `F`；③ `var derived_dyn := preload(…)` + `derived_dyn.F == 1.5`；④ **新增**实例方法走链断言 `has_method("greet") == true` / `has_method("__nope__") == false`
- `static-members-target.ts`：**新增** `greet(): number { return 1; }`（派生类不重声明，供 ④ 用）

### 本轮验证（全部实测）

| 项 | 结果 | 日志 |
|---|---|---|
| `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `RC=0` | `.agent_tmp/final_scons.log` |
| C++ 双套件 `--jsb-run-tests` | `RC=0`、editor **3/3**（12 assertions）、runtime **54/54**（698 assertions） | `.agent_tmp/final_tests.log` |
| 完整验收 `--verbose` | `RC=0`、Orphan StringName=**0**、COMPLETED=**1**、FAILED=**0**、`STATIC-MEMBERS-GD-OK`=1 | `.agent_tmp/final_acc.log` |
| `tsc`（先删 `.godot/.tsbuildinfo`） | `RC=0` | —— |
| `misc/verify_codegen.py`（官方 4.7.2） | **仍 3 处**（`jsb.runtime.gen.d.ts` / `jsb.runtime.bundle.d.ts` / `StaticMembers.tscn.gen.ts`），逐条归因同 A11 ⇒ hook 改动**未产生新差异** | `.agent_tmp/vc_final.log` |
| 继承方法运行期探针 | `PROBE_INHERITED_CALL_OK` / `has_method("greet")=true` / `has_method("__nope__")=false` | `.agent_tmp/probe_meth.log` |

> `--jsb-run-tests` 日志中的 `ERROR: [jsb][Error] failed to eval_source: Uncaught SyntaxError: Unexpected identifier 'is'` 是**既有**测试用例 `eval propagates JS syntax errors as Error values` 的预期输出（`.agent_tmp/verify_revert.log:72-75` 同），非本轮引入。

### 本轮新增的实测边界：`_get_property_list` / `_get_script_property_list` **必须走链**（对齐 GDScript）

上面「只报自有」的定案**不覆盖**这两个钩子。实读 `GDScript::_get_property_list`（`gdscript.cpp:1058-1082`）：
`while (top) { classes.push_back(top); top = top->base.ptr(); }` 收整条链 → `for (E = classes.back(); …)`
**root-first** push 每个脚本的 `static_variables_indices[*].property_info`。实机探针（官方 4.7.2，`.agent_tmp/svprobe.log`）：

```
BASE_SCRIPT_LIST=…,script/source|u=10,bsv|u=4096,script|u=1048586
DERIVED_SCRIPT_LIST=…,script/source|u=10,bsv|u=4096,script|u=1048586   ← 含基类的 bsv
DERIVED_HAS_bsv=true
BASE_SCRIPT_PROP_LIST=_svbase.gd,biv
DERIVED_SCRIPT_PROP_LIST=_svderived.gd,_svbase.gd,biv                  ← 含基类脚本名与基类实例成员
```

⇒ 我方 `jsb_script.cpp:472-492` / `:510-532` 的 `for (current = this; …; current = current->base.ptr())`
走链是**对齐**，删掉会让继承的静态变量在分析期与 inspector 面**双双消失**。已写进
`architecture-constraints.md` 的同条目「边界」段。

### 文档同步（旧「沿 `base` 链合并」表述全部改写）

`qa-q1-q7.md` Q6 表（原写「`_get_constants`/`_has_method`/`_get_method_info` 必须走链、保留合并」——
**已被推翻**，改写为「四个都只报自有」+ `const` vs `var` 实测边界）、`design.md:193`、
`implement.md:97-101`、`report.md:211/338/851`、`report.md:919-926`（第 3 问 JSB_TOOLS 段的过期表述加修正块）、
`prd.md` R2.1 / A7、`architecture-constraints.md`「`Script` 的反射钩子」条目（含新「边界」段）。

### 清理

`project/_probe_meth.gd`、`project/_svprobe.gd`、`project/_svbase.gd`、`project/_svderived.gd`
（含 `.uid`）已删；`project/` 下 glob 复查**无 `_*` 散落**。

---

## 静态变量归属质询（2026-09-26，**只查证、未改代码**）

用户质询：`@bind.exposed.shared()` 的值为何不随 `GodotJSScript` 的生命周期，而 GDScript 的 `static var`
「我记得也是跟随这个脚本对象本身的生命周期的」。**结论：用户对 GDScript 一侧的判断正确**；我方存储
按 module id 键控，确实在生命周期上偏离。

### GDScript 侧（源码实读 + 实机探针，官方 4.7.2）

存储就是**脚本对象的实例字段**（`modules/gdscript/gdscript.h:94-95`）：

```cpp
HashMap<StringName, MemberInfo> static_variables_indices;   // h:94
Vector<Variant> static_variables; // Static variable values. // h:95
```

生命周期事件全部挂在脚本对象上：

| 事件 | 位置 | 作用 |
|---|---|---|
| 默认值初始化 | `_static_default_init()` `gdscript.cpp:688-713`（由编译器调，`gdscript_compiler.cpp:3086`） | 按类型填默认值 |
| 用户 `static func _static_init()` | `_static_init()` `:670-686`（由 `reload()` `:886` 调） | 跑初值表达式 |
| 热重载保值 | `_save_old_static_data()` `:717-723` / `_restore_old_static_data()` `:725-736`（`#ifdef TOOLS_ENABLED`，`reload()` `:809-810`/`:893-894`） | 仅 `p_keep_state` 时保留活值 |
| **释放** | `GDScript::clear()` `:1461-1462`（`static_variables.clear(); static_variables_indices.clear();`） | 由 **`~GDScript()` `:1534`** 与 `GDScriptLanguage::finish()` `:2277` 调用 |

**实测（`.agent_tmp/svperobj.log`）——决定性**：同一份源码造**两个独立对象**（`GDScript.new()` +
`source_code` + `reload()`；`ResourceLoader` 会返回缓存对象，答不了这个问题）：

```
A_INIT=100   A_AFTER_SET=5   B_INIT=100   A_STILL=5
```

⇒ 值**按脚本对象**存放，不是按路径的进程级全局。用户判断正确。

（`.agent_tmp/svlife.log` 补充：`load()` 同一路径两次拿到同一对象、读到 42；`reload(true)` 后读到 7
—— 与「保值走 `_restore_old_static_data`」一致。）

### 我方侧（`jsb::SharedStatics`）

- 存储：`jsb_shared_statics.h:48-65`，进程级 `HashMap<StringName /*module_id*/, HashMap<StringName, Variant>>`
  + `std::recursive_mutex`。**键是 module id（路径），不是脚本对象**。
- 唯一释放点：`GodotJSScriptLanguage::_finish()` → `jsb_script_language.cpp:264`（Orphan StringName 缺陷 A 的同型处理）。
- 写入口：`_get`/`_set`（`jsb_script.cpp:448-467`）、实例面（`jsb_script_instance.cpp:455-456`/`:504-505`）、
  以及类解析期装的 JS 访问器（`jsb_class_info.cpp:290-352`）。
- 实测（`.agent_tmp/svstore.log`）：`S1_SCORE_SET=123` → 同一路径再次 `load()` 读到 `123`。
- 重解析保值：C++ 用例断言「the live value must survive the re-parse」（`test_jsb_static_members.h:330-331`，
  `CHECK((int64_t)value == 7)`）—— `ensure` 刻意不覆盖已存在的项。

### 偏离的准确形状（区分实测与推断）

- **实测的差异**：GDScript 的值在 `~GDScript()` 时随对象释放、下一个新对象回到初值；我方的值活到
  `_finish()`，且 `ensure` 不覆盖 ⇒ 同一 module id 的新对象会读到**上一个对象留下的活值**。
- **推断（未实测，标记为 `[INFERENCE]`）**：该差异要有两个同路径 `GodotJSScript` 对象才可观测。
  经 `ResourceLoader` **造不出来** —— `ResourceFormatLoaderGodotJSScript::_load` 把
  `CACHE_MODE_IGNORE`/`IGNORE_DEEP` 显式降级为 REUSE（`jsb_resource_loader.cpp:110-116`，注释
  「seems safe because GodotJSScript is stateless now」）；实测 `CACHE_MODE_REPLACE`/`IGNORE`
  返回的都是同一对象（`.agent_tmp/svobj.log`：`DISTINCT=false`）。`GodotJSScript.new()` + `set_path()`
  理论上可造第二对象，但该探针**把引擎挂住**（已 `taskkill`，结论不成立）。
- **设计意图不是生命周期**：`design.md` §5.2 给方案 A 的理由是**跨环境一致性**（`script_class_info_`
  是首个加载环境的快照、各 isolate 独立 import ⇒ JS 静态属性天然 per-isolate），**不是**"静态数据不该
  跟随脚本对象生命周期"。但用户指出的后果成立：按 module id 键控确实让值的生命周期脱离了脚本对象。
- **撤回一条我写错的结论（2026-09-26 用户质询后自查）**：我此前写「跨环境一致性没有替代实现，
  两条路互斥」——**这是错的**。两件事是**正交**的：
  1. **跨环境一致性**靠的是「所有环境 + GDScript 读同一个持有者」，**不是**靠"进程级 map"。
     `GodotJSScript` 本身就是**进程级共享的 Resource**（`ResourceFormatLoaderGodotJSScript::_load`
     对 REUSE 直接返回缓存对象，`jsb_resource_loader.cpp:117-122`；ResourceCache 进程级）
     ⇒ **每路径一个对象**，把它当持有者照样是一个值供所有环境 + GDScript 读。
  2. **不携带环境信息**（`StatelessScriptClassInfo` 无 `EnvironmentID`）恰恰是**好事** ——
     它让该对象可跨环境/跨线程共享，正是"能当共享持有者"的前提，而不是阻碍。
  3. **访问器确实已经指向 C++ 存储**（回答用户"是不是压根没改访问器"）：**改了**。
     `_install_shared_static_accessor`（`jsb_class_info.cpp:347-352`）用
     `SetAccessorProperty(name, getter, setter)` 替换原 data 属性；getter → `SharedStatics::get`
     （`:303`）、setter → `SharedStatics::set`（`:342`）。跨环境用例（worker 写 2000、主环境读到
     2000）实测通过。

  **真正的约束（比我原来说的窄）**：访问器是在**类解析期**装的（`_parse_script_class`），
  而类解析**可以在没有 `GodotJSScript` 的情况下发生** ——
  worker 隔离区 `env->load(impl->path_)`（`jsb_worker.cpp:449`）、编辑器桥
  `env->load(path, &module)`（`jsb_bridge_table.cpp:137`/`:163`）、以及纯 JS 侧 `import`
  触发的模块解析。**那一刻只有 `module_id`**，脚本对象可能尚不存在。
  ⇒ 持有者必须**仅凭 `module_id` 就能解析到**。这是键控按 module id 的**唯一**硬理由，
  与"生命周期该不该跟随脚本对象"无关。
  ⇒ 可行的改法：保留 module_id→槽位的查找，但把**槽位生命周期绑到脚本对象**（`~GodotJSScript`
  时释放该 module 的条目）；代价是「无脚本资源的纯模块」没有可挂的持有者。

- **裁决（2026-09-26，用户拍板）：保持现状，值生命周期独立于 `GodotJSScript`。**
  用户原话：「你还是把静态变量的生命周期改成原来的独立于 GodotJSScript 吧，你说的确实是个问题，
  GodotJSScript 对象的析构如果把静态变量给搞没了也是大问题，纯 JS import 确实是个应用场景。」
  ⇒ **不采纳**"绑到脚本对象"的改法。两条理由：① `GodotJSScript` 析构即丢静态值是更大的问题
  （`static var` 语义上是**类级别**状态，且对作者不可观测）；② **纯 JS import 是真实场景**
  （worker `jsb_worker.cpp:449`、编辑器桥 `jsb_bridge_table.cpp:137`/`:163` 都在**没有
  `GodotJSScript`** 时触发类解析），绑对象会让这些场景无处可写。
  **本轮未改任何代码**（当前实现即此形态）。理由已落三处：
  `src/runtime/bridge/jsb_shared_statics.h` 文件头 `@note`、`design.md` §5.2.1、`prd.md` R6.3。

## `_parse_script_class_iterate` 收进 `jsb::internal`（2026-09-26，用户要求）

用户质询「公开到头文件是何意味，为了 C++ 测试吗？那至少再套一层 internal 命名空间」。

- **是为 C++ 测试**：唯一非测试调用点在同文件内（`jsb_class_info.cpp:899` 的
  `ScriptClassInfo::_parse_script_class`）；其余全部是 `src/runtime/tests/test_jsb_static_members.h`
  （`:136`/`:316`/`:327`）。生产路径走 `ScriptClassInfo::_parse_script_class`。
- **改动**：声明与定义都包进 `namespace internal`（`jsb_class_info.h:292-296`、
  `jsb_class_info.cpp:355`/`:799`），四处调用点加 `internal::` 限定。`internal` 是既有约定
  （`jsb::internal` 在 `src/runtime/internal/` 普遍使用，如 `jsb_sarray.h:38`）。
- **验证**：`scons …tests=yes -j5` → `RC=0`（重编 `jsb_class_info.cpp`）；`--jsb-run-tests` → `RC=0`、
  editor **3/3**、runtime **54/54**（698 assertions）；完整验收 → `RC=0`、Orphan=**0**、COMPLETED=**1**、
  FAILED=**0**、`STATIC-MEMBERS-GD-OK`。日志 `.agent_tmp/ns_scons.log` / `ns_tests.log` / `ns_acc.log`。
- `verify_codegen.py` 复跑仍 **3 处**（未产生新差异），`.agent_tmp/vc_after_ns.log`。

## 基线差异的文档位置（2026-09-26，用户质询）

用户问「基线差异你写在哪？文件路径和行号给到我」。四处落点：

| # | 文件 | 位置 | 内容 |
|---|---|---|---|
| ① | `.trellis/tasks/09-24-script-static-members/prd.md` | **A11 条目，行 438–478**；3 处清单在 **行 470–477** | 归因纠正 + 固定引擎重建基线 + 3 处逐条归因 |
| ② | `.trellis/tasks/09-24-script-static-members/report.md` | 标题 **行 883**「固定引擎的前后基线」；归因表 **行 895–901**；决定性结论 **行 903** | 5 步操作表 + 3 处归因 + 11 分片差异消失的定性 |
| ③ | `.trellis/tasks/09-24-script-static-members/qa-q1-q7.md` | **Q1 段，行 9–31** | 答「A11 归因是什么、文档路径和描述位置」 |
| ④ | 原始日志（`.agent_tmp/`，未入库） | `vc_final.log`（6045 B）、`vc_after_ns.log`（6045 B）、`baseline_before.log`、`baseline_determinism_round2.log` | 3 处 diff 全文 + 双轮确定性证据 |

基线数据本体：**`.codegen-baseline/`**（含 `typings/`、`gen/`、`tsconfig.json`；gitignore 本地自持，不入库），
由 `misc/verify_codegen.py` 的 `BASELINE_DIR`（`misc/verify_codegen.py:50`）定位。

3 处差异（`verify_codegen.py` 实测，`.agent_tmp/vc_after_ns.log`）：

1. `[typings] jsb.runtime.gen.d.ts` —— `exposed` 块，两个叶子各多 `ClassMemberDecorator<StaticMemberDecoratorContext>` 泛型实参
2. `[typings] jsb.runtime.bundle.d.ts` —— `export type StaticMemberDecoratorContext`（+19 行）
3. `[gen] godot/tests/static-members/StaticMembers.tscn.gen.ts` —— `PackedScene<Node<…>>` → `PackedScene<TestStaticMembers>`

**仍未 `--update-baseline`**（会掩盖真实回归，待用户拍板）。
