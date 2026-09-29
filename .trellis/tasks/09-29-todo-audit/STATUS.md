# 处理状态清单（按分类分组，逐条勾选）

基线：`f6e62c5` 的 188 条。编号沿用 `research/todo-audit.md` 的逐条表编号。
当前计数：**152 条**（188 − 已实现/已删 41 + 恢复 5）。

图例：`[x] 已完成` · `[~] 已裁定（不改代码，结论写入注释/文档）` · `[ ] 未做` · `[-] 不适用/非行动项`

---

## A 类（55 条）——可简单实现

| 状态 | # | 位置 | 内容/处置 |
|---|---|---|---|
| [x] | 169 | `jsb_script_language.h:213` | `_find_function` 已实现（G1，`3f00994`） |
| [x] | 176 | `jsb_script_language.h:236` | `_validate_path` 已实现（G1） |
| [x] | 155 | `jsb_script.h:215` | `_get_member_line` 已实现（G1） |
| [x] | 140 | `jsb_script.cpp:196` | `_inherits_script` 已实现（G2，`045d274`） |
| [x] | 147 | `jsb_script.cpp:847` | `_get_rpc_config` → 裁定不并入父类（G2） |
| [x] | 156 | `jsb_script_instance.cpp:405` | `to_string` 已实现（G2） |
| [x] | 157 | `jsb_script_instance.cpp:553` | 静态方法转 Callable → 裁定不做 + 理由（G2） |
| [x] | 153 | `jsb_script.h:101` | `placeholders` → 裁定保持 `LocalVector` + 理由（G2） |
| [x] | 141 | `jsb_script.cpp:246` | `_reload` 未加载时先加载（G2） |
| [x] | 145 | `jsb_script.cpp:280` | `_get_doc_class_name` 已实现（G2） |
| [x] | 167 | `jsb_script_language.cpp:833` | 内置脚本重载 → 裁定保持现状 + 理由（G2） |
| [x] | 31 | `jsb_async_module_loader.cpp:86` | 加了 `TryCatch`（G4，`ceb9c07`，真 bug） |
| [x] | 45 | `jsb_class_info.h:332` | `is_valid` → **按用户要求已撤掉**（`801f876`） |
| [x] | 62 | `jsb_environment.h:179` | friend → TODO 保留 + 可行做法（`801f876`） |
| [x] | 50 | `jsb_environment.cpp:382` | debugger → 裁定不存在该钩子 + 理由（G4） |
| [x] | 66 | `jsb_essentials.cpp:180` | V8 cast → 裁定前置未满足（pin 12.4.254.21）（G4） |
| [x] | 69 | `jsb_internal_module_loader.cpp:32` | **TODO 保留**（无调用点但功能不完整，`801f876`） |
| [x] | 79 | `jsb_reflect_binding_util.h:398` | ClassID → 裁定说明（G4） |
| [x] | 81 | `jsb_shadow_realm.cpp:530` | Freeze → 裁定不做 + 理由（G4） |
| [x] | 82 | `jsb_shadow_realm.cpp:549` | `jsb_stackalloc` 已实现（G4，`memnew_placement`，`801f876` 修正为一次构造） |
| [x] | 84 | `jsb_transpiler.h:55` | 无调用点 → 裁定说明（G4） |
| [x] | 87 | `jsb_type_convert.cpp:610` | Promise 字段 → 裁定说明（G4） |
| [x] | 88 | `jsb_type_convert.cpp:655` | 整数检查 → 裁定说明（G4） |
| [x] | 55 | `jsb_environment.cpp:1282` | `_rebind` → 裁定不能在那里调用 + 依据（G4） |
| [x] | 10 | `jsb_editor_plugin.cpp:716` | **TODO 保留**（目标是建立分工，`801f876`） |
| [x] | 11 | `jsb_editor_plugin.cpp:1291` | tsc 输出 → 裁定已接收 + 依据（G3，`60021eb`） |
| [x] | 12 | `jsb_editor_plugin.h:161` | 类型 → **评估后保持 `Vector<String>`**（`801f876`） |
| [x] | 13 | `jsb_editor_plugin.h:162` | 同上 |
| [x] | 15 | `jsb_export_plugin.cpp:297` | **TODO 保留**（`801f876`） |
| [x] | 17 | `jsb_repl.cpp:58` | **TODO 保留**（`801f876`） |
| [x] | 24 | `jsb_preset_source.h:130` | 去一次 memcpy（G3） |
| [x] | 26 | `jsb_process.cpp:310` | 裁定已被 CI Linux 腿覆盖 + 依据（G3） |
| [~] | 14 | `jsb_export_plugin.cpp:273` | web 单体打包 → 属 D 类范畴，未动 |
| [ ] | 93 | `jsb_jsc_data.cpp:49` | jsc value hash 改进 |
| [ ] | 94 | `jsb_jsc_handle.h:156` | **B3 调研结论：已是 B 类**（`JSWeak*` 已落地）——待删 |
| [ ] | 96 | `jsb_jsc_isolate.cpp:472` | `copy or steal?` → B3 判 A 并给出单行改法 |
| [ ] | 101 | `jsb_jsc_primitive.cpp:57` | `ToDetailString` 无等价实现 |
| [ ] | 102 | `jsb_jsc_primitive.cpp:100` | `External::Value` 待确认 |
| [ ] | 108 | `jsb_quickjs_ext.h:118` | unsafe eq check |
| [ ] | 109 | `jsb_quickjs_isolate.cpp:67` | JSObject realloc 假设 |
| [ ] | 110 | `jsb_quickjs_isolate.cpp:92` | 同上 |
| [ ] | 113 | `jsb_quickjs_object.cpp:332` | `configurable` 标志 |
| [ ] | 115 | `jsb_quickjs_primitive.cpp:223` | 避免 `JS_NewUint32` |
| [ ] | 116 | `jsb_quickjs_typedef.h:64` | 待验证 |
| [ ] | 122 | `monolith.ts:1051` | browser global object |
| [ ] | 124 | `monolith.ts:1526` | `i64` BigInt64Array |
| [ ] | 125 | `monolith.ts:1535` | `u64` BigUint64Array |
| [ ] | 127 | `jsb_web_helper.h:43` | SetDeleter 未测试 |
| [ ] | 128 | `jsb_web_helper.h:67` | copy from HEAP? |
| [ ] | 129 | `jsb_web_object.cpp:199` | `CONFIGURABLE` 标志 |
| [ ] | 133 | `test_jsb_any_runtime.h:168` | node 构建路径差异 |
| [ ] | 160 | `jsb_script_language.cpp:193` | 脚本列表管理 |
| [ ] | 162 | `jsb_script_language.cpp:402` | `_validate` parse error info |
| [ ] | 163 | `jsb_script_language.cpp:449` | `icon_path` 未赋值 |
| [ ] | 164 | `jsb_script_language.cpp:451` | `is_abstract` 未置位 |
| [~] | 152 | `jsb_script.cpp:1039` | `_update_exports` 行为确认 → 需设计确认 |
| [~] | 86 | `jsb_type_convert.cpp:584` | `IsFunction` → `Callable` → 用户已给方向，**未判** |

