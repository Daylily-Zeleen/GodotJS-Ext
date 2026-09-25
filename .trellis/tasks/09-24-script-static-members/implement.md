# 执行计划：GodotJSScript 静态成员

> 顺序为**强依赖**顺序：阶段 1 的数据模型是阶段 2–3 的前提。
> 每阶段结束必须可验证（有可观测证据），未通过不得进入下一阶段。
> 决策项 Q1–Q3 **均已拍板**：Q1=(c) 不做静态函数；Q2= `@bind.exposed.const()` /
> `@bind.exposed.shared()`；Q3 **最终（2026-09-25 用户拍板）**：常量支持面 =
> **JS 基础值 + enum + 容器（`GArray`/`GDictionary` 包装，递归 `make_read_only()`）**（R2.3 白名单）
> （依据见 `prd.md` 的「已拍板」段 / §R5 / §R2.3 / §R2.5 / §R2.4 与 `design.md` §6.0 / §4.2 / §5.3）。
> **Q2 附带决定**：方法参数/返回值/信号参数的类型信息**本任务不做**（用户否决"为每个函数重写一遍
> 类型注解"）；取证留档 `research/q1q2-freeze-and-signatures.md` §Q2。
> 阶段总数：**0–3**（原「阶段 4：静态函数」随 Q1=(c) 删除）。
> 任务状态：`planning`（**未** `task.py start`，禁止开工）。

---

## 阶段 0：前置验证（阻塞性，先做）

**目标**：把 `design.md` 里剩余的 `[INFERENCE]` 变成实测事实，避免按错误前提设计。

> 结论已落在 `research/phase0-findings.md`（0.1–0.3 已完成）。**0.1–0.3 的结论已回写进
> `design.md` §4.1/§4.2/§4.3/§6.4 与 `prd.md` R1.1/R1.2** —— 实施阶段按回写后的版本执行，
> 不要按本清单的原始描述执行。

- [x] 0.1 TS 产物 dump ✅（`research/phase0-findings.md` §0.1，探针 `.agent_tmp/tsprobe/`（任务收尾已清理））：
      `static` **字段可枚举** / **方法不可枚举**；`length`/`name`/`prototype` 必须显式跳过；
      静态成员**不继承**；`const enum` 完全擦除；`static readonly` 的 `writable` 仍为 `true`
      （⇒ 运行期无法判定 const）；`Function.length` 对 rest/默认值参数截断。
- [x] 0.2 `js_to_gd_var` 判定表 ✅（`research/phase0-findings.md` §0.2，源码实读）：
      无提示路径对 JS 原生 `Array` / 纯 JS 对象 / `symbol` / 函数**全部返回 `false`**；
      `symbol` 会打 `Error` 日志 ⇒ 须先按 `typeof` 预筛。
      **纯 JS 对象在任何路径都转不成 `Dictionary`** ⇒ 枚举须由我方手工构造 `Dictionary`（R1.3）；
      容器常量须由作者用 `GArray.create([...])` / `GDictionary.create({...})` 构造
      （**不能**靠 `Variant::ARRAY` 提示重载 —— 它经 `try_convert_array_any` **新建** `Array`，
      新 `_p` 与 JS 原数组无关，施加只读保护不了 JS 侧，见 R2.3）。
- [x] 0.3 `make_read_only()` 语义 ✅（`research/phase0-findings.md` §0.3，GDScript 探针
      `.agent_tmp/gdcallable/ro.gd` 实跑）：`Array`/`Dictionary` 均可用；**浅层**；
      **`duplicate()` 不继承只读标记**；只读标记随 `_p` 传播。目标 API 4.7 内含该方法。
- [x] 0.4 ~~运行期 MethodBind 注册探针~~ **不做**（Q1=(c)，静态函数整体剔除）

**验证证据**：`research/phase0-findings.md`；GDScript 侧「写入常量」形态的补充实测见
`.agent_tmp/gdcallable/`（任务收尾已清理） 的 `y1`–`y8`（const 基 → 编译期拒绝）、`w1`–`w6`、`z1`–`z6`
（非 const 基 → 运行期错误 + **仅终止当前函数**），结论已回写 `design.md` §5.3。
**回滚点**：无（纯探针，不碰 `src/`）。

