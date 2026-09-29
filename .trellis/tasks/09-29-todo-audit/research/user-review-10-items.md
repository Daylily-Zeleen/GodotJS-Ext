# 修正记录：用户复核 10 条（2026-09-29）

上一轮我把「TODO 改成说明文字」当成完成，用户逐条驳斥。这里记录每条的复核结论、
我改了什么、没改的为什么没改。

## 逐条复核

### Q1 `jsb_declaration_matcher_` 的位置 + 「以下三个」的第三个在哪 —— **用户对**

两个事实：

1. **位置错**：新增的 `js_declaration_matcher_` 与 `find_identifier_line()` 的消费者
   （`_find_function`、`_validate_path`、`GodotJSScript::_get_member_line`）**全在 `#if JSB_TOOLS` 内**，
   而我把声明放在了 `JSB_TOOLS` 外面（头文件 :145-153 原来是裸的）。已把它们连同
   `_init`/`_finish` 里的 create/unref 一起移进 `#if JSB_TOOLS`。
   顺带确认：`jsb_script_language.cpp` 里 `locate_identifier_line` 现在也在 `#if JSB_TOOLS` 内。

2. **「以下三个不适用」的第三个是 `_add_global_constant`**，它在头文件 :293，
   与 :250/:251 的两个**不相邻**——因为 `#if JSB_TOOLS` 区段在 :274 结束，而
   `_add_global_constant` 声明在区段之后。原措辞「以下三个」因此是错的（读者看不到第三个）。
   已改成「以下两个」并注明第三个的位置与原因。

   **对照引擎**：`core/object/script_language_extension.h:572` 起，三个 autoload 钩子
   （`:580/581/582`）**也不在 `TOOLS_ENABLED` 内**——所以「`_add_global_constant` 落在
   `JSB_TOOLS` 外」与引擎的分区一致，不是我们头文件的分区错误；
   错的是我的措辞，不是声明位置。这一条保留原分区，只修措辞。

### Q2 `jsb_repl.cpp` 构造里的 TODO —— **用户对**

我把 `//TODO` 换成了「这是一段说明且本身写着『待设计的功能』」的文字，等于**用散文掩盖了待办**。
已恢复为 `//TODO 列出所有 realm 实例并与之交互：…`，并把实现所需的三个前提
（枚举入口 / 选择控件 / 跨 realm 求值通道）写在 TODO 正文里。

### Q3 `jsb_export_plugin.cpp` #15 —— **用户对**

我因为「本机跑不了导出验证」就把 TODO 删成说明，这是拿验证难度当删除理由。已恢复为
`//TODO`，并写明补法是「对 `.js` 也调用 `export_compiled_script`」，同时保留真正需要先确认的点
（`export_raw_file` 的 `exported_paths_` 只按路径去重、不区分身份，可能重复打包）。

### Q4 `verify_file` 的 d.ts 注释 —— **用户对**

我写的是「现状没有分工，所以那条被注掉的代码不能启用」——但 TODO 的本意正是**要建立这个分工**。
已恢复为 `//TODO d.ts 应当只在 GENERATE 阶段产出、不在 INSTALL 阶段安装`，并写明阻塞点：
现在「api 生成的 d.ts」与「preset 分发的 d.ts」共用 `CH_D_TS` 一个 hint，得先把两类分开。

### Q5 `Vector<String>` → `PackedStringArray` 是否该反过来 —— **用户对，已回退**

评估结论（**评估依据**）：

- GDExtension 下 `PackedStringArray` 的逐元素 API 每个都是一次函数指针调用：
  `push_back` / `operator[]` / `size` / `is_empty` 全部走
  `third/godot-cpp/gen/src/variant/packed_string_array.cpp` 的
  `_call_builtin_method_ptr_*`；只有 `ptr()/ptrw()` 是**一次**调用取整块
  （`third/godot-cpp/src/variant/packed_arrays.cpp`，直接 `packed_string_array_operator_index`）。
- `Vector<String>` 是纯 C++ CowData，零跨语言调用。
- 这批函数的形态是「递归遍历 EFS 逐条 `push_back`，再逐条过滤 / 逐条取 md5」，
  即**逐元素**访问 ⇒ 改成 `PackedStringArray` 会为每个路径付一次函数指针调用，**更慢**。
- 唯一的跨语言边界是 `resources_reimported` 信号回调
  （`jsb_editor_plugin.cpp:227` → `_generate_imported_resource_dts(const PackedStringArray&)`）：
  引擎给的就是 `PackedStringArray`，进我们这边本来就要拷一次进 `Vector<String>`——
  现在只拷这一次，没有额外开销。

