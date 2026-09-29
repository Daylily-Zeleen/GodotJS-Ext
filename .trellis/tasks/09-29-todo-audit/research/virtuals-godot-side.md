# Godot 侧虚函数调用点与功能调研（`jsb_script_language.h` / `jsb_script.h`）

日期：2026-09-29
范围：`src/runtime/weaver/jsb_script_language.h` 与 `jsb_script.h` 中标 `//TODO` 的
`ScriptLanguageExtension` / `Script` / `ScriptExtension` 虚函数覆写，
逐个查清 Godot 侧**谁调用、什么场景调用、GDScript 与 C# 怎么实现**，以判定对本项目是否有必要。

证据来源：`D:/Dev/godot/godot`（引擎源码）+ `third/godot-cpp/gen/include`（GDExtension 绑定）。

---

## 0. 先厘清两条容易混淆的调用链

Godot 4.7 里编辑器用的**不是** `ScriptLanguageExtension` 自身的这些虚函数，而是
`ScriptLanguage::get_editor_language()` 返回的 `EditorLanguage` 对象
（`core/object/editor_language.h`，虚函数带默认空实现）。

`ScriptLanguageExtension` 的实现是 `EditorAdapter`
（`core/object/script_language_extension.h:296-321`），恰好逐个转调回我们覆写的那批 `_xxx`：

| `EditorLanguage` 方法 | `ScriptLanguageExtension` 转调到的 | 我们仓库里的覆写 |
|---|---|---|
| `validate(...)` | `script_language->validate(...)` | `_validate`（已实现） |
| `find_function(f, code)` | `script_language->find_function(...)` | **`_find_function`（空，返 -1）** |
| `format_code(code, from, to)` | `script_language->auto_indent_code(...)` | **`_auto_indent_code`（原样返回）** |
| `complete_code(...)` / `lookup_code(...)` | 同名 | 空（注释已说明「暂无计划实现编辑器内编写」） |

**注意 `find_function` 的不对称**：`ScriptLanguageExtension::auto_indent_code` 是 `static`
（`script_language_extension.h:573`），`EditorAdapter` 直接按值调；而 `find_function` 是
**普通成员**（`:469`），`EditorAdapter` 调它（`:306`）→ 它是**真实虚函数分派**，会进我们的覆写。
**所以 `_find_function` 确实是活路径，`_auto_indent_code` 在 extension 路径下不会被调用。**

---

## 1. `add_global_constant` / `add_named_global_constant` / `remove_named_global_constant`

### Godot 侧调用点（全引擎穷举）

| 调用点 | 参数 | 时机 |
|---|---|---|
| `main/main.cpp:4490` | `(info.name, Variant())` | **游戏启动**，autoload 第一遍：先占名字，让脚本加载前就能引用 |
| `main/main.cpp:4538` | `(info.name, n)` | **游戏启动**，autoload 第二遍：把实例化好的 Node 塞进去 |
| `editor/settings/editor_autoload_settings.cpp:396 / 599` | `(info.name, info.node)` | **编辑器**：autoload 列表初始化/新增（`add_named_global_constant`） |
| `editor/settings/editor_autoload_settings.cpp:877` | `(info.name, Variant())` | **编辑器**：先占名字，早于脚本解析 |
| `editor/settings/editor_autoload_settings.cpp:567` | `(name)` | **编辑器**：删除 autoload → `remove_named_global_constant` |

调用**全部来自 autoload（单例）机制**，没有第二类使用者。

### GDScript 的实现

```cpp
// jsb → gdscript.cpp:2079
void GDScriptLanguage::_add_global(const StringName &p_name, const Variant &p_value) {
	// globals: StringName -> global_array 下标；global_array: 值数组（带空位回收）
}
// gdscript.cpp:2107
void GDScriptLanguage::add_global_constant(const StringName &p_variable, const Variant &p_value) {
	_add_global(p_variable, p_value);
}
// gdscript.cpp:2111
void GDScriptLanguage::add_named_global_constant(const StringName &p_name, const Variant &p_value) {
	named_globals[p_name] = p_value;
}
// gdscript.cpp:2125
void GDScriptLanguage::remove_named_global_constant(const StringName &p_name) {
	ERR_FAIL_COND(!named_globals.has(p_name));
	named_globals.erase(p_name);
}
```

