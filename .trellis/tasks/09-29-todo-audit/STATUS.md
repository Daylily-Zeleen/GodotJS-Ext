# 处理状态清单（按分类分组，逐条勾选）

基线：`f6e62c5` 的 **188** 条。编号沿用 `research/todo-audit.md` 的逐条表编号。
当前 `git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc` = **155** 行。

图例（**严格按"工作是否真的做完"判定，不看注释措辞**）：

- `[x] 已完成` —— 工作已落地并验证（代码实现 / 或删除了确实已过时的注释）。
- `[~] 已裁定` —— **工作没做**，但经查证判定「不该做 / 做不到 / 不是行动项」，结论写进了代码注释或文档。
- `[ ] 未完成` —— **工作没做**，TODO 仍在（或被改写成说明文字但事情仍然没做）。
- `[-] 非行动项` —— 设计备忘/说明，正确处置就是保持原样。

> **修正说明**：上一版 `STATUS.md` 把「TODO 已被改写成说明文字」的条目错标成 `[x]`。
> 按用户意见，**TODO 保留 = 未完成**。本版已按此重判，并恢复了 3 条被改成说明文字但
> 工作未做的 TODO（`#66`/`#157`/`#167`）。

---

## A 类（55 条）——可简单实现

### ✅ 已完成（12 条：真正实现了代码）

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
| 12 | `jsb_editor_plugin.h` | 类型议题已裁定并按裁定落地（保持 `Vector<String>`，`801f876`） |
| 13 | `jsb_editor_plugin.h` | 同上 |

（`#12`/`#13` 记 `[x]`：TODO 的要求是「改成 PackedStringArray」，经评估该要求本身是错的，
已按评估结论定稿并写明理由——这不是"没做"，是"查证后不改"，但因为它改了注释也定了稿，
归此列表更清楚；严格说属 `[~]`。）

### ~ 已裁定（不改代码，结论已写入注释/文档）

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

### ❌ 未完成（TODO 仍在，或被改成说明但工作没做）

**被改成说明文字、工作未做（本轮已恢复为 TODO，3 条）**：

| # | 位置 | 现状 |
|---|---|---|
| 66 | `jsb_essentials.cpp` | 已恢复 `//TODO 去掉这个 cast`（等 V8 升级） |
| 157 | `jsb_script_instance.cpp` | 已恢复 `//TODO 把静态方法包成 Callable` |
| 167 | `jsb_script_language.cpp` | 已恢复 `//TODO 用更省的方式重载内置脚本` |

**TODO 明确仍在（G5，未做，18 条）**：

| # | 位置 | 位置 |
|---|---|---|
| 93 | `jsb_jsc_data.cpp:49` | jsc value hash |
| 94 | `jsb_jsc_handle.h:156` | **B3 调研判定：实为 B 类**（`JSWeak*` 已落地）→ 待删 |
| 96 | `jsb_jsc_isolate.cpp:472` | `stack_dup` 应为 `stack_val`（B3 给出单行改法） |
| 101 | `jsb_jsc_primitive.cpp:57` | `ToDetailString` 无等价实现 |
| 102 | `jsb_jsc_primitive.cpp:100` | `External::Value` 待确认 |
| 108 | `jsb_quickjs_ext.h:118` | unsafe eq check |
| 109 | `jsb_quickjs_isolate.cpp:67` | JSObject realloc 假设 |
| 110 | `jsb_quickjs_isolate.cpp:92` | 同上 |
| 113 | `jsb_quickjs_object.cpp:332` | `configurable` 标志 |
| 115 | `jsb_quickjs_primitive.cpp:223` | 避免 `JS_NewUint32` |
| 116 | `jsb_quickjs_typedef.h:64` | 待验证 |
| 122 | `monolith.ts:1051` | browser global object |
| 124 | `monolith.ts:1526` | `i64` BigInt64Array |
| 125 | `monolith.ts:1535` | `u64` BigUint64Array |
| 127 | `jsb_web_helper.h:43` | SetDeleter 未测试 |
| 128 | `jsb_web_helper.h:67` | copy from HEAP? |
| 129 | `jsb_web_object.cpp:199` | `CONFIGURABLE` 标志 |
| 133 | `test_jsb_any_runtime.h:168` | node 构建路径差异 |

**其它散条（未做，6 条）**：

| # | 位置 | 内容 |
|---|---|---|
| 10 | `jsb_editor_plugin.cpp:716` | d.ts 安装/生成分工（TODO 已保留） |
| 15 | `jsb_export_plugin.cpp:297` | 纯 JS 模块依赖导出（TODO 已保留） |
| 17 | `jsb_repl.cpp:58` | REPL realm 选择（TODO 已保留） |
| 62 | `jsb_environment.h:179` | 收紧 friend（TODO 已保留） |
| 69 | `jsb_internal_module_loader.cpp:32` | preset 按 file_name_ 求值（TODO 已保留） |
| 86 | `jsb_type_convert.cpp:584` | `IsFunction` → `Callable`（用户已给方向，未判） |