---

## 阶段 1：数据模型与解析（R1）

**目标**：类解析阶段能产出常量 / 枚举 / 静态变量三个集合（**无静态函数**，R8.5）。

- [x] 1.1 `src/runtime/bridge/jsb_class_info.h`：新增 `ScriptConstantKind` /
      `ScriptConstantInfo` / `ScriptStaticVariableInfo`；`StatelessScriptClassInfo` 加
      `constants` / `static_variables`（`design.md` §3.1）。
      **不加 `static_methods`、不动 `ScriptMethodInfo`** —— 静态函数不做（R8.5），
      数据模型里不为其留位置。
- [x] 1.2 `src/runtime/bridge/jsb_class_info.cpp` `_parse_script_class_iterate`：
      - 在既有 `clear()` 处（`:100-104`）同步清空两个新容器（**A8 的前提**）
      - 新增 `class_obj` 静态自有属性枚举（`getOwnPropertyNames` + `getOwnPropertyDescriptor`，
        对齐既有 prototype 枚举的写法 `:110-130`）；**显式跳过 `length` / `name` / `prototype`**
      - **先按注解分派**（D2 / R4.4）：**只有被注解标记的成员**才进入后续分类，未标记的一律忽略
      - 被 `@…const()` 标记的值按 `typeof` 预筛（`boolean`/`number`/`string`/`bigint`/`object`），
        再走 `TypeConvert::js_to_gd_var`（**无提示重载**即可 —— 容器由 `GArray`/`GDictionary` 包装
        命中 `IF_VariantFieldCount` 分支原样返回）；`typeof === "object"` 的先试枚举识别；
        转换失败静默忽略（R1.2 / A5）。判定表见 `research/phase0-findings.md` §0.2
      - **转换成功后按 R2.3 白名单放行**（`prd.md` R2.3 / `design.md` §4.2）：只接受 **8 种**
        Variant 类型 —— `NIL` / `BOOL` / `INT` / `FLOAT` / `STRING` / `STRING_NAME` + `ARRAY` /
        `DICTIONARY`；`Vector2`/`Color`/`PACKED_*`/`OBJECT`/`CALLABLE`/`SIGNAL` 一律剔除。
        **不能**指望 `js_to_gd_var` 返回 false —— 这些包装都命中
        `InternalFieldCount == IF_VariantFieldCount` 分支原样返回 Variant ⇒ 转换"成功"
      - 产出 `ARRAY`/`DICTIONARY` 的常量标 `ScriptConstantKind::Container`，**递归** `make_read_only()`
        （浅层 API，须自行遍历内层；见 `design.md` §4.2 / §5.3、`research/phase0-findings.md` §0.3）
      - 被白名单剔除却带注解的成员打 `JSB_LOG(Warning, ...)`（R5.3），不产出成员
      - 枚举识别与归一化（`design.md` §4.3；数字枚举 → `Dictionary{name:int}`，
        字符串枚举 → 普通 `Dictionary` 常量）
      - **函数值（`typeof value === "function"`）按 R1.2 忽略**，不产生任何成员；
        被注解标记却转换失败时打 `JSB_LOG(Warning, ...)`（R5.3）
      - 注解标记的常量/静态变量按符号分派；**未标记的忽略**（D2）
- [x] 1.3 `src/runtime/bridge/jsb_environment.h` `Symbols`：新增 `ClassConstants` /
      `ClassSharedStatics`（追加在 `MemberDocMap` 之后、`ClassModuleId` 之前，`design.md` §8.2）
- [x] 1.4 C++ 单测（`src/runtime/tests/`，遵循 `.trellis/spec/godotjs-ext/test/doctest.md`）：
      覆盖「基础类型常量（`number`/`string`/`boolean`/`bigint`/`null`）/ 枚举归一化 /
      字符串枚举作为普通 Dictionary / **容器常量**（`GArray.create` / `GDictionary.create` 构造，
      断言递归后顶层与**内层** `is_read_only()` 均为 `true`）/ **白名单剔除**：`Vector2`、`Color`、
      `Packed*Array`、Godot 对象、`Callable`、`Signal` 全部被忽略 / 纯 JS 对象与 JS 字面量数组
      被忽略 / 函数值被忽略 / `undefined` 被忽略（含 `static X;` 未初始化形态）/
      `null` 作为 NIL 常量保留」。