区别就在**消费者**：

- `globals` / `global_array`：给**解析器/编译器**用（`_add_global` 同时被 `init()` 用来灌
  `CoreConstants`、`PI/TAU/INF/NAN`、`ClassDB` 全局类）。索引式数组，为常量折叠服务。
- `named_globals`：给**运行期标识符查找**用——
  - `gdscript_analyzer.cpp:825 / 4673 / 4710`（`has_any_global_constant` / `get_any_global_constant`）
  - `gdscript_compiler.cpp:471`、`gdscript_vm.cpp:3820/3828`（`get_named_globals_map()`）
  - `gdscript_editor.cpp:2609`（补全类型提示）

autoload 走的是 **`named_globals` 这一支**。

### C# 的实现

```cpp
// modules/mono/csharp_script.h:519
/* TODO */ void add_global_constant(const StringName &p_variable, const Variant &p_value) override {}
```

`add_named_global_constant` / `remove_named_global_constant` **C# 根本没覆写**，用的是
`ScriptLanguage` 基类的空默认（`core/object/script_language.h:279-280`）。
C# 靠 **source generator + GDScript 侧 GlobalUsings / `[Autoload]` 处理**，不经过这两个钩子。

### 我们的现状（`jsb_script_language.h:265` 等）

```cpp
virtual void _add_global_constant(const StringName &p_name, const Variant &p_value) override {} // TODO
virtual void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override {} // TODO
virtual void _remove_named_global_constant(const StringName &p_name) override {} // TODO
```

### 结论：**对我们没有必要**（判定：注释应改写为「不适用」，而不是「待实现」）

理由链：

1. 唯一语义是「让脚本里**裸标识符**（如 `MyAuto`）解析到该值」。
2. TS/JS 里没有裸标识符解析——`strict` 模式下未声明标识符直接 `ReferenceError`，
   不可能靠语言层注入；JS 侧要拿 autoload 应该 `import` 或在 `godot` 模块里查。
3. 我方**已经**有对应能力的等价物：全局对象注册（`Essentials`/`global->Set`）、
   `godot` 模块导出。
4. 本仓**没有任何 autoload 集成代码**（`git grep -n autoload -- src` 无结果），
   即连「需要注入」的触发方都不存在。
5. 上游 C# 也是空实现 / 不覆写——这是 JS/静态类型语言在 Godot 里的通行做法。

**建议**：把三条 `// TODO` 改写为「不适用（JS 无裸标识符解析；参见 C# 的空实现）」，
或直接删除注释但保留空实现（保持与基类契约一致）。

---

## 2. `_get_public_functions` / `_get_public_constants` / `_get_public_annotations`

### 调用点

唯一消费者是 `editor/doc/doc_tools.cpp:1107 / 1139 / 1152`：为**每个脚本语言**生成一个
`@<语言名>` 的文档类（`:1099` `String cname = "@" + lang->get_name();`），
把语言的 built-in 方法/常量/注解灌进引擎文档。

### GDScript 的实现

`get_public_functions` → `gdscript_editor.cpp:498`，此外还被 **GDScript 自己的解析器/分析器**
用（`gdscript_parser.cpp:1623`、`gdscript_analyzer.cpp:6198`）——为此 GDScript 单独维护了一份
「全局函数签名表」（`get_public_functions` 返回值即语言的内建函数集合）。

### C# 的实现

```cpp
/* TODO? */ void get_public_functions(List<MethodInfo> *r_functions) const override {}
/* TODO? */ void get_public_constants(List<Pair<String, Variant>> *r_constants) const override {}
/* TODO? */ void get_public_annotations(List<MethodInfo> *r_annotations) const override {}
```

三个都空，且注释是 **`TODO?`（带问号）**——上游自己也不确定要不要做。

### 我们的现状

