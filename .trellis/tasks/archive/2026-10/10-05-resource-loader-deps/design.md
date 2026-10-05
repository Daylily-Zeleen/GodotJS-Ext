# 技术设计：`_get_dependencies` 静态依赖扫描

## 1. 落点与签名

- 文件：`src/runtime/weaver/jsb_resource_loader.cpp:123-126`
- 签名（不可改）：`PackedStringArray ResourceFormatLoaderGodotJSScript::_get_dependencies(const String &p_path, bool p_add_types) const`
- 头文件：`src/runtime/weaver/jsb_resource_loader.h:49`

## 2. 镜像对象：GDScript 怎么做

`modules/gdscript/gdscript_resource_format.cpp:79-93`：

```cpp
Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
String source = file->get_as_utf8_string();
GDScriptParser parser;
if (OK != parser.parse(source, p_path, false)) return;
for (const String &E : parser.get_dependencies()) p_dependencies->push_back(E);
```

要点：**打开源码 → 解析 → 收集依赖**；不执行脚本，不接触 VM。我们照此办理，只是解析器换成一个小型扫描器。

## 3. 为什么不用现成的 runtime API

`Environment::get_module_direct_dependencies`（`src/runtime/bridge/jsb_environment.cpp:1514-1545`）虽然能给出准确的模块图，但它：

- 内部 `load(p_module_id, …)` → **真的会编译/执行模块**；
- 取 `get_isolate()` / `get_context()`，必须在主线程使用（导出插件的调用点带 `Thread::is_main_thread()` 守卫：`src/editor/weaver-editor/jsb_export_plugin.cpp:244`）；
- 依赖 per-isolate 的 `Environment` 生命周期与语言初始化状态。

而 `_get_dependencies` 是 `const`，会在编辑器文件系统扫描（可能后台线程、且可能在语言未初始化时）被调用 → 不可用。

## 4. 方案

### 4.1 结构

新增一个不依赖 `Environment` 的纯函数模块（放 `src/runtime/weaver/` 下，与 loader 同层；名字待定，如 `jsb_script_deps.h/.cpp`）：

```
PackedStringArray scan_module_dependencies(const String &p_source, const String &p_dir); // 返回 res:// 绝对路径
```

loader 侧只做「读文件 → 调 scan → 返回」。

### 4.2 扫描器（单趟，无正则回溯）

按 JS/TS 词法走：跳过 `//`、`/* */` 注释与字符串/模板字面量，识别：

| 形态 | 处理 |
|---|---|
| `import … from "spec"` / `import "spec"` / `import type … from "spec"` | 取 spec |
| `export … from "spec"` / `export * from "spec"` | 取 spec |
| `require("spec")`（字面量参数） | 取 spec |
| `import("spec")`（字面量参数） | 取 spec |
| `import(expr)` / `require(name)`（非字面量） | 忽略（R5） |

### 4.3 说明符解析

| 输入 | 结果 |
|---|---|
| `./x`、`../x` | `res://` + 相对 `p_dir` 归一化；补扩展名（`.ts`/`.js`/`.mjs`/`.cjs`），文件存在则计入 |
| `res://…` | 原样（补扩展名同上） |
| `godot`、`node:*`、npm 包名 | 忽略（R4） |
| `@alias/…` | 见 4.4 |

归一化复用现成工具：`jsb::internal::PathUtil`（仓库已有；`src/runtime/bridge/jsb_path_util.*`）+ 既有 `.paths_mapping` 解析测试 `src/runtime/tests/test_jsb_paths_mapping.h`。

### 4.4 别名（AC4）

构建产物 `.godot/godotjs_ext/.paths_mapping` 记录了 tsconfig `paths` 的映射，样例内容（`project/.godot/godotjs_ext/.paths_mapping`）：

```
@tests/*=tests/*
```

设计取舍：

- **读取时机**：调用时读（`FileAccess`，不存在则跳过该别名，属正常情形：未构建过/无别名）。
- **风险**：`.godot/` 是构建产物，内容与版本可能变化；只做「前缀替换 + 相对路径归一化」，不引入额外语义。
- 备选：完全不支持别名并在注释/文档里写明（若实现成本超预期则退到这一步，但必须在 PRD 的 AC4 里记录结论）。

### 4.5 输出

- 元素 = `res://` 源文件路径；**去重**（同一模块被多次 import 只报一次），保持**首次出现顺序**（引擎不保证顺序，但稳定顺序便于测试与调试）。
- `p_add_types` 忽略（与 GDScript 一致，`modules/gdscript/gdscript_resource_format.cpp:79-93` 未使用该参数）。
- 目标文件不存在 / 无法打开 → 返回空数组（GDScript 用 `ERR_FAIL_COND_MSG` 打错误；这里对编辑器扫描更应安静——**决定：返回空，不报错**，理由：编辑器会对新建/未保存文件调用）。

### 4.6 线程与性能

- 只读 `FileAccess` + 只读映射表；无锁、无全局状态、不碰 `Environment`。
- 单趟 O(n)，n = 源码字节数；`get_as_utf8_string()` 拷贝一次（与 GDScript 相同），可接受。

## 5. 备选方案

| 方案 | 结论 |
|---|---|
| **A（采纳）** 纯文本扫描 | 无 JS 执行、线程安全、与 GDScript 语义一致 |
| B 接 `Environment::get_module_direct_dependencies` | 不可用：需主线程 + 会加载模块（见 §3） |
| C 不实现，改成「已知限制」注释 + 文档 | 保留为 drop 选项；若采纳需更新 PRD 的 AC 与 STATUS 清单 |

## 6. 测试计划（doctest）

新增 `src/runtime/tests/test_jsb_resource_deps.h`（命名与现有 `test_jsb_*.h` 一致，注册方式见 `jsb_test_main.cpp`）：

1. 造两个临时用例：一个 `.ts` 里同时含相对 import 与 `res://` import；断言 `ResourceLoader::get_singleton()->get_dependencies("res://…")` 的结果集合。
2. 断言 `godot`、裸包名、`import(expr)` 不出现。
3. 断言「未 `load()` 过该模块」时结果已正确（证明无 JS 执行；同时可断言模块缓存无新条目）。
4. 别名用例（若实现）。

测试资源放 `project/tests/`（现有测试同层），避免污染既有用例；如需新的 fixture 目录按仓库惯例命名。

## 7. 回滚

改动集中在一个新模块 + loader 的 5 行；回滚 = 恢复 loader 原 `return {};` 并删新模块/测试（提交粒度上独立，可安全 revert）。