**A 类小结：已处置 31 / 55，未做 24。**

---

## B 类（4 条）——已过时

| 状态 | # | 位置 | 处置 |
|---|---|---|---|
| [x] | 46 | `jsb_environment.cpp:55` | 删注释（`7f51ac7`） |
| [x] | 73 | `jsb_module_resolver.cpp:97` | 删 2 行（`7f51ac7`） |
| [x] | 100 | `jsb_jsc_pch.h:37` | 改写为垫片说明（`7f51ac7`） |
| [x] | 158 | `jsb_script_instance.cpp:753` | 删该行（`7f51ac7`） |

**B 类 4/4 完成。**

---

## C 类（10 条）——不可实现 / 受上游限制

| 状态 | # | 位置 | 依据 |
|---|---|---|---|
| [~] | 9 | `compat/editor_settings.cpp:133` | godot-cpp 无 `set_restart_if_changed` |
| [~] | 18 | `jsb_repl.cpp:159` | godot-cpp 无 `set_disable_visibility_clip` |
| [~] | 27 | `jsb_variant_util.h:75` | godot-cpp 无 `Dictionary::id()`，只有 `hash()` |
| [~] | 28 | `jsb_variant_util.h:79` | godot-cpp 无 `Array::id()` |
| [~] | 83 | `jsb_thread_safe_for_nodes_scope.h:31` | godot-cpp 无 `set_thread_safe` |
| [~] | 106 | `jsb_quickjs_data.cpp:113` | QuickJS 只有 `JS_TAG_INT` |
| [~] | 107 | `jsb_quickjs_data.cpp:120` | 同上 |
| [~] | 132 | `jsb_test_helpers.h:75` | godot-cpp 无 `OS::set_cwd` |
| [~] | 151 | `jsb_script.cpp:936` | godot-cpp 无 `EditorHelp::get_doc_data` |
| [~] | 168 | `jsb_script_language.cpp:892` | godot-cpp 无 `property_set_fallback` |