三个都返回空（`jsb_script_language.h:223-225`，注释写 `// TODO: Vector<StackInfo>`——**该措辞已过时**，
Godot 4.7 的 `ScriptLanguageExtension` 对应虚函数签名就是 `TypedArray<Dictionary>`，
底层转成 `List<MethodInfo>`）。

### 结论：**不是「填实空实现」那么简单**（判定：D 类，且当前不必做）

- 价值仅在「编辑器帮助里出现 `@GodotJSScript` 页」。
- 前提是先有一份**可枚举的语言内建表**。我方内建 JS API（`godot` 模块、`@bind` 注解等）
  是脚本侧实现，不是 C++ 侧的一张表；`@bind` 那套在 `scripts/jsb.runtime/src/godot.annotations.ts`
  里，属于运行时 bundle，不满足 `get_public_functions` 的同步 C++ 接口。
- 真正要做需要**新增一张语言内建清单并保持双向同步**——远超「填一个函数」。
- C# 至今不做，也说明其优先级低。

---

## 3. `_find_function`（**活路径**，与其它不同）

### 调用点

```
editor/scene/connections_dialog.cpp:1016 / 1024   信号连接对话框：判断方法在脚本里是否存在（含基类链）
editor/script/script_text_editor.cpp:428          add_callback：定位函数插入位置
```

### GDScript 实现

`modules/gdscript/gdscript_editor.cpp:258` —— 走真解析。

### C# 实现

`CSharpScript` **没有** `find_function` 覆写；`CSharpLanguage` 的 `EditorLanguage` 走
`EditorLanguage` 基类默认空实现（返回 -1），语义是「找不到」。

### 我们的现状（`jsb_script_language.h:213`）

```cpp
virtual int32_t _find_function(const String &p_function, const String &p_code) const override { return -1; } // TODO
```

返回 -1 的**后果**（读 `connections_dialog.cpp:1017-1034`）：

- `line == -1` → 认为方法不在本脚本 → 继续沿基类脚本找 → 都没有则
  `add_script_function_request = !found_inherited_function`（**true**）→
  编辑器会**主动把该函数补写进脚本**。

也就是说 `-1` 不会崩，但会让「连接信号到脚本里已有的方法」时产生**多写一个空函数**的行为。

### 结论：**建议实现**，且成本可控

`jsb_script_language.cpp:429` 的 `_get_global_class_name` 已经在用正则从源码提取类名/基类
（`js_class_name_matcher1_` 等），**同一套设施**可以做一个「按函数名在源码里定位」的正则
（TS 方法定义 `name(...)`、类字段 `name = (...) =>`；JS 同理）。判定为 A 类。

---

## 4. `_auto_indent_code`（**在 extension 路径下是死代码**）

### 事实

- 唯一调用方是 `EditorAdapter::format_code`（`script_language_extension.h:310-312`）。
- 但 `ScriptLanguageExtension::auto_indent_code` 是 **`static`**（`:573`），
  `EditorAdapter` 直接值调用该静态函数，**不经过虚分派**，所以进不了我们的覆写。
- `_auto_indent_code` 的 GDVIRTUAL 仅在 **GDScript 语言**（`ScriptLanguageExtension` 的脚本实现）里才有意义。
- `format_code` 的真实用户是编辑器菜单「自动缩进」：`script_text_editor.cpp:1789 / 1801`。

### GDScript / C#

- GDScript：`GDScriptEditorLanguage::format_code`（`gdscript_editor.cpp:3910`），**不经过**
  `auto_indent_code`，走自己的 `EditorLanguage` 实现。
- C#：无 `format_code` / `auto_indent_code`，用 `EditorLanguage` 基类空实现。

### 结论：**不是「可以简单实现」，而是应先判定它是否会被调用**

在 `ScriptLanguageExtension` 这条路上，`_auto_indent_code` 永远不会被调用（static 遮盖）。
若判为「不需要」，应是**删注释 + 保留空实现**（或加一句说明为何不实现）；
若判为「需要」，要做的是 `EditorLanguage` 侧的 `format_code`，而**不是**这个 static 函数。
判定：先确认（B/E 类），不是 A 类。

---

## 5. `_validate_path`

### 调用点

