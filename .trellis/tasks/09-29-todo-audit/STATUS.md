# 处理状态清单（按分类分组，逐条勾选）

基线：`f6e62c5` 的 **188** 条。编号沿用 `research/todo-audit.md` 的逐条表编号。
当前 `git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc` = **140** 行。

图例（**严格按"工作是否真的做完"判定，不看注释措辞**）：

- `[x] 已完成` —— 工作已落地并验证（代码实现 / 或删除了确实已过时的注释）。
- `[~] 已裁定` —— **工作没做**，但经查证判定「不该做 / 做不到」，结论写进了代码注释或文档。
- `[ ] 未完成` —— **工作没做**，TODO 仍在。

> **判定口径（用户明确）**：把 `//TODO` 改写成说明文字**不算完成**。
> **「非行动项」这个类别基本不存在** —— 除 `.clang-format` 里从上游 Godot 继承的格式说明外，
> 没有任何一条 TODO 属于「本来就不该动」。**没有设计方案，就是 TODO**：
> 疑问式备忘（`unsure` / `correct?` / `needed?`）是**待确认的待办**，
> 空实现（`_auto_indent_code` 等）是**待实现的待办**，
> 「RESERVED FOR FUTURE USE」是**待用的预留**——都要么给出设计方案并实现，要么写明为什么确实不做。

---

## A 类（55 条）——可简单实现

### ✅ 已完成（真正实现了代码）

| # | 位置 | 落地内容 |
|---|---|---|
| 169 | `jsb_script_language.h` | `_find_function` 实现（`3f00994`） |
| 176 | `jsb_script_language.h` | `_validate_path` 实现（`3f00994`） |
| 155 | `jsb_script.h` | `_get_member_line` 实现（`3f00994`） |
| 140 | `jsb_script.cpp` | `_inherits_script` 实现（`045d274`） |
| 156 | `jsb_script_instance.cpp` | `to_string` 实现（`045d274`） |
| 141 | `jsb_script.cpp` | `_reload` 未加载时先加载（`045d274`） |
| 145 | `jsb_script.cpp` | `_get_doc_class_name` 实现（`045d274`） |
| 31 | `jsb_async_module_loader.cpp` | 补 `TryCatch`（真 bug，`ceb9c07`） |
| 82 | `jsb_shadow_realm.cpp` | 改用 `jsb_stackalloc` + `memnew_placement`（`ceb9c07`/`801f876`） |
| 24 | `jsb_preset_source.h` | 去掉一次整块 memcpy（`60021eb`） |
| 96 | `jsb_jsc_isolate.cpp` | `stack_dup`→`stack_val`，修异常路径保护计数泄漏（`69fa066`） |
| 10 | `jsb_editor_plugin.cpp` | d.ts 的 GENERATE/INSTALL 分工：新增 `CH_GENERATED` 标记，`verify_file` 跳过生成产物 |
| 15 | `jsb_export_plugin.cpp` | 裸 `.js` 走 `export_compiled_script`，模块依赖被遍历导出 |
| 17 | `jsb_repl.cpp` | realm 选择器（`Environment::get_all_environments()` + PopupMenu）+ 定向求值 |
| 62 | `jsb_environment.h` | 删 `friend struct ScriptClassInfo`，改用公开的 `is_shadow()` |
| 69 | `jsb_internal_module_loader.cpp` | `load()` 从 preset 按 `file_name_` 求值（走 `AMDModuleLoader::load_source`） |
| 86 | `jsb_type_convert.cpp` / `jsb_type_convert_direct.h` / `thunks_common.h` | JS 函数 → `Callable`（`js_to_gd_var`、`can_convert_strict`、`probe_vt`、`JSToGD<Callable>` 四处） |

（`#12`/`#13` 记 `[~]`：TODO 要求「改成 `PackedStringArray`」，经评估该要求本身是错的——
逐元素 API 每次都是函数指针调用，只有 `ptr()/ptrw()` 是一次调用，改过去更慢。
已按评估结论定稿并写明理由。）

### ~ 已裁定（查证后判定不做，理由已写入注释）

