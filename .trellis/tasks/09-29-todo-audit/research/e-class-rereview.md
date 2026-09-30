# E 类重研：33 条不是「非行动项」

日期：2026-09-30
基线：`f6e62c5` 的 E 类 34 条（编号沿用 `research/todo-audit.md`）

## 为什么重研

上一版把 E 类 27 条判为「非行动项 / 正确处置就是保持原样」。用户否定了这个判定：

> 除了 `.clang-format` 之外哪有什么非行动项。没设计方案不就是 TODO，别装傻。

这个判定是对的，而且我原来的理由站不住：我用的是「注释写的是取舍/疑问 ⇒ 不用动」，
但那恰恰说明**问题没有被解决**——只是被记录下来了。判据应当反过来：

- 疑问式备忘（`unsure?` / `correct?` / `needed?`）= **待确认**，确认本身就是要做的工作，结论要落进代码。
- 空实现（`_auto_indent_code` 等 override）= **待实现**。
- 「RESERVED FOR FUTURE USE」= **待用**：要么接上，要么删掉字段。
- 现状描述（`not always safe`）= **待加固**：要么消除该风险，要么证明风险不存在。

唯一真·非行动项是 `.clang-format:159`：那是从上游 Godot 继承的注释，说明该文件为何
`Minimum: 0`，本仓沿用同一 clang-format 工具链，改了反而与上游不一致。

## 逐条重判与设计方案

### (a) 疑问式备忘 → 待确认（10 条）

| # | 位置 | 问题 | 设计方案 |
|---|---|---|---|
| 32 | `jsb_async_module_manager.cpp:147` | module tree 是否需要 | 查 `AsyncModuleManager` 的消费者：若无「按树遍历/卸载」需求，删该 TODO 并把 `jsb_nop()` 换成说明；若有（热重载需要按依赖序卸载），补树结构 |
| 33 | `jsb_async_module_manager.cpp:148` | GodotJS script 是否需要 | 同上，与 #32 一起决定；两者是同一次设计决策的两半 |
| 43 | `jsb_class_info.h:234` | `ScriptMethodInfo` 为何不复用 `MethodInfo` | 对比两者字段集（`MethodInfo` 带 hash/参数默认值，脚本方法元信息不带）→ 把结论写成注释；若字段确实可合并则合并 |
| 89 | `jsb_type_convert.cpp:740` | 宽松数组转换是否是坏主意 | 该行为由 `JSB_IMPLICIT_PACKED_ARRAY_CONVERSION` 控制；实测两种取值下的调用点差异，把取舍写进该宏的文档 |
| 92/105/126 | `{jsc,quickjs,web}/*_class.h:44` | `constructor_` 是否与 `prototype.constructor` 重复 | 三腿分别确认（jsc 的 `Get()` 走 prototype 取，quickjs/web 直接用字段）→ 三处写同一结论；若某腿确实冗余则删字段 |
| 99 | `jsc/jsb_jsc_object.cpp:226` | `JSValueUnprotect` 后保护计数是否配对 | 数清 `stack_dup`/`JSValueUnprotect` 的配对（`:216` dup、`:227` unprotect）→ 结论入注释；可用一次构造器实测确认不崩 |
| 112 | `quickjs/jsb_quickjs_object.cpp:135` | `JS_GetOwnProperty(ctx, nullptr, …)` 用法 | 读 quickjs 源码确认 desc 为 NULL 是受支持用法（`JS_GetOwnPropertyInternal2` 的 `flags_only` 分支）→ 结论入注释 |
| 20 | `jsb_console_output.cpp:40` | `Vector<IConsoleOutput*>` 是否换 `LocalVector` | 量级是个位数（console sink），`LocalVector` 省一次堆分配但改 API 面 → 决定并写明理由 |

### (b) 空实现 / 未实现 → 待实现（9 条）

