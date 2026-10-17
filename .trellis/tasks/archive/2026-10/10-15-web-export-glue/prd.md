# Web 导出的 JS 桥 glue 注入

## Goal

让本仓（GDExtension 形态）的产物 **用官方 web 模板** 导出后能在浏览器里真正运行：
扩展 side module 对 `jsbi_*` 的导入必须能被解析（当前任何官方模板下，页面一启动就
`Aborted(Assertion failed: undefined symbol 'jsbi_init')`）。

## 背景（本会话实测，均为事实）

| 事实 | 证据 |
|---|---|
| 官方 4.7.2 模板导出成功，页面启动即 abort | `Godot Engine v4.7.2.stable.official.ed1daf0bf` 后紧跟 `Aborted(undefined symbol 'jsbi_init')`（`.agent_tmp/web_run13.log`） |
| 上游无此问题 | 上游 `SCsub:578-579`：`env.AddJSPre(["impl/web/js/jsbb.impl.js"])` + `env.AddJSLibraries(["impl/web/js/library_godotjs_jsbi.js"])` —— glue 编进**引擎主模块**（Godot 引擎自身也是这套 scons API：`platform/web/SCsub:37,57`） |
| 本仓没有接线 | `SConstruct` / CI / `scripts/` 对 `js-library` / `pre-js` / `jsbb` / `jsbi` 零引用 |
| Godot 没给 GDExtension 留钩子 | `core/extension/`、`editor/export/`、`platform/web/export/`、`platform/web/js/` 全无命中；`engine.js:166` 只检查 `loadDynamicLibrary` 是否存在 |
| 原理 | emscripten 解析 side module 导入时查主模块 JS 工厂里的 `wasmImports` 表：`src/lib/libdylink.js:68,586,763`；`loadDynamicLibrary` 只接受 wasm（无 `.js` 分支） |
| 注入可行（POC） | 向模板 `godot.js` 的工厂作用域（`var wasmImports` 之前，`Module["preRun"]` 里注册）注入 glue 后：浏览器输出 `[jsb-poc] injected 110 jsbi symbols` → `Initialized GodotJS bridge` → WebGL 上下文创建成功（`.agent_tmp/web_run14.log`） |
| 注入后暴露的下一层问题 | 同上日志：随后 `RuntimeError: memory access out of bounds`，崩点在扩展 side module 内部（引擎主模块调用进来） |

## Requirements

- **R1** 用**官方** web 模板导出的包，扩展 JS 桥必须能初始化（110 个 `jsbi_*` 全部可解析），不要求用户自建引擎模板。
- **R2** 注入必须**可失败可见**：模板 glue 结构不匹配（锚点缺失）时导出报错，错误信息含文件与锚点，绝不静默产出坏包。
- **R3** 提供开关（项目设置 / 导出预设），默认开启，可关闭以还原当前行为。
- **R4** 仅 web 平台导出时生效，其它平台产物与行为零影响。
- **R5** glue 源（`jsbb.impl.js` + `library_godotjs_jsbi.js`）随 addon 分发，导出时从 addon 读取；不依赖用户机器上的构建目录。
- **R6** 线程（pthreads）/ worker 形态下同样有效。

## Acceptance Criteria

- **AC1** 官方 `web_dlink_debug.zip` + 测试项目导出后，在无头 Chrome 里出现 bridge 初始化日志，且不再出现 `undefined symbol 'jsbi_init'`。
- **AC2（分两级，取决于 web 腿自身 bug 是否在本任务内修）**
  - 一级：页面进入脚本执行阶段（不再 abort，能打印引擎/桥日志）。
  - 二级：集成测试跑出 `GODOTJS_TEST_PROJECT_COMPLETED` 哨兵（需同时修掉 `memory access out of bounds`）。
- **AC3** 人为构造锚点缺失的模板 js，导出必须失败并给出含锚点的诊断。
- **AC4** 关闭开关时导出产物与现状一致；开启时仅 web 产物变化，其它平台产物字节不变。
- **AC5** 文档更新：说明 web 导出依赖官方模板 + 注入机制，以及已知限制（依赖 emscripten glue 内部符号）。

## Constraints

- 不改 Godot 引擎源码；不要求使用者自建模板（除非最终选路线 C）。
- 不得引入新的第三方依赖。
- 遵守仓库约束：临时文件入 `./.agent_tmp/`；`*.gen.*` 不手改；不自行 commit/push。

## Out of scope（待用户确认）

- web 腿自身的运行时缺陷（`memory access out of bounds` 等）。它是 AC2 二级的前置，**建议拆子任务**，除非用户要求本任务一并修。
- `src/editor/weaver-editor/jsb_export_plugin.cpp:273` 那条「web 需要把脚本打包成 monolithic 并预加载」的 TODO —— 与本次注入无直接关系，另计。

## Open Questions

- **Q1** 选哪条路线？（见 `design.md` 的 A / B / C 对比）
- **Q2** `memory access out of bounds` 是否拆子任务？
- **Q3** glue 源的分发形态：随 addon 放两个 `.js`（导出时读文件）vs 编进 C++ 字符串。