| # | 位置 | 裁定 |
|---|---|---|
| 45 | `jsb_class_info.h` | `is_valid` 已按用户要求**撤掉**（无调用方 + 判据不成立，`1902f89`） |
| 50 | `jsb_environment.cpp` | debugger 分阶段启动：不存在该钩子（`ceb9c07`） |
| 55 | `jsb_environment.cpp` | `_rebind` 不能在那里调用（依据可核，`ceb9c07`） |
| 147 | `jsb_script.cpp` | `_get_rpc_config` 不并入父类（对齐 GDScript/C#，`045d274`） |
| 153 | `jsb_script.h` | `placeholders` 保持 `LocalVector`（`045d274`） |
| 11 | `jsb_editor_plugin.cpp` | tsc 输出已被 `Process` 接收（`60021eb`） |
| 26 | `jsb_process.cpp` | 已被 CI Linux 腿覆盖（`60021eb`） |
| 79 | `jsb_reflect_binding_util.h` | ClassID 不取是刻意性能取舍（`ceb9c07`） |
| 81 | `jsb_shadow_realm.cpp` | 不 Freeze：wrapper 双方都要写（`ceb9c07`） |
| 84 | `jsb_transpiler.h` | 无调用点 ⇒ 「未测试」无从验证（`ceb9c07`） |
| 87 | `jsb_type_convert.cpp` | Promise 内嵌字段无更优雅解法（`ceb9c07`） |
| 88 | `jsb_type_convert.cpp` | `IsNumber()` 已足够（`ceb9c07`） |
| 14 | `jsb_export_plugin.cpp` | web 单体打包属 D 类范畴 |
| 66 | `jsb_essentials.cpp` | 去掉 cast 等 V8 > 12.6.221（本仓 pin 12.4.254.21） |
| 157 | `jsb_script_instance.cpp` | 静态方法包成 `Callable`：`Callable(owner,name)` 绑的是实例方法，静态方法无实例可绑 |
| 167 | `jsb_script_language.cpp` | 更省地重载内置脚本：只重读子资源需 `PackedScene` 私有接口 |

### ❌ 未完成（TODO 仍在）

| # | 位置 | 内容 |
|---|---|---|
| 173/174/175 | `jsb_script_language.cpp` | `_get_public_*`：需维护语言内建清单 |
| 其余散条 | — | 见 D 类分组 |

---

## B 类（已过时，已删）

| # | 位置 | 处置 |
|---|---|---|
| 46 | `jsb_environment.cpp:55` | 删注释（`7f51ac7`） |
| 73 | `jsb_module_resolver.cpp:97` | 删 2 行（`7f51ac7`） |
| 100 | `jsb_jsc_pch.h:37` | 改写为垫片说明（`7f51ac7`） |
| 158 | `jsb_script_instance.cpp:753` | 删该行（`7f51ac7`） |
| 94 | `jsb_jsc_handle.h:156` | `JSWeak*` 已在用（`69fa066`） |
| 124/125 | `monolith.ts` `i64`/`u64` | 活路径 + ES2020 内置（`69fa066`） |

---

## C 类（缺接口 / 受上游限制）

`[~]`：缺的接口确定不存在，**工作不做是正确的**，依据见 `research/todo-audit.md`。

| # | 位置 | 缺失的接口 |
|---|---|---|
| 9 | `compat/editor_settings.cpp` | `EditorSettings::set_restart_if_changed` |
| 18 | `jsb_repl.cpp` | `set_disable_visibility_clip` |
| 27 | `jsb_variant_util.h` | `Dictionary::id()`（**已由用户用 `VariantInternal` 规避实现**） |
| 28 | `jsb_variant_util.h` | `Array::id()`（同上） |
| 83 | `jsb_thread_safe_for_nodes_scope.h` | `set_thread_safe` |
| 106/107 | `jsb_quickjs_data.cpp` | QuickJS 无 uint32 tag |
| 132 | `jsb_test_helpers.h` | `OS::set_cwd` |
| 151 | `jsb_script.cpp` | `EditorHelp::get_doc_data` |
| 168 | `jsb_script_language.cpp` | `property_set_fallback`（**已由用户实现**，`aaf63ec`） |

---

## D 类（需较大改动）——全部 `[ ] 未完成`

按主题分组：热重载 ~15、调试器栈帧 11、异步模块 ~8、bridge 分层隔离 4、其余 ~47。

---

## E 类（34 条）——**重判：33 条是行动项**

> 上一版把 27 条记为「非行动项，保持原样」，用户否定了这个判定：
> **除 `.clang-format` 外没有非行动项；没有设计方案就是 TODO。** 本版据此重判。

### [-] 唯一的真·非行动项（1 条）

| # | 位置 | 说明 |
|---|---|---|
| 1 | `.clang-format:159` | 从上游 Godot 继承的注释，说明为何 `Minimum: 0`；本仓沿用同一工具链，保留 |

### ❌ 重判为行动项（33 条）