**验证证据**：单测通过；`.agent_tmp/` 下的解析 dump（VeryVerbose 日志或临时 dump 脚本）。
**回滚点**：仅 `jsb_class_info.*` + 符号枚举；`GodotJSScript` 未改动，行为不变。

---

## 阶段 2：常量与枚举的跨语言暴露（R2 / R7.2）

**目标**：GDScript 能读常量/枚举；inspector 与远程调试器能看到。

- [x] 2.1 `GodotJSScript::_get_constants()`：由 `script_class_info_.constants` 展开，
      **只报自有、不合并 `base` 链**（对齐 `GDScript::get_constants` `gdscript.cpp:911-917`；
      `Script::get_script_constant_map()` 是 ClassDB 绑定，dump 的就是这个返回值）。
      **不得包含函数名**（R5.2）。
      （2026-09-26 修正：原实现沿 `base` 链合并，实测使派生脚本的该 API 偏离 GDScript，已删除。）
- [x] 2.0 **修 `_get_method_info()`（R5.1）—— 这是 R2 的前置，不是可选优化**：
      现状（`jsb_script.cpp:365-372`）对**任意**名字都返回 `{"name": p_method}`
      （release 下 `jsb_check` 是空操作），而 `reduce_identifier_from_base` 的 ② 分支
      （`gdscript_analyzer.cpp:4366-4373`）**无 `has_method` 守卫** ⇒ 常量（④）与信号（③）
      **永远解析不到**。改为：命中 `script_class_info_.methods` 才返回 `MethodInfo`，
      否则返回空 `Dictionary`（`MethodInfo::from_dict` 得 `name == StringName()`，② 自然不命中）
- [x] 2.2 `GodotJSScript::_get()`：先实现常量分支 + 基类链
      （对齐 `GDScript::_get` 的 `constants` → 基类，`gdscript.cpp:975-1010`）
- [x] 2.3 `GodotJSScript::_get_members()`：返回实例成员名集合（R7.1 / A6）
- [x] 2.4 **枚举 Dictionary 与容器常量施加 `make_read_only()`**（容器**递归**）；基础值常量无需
      施加（引擎对基础值没有只读概念）。保护见 `design.md` §5.3：枚举/容器靠只读标记
      （容器与 JS 侧共享 `_p` ⇒ 两侧同时冻结），基础值靠「常量不进 `_get_property_list()` +
      `_set` 拒绝写常量」。写入的后果是**打印脚本错误并终止当前 GDScript 函数**（已实测澄清：
      不是进程挂起）。**文档必须写明**：容器常量施加后 JS 侧写该容器也会报 engine error
- [x] 2.5 `project/` 测试项目：在现有 `.ts` 测试脚本中加常量与枚举；新增 GDScript 测试脚本
      （`project/` 下目前无 `.gd`；`--script res://x.gd` + `extends SceneTree` 的形态已实测可行）
      断言 `SomeScript.CONST` / `SomeScript.MyEnum` / `load(...) as Script` 后的
      `get_script_constant_map()`（**不能**用 `SomeScript.get_script_constant_map()` 直连，
      实测为编译期错误）

**验证证据**：A1 / A2 / A4 / A5 / A6 的实测输出（GDScript 侧打印或 C++ 单测断言）。
**回滚点**：只回滚 `jsb_script.*`；阶段 1 的数据模型保留。

> **决策门**：阶段 2 通过后，A1/A2/A4/A5/A6 即达成。

---

## 阶段 3：注解体系 + 静态变量（R3 / R4 / R6）

**依赖**：Q2 命名已定 —— `@bind.exposed.const()` / `@bind.exposed.shared()`。

