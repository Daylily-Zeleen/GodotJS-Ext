# 执行记录

## 2026-10-15 · 立项与调研（planning）

**已完成**

1. 分支：`feature/web-export-glue`（从 `main` 开）。
2. 任务：`10-15-web-export-glue`（`prd.md` + `design.md` 已写；分支已 set；`implement.jsonl`/`check.jsonl` 已加 3 条 spec 上下文）。
3. 定位问题链（全部实测，非推断）：
   - 官方模板导出成功 → 浏览器 `Aborted(Assertion failed: undefined symbol 'jsbi_init')`（`.agent_tmp/web_run13.log`）；
   - 上游 `SCsub:578-579` 把 `AddJSPre(jsbb.impl.js)` / `AddJSLibraries(library_godotjs_jsbi.js)` 编进**引擎主模块**；本仓构建脚本零接线；
   - emscripten 解析 side module 导入查的是主模块工厂的 `wasmImports`（`libdylink.js:68,586,763`）；`loadDynamicLibrary` 只收 wasm；
   - Godot 无 GDExtension JS library 钩子（`core/extension/`、`platform/web/export/`、`platform/web/js/` 无命中）。
4. **POC 已验证注入可行**：`.agent_tmp/inject_web_glue.py` 把 glue 注入模板 zip 的 `godot.js`（工厂作用域、`var wasmImports` 之前、`Module["preRun"]` 注册），跑 `--runtimes web-browser --skip-builds` 得到：
   - `[jsb-poc] injected 110 jsbi symbols` → `Initialized GodotJS bridge` → WebGL 上下文成功；
   - 之后暴露下一层问题：`RuntimeError: memory access out of bounds`（崩点在扩展 side module 内，引擎调用进来）——web 腿从未在浏览器执行过，属运行时 bug，建议拆子任务。
5. 仓库内改动（本轮）：`scripts/test/run-runtime-matrix.mts` 哨兵超时 120s→300s，且超时路径改用 `rejectWithTail` 打印浏览器控制台尾巴（原来超时看不到任何浏览器输出）。
6. 本地素材：`tests/bin/godot.exe`（官方 4.7.2 editor）、`tests/bin/godot-web-browser-template_debug.zip`（官方 `web_dlink_debug.zip`，`.orig` 为未打补丁的备份）。

**待用户裁决**：路线 A / B / C（见 `design.md`）；`memory access out of bounds` 是否拆子任务；glue 源分发形态 (a)/(b)。

## 2026-10-15 · Route B 实现并验证（AC1 达成）

**决策（用户）**：① 走 B；② OOB 并入本任务、先记录复现路径；③ glue 不做"随 addon 分发"（B 下不需要），必要时内嵌二进制。

**机制核实**（决定 B 可行的依据）
- `src/lib/libdylink.js:846-861`：加载 side module 时把它的 EM_ASM 常量注册进主模块 JS；`:863-881` `addEmJs()` 把 side module 的 `__em_js__*` 数据物化成函数（注释指明对应构建期 `create_em_js`）。
- `:740-790`：side module 的导入用 Proxy + 惰性 stub 解析 ⇒ 只要在首次调用前注册好即可。
- 作用域：物化用 `eval(...)`，发生在主模块工厂内 ⇒ 桥可访问 `wasmMemory/HEAPU8/HEAP32/HEAP8/_malloc/_free/PThread/ENVIRONMENT_IS_PTHREAD`。

**改动**
- 新增 `misc/build/generate_web_glue_header.py`：从 `jsb_web_interop.h`（C 原型）+ `js/library_godotjs_jsbi.js`（实现）+ `js/jsbb.impl.js`（桥）生成 `src/runtime/impl/web/jsb_web_glue.gen.h`（110 个 EM_JS 包装 + 两个 js 源的**字节数组**内嵌 + `jsb_web_glue_setup`）。
- 新增 `src/runtime/impl/web/jsb_web_glue.cpp`：提供 `jsb_web_glue_ensure()`。
- `jsb_web_interop.h`：声明 `jsb_web_glue_ensure()`。
- `jsb_web_global_init.cpp:201`：`jsbi_init(...)` 之前调用 `jsb_web_glue_ensure()`。
- `SConstruct:689-693`：构建时调用生成器（与 `templates.gen.h` 同位置风格）。

**验证**
- 构建：`scons platform=web target=template_debug arch=wasm32 threads=yes -j4` → rc=0（95s），产物自动装进 `project/addons/godotjs-ext.daylily-zeleen/bin/web/`。
- wasm 内嵌检查：`__em_js__jsbi_init` / `__em_js__jsbi_Eval` / `__start_em_js` 各命中 1 次。
- 官方模板（未打任何补丁）导出 + 无头 Chrome 运行（`.agent_tmp/web_run15.log`）：
  - `Godot Engine v4.7.2.stable.official.ed1daf0bf`
  - `[jsbb] Initialized GodotJS bridge`  ← AC1
  - `OpenGL API OpenGL ES 3.0 (WebGL 2.0 ...)`
  - `undefined symbol 'jsbi_init'` 消失（对比 `.agent_tmp/web_run13.log`）

**OOB 复现路径（保留在本任务内）**
- 命令：`node scripts/test/run-runtime-matrix.mts --runtimes web-browser --skip-builds`（依赖 `tests/bin/godot.exe` = 官方 4.7.2 editor，`tests/bin/godot-web-browser-template_debug.zip` = 官方 `web_dlink_debug.zip`）
- 日志：`.agent_tmp/web_run15.log`（也不时复现于 run14：注入方案）
- 现象：桥初始化、GL 上下文创建之后，`RuntimeError: memory access out of bounds`，栈首帧在扩展 side module 内（`wasm://wasm/006200aa:wasm-function[1848]`），由主模块侧（`0a25b1fa`）调用进来。
- 已排除的猜想：桥对内存增长有处理（`monolith.ts:1514-1537` 会在 `wasmMemory.buffer != HEAP8.buffer` 时重建视图）。

**未做**：非 web 平台回归（AC4）尚未跑；OOB 尚未修（AC2 二级未达）。
