# Resource loader dependencies for JS/TS modules

## Goal

实现 `ResourceFormatLoaderGodotJSScript::_get_dependencies`，让编辑器依赖图与 `ResourceLoader.get_dependencies()` 不再把 `.ts` / `.js` 脚本报成「零依赖」。

## 背景

- 现状：`src/runtime/weaver/jsb_resource_loader.cpp:123-126` 恒 `return {};`（原 `//TODO`）。
- 引擎侧入口是 `ResourceLoader::get_dependencies`，消费者：

| 消费者 | 位置 | 用途 |
|---|---|---|
| 编辑器文件系统 | `editor/file_system/editor_file_system.cpp:2094`（填 `FileInfo::deps`，`FileInfo` 于 `:996`/`:1362`/`:2495`/`:2807`/`:3091` 使用） | 扫描/重扫依赖传播 |
| 依赖面板 | `editor/file_system/dependency_editor.cpp:262`、`:1032` | Resource 依赖视图 |
| 脚本 API | `core/core_bind.cpp:111-113`（`ResourceLoader.get_dependencies`） | 用户/插件查询 |

## 不实现的后果（明确记录）

1. 编辑器把 `.ts`/`.js` 当零依赖 → 改一个被 `import` 的模块，不会让依赖它的脚本进入重扫/重导入流程。
2. 依赖面板对 `.ts` 不显示依赖。
3. `ResourceLoader.get_dependencies("res://x.ts")` 返回空（API 对脚本类型撒谎）。
4. **不影响导出打包**：导出走导出插件自己的模块图遍历（`src/editor/weaver-editor/jsb_export_plugin.cpp:260-268` → `env->get_module_direct_dependencies` + 递归 `export_compiled_script`）。
5. **不影响运行期**：模块图由 AMD loader 在运行时建立。

→ 结论：这是**编辑器侧一致性/可用性**问题，非阻塞（这就是此前「照用也没事」的原因）。若决定不做，应把 `//TODO` 换成「已知限制」说明并文档化（drop 选项见 `design.md` 备选 C）。

## 需求

- **R1** `_get_dependencies(p_path, p_add_types)` 返回该脚本的**直接**依赖，元素为 `res://` 路径（指向**源文件**，与 GDScript 报 `preload` 路径的做法一致）；不递归。
- **R2** 必须能在 `const` 且可能位于编辑器后台扫描线程的环境下调用：**不得**触碰 `Environment`/isolate/context，**不得**加载或执行 JS。
- **R3** 覆盖说明符形态：相对（`./`、`../`）、`res://` 绝对、`import`/`export … from`/`require("…")`；别名（`@alias/*`）见 AC4。
- **R4** 裸包名（`godot`、`node:*`、npm 包）**不计入**（对齐 GDScript 只报资源依赖）。
- **R5** 静态不可判定的一律忽略并文档化：动态 `import(expr)`、拼接/变量 `require`、条件加载。

## 非目标

不做递归；不改导出插件流程；不改运行期模块图；不改 `Environment::get_module_direct_dependencies`；不引入第三方解析库。

## 验收标准

- [ ] **AC1** doctest：`ResourceLoader.get_dependencies()` 对测试用 `.ts` 返回其 `import` 的 `res://` 路径集合（相对与 `res://` 两种写法各覆盖）。
- [ ] **AC2** doctest：`godot` / 裸包名 / 动态 `import(expr)` 不出现在结果里。
- [ ] **AC3** 证明「无 JS 执行」：语言未初始化（或目标模块从未被 `load()`）时调用仍返回正确结果且不崩、不产生模块缓存条目。
- [ ] **AC4** 别名 `@tests/*`（`.paths_mapping`，样例 `project/.godot/godotjs_ext/.paths_mapping` = `@tests/*=tests/*`）行为明确：实现，或显式记录为不支持并写明理由/回退行为。
- [ ] **AC5** 本地门禁：v8 + quickjs-ng 构建 rc=0、doctest 全绿、smoke 打印 `GODOTJS_TEST_PROJECT_COMPLETED`。
- [ ] **AC6** 若产生新约定（如 `.paths_mapping` 的读取契约），同步进 `.trellis/spec/`。

## 约束

- 不新增第三方依赖；单趟扫描，避免正则回溯；大文件（>1 MB）不得成为编辑器扫描热点。
- 临时文件放 `./.agent_tmp/`；项目文档中文。

## 开放问题（进 `design.md` 讨论）

1. 依赖报**源**路径（`.ts`）还是生成物（`.js`）？——倾向源路径。
2. `.paths_mapping` 不存在（未构建过）时如何回退？
3. 是否需要在结果里做去重与排序（引擎是否依赖顺序）？
4. `p_add_types` 忽略是否与 GDScript 一致（`modules/gdscript/gdscript_resource_format.cpp:79-93` 忽略）。