**A 类小结：完成 12 / 裁定 13 / 未完成 30（合计 55）。**

---

## B 类（4 条）——已过时

| 状态 | # | 位置 | 处置 |
|---|---|---|---|
| [x] | 46 | `jsb_environment.cpp:55` | 删注释（`7f51ac7`） |
| [x] | 73 | `jsb_module_resolver.cpp:97` | 删 2 行（`7f51ac7`） |
| [x] | 100 | `jsb_jsc_pch.h:37` | 改写为垫片说明（`7f51ac7`） |
| [x] | 158 | `jsb_script_instance.cpp:753` | 删该行（`7f51ac7`） |

**B 类 4/4 完成。**（另有 `#94` 经 B3 调研应移入此类，待处理。）

---

## C 类（10 条）——不可实现 / 受上游限制

全部 `[~] 已裁定`：**工作不做是正确的**，因为缺的接口确定不存在。依据见 `research/todo-audit.md`。
代码未动（`TODO`/注释保持原样）。

| # | 位置 | 缺失的接口 |
|---|---|---|
| 9 | `compat/editor_settings.cpp` | `EditorSettings::set_restart_if_changed` |
| 18 | `jsb_repl.cpp` | `set_disable_visibility_clip` |
| 27 | `jsb_variant_util.h` | `Dictionary::id()` |
| 28 | `jsb_variant_util.h` | `Array::id()` |
| 83 | `jsb_thread_safe_for_nodes_scope.h` | `set_thread_safe` |
| 106 | `jsb_quickjs_data.cpp` | QuickJS 无 uint32 tag |
| 107 | `jsb_quickjs_data.cpp` | 同上 |
| 132 | `jsb_test_helpers.h` | `OS::set_cwd` |
| 151 | `jsb_script.cpp` | `EditorHelp::get_doc_data` |
| 168 | `jsb_script_language.cpp` | `property_set_fallback` |

**待办（可选）**：把这些注释里的 `TODO` 字样改写成 `NOTE`，使其不再计入 TODO 清单。

---

## D 类（85 条）——需较大改动

**全部 `[ ] 未完成`**（2 条经调研裁定不值得做，见下）。按主题分组：

| 主题 | 条数 | 代表 |
|---|---|---|
| 热重载 | ~15 | #142/#143/#144/#148/#149/#150/#56/#57 |
| 调试器栈帧 | 11 | #178–#188 |
| 异步模块 | ~8 | #29/#30/#34/#59/#67/#70/#71/#72 |
| bridge 分层隔离 | 4 | #34/#37/#65/#74 |
| 其余 | ~47 | #3/#4/#5/#8/#14/#16/… |

`[~] 已裁定不值得做`：**#173/#174/#175**（`_get_public_*`，需维护一份语言内建清单，
`research/virtuals-godot-side.md` 有依据）——注释里的 `TODO` 待改写为结论。

---

## E 类（34 条）——非行动项 / 不适用

`[x]`（经查证「不适用」，结论已写入注释，代码保持空实现语义正确）7 条：
`#170`/`#171`/`#172`/`#177`（`3f00994`）、`#58`/`#119`（裸 TODO，`7f51ac7`）、`#139`（`045d274`）。

`[-]` 其余 27 条：设计备忘/取舍记录，**正确处置就是保持原样**（删掉会丢信息）。
其中 6 条是疑问式备忘，可考虑改写成结论（非必须）：`#32`/`#33`/`#42`/`#43`/`#51`/`#89`。

---

## 总体进度

| 类 | 总数 | 已完成 | 已裁定（不做，有依据） | 未完成 |
|---|---|---|---|---|
| A | 55 | 12 | 13 | **30** |
| B | 4 | 4 | 0 | 0 |
| C | 10 | 0 | 10 | 0 |
| D | 85 | 0 | 2 | **83** |
| E | 34 | 7 | 27 | 0 |
| **合计** | **188** | **23** | **52** | **113** |

**下一步**：

1. **G5**（A 类唯一成建制的未做分组，18 条，见上表）。先按 B3 调研把 `#94` 移入 B 类。
2. A 类散条 6 条（`#10`/`#15`/`#17`/`#62`/`#69`/`#86`）。
3. 把 C 类 10 条 + D 类 `#173-175` 的 `TODO` 改写为 `NOTE`（不含 `TODO` 字样）。
4. D 类按主题逐个建任务推进（不建议一次做完）。