唯一：`editor/script/script_create_dialog.cpp:301`，
`ScriptServer::get_language(language_menu->get_selected())->validate_path(p)` —— 新建脚本时校验文件名。

### GDScript / C#

- GDScript：`gdscript_editor.cpp` 有实现（检查扩展名/标识符合法性）。
- C#：`csharp_script.h:514` `String validate_path(const String &p_path) const override;` 有实现。

### 我们的现状

```cpp
virtual String _validate_path(const String &p_path) const override { return ""; } // TODO: 返回指定路径文件的错误信息（脚本创建对话框处使用）
```

返回空串 = 不报错。判定：A 类（局部可做），但要先确认「新建 TS 脚本」这个流程在本项目里是否被使用
（`script_create_dialog` 是「New Script」对话框；本项目主要用法是在外部编辑 TS 后编译）。

---

## 6. `get_member_line`（`jsb_script.h:215`，**注意：这是 `Script` 上的，不是语言上的**）

### 调用链

```
editor/script/script_editor_plugin.cpp:3899  ScriptEditor::script_goto_method(script, method)
   └─ p_script->get_member_line(p_method)        → Script::get_member_line（我们的覆写）
调用者：editor/scene/connections_dialog.cpp:1294（连接信号后跳到方法定义）
        editor/animation/animation_track_editor.cpp:3475（方法轨双击跳转）
        script_editor_plugin.cpp:2683（脚本编辑器 "go_to_method" 信号）
```

### GDScript / C#

- GDScript：`gdscript.h:317`，查 `member_lines`；**该表在 TOOLS 构建下解析源码时填充**。
- C#：`csharp_script.cpp:2749`——`// TODO omnisharp` + `return -1`（**上游也没做**）。

### 我们的现状

`jsb_script.h:215` 恒返回 -1 → 「跳转到方法定义」**静默失效**（`line == -1` 直接 return false）。

### 结论：A 类，但需先有「成员行号」数据

我方已用正则解析源码（同 `_get_global_class_name`），可以顺带记录成员的声明行号并缓存，
供 `_get_member_line` 查。属局部实现。

---

## 7. 汇总判定（供 TODO 注释改写）

| TODO 位置 | Godot 侧调用方 | GDScript | C# | 我们是否有必要 |
|---|---|---|---|---|
| `jsb_script_language.h:265` `_add_global_constant` | autoload（游戏启动 + 编辑器） | 灌 `globals` 数组 | **空实现（TODO）** | **不必要**——JS 无裸标识符解析，且本仓无 autoload 集成 |
| `:220` `_add_named_global_constant` | autoload（同上） | 灌 `named_globals` | **不覆写**（基类空） | **不必要**，同上 |
| `:221` `_remove_named_global_constant` | autoload 删除（编辑器） | 擦 `named_globals` | **不覆写** | **不必要**，同上 |
| `:223-225` `_get_public_*` | `doc_tools.cpp`（生成 `@<语言>` 文档页） | 真实现（还供解析器用） | **空实现（`TODO?`）** | **不值得**——需要新增并维护一份语言内建清单 |
| `:213` `_find_function` | 信号连接对话框、`add_callback` | 真解析 | 不覆写（-1） | **有必要**（A 类）——正则可做，且缺失会导致编辑器多写空函数 |
| `:219` `_auto_indent_code` | **无**（被 static 遮盖） | 走 `EditorLanguage::format_code` | 无 | **先判定**——extension 路径下是死代码 |
| `:236` `_validate_path` | 新建脚本文档对话框 | 有实现 | 有实现 | **看是否需要「新建 TS 脚本」流程** |
| `jsb_script.h:215` `_get_member_line` | 「跳转到方法定义」 | 查 `member_lines` | `TODO omnisharp` + -1 | **有必要**（A 类）——需先记录成员行号 |

### 另外一条重要发现（§0）

`_find_function` 与 `_auto_indent_code` **不对称**：前者是真实虚分派（活），后者被 `static` 遮盖（死）。
所以「把这一批空实现逐个填实」的想法本身需要修正——应按**实际是否被调用**分别处理。