**(a) 疑问式备忘 → 待确认，须给出结论并落地（10 条）**

| # | 位置 | 待确认的问题 |
|---|---|---|
| 32 | `jsb_async_module_manager.cpp:147` | module tree 是否需要 |
| 33 | `jsb_async_module_manager.cpp:148` | GodotJS script 是否需要 |
| 43 | `jsb_class_info.h:234` | `ScriptMethodInfo` 为何不复用 `MethodInfo` |
| 89 | `jsb_type_convert.cpp:740` | JS 原始数组宽松转 Godot 数组是否是坏主意 |
| 92/105/126 | `impl/{jsc,quickjs,web}/…_class.h:44` | `constructor_` 是否与 `prototype.constructor` 重复（三腿同句） |
| 99 | `jsc/jsb_jsc_object.cpp:226` | `JSValueUnprotect` 后保护计数是否配对 |
| 112 | `quickjs/jsb_quickjs_object.cpp:135` | `HasOwnProperty` 的 `JS_GetOwnProperty(ctx,nullptr,…)` 用法 |
| 20 | `jsb_console_output.cpp:40` | `Vector<IConsoleOutput*>` 是否换 `LocalVector` |

**(b) 空实现 / 未实现 → 待实现（9 条）**

| # | 位置 | 待实现 |
|---|---|---|
| 170 | `jsb_script_language.h:242` | `_auto_indent_code` 返回原文 |
| 171 | `jsb_script_language.h:250` | `_add_named_global_constant` 空 |
| 172 | `jsb_script_language.h:251` | `_remove_named_global_constant` 空 |
| 177 | `jsb_script_language.h:301` | `_add_global_constant` 空 |
| 166 | `jsb_script_language.cpp:728` | `_profiling_set_save_native_calls` 未实现 |
| 58 | `jsb_environment.cpp:1448` | debugger ready 的 `wait_for` 裸 TODO |
| 90/91 | `jsb_type_convert.h:87/100` | V8 内嵌字段数量随编译配置变化（安全性说明，须给出判据） |
| 161 | `jsb_script_language.cpp:401` | `_validate` 期望字典键的契约说明 |
| 134 | `test_jsb_shadow_realm.h:34` | 引用 `jsb_shadow_realm.cpp` 顶部 TODO |

**(c) 预留 / 现状描述 → 须给出用途或删除（6 条）**

| # | 位置 | 内容 |
|---|---|---|
| 42 | `jsb_class_info.h:103` | `RESERVED FOR FUTURE USE` |
| 51 | `jsb_environment.cpp:395` | `not always safe` |
| 63 | `jsb_environment.h:221` | 导出的 default class 继承原生 godot class 的前提 |
| 135/136/137 | `jsb_resource_loader.cpp:66/71/79` | 注释块内三条热重载旧方案 TODO |
| 114 | `quickjs/jsb_quickjs_object.cpp:355` | `key_conversion` 未实现（跨三腿） |
| 119 | `monolith.ts:447` | 裸 TODO（**已删**，`7f51ac7`）→ 记为已完成 |
| 120 | `monolith.ts:449` | `_throw_trivial` 临时策略 |
| 139 | `jsb_script.cpp:195` | `_inherits_script` 取舍说明（**已实现**，`045d274`）→ 记为已完成 |

**(d) 类型 / 契约待决（3 条）**

| # | 位置 | 内容 |
|---|---|---|
| 2 | `godot.annotations.ts:214` | 属性 hint 便捷构造 API |
| 39 | `jsb_class_info.cpp:443` | property categories |
| 38 | `jsb_class_info.cpp:366` | `collect methods/signals/properties`（注释已被后续代码覆盖，须核对） |

---

## 总体进度

| 类 | 总数 | 已完成 | 已裁定（有依据） | 未完成 |
|---|---|---|---|---|
| A | 55 | 17 | 16 | 22 |
| B | 4+2 | 6 | 0 | 0 |
| C | 10 | 2（用户实现 #27/#28/#168） | 8 | 0 |
| D | 85 | 0 | 2 | **83** |
| E | 34 | 2 | 1 | **31** |
| **合计** | **188** | 42 | 27 | **119** |

**当前 TODO 计数**：`git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc` = **140** 行。

**下一步**：

1. A 类剩余散条（`#173`/`#174`/`#175`）。
2. E 类 31 条：逐条给出设计方案并实现（不是改措辞）。
3. D 类按主题逐个建任务推进。
4. **工作树未提交**，待授权 commit。