→ **回退**：`generate_scene_nodes_types` / `generate_resource_types` / `_filter_resource_paths` /
`_cache_files_md5` / `get_all_scenes` / `get_all_resources` / 两个 codegen generator /
`generate_all_types` 全部改回 `Vector<String>`。原 TODO 的「改为 PackedStringArray」建议
是错的，结论以注释形式写在 `jsb_editor_plugin.h:109-116`。

### Q6 `jsb_shadow_realm.cpp` 为何不用 `memnew_placement`、为何 new 两次 —— **用户对**

两处都是我的问题：

1. 我手写了 `new (args + i) LocalValue(...)`，而**本仓统一用 `memnew_placement`**
   （`jsb_environment.cpp:1661`、`jsb_amd_module_loader.cpp:51`、`jsb_timer_action.cpp:51`、
   以及 static_binding 各处）。已改为 `memnew_placement(&args[i], LocalValue(...))`。
2. **多余的默认构造 + 赋值**：我原先写 `new (args+i) LocalValue();` 然后 `new (args+i) LocalValue(wrapped)`，
   等于构造两次。已改为一次 `memnew_placement` 直接用包装结果构造。

   （注：`godot-cpp` 的 `memnew_placement` 来自 `include/godot_cpp/core/memory.hpp`；
   父仓 `git grep` 搜不到 `third/godot-cpp`，因为它是不被父仓索引的 submodule，
   前面几轮我因此误判过几处「不存在」。）

### Q7 `InternalModuleLoader::load` —— **用户对**

「没有调用点」只能说明**目前不实现**，不能说明**不该挂 TODO**：它自身功能就是不完整的
（`file_name_` 存了从不读）。已恢复 `//TODO 从 preset 里按 file_name_ 求值源码`，
并把「无注册点」作为现状写进 TODO 正文，而不是当作删除理由。

### Q8 `jsb_environment.h:179` 的 friend —— **用户对**

我写的「属 API 面改动，本轮不动」等于给未来留借口。已恢复为
`//TODO 收紧这个 friend`，并写出可行做法：给 `Environment` 加一个语义明确的公开谓词
（现有 `is_shadow()` 表「本环境即影子环境」，与「以影子模式创建实例」不是同一件事）。

### Q9 `StatelessScriptClassInfo::is_valid` —— **用户对**

我原来的注释把「恒 true」说成设计如此，方向反了：这个判据本来就该回答
**「这个脚本类还能不能用」**。已改为：

```cpp
_FORCE_INLINE_ bool is_valid() const { return !module_id.is_empty(); }
```

依据：`module_id` 由 `ScriptClassInfo::_parse_script_class` 在解析成功时写入
（`jsb_class_info.cpp:884`），而模块加载失败时调用方把整个 `script_class_info_` 重置为 `{}`
（`jsb_script.cpp:913`），此时 `module_id` 为空。该判据与脚本侧的
`GodotJSScript::is_valid_internal()` 一致（`jsb_script.h:292` → `VariantUtil::is_valid_name`，
实现即 `!p_name.is_empty()`，`jsb_variant_util.h:189`），避免两侧结论相反。

**未实现的部分（明确留作 TODO）**：用户提到的「文件本身是否还存在」这一层判断没有做——
`StatelessScriptClassInfo` 刻意不依赖环境/文件系统（见其类注释），做文件存在性检查需要
决定「谁负责在模块被卸载时把它标失效」。当前只覆盖了「模块已不可用」这一层。

### Q10 进度与确认项写在哪 —— **用户对，我写错位置了**

AGENTS.md 的规定是：**有任务时写任务目录**（`.trellis/tasks/<任务>/report.md`），无任务时才写
`.agent_tmp/progress.md`。我这几轮一直往 `.agent_tmp/progress.md` 追加，任务目录里的
`report.md` 停在 9 小时前。

已修正：

- `report.md` 现在包含：目标 → 动作+证据 → 分组提交一览 → 每条「确认项」的结论与依据
  → 遗留。**确认项的说明集中在 `report.md` 的「判定与确认记录」一节**，本文件是它的
  逐条溯源（含用户复核意见）。
- 本文件 `research/user-review-10-items.md` 保存了上面这 10 条的完整复核过程。
- 后续进度继续写 `report.md`，不再写 `.agent_tmp/progress.md`。

## 本轮验证

- 重编：dll md5 `a86c94c59fb788c476f3f64a758a2fc5`，`bin/windows/` 与
  `project/addons/.../bin/windows/` 两处一致
- doctest：76/76 cases、1127/1127 assertions、rc=0
- 项目跑测：rc=0、COMPLETED=1、FAILED=0、Orphan StringName=0、failed to check out module=0
- `TODO` 计数：147 → 152（恢复被误删的 5 条 TODO；净 +5）