- [x] 3.1 注解实现（**命名已定**：`@bind.exposed.const()` / `@bind.exposed.shared()`）：
      - `scripts/jsb.runtime/src/godot.annotations.ts` 的 `createClassBinder()` 增加注解
      - `scripts/typings/godot.minimal.d.ts` 增加 `jsb.internal.*` 类型声明
      - `src/runtime/bridge/jsb_bridge_module_loader.cpp` 增加对应桥接函数 + 绑定表项（`:542-548`）
      - `src/editor/codegen/jsb_codegen_annotations.cpp` 登记注解类型（R4.3 / A10）
- [x] 3.2 方案 A（`design.md` §5.2）：进程级 `HashMap<module_id, HashMap<name, Variant>>`
      + JS 访问器替换（`defineProperty` get/set → `jsb.internal.get/set_static_shared`）
      - **必须在 `GodotJSScriptLanguage::_finish()` 显式 `clear()`**
        （对齐 `jsb_script_language.cpp:256-259` 的既有先例）→ A9 的前置
      - 锁：独立互斥量，明确与 `GodotJSScriptLanguage::mutex_` 的加锁顺序
- [x] 3.3 `GodotJSScript::_get_property_list()`：`script/source` + 各静态变量的 `PropertyInfo`
- [x] 3.4 `_set()`：只查静态变量（对齐 `gdscript.cpp:1023-1053`）
- [x] 3.5 继承链语义（A7，2026-09-26 修正）：`_get_constants()` / `_has_method()` / `_get_method_info()` /
      `_get_members()` **只报自有**，**不再合并** `base` 链（对齐 GDScript；`get_script_constant_map()`
      是 ClassDB 绑定，合并即偏离）。继承值仍经 `_get` / `_set` 在**运行期**可达（`_get` 自己走 `base`）；
      `_get_property_list()` / `_get_script_property_list()` 反过来**必须**走链（GDScript 的这两个面
      自己也走）。实测边界（`const` 基解析期硬失败 vs `var` 基退化到运行期）见 `prd.md` A7 与
      `qa-q1-q7.md` Q6。
- [x] 3.6 热重载（A8）：重载后重建成员集合；方案 A 需保留仍存在键的值、删除已移除键，逻辑幂等
- [x] 3.7 跨环境一致性实测（A3）：主环境 + worker 各读写一次，断言互见

**验证证据**：A3 / A7 / A8 的实测输出；`_finish()` 释放后 Orphan StringName 仍为 0。
**回滚点**：方案 A 的进程级存储与访问器可独立回退到方案 B（保留 `_get` 常量分支）。

## 全阶段收尾

- [ ] C1 `misc/verify_codegen.py` 全量校验（A11）；typings 基线仅预期变化
      —— 复核（2026-09-25 追加轮）：**24 处差异**（20 → 24，增量全部是新增夹具的生成产物），
      已逐项归因（见 `report.md`「A11 复核」表）。`--update-baseline` 仍未执行，待用户确认
      —— 质询答复轮（2026-09-25）：**25 处**（+`jsb.runtime.bundle.d.ts`，因 Q3 修复新增导出类型
      `StaticMemberDecoratorContext`），归因见 `report.md`「质询答复轮」段
- [x] C2 完整验收：`cd project && godot --audio-driver Dummy --headless --path . --verbose`
      → Orphan StringName = 0、`GODOTJS_TEST_PROJECT_COMPLETED` = 1、`GODOTJS_TEST_PROJECT_FAILED` = 0
      （命令与判据见 `.trellis/spec/godotjs-ext/test/index.md`）