**C 类 10/10 已裁定**（判定依据在 `research/todo-audit.md` 的逐条表；代码未动是正确处置）。
**待办：把「未暴露」的结论从 `TODO` 改写成 `NOTE`**，使其不再计入 TODO（用户未要求，未做）。

---

## D 类（85 条）——需较大改动

**全部未做**，按主题聚为 5 组（每组的规模与建议见 `research/todo-audit.md` §9）：

| 主题 | 条数 | 代表条目 |
|---|---|---|
| 热重载 | 约 15 | #142/#143/#144/#148/#149/#150/#56/#57 |
| 调试器栈帧（`_debug_*`） | 11 | #178–#188 |
| 异步模块 | 约 8 | #29/#30/#34/#59/#67/#70/#71/#72 |
| bridge 分层隔离 | 4 | #34/#37/#65/#74 |
| 其余（导出/类型转换/平台腿等） | 约 47 | #3/#4/#5/#8/#14/#16/… |

**注意**：#173/#174/#175（`_get_public_*`）虽列 D 类，但**已按调研裁定「不值得做」**，只差把注释从 TODO 改写为结论。

---

## E 类（34 条）——非行动项 / 不适用

| 状态 | # | 位置 | 处置 |
|---|---|---|---|
| [x] | 171 | `jsb_script_language.h:220` | autoload → 不适用注释（G1） |
| [x] | 172 | `jsb_script_language.h:221` | 同上 |
| [x] | 177 | `jsb_script_language.h:265` | 同上 |
| [x] | 170 | `jsb_script_language.h:219` | `_auto_indent_code` 死代码注释（G1） |
| [x] | 139 | `jsb_script.cpp:195` | 随 `_inherits_script` 实现一并处置（G2） |
| [x] | 119 | `monolith.ts:447` | 裸 TODO 已删（`7f51ac7`） |
| [x] | 58 | `jsb_environment.cpp:1448` | 行尾裸 TODO 已删（`7f51ac7`） |
| [ ] | 其余 27 | — | 见 `research/todo-audit.md` §6（多为设计备忘/说明，保持原样即为正确处置） |

**E 类小结：已处置 7 / 34，其余 27 条的正确处置就是「保持原样」**（它们是说明/取舍记录，
删掉会丢信息）。唯一可考虑的是把其中 6 条「疑问式备忘」改写成结论。

---

## 总体进度

| 类 | 总数 | 已处置 | 未做 | 说明 |
|---|---|---|---|---|
| A | 55 | 31 | 24 | 未做集中在 impl 腿（G5）24 条 |
| B | 4 | 4 | 0 | 完成 |
| C | 10 | 10（裁定） | 0 | 代码未动是正确处置；可改写为 NOTE |
| D | 85 | 3（部分判定） | 82 | 按主题分组推进 |
| E | 34 | 7 | 27 | 其余保持原样即正确 |
| **合计** | **188** | **55** | **133** | |

**下一步（按优先级）**：

1. **G5**（A 类剩余里唯一成建制的分组，18 条）：`#93`/`#94`/`#96`/`#101`/`#102`/`#108`/`#109`/`#110`/`#113`/`#115`/`#116`/`#122`/`#124`/`#125`/`#127`/`#128`/`#129`/`#133`。
   先按 B3 调研修正分类（`#94` 应为 B 类）。
2. A 类剩余 6 条（`#14`/`#86`/`#152`/`#160`/`#162`/`#163`/`#164`）逐条裁定。
3. 把 C 类 10 条与 E/D 类中已裁定「不做/不适用」的条目注释由 `TODO` 改写为 `NOTE`。
4. `#55`/`#86` 已完成裁定（`#55` 在 G4；`#86` 待定）。
