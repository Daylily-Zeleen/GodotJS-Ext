# PRD：用常驻工具进程解析全局类元数据

## 背景

`GodotJSScriptLanguage::_get_global_class_name`（`src/runtime/weaver/jsb_script_language.cpp:521`）
供编辑器的**全局类缓存**调用（`EditorFileSystem` 后台扫描线程、`scene_tree_dock`、
`script_create_dialog`）。它返回 `class / base_type / icon_path / is_tool / is_abstract` 五个字段，
现在用**正则**从源码提取：

- `icon_path` 恒空（`:541` 的 TODO）——全局类在编辑器里永远默认图标；
  `@bind.icon("res://...")` 已存在（`godot.annotations.ts:720`）但只在加载后生效，
  进不了这条后台扫描路径。
- `is_abstract` 恒 false（`:543` 的 TODO）——抽象类不被标记。
- 正则只能匹配受限形态（单行 `export default class X extends Y`，或两步拆解），
  跨行写法、字符串/注释里的假命中都处理不了（函数头注释已声明只支持规定写法）。

仓库已有成熟的 TS Compiler API 设施：`EditorToolClient`（常驻 Node 工具进程，
NDJSON over stdio，阻塞请求，上限 5s，见 `src/editor/weaver-editor/jsb_editor_tool.h`），
现服务于 `signatures` 与 `doc` 两个 op。

## 目标（用户已拍板的方向）

**用常驻工具进程同步解析**：新增 `global-class` op，扫描线程对每个脚本文件发一次
同步请求（常驻进程、TS 已加载，单文件毫秒级），拿到五个字段。**不做缓存**，
当独立解析功能对待。**正则实现删除**（用户裁决：不留第二套语义）。

## 已核实的关键约束

| 约束 | 事实 | 出处 |
|---|---|---|
| 调用线程 | `EditorFileSystem` **后台扫描线程**（`thread.start(_thread_func)`） | 引擎 `editor_file_system.cpp:1144` → `_process_file_system`(1309/1339/1361/1545) → `_get_global_script_class`(2108) |
| 启动顺序 | 首次扫描**晚于** `init_plugins()`（plugin 的 NOTIFICATION_READY + deferred 之后） | `jsb_editor_plugin.cpp:265` 注释 |
| 分层 | runtime **零处** include editor 头 | 全仓 grep 验证 |
| 客户端线程模型 | 现声明「只有编辑器主线程触达」；id 分配在锁内、应答等待用 cv；**进程生命周期（teardown/spawn/is_running）在锁外** | `jsb_editor_tool.h` 头注释、`jsb_editor_tool.cpp:167-210` |
| 工具可用性 | 依赖 `res://.godot/.../jsb.editor.tools` 已安装且 `res://node_modules/typescript` 存在 | `jsb_editor_tool.cpp:72-93`、`jsb_editor_plugin.cpp` 各 `_regenerate_*` 前置检查 |
| 非编辑器构建 | `_get_global_class_name` **只被编辑器调用**：引擎侧调用点全在 `editor/`（`editor_file_system.cpp:2108`、`scene_tree_dock.cpp:4421`、`script_create_dialog.cpp:66/71`），`core/` 只有基类声明/绑定。模板构建不会调用，不需要实现体 | 引擎全仓 grep |
| 工具可用性即编辑器前提 | 工具是 `use_typescript`（**默认 True**）的一部分；编辑器本就要求 `res://node_modules/typescript`（`jsb_editor_plugin.cpp:1326` 直接报错要求 `npm i`）。「编辑器里没有 typescript」不是正常状态 | `SConstruct:34/421`、`jsb_editor_plugin.cpp:1326` |

## Requirements

- **R1 协议**：`scripts/jsb.tools/src/jsb.editor.tools.cts` 新增 op `"global-class"`，
  请求 `{"path":"res://..."}`，应答 `{ok, class?, base_type?, icon_path?, is_tool?, is_abstract?}`。
  用 `ts.createSourceFile` 解析：类名/基类（含跨行）、`abstract class`、
  `@bind.icon("res://...")`（含 `@icon` 别名）、`@tool`。语义即函数头注释声明的契约
  （`.ts`：`export default class X extends Y`；`.js`：`class X extends Y` + `exports.default = X`）。
- **R2 客户端线程安全**：`EditorToolClient::request()` 允许从非主线程调用。
  进程生命周期（teardown/spawn/is_running 检查）加锁；id 分配与应答等待维持现状。
- **R3 runtime 钩子**：`GodotJSScriptLanguage` 暴露
  `set_global_class_extractor(callable)`（`std::function<bool(const String &path, Dictionary &out)>`，
  返回 false 表示"本次请走降级"）。`_get_global_class_name` 先问钩子，失败/未注册走现有正则。
- **R4 注册**：editor plugin 在 NOTIFICATION_READY 时注册钩子，路由到 `EditorToolClient`
  （`global-class` op）。plugin 销毁时注销。
- **R5 失败语义**：钩子未注册 / 工具进程不可用 / 请求超时 / 解析失败 → 返回空 Dictionary
  （与"这个文件解析不出类名"同语义）。**不再有正则兜底**。失败必须有日志：这不是每文件常态，
  而是环境故障，静默会把"工具没起来"藏成"所有脚本都没有全局类"。
- **R6 删除正则**：`_get_global_class_name` 里的 `js_class_name_matcher1_` / `matcher2_`、
  动态构造的 base matcher 及相应成员全部删除；函数体只做「问钩子 → 组 Dictionary」。
- **R7 模板构建**：`_get_global_class_name` 只被编辑器调用（见上表），模板构建只需编译通过，
  不需要实现体；钩子未注册时返回空。

## 验收标准

- **AC1**：一个 `.ts` 文件含 `@bind.icon("res://icon.svg")` 与 `@tool`，
  `export default class Foo extends Node`（允许跨行）→ 五字段全对。
- **AC2**：`abstract class`（未导出为 default）→ `is_abstract=true`。
- **AC3**：`.js` 两步形态（`class X extends Y` + `exports.default = X`）→ 与正则结果一致。
- **AC4**：工具进程未启动/被杀 → 返回空 Dictionary 且有错误日志（不静默）。
- **AC5**：模板构建（`target=template_*`）编译通过且路径走正则。
- **AC6**：编辑器实跑：含上述脚本的工程扫描后，全局类缓存里 icon_path/is_abstract 正确
  （`ProjectSettings.get_global_class_list` 可查）。
- **AC7**：doctest 全绿；`test_jsb_script_language_queries.h` 风格的新用例覆盖 R1 的解析
  （工具侧）与 R3 的钩子语义（未注册→空、注册→用其值）。
- **AC8**：文档：`.trellis/spec/` 相应层（cpp / editor 工具链）补一条
  「runtime↔editor 经钩子解耦、扫描线程同步请求工具进程」的约定。

## 非目标

- 不改 `@bind.icon` 运行期路径。
- 不引入缓存/失效机制（用户明确：不要缓存）。
- 不做 tree-sitter。
- 不为"编辑器里没有 typescript"设计运行路径（那不是受支持的状态）。

## 已接受的代价

`@bind.icon("res://...")` 只认**字面量**实参；变量实参不再有兜底。正则本来也处理不了变量，
故非回归。