- [x] C3 文档（`README.md` 或 `scripts/CHANGELOG.md`）：
      - 支持的常量类型范围（**JS 基础值 + enum + 容器**；容器**必须**用 `GArray.create([...])` /
        `GDictionary.create({...})` 构造，且**施加只读后 JS 侧写它也会报 engine error** ——
        两侧同时不可变是特性不是缺陷）；`Vector2`/`Color`/`Packed*`/对象等带对象身份的值
        **不作常量**（理由：JS 的 `const` 只冻结绑定、不冻结对象，且 `Object.freeze` 实测挡不住
        值类型的 prototype accessor）与「运行时无法自动判定 const」的事实（靠注解声明意图）；
        需要跨语言暴露这些值 → `@bind.exposed.shared()` 静态变量
      - **边界**：TS `readonly` 编译后被擦除 ⇒ JS 侧 `MyClass.MAX = 999` 仍合法；
        我们的常量是**解析期值快照**，GDScript 侧读到的值恒定不变（不联动、不报错）。
        不可变性只对 GDScript 侧成立
      - `const enum` 不支持；字符串枚举按普通 `Dictionary` 常量暴露
      - **静态函数不支持**：`SomeScript.fn` / `SomeScript.fn.call(...)` /
        `SomeScript.call("fn", ...)` 三种形态 + 原因（R8.5 / A12）
      - **常量禁止写入**：写入 = 打印脚本错误并终止当前 GDScript 函数（非挂起）；
        基础值常量保护来自 `_set` 拒绝 + 不进 `_get_property_list()`，
        枚举/容器常量保护来自 `make_read_only()`（容器递归）
      - 静态变量的跨环境语义（方案 A）
      - 枚举常量是 `Dictionary`（`Dictionary{name:int}`；字符串枚举为普通 `Dictionary`），
        由我方新建并 `make_read_only()`；容器常量只读是**递归施加**的
      - **GDScript 编辑器识别**（R2.5，已实测）：**直接赋值** `SomeScript.N = 5` **编译期**报
        `Cannot assign a new value to a constant.`（`_get_constants()` 实现即得）。
        **嵌套形式永不折叠**（折叠路径因基是 metatype 而跳过）⇒ `SomeScript.ARR[0] = 7`
        **编译期不报**、只在运行期被 `make_read_only()` 拒绝；**枚举成员须用索引访问**
        （`SomeScript.E["EA"]` 可用，点访问 `SomeScript.E.EA` 编译期报 `Cannot find member`）。
        **运行期 guard 无论如何都在**
      - **不做**：方法参数/返回值/信号参数的类型信息（Q2 附带决定，理由见
        `research/q1q2-freeze-and-signatures.md` §Q2）
- [x] C4 spec 更新：把「GDExtension 无法覆盖 `Object::callp`」「`_get`/`_set`/`_get_property_list`
      对 ScriptExtension 可达」「`_get_constants` 走 `make_builtin_meta_type` 会让 `.call()`
      在编译期被拒」三条引擎事实写入 `.trellis/spec/godotjs-ext/cpp/`
- [x] C5 清理：`./.agent_tmp/` 下的探针与 dump；`project/` 下无临时文件

---

## 追加轮（2026-09-25）：注解双形态 + 审查项修复

- [x] D1 双形态可行性验证（探针，结论：运行期完全同形、C++ 侧零改动）：
      `shape.mjs` 描述符逐字段相等；`probe3/probe4/probe5.ts` 证 TS1068/TS1206；
      `order.ts` 证类装饰器执行时命名空间成员未挂上；`coexist.ts`/`answer.ts` 证两形态并存
- [x] D2 `scripts/jsb.runtime/src/godot.annotations.ts`：`exposed.const` / `exposed.shared` 改为
      **局部重载函数** `exposed_const` / `exposed_shared`（对象字面量方法不能承载重载签名），
      按实参个数分派；JSDoc 写明两形态等价、TS1206（不能装饰命名空间成员）、TS2652（合并声明不能带
      `export default`）与类级点名是解析期解析
- [x] D3 `src/editor/codegen/jsb_codegen_annotations.cpp` `build_class_binder()`：`exposed` 叶子改为
      **两个调用签名的交集**（`DescriptorType` 无 rest 支持 ⇒ 用 `make_intersection`）；零参签名必须
      **在前**（rest 签名也接受零参，靠前者优先）
- [x] D4 `scripts/typings/godot.generated.d.ts`：手工维护的 `ClassBinder` 镜像同步补 `exposed`
- [x] D5 测试项目：新增 `static-members-namespaced.ts`（类级点名 + 类内成员形态共存 +
      缺失名字 `NS_MISSING` + 双标 `NS_CONFLICT`）；`test-static-members.ts` 加 `checkNamespacedForm()`；
      `static-members-gdcheck.gd` 加 GDScript 侧断言
