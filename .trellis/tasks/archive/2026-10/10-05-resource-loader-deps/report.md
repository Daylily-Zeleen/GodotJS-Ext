# 结论：不实现（转为已知限制 + 文档）

**裁定（2026-10-05）**：`ResourceFormatLoaderGodotJSScript::_get_dependencies` 保持空实现。
理由见 `design.md` §5 备选 C 与 `prd.md` 的「不实现的后果」——引擎里没有任何脚本语言上报依赖：

- GDScript 的 `GDScriptParser::get_dependencies()` 自身就是空实现（`modules/gdscript/gdscript_parser.h:1684-1687`，
  函数体写着 `// TODO: Keep track of deps.`），加载器那层只是转发它（`modules/gdscript/gdscript_resource_format.cpp:79-93`）；
- C# 没有覆写这个 loader 钩子（`modules/mono/` 下无 `get_dependencies`）；
- 基类默认实现（`core/io/resource_loader.cpp:184-192`）在未覆写时什么都不产出。

**已落地**：

- `src/runtime/weaver/jsb_resource_loader.cpp:124-126` 的注释内容改写为中文，**标记仍是 `// TODO`**
  （用户要求：留着 TODO，只把内容写成已知限制），含依据、核对日期 2026-10-05、引擎 4.8.0-dev、
  影响范围：仅编辑器依赖图/依赖面板/`ResourceLoader.get_dependencies`；导出与运行期不受影响。
- 文档站新增页 `misc/known-issues`（docs 仓），以**用户视角**记录本条与 autoload 的限制（不提引擎内部机制）。

**未做**：静态依赖扫描（`design.md` 方案 A）——等上游 Godot 真正实现该钩子后再评估。

PRD / design / implement 保留作为决策记录，本任务可归档。
