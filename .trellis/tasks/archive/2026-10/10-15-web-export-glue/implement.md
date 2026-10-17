# 执行计划：Route B（扩展 side module 自带 JS 桥）

## 机制（已核实，取代"改模板"的 hack）

emscripten 加载 side module 时，会把该模块自带的 JS **注册进主模块的 JS 作用域**：

- `src/lib/libdylink.js:846-862`：side module 的 EM_ASM 常量 → `addEmAsm` → `ASM_CONSTS[...] = eval(func)`；
- `src/lib/libdylink.js:865-890`：side module 的 `__em_js__*` 数据 → `addEmJs(name, sig, body)`（注释指明这与 emcc 构建期 `create_em_js` 对应）。

`eval(func)` 在主模块工厂内执行 ⇒ 这些 JS 可以访问主模块作用域变量。桥实测依赖：
`wasmMemory` / `HEAPU8` / `HEAP32` / `HEAP8` / `_malloc` / `_free` / `PThread` / `ENVIRONMENT_IS_PTHREAD` —— 全部都在主模块作用域里。

现状问题：`jsb_web_interop.h:37` 的 `JSBROWSER_API` 是裸 `extern "C"` ⇒ 110 个 `jsbi_*` 变成 wasm 的**未定义导入** ⇒ 官方模板 abort（实测）。改为 EM_JS 后，JS 随**我们的 wasm**（em_js/em_asm 段）分发，加载时由主模块物化 ⇒ 不需要改模板、不需要改引擎。

## 步骤

**S1 基线与构建路径**
- 命令（需要 emsdk 环境）：`scons platform=web target=template_debug arch=wasm32 threads=yes verbose=yes -j4`
- 产物：`bin/web/godotjs-ext.web.template_debug.wasm32.wasm`；装到 `project/addons/godotjs-ext.daylily-zeleen/bin/web/`
- 还原官方模板（`.orig`）后跑 `node scripts/test/run-runtime-matrix.mts --runtimes web-browser --skip-builds`
  ⇒ 期望复现 `undefined symbol 'jsbi_init'`，日志存 `.agent_tmp/`（基线证据）。

**S2 生成器 `misc/build/generate_web_glue_header.py`**（对齐既有 `generate_templates_header.py` 模式）
- 输入：`src/runtime/impl/web/js/library_godotjs_jsbi.js`（110 个薄封装）+ `src/runtime/impl/web/js/jsbb.impl.js`（桥，tsc 产物）
- 输出：`src/runtime/impl/web/jsb_web_glue.gen.h`（`// AUTO-GENERATED`，禁止手改）
  - ① 桥源码 **内嵌二进制**：`constexpr unsigned char kWebBridgeSource[]` + 长度（不写成 C++ 字符串字面量，避免手改/转义漂移）
  - ② 由 js 库文件生成的 110 个 `EM_JS(...)` 定义（单一来源，避免手抄漂移）
- SConstruct 在构建时调用（与 `templates.gen.h` / `jsb_project_preset.gen.cpp` 同位置风格）。

**S3 接线**
- `src/runtime/impl/web/` 中把 `JSBROWSER_API` 外部声明换成生成的 `EM_JS` 定义；非 web 构建不参与。
- C++ 调用点（`jsb_web_global_init.cpp:201` 等）**不改**：EM_JS 生成的就是同名 C 函数。

**S4 桥装载**
- 一个 EM_JS 入口读取 `kWebBridgeSource` 并在主模块作用域执行（幂等，只执行一次）；桥自身已把 `_jsbb_` 挂到 `globalThis`，薄封装按 `_jsbb_` 全局名调用。

**S5 验证**
- web：S1 流程 ⇒ AC1（出现 bridge 初始化日志、不再 abort）；关闭注入前的官方模板也要能跑（本路线无注入）。
- 非 web 回归：`scons platform=windows target=editor use_node=yes dev_build=yes debug_symbols=yes tests=yes -j4` ⇒ doctest 96/96 + 集成 COMPLETED=1 / FAILED=0（AC4）。

**S6 `memory access out of bounds`（并入本任务，按用户要求"先记录复现路径"）**
- 把复现路径（命令 + 日志 + 崩点栈）写进 `report.md`；实现后再看是否仍在，若仍在则单列子任务。

## 验证命令速查

| 目的 | 命令 |
|---|---|
| web 扩展构建 | `cmd /c "call D:\Dev\emsdk\emsdk_env.bat && scons platform=web target=template_debug arch=wasm32 threads=yes verbose=yes -j4"` |
| web 运行测试 | `node scripts/test/run-runtime-matrix.mts --runtimes web-browser --skip-builds`（需 `tests/bin/godot.exe` + 官方模板） |
| 非 web 回归 | `scons platform=windows target=editor use_node=yes dev_build=yes debug_symbols=yes tests=yes -j4` + doctest / 集成两条 |

## 回滚点

- 新增：生成器 + `jsb_web_glue.gen.h`（删除即回退）；SConstruct 调用点（一行）；
- 改动集中在 `src/runtime/impl/web/`（`git diff` 可回退）；
- 不动 `library_godotjs_jsbi.js` 内容（保持与上游引擎路线一致；生成器只读它）。

## 风险

- `EM_JS` 在 `-sSIDE_MODULE` 下是否按预期把 `__em_js__*` 数据放进 wasm（S1/S2 后立刻可见；若不行，退化为 EM_ASM 或回到路线 A）。
- 参数/返回类型限制（EM_JS 的 C 签名解析）：现有 110 个函数签名都是简单类型（指针/整数/void），预期可行。
- 线程形态下每个实例各自物化（预期一致，S5 覆盖 threads=yes 与 =no）。
