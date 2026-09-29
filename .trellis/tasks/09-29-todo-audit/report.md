# 收尾报告：TODO 全量审计 + B 类清除 + 裸 TODO 处理 + 虚函数必要性调研

日期：2026-09-29
任务：`.trellis/tasks/09-29-todo-audit`
基线：`HEAD = f6e62c5`

---

## 1. 目标

1. **调研**：清点本仓全部 `TODO`（用户量级「大致 188 处」），分类。
2. **实施**：清除 B 类（已过时），处理有明确意图的裸 TODO。
3. **专项调研**：查 Godot 类型虚函数（`jsb_script_language.h` / `jsb_script.h` 那批）在 Godot 侧的
   调用点与真实功能，参考 GDScript 与 C# 的实现，判定对本项目是否有必要。

---

## 2. 动作 + 证据

### 2.1 清点

```bash
git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc   # → 188 行
```

### 2.2 分类（含调研后的重判）

| 类 | 含义 | 条数 |
|---|---|---|
| **A** | 可简单实现 / 待确认后实现 | 55 |
| **B** | 已过时 | 4 |
| **C** | 不可实现 / 受上游限制 | 10 |
| **D** | 需较大改动 | 85 |
| **E** | 非行动项 / **经调研判定不适用** | 34 |
| | 合计 | 188 |

横切事实：**122/188 文本与上游 GodotJS 相同**；`git blame` 显示 **128 条来自 2026-08-05
「批量代码格式化」**一次提交。

### 2.3 虚函数专项调研（新增交付物）

`research/virtuals-godot-side.md` — 逐条查 Godot 侧调用点与 GDScript/C# 实现。核心结论：

| 虚函数 | Godot 侧调用方 | GDScript | C# | 判定 |
|---|---|---|---|---|
| `_add_global_constant` | autoload（`main.cpp:4490/4538`） | 灌 `globals` **给解析器做常量折叠** | 空实现（`/* TODO */`） | **不适用**——JS 无裸标识符解析；本仓无 autoload 集成（`git grep autoload src` 为空） |
| `_add_named_global_constant` | autoload（`editor_autoload_settings.cpp:396/599`） | 灌 `named_globals`（运行期标识符查找） | **不覆写**（基类空） | **不适用**，同上 |
| `_remove_named_global_constant` | autoload 删除（`:567`） | 擦 `named_globals` | **不覆写** | **不适用**，同上 |
| `_get_public_functions/constants/annotations` | `doc_tools.cpp:1107/1139/1152`（生成 `@<语言>` 文档页） | 真实现（还供解析器用） | 空 + `TODO?` | **不值得**——需新增并维护语言内建清单；我方 `@bind` 在运行时 bundle 里，不满足该同步接口 |
| `_find_function` | **活**：`connections_dialog.cpp:1016/1024`、`script_text_editor.cpp:428` | 真解析 | 不覆写（-1） | **该做**——恒 -1 会使「连接信号到脚本已有方法」时编辑器**多写空函数**（`:1017-1034`） |
| `_auto_indent_code` | **无**：`ScriptLanguageExtension::auto_indent_code` 是 `static`（`script_language_extension.h:573`），`EditorAdapter::format_code` 值调用它、**不虚分派** | 走自己的 `EditorLanguage::format_code` | 无 | **先判定**——extension 路径下是死代码；真要做得实现 `EditorLanguage::format_code`（`script_text_editor.cpp:1789/1801`） |
| `_validate_path` | `script_create_dialog.cpp:301`（新建脚本对话框） | 有实现 | 有实现 | **看是否需要「新建 TS 脚本」流程** |
| `_get_member_line`（`Script` 上） | `ScriptEditor::script_goto_method`（`script_editor_plugin.cpp:3899`）← 信号连接跳转、动画方法轨双击 | 查 `member_lines` | `TODO omnisharp` + -1 | **该做**——恒 -1 使「跳转到方法定义」静默失效 |

**关键发现**：`_find_function`（活）与 `_auto_indent_code`（被 static 遮盖，死）**不对称**——
那批空实现不能一刀切填实，必须按**是否真被调用**分别处理。

### 2.4 本轮改动（6 文件）

- **B 类 4 条**：`#73` 删已实现的 `require.cache`；`#158` 删已实现的 `_notification`；
  `#46` 删误导性 `//TODO remove this`；`#100` 改写 jsc「NOT SUPPORTED TO BUILD」过时警告。
- **裸 TODO 2 条**：`#58`、`#119`（无意图可补，删除）。
- **#16 补全说明**：`_supports_platform` 的 TODO **未删除**，写入实现要点（平台名取 `get_os_name()`
  小写、映射 `bin/<platform>/` 产物、缺库返回 false；参考 `[libraries]` 键与 `leg_library_key()`）。

保留不动：**#55**（先确认 `_rebind` 是否仍需要）、**#86**（考虑 `IsFunction` 转 `godot::Callable`）。

**中途自纠**：#119 删除时一度连带删掉活方法 `_throw_trivial`（`monolith.ts` 内 13 处调用），已恢复。

### 2.5 验证（重编后实测）

dll md5 `ec757e9672fa838abd551f1574416e3b`，两处部署位一致。

| 项 | 结果 |
|---|---|
| `--headless --path ./project` | rc=0；`COMPLETED=1`；`FAILED=0`；`Orphan StringName=0`；`failed to check out module`=0 |
| `--jsb-run-tests` | **73/73 cases、1094/1094 assertions**，rc=0 |
| `TODO` 计数 | 188 → **182** |

---

## 3. 产出

| 文件 | 内容 |
|---|---|
| `research/todo-audit.md`（61 KB） | 分类统计 → 横切事实 → A 类分组 → B/C/E 清单 → §7 已清除/已补全记录 → **§8 全量逐条表 188/188** → §9 推进顺序 |
| `research/virtuals-godot-side.md`（15 KB） | Godot 侧虚函数调用点与 GDScript/C# 对照调研 |

逐条表核对：188 行、编号无缺无重、类别 55+4+10+85+34 = 188。

---

## 4. 遗留

1. **未 commit / 未 push**（未授权）。工作树：`M` 6 源文件 + `?? .trellis/tasks/09-29-todo-audit/`。
2. 剩 **182** 条。按 §2.3 的结论，`jsb_script_language.h` 那批应分类处理：
   该做 3 条（`_find_function`、`_validate_path`、`_get_member_line`）、改注释 3 条（autoload 系列）、
   先判定 1 条（`_auto_indent_code`）、不值得 3 条（`_get_public_*`）。
3. **#55 / #86 的判定**待做（方向已由用户给出）。
4. **上一轮遗留**：`678bc92` 扫入的 6 处 `//TODO` 删除、其 commit message 措辞问题——涉及改写已推送历史。
5. **过程失误（已纠正）**：审计切片曾派给只读的 `scout` 子代理（无法写文件），全部未落盘；
   我随后自行完成全部 188 条的读取与判定。