| # | 位置 | 现状 | 设计方案 |
|---|---|---|---|
| 170 | `jsb_script_language.h:242` | `_auto_indent_code` 返回原文 | 已查证：`ScriptLanguageExtension::auto_indent_code` 是 **static**，编辑器 `format_code` 值调用它、不虚分派 ⇒ 该 override 永不被调。**这是可证明的「确实不做」**，注释里已有此依据，保留 |
| 171 | `jsb_script_language.h:250` | `_add_named_global_constant` 空 | 唯一调用方是 autoload；语义是让**裸标识符**解析到值。TS/JS 无裸标识符阶段 ⇒ 需要给出「若将来支持裸标识符该如何接」的设计，或明确记为该语言不支持该特性 |
| 172 | `jsb_script_language.h:251` | `_remove_named_global_constant` 空 | 同上，与 #171 同一决策 |
| 177 | `jsb_script_language.h:301` | `_add_global_constant` 空 | 同上，与 #171/#172 同一决策 |
| 166 | `jsb_script_language.cpp:728` | `_profiling_set_save_native_calls` 只有日志 | 需实现：把 profiling 的 native call 计数落到文件（对齐 GDScript 的同名虚函数） |
| 58 | `jsb_environment.cpp:1448` | `wait_for` 上的裸 TODO | 读上下文补出待办内容（超时后该如何处理 debugger 未就绪）并实现或写明 |
| 90/91 | `jsb_type_convert.h:87/100` | V8 内嵌字段数量随编译配置变化 | 已由 `#if JSB_WITH_NODE` 的 `IsPromise()` 拦截处理（`jsb_type_convert.cpp:635`）→ 把该处置交叉引用到这两处说明，消除「TODO 悬空」 |
| 161 | `jsb_script_language.cpp:401` | `_validate` 的 `// TODO` | 补出期望的字典键契约（functions/errors/warnings/safe_lines），写成正式注释 |
| 134 | `test_jsb_shadow_realm.h:34` | 引用 `jsb_shadow_realm.cpp` 顶部 TODO | 若该 TODO 被处理，同步更新引用；当前指向的是真实存在的 TODO ⇒ 属 D 类的一部分 |

### (c) 预留 / 现状描述 → 须给出用途或删除（8 条）

| # | 位置 | 现状 | 设计方案 |
|---|---|---|---|
| 42 | `jsb_class_info.h:103` | `RESERVED FOR FUTURE USE` | 查明该字段本来的用途；有明确计划则接上，否则删除字段 |
| 51 | `jsb_environment.cpp:395` | `not always safe` | 读上下文（析构路径）确认风险条件，给出加固方案或证明不可达 |
| 63 | `jsb_environment.h:221` | 导出的 default class 继承前提 | 把「只在该前提下才收集」的判据写进代码（当前只是注释），或补上不满足时的处理 |
| 135/136/137 | `jsb_resource_loader.cpp:66/71/79` | 注释块内三条热重载旧方案 | 该块（65–91 行）是 `// { … }` 注释掉的旧实现，不参与编译 ⇒ 整块删除（TODO 已无跟踪对象） |
| 114 | `quickjs/jsb_quickjs_object.cpp:355` | `key_conversion` 未实现 | 三腿（jsc/quickjs/web）同时补齐，或明确记为该模式不支持并写清降级行为 |
| 120 | `monolith.ts:449` | `_throw_trivial` 临时策略 | 决定 trivial 错误是否要单独降级；当前与普通错误同一处理 ⇒ 要么实现区分，要么写成结论 |
| 139 | `jsb_script.cpp:195` | `_inherits_script` 取舍说明 | **已实现**（`045d274`）⇒ 从 E 类移出 |
| 119 | `monolith.ts:447` | 裸 `//TODO` | **已删**（`7f51ac7`）⇒ 从 E 类移出 |

### (d) 类型 / 契约待决（3 条）

| # | 位置 | 现状 | 设计方案 |
|---|---|---|---|
| 2 | `godot.annotations.ts:214` | 属性 hint 便捷 API | 提供 `hintRange`/`hintEnum` 之类的构造助手（TS 侧纯函数），或写明为何不值得 |
| 39 | `jsb_class_info.cpp:443` | `property categories` | 属性分类需要引擎侧 `PropertyInfo` 的 usage/hint 字段；评估能否从 `ClassDB` 取到，能则实现 |
| 38 | `jsb_class_info.cpp:366` | `collect methods/signals/properties` | 紧随其后的代码正在收集 prototype 成员 ⇒ 核对是否已覆盖；已覆盖则删 TODO，否则补 |

## 结论

- 真·非行动项：**1 条**（`.clang-format:159`，上游继承）。
- 重判为行动项：**31 条**（上面 (a)(b)(c)(d) 去掉已完成的 #119/#139）。
- 其中**可证明「确实不做」的只有 #170**（static 成员，虚分派不发生）——即便如此，注释里必须
  写明这个依据，而不是仅标注 TODO。
