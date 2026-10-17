# 技术设计：web 导出 glue 注入

## 问题本质

`jsbi_*` 是扩展 side module 的**未定义导入**（`jsb_web_interop.h:37` 的 `JSBROWSER_API` 就是裸 `extern "C"`）。
emscripten 解析 side module 导入时，查的是**主模块 JS 工厂作用域里的 `wasmImports` 表**
（`src/lib/libdylink.js:68,586,763`）。因此物理入口只有两类：

1. **构建期**把 glue 打进主模块（路线 B / C）；
2. **事后**改写主模块 glue（路线 A）。

## 已排除的做法（含"直接打包进去"的设想）

| 做法 | 为什么不行 |
|---|---|
| 把两个 js 拷进导出目录 / 打进 pck | 没有任何加载器会执行它们：`loadDynamicLibrary` 只接受 wasm（`libdylink.js` 里没有 `.js` 分支，只有 `.so`）；`platform/web/js/` 里没有加载扩展 js 的代码（`engine.js:166` 只是个"是否支持"检查）；真正的加载发生在 wasm 侧 `dlopen`/LDSO |
| 页面里用 `<script>` 加载桥 | 桥依赖工厂作用域符号：`wasmMemory` / `HEAPU8` / `HEAP32` / `HEAP8` / `_malloc` / `_free` / `PThread` / `ENVIRONMENT_IS_PTHREAD`（本会话实测清单），页面里都拿不到；且够不到 `wasmImports` |
| 自定义 HTML shell + `Module` 公共 API | `Module` 上不暴露 `wasmImports` / `wasmMemory`；`loadDynamicLibrary` 不接受 js |

## Route A：导出后改写模板 glue（推荐）

**做法**：导出插件在 web 导出结束后，以 addon 里的两个 js 为原料，注入到导出的 `<name>.js`：

- 注入位置：工厂作用域内、`var wasmImports` 之前（POC 用的是模板里的 `godot.js:5627776`）；
- 注入形式：

```js
Module["preRun"]=Module["preRun"]||[];
Module["preRun"].push(function(){
  /* jsbb.impl.js 全文 */
  /* library_godotjs_jsbi.js 全文（去掉 mergeInto(LibraryManager.library, ...) 一行） */
  for (var k in GodotJSBrowserInterface) { wasmImports[k] = GodotJSBrowserInterface[k]; }
});
```

- 前置校验：锚点或 `Module["preRun"]` 不存在 ⇒ 导出直接报错（满足 R2）。

**优点**
- 纯本仓实现；用户可继续用官方模板（满足 R1）；不需要引擎源码 / emsdk。
- POC 已在真浏览器验证链路：`injected 110 jsbi symbols` → `Initialized GodotJS bridge` → WebGL 上下文创建成功。
- 可检测失败、可用开关回退（R3），对其它平台零影响（R4）。

**缺点 / 风险**
- 依赖 emscripten glue 内部形态（`wasmImports` 变量名 + `Module["preRun"]` 语义 + `var` 提升）⇒ 换 emscripten/模板版本可能失效；用锚点校验把"失效"变成"显式报错"而不是坏包。
- 每次导出改写约 5.7MB 文本（一次性，可接受）。
- 本质等价于"事后 `--pre-js`"，属 hack；上游 glue 结构一变就要跟。
- pthread worker 是否共用同一份 js（理论上共用，需实测，对应 R6）。

**代价**：中。**验证**：POC 复用 + AC1/AC3/AC4。

## Route B：把 glue 编进扩展自己的 side module

**做法**：web 构建给扩展接 `--js-library library_godotjs_jsbi.js` + `--pre-js jsbb.impl.js`，
让 `jsbi_*` 从"对主模块的导入"变成"本模块自带 JS 库的导出"。

**优点**：形态最正确，符号不跨模块；理论上与主模块/模板无关。

**缺点 / 风险**
- **未验证**：emscripten 的 `loadDynamicLibrary` 只收 wasm、Godot 的 `gdextensionLibs` 只列 wasm
  ⇒ "side module 自带的 JS glue 由谁加载"没有证据；很可能仍要改模板或引擎（退化成 A+C 组合）。
- 本仓 web 目标怎么出的（emcc 调用点）本会话没定位到，接线本身也要先摸清。
- emscripten 6.0.1 下"side module + js library"未见文档化路径。

**代价**：高（探索性）。建议作为**独立实验子任务**，不阻塞 A。

## Route C：自建带 glue 的 web 模板（上游做法）

**做法**：在带 GodotJS 的引擎源码树里，`platform/web/SCsub` 加 `AddJSLibraries` / `AddJSPre`，
产出 `web_dlink_debug/release.zip` 随发布分发。

**优点**：与上游一致、无 hack；构建期注入天然覆盖 pthread/worker；符号在主模块里是"一等公民"。

**缺点 / 风险**
- 需要引擎源码 + emsdk 6.0.1，本仓没有该流水线（CI 只构扩展）；上一轮本地自建模板卡在 emcc GL 签名（未解决）。
- 重分发引擎模板的发布/体积/许可问题。
- 用户必须用我们的模板，不能配官方模板 ⇒ 与 R1 冲突，选 C 需改写 R1。

**代价**：最高（跨仓库 + 构建基础设施）。

## 建议

1. 主路线 **A**（已验证、本仓内、可回退开关、失败可检测）。
2. **B 作为独立实验子任务**：只有当 A 的锚点脆弱性在目标模板版本上真的暴露，B/C 才有投入价值。
3. AC2 二级所需的 `memory access out of bounds` 修复**拆子任务**（web 腿从未在浏览器跑过，可能不止一处）。

## 待定（Q3）

glue 源分发形态：

- **(a)** addon 里放 `bin/web/js/{jsbb.impl.js,library_godotjs_jsbi.js}`，导出时读文件 —— 实现简单、可诊断、可被用户覆盖；
- **(b)** 编进 C++ 字符串（`*.gen.*` 由 SConstruct 生成）—— 不落文件，但每次改桥都要 regen。

建议 **(a)**。