- [x] D6 审查项修复（3 项）：`_shared_static_setter` 静默写 NIL；`const`+`shared` 双标矛盾
      （新增 `annotated_constant_names` 守卫，常量胜出）；`jsb_shared_statics.cpp` 锁注释不实
      （改为只声明「叶子锁」这一成立的事实）。第 4 项（重复值/浮点值枚举被整体拒绝）**只文档化**，
      行为不变
- [x] D7 负向控制 ×2（按 spec）：冲突守卫改 `if (false && …)` → C++ 单测 `52/53`、`TESTS_RC=1`，
      失败点正是 `test_jsb_static_members.h(250)`；类级注册改 `void name;` → 验收 `FAILED=1`、
      失败项 `class form int`。两者还原（md5 核对）后全绿
- [x] D8 验证：`scons … tests=yes` `BUILD_EXIT=0`；C++ 单测 **53/53、681/681**；
      `pnpm gen:types`（含强制 `tsc`）`RC=0`；完整验收 `orphan=0`/`COMPLETED=1`/`FAILED=0`/`scenes=11`

---

## 独立静态审查轮（2026-09-25）：`trellis-check` 5/6 PASS + 1 FAIL

派单限定「不重编、不重跑测试、不做 git 操作」（重跑已有绿证属明令禁止的浪费），并要求
「≤20 次工具调用即 yield，未决项标 UNVERIFIED」+ `outputSchema` 结构化输出。

- [x] D9 审查结论落档：4 项 PASS（重载分派 / codegen 与实现一致 / setter 无 NIL 写入路径 /
      冲突守卫位置）+ 1 项 PASS（无负控残留）
- [x] D10 **FAIL 已修**：`_shared_static_setter` 的 NIL 修复**无永久断言** ⇒
      `test_jsb_static_members.h` 的 store 用例新增一段（向已安装访问器写纯 JS 对象，断言存储值不变）
- [x] D11 脆弱性 A 已修：两个类级分支加 `context.kind !== "class"` 守卫（把静默丢弃变明确报错），
      参数类型放宽为 `ClassDecoratorContext | ClassMemberDecoratorContext`
- [x] D12 脆弱性 B **记录不改**：共享槽位绝对初值断言依赖「本场景首个写者」；每轮验收新进程 +
      `_finish()` 清空存储 ⇒ 初值由初始化器确定性播种。已记入 `report.md`「已知边界」
- [x] D13 审查后复验：`scons … tests=yes` `BUILD_EXIT=0`；C++ **53/53、684/684**；
      `pnpm gen:types` `RC=0` 且 tsc error=0；完整验收 `orphan=0`/`COMPLETED=1`/`FAILED=0`/`scenes=11`

---

## 验证命令速查

```bash
# C++ 单测（按 spec 的双套件约定，见 test/doctest.md）
# 构建（只用规范组合，禁止 scons --clean，见 build/scons-build.md）
scons platform=windows arch=x86_64 target=editor dev_build=yes tests=yes

# TS 编译（改动 project/ 下 TS 后）
cd project && node node_modules/typescript/bin/tsc

# typings 重生成 + 类型检查
cd project && pnpm gen:types

# codegen 基线校验
python misc/verify_codegen.py

# 完整验收
cd project && godot --audio-driver Dummy --headless --path . --verbose
# 判据：Orphan StringName = 0；GODOTJS_TEST_PROJECT_COMPLETED = 1；GODOTJS_TEST_PROJECT_FAILED = 0
```

---

## 审查门

- **开工前**：用户对三件套的**审查与开工批准**（Q1=(c) / Q2 已拍板；Q3 已按 2026-09-25 用户指令
  定为「JS 基础值 + enum + 容器」）+ 阶段 0 实测结论。
- **阶段 2 后**：常量/枚举/容器交付物审查（`_get_method_info` 修复必须已验证，否则 A1/A2 不成立；
  A13 的容器递归只读与 A14 的编辑器识别必须实测）。
