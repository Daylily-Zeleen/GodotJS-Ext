# SCons 构建规范

## 强制命令

编译 GodotJS-Ext **必须且只能**使用：

```
scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6
```
其他平台/引擎组合按需在规范命令上叠加参数（示例：全量测试构建 `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`；测其他 JS 引擎加 `use_quickjs_ng=yes` / `use_node=yes` / `use_quickjs=yes`；引擎可执行文件名随 target/platform 变化，产物在 `bin/<platform>/`）。

1. **绝不自行编造编译命令**——只用上述参数组合
2. **绝不 `scons --clean`**——全量重建极耗时；增量编译自动跳过未更改部分
3. 需要重编就直接跑上述命令（增量构建）
4. **bindings 强制重生成（2026-10-16 起）**：`SConstruct` 里 `env["generate_bindings"] = True`，每次构建都会按当前 arch 重新生成 godot-cpp 的 `gen/`。原因：gen 目录跨平台共享，而 builtin 尺寸按 arch 不同（wasm32 `String`=4 字节 vs x64 = 8）；复用别平台布局会让 wasm32 侧模块出现步长错位（2026-10-16 web 腿 `PackedStringArray` range-for 崩溃根因）。代价：**在 x64 与 wasm32 之间切换平台时 godot-cpp 会全量重编一轮**，属预期行为；同平台增量构建不受影响（SCons MD5 内容签名，同内容重写不触发重编）

> **并行编辑竞态（重要）**：编译失败但报错位置/形态不在你自己的改动面内时，**先查 `git log`（新 commit？）与报错文件的 mtime**——用户或其他会话可能在同一工作树上并行编辑（2026-09-13 实例：rebuild 撞上用户 mid-edit 的 `class_methods.h` 半改状态，报 `HashC undeclared`；等用户提交后复跑即绿）。绝不先回滚/还原你未编辑过的文件；也绝不凭单次 md5 异常下结论（引擎 editor 模式会重拷 `~` 副本，拷贝进行中读 md5 会得到一次性假值——重读一次再判）

## 构建机制要点

- 构建前提：`third/godot-cpp` 子模块已初始化（`git submodule update --init`），否则 SConstruct 报错退出
- **不要回退旧引擎**：旧二进制（如 7-28 的 editor.dev）加载不了新构建扩展（godot-cpp ABI 不匹配，插件实例化即崩）；官方 4.8.dev 宿主会挂死。
- **C++ 测试宿主（CI 同款）用官方稳定版**（`godot`）；**不要用** `bin/windows/` 下自己构建的 godotjs-ext 可执行文件（headless 必崩，与改动无关）。
- `--godotjs-api-generate` 会【消费删除】`project/extension_api.json`；需要资源声明 gen（`extension_api.json.gen.ts`）时，先把官方引擎自带的 `extension_api.json`（随引擎分发，在其安装目录下）复制回 `project/` 再重跑 `--generate-types`。
- `project/.godot` 删除后的重建三件套：①手工写 `project/.godot/extension_list.cfg`（**一行**：`res://addons/godotjs-ext.daylily-zeleen/godotjs-ext.gdextension`）；②重新编译 TS；③重新生成 api 数据。
- **测试项目重置**（需要完整初始环境时，先删除）：`./project/.godot`、`./project/gen`、`./project/typings`（如果有）；`./project/tsconfig.json` **只有**要执行 `GodotJSEditorPlugin::try_install_project_files()` 的测试才删除（git 跟踪的预设文件）。

### web 侧模块（wasm32 dlink side module）

```
scons platform=web target=template_debug arch=wasm32 threads=yes -j4
```

- emsdk 必须用 **4.0.11**（CI 同款，`.github/actions/setup-godotjs-ext/action.yml` 的 `em-version`）；已在本机 `emsdk activate 4.0.11`
- **不要加 `optimize=debug`**（-Og）：4.0.11 下产出**非法 wasm**（浏览器 wasm 校验器拒收：`GodotJSRuntimePreset::get_source` fallthru 校验错；2026-10-16 在修正 gen 布局后独立复现，与 gen 问题无关），默认 `speed_trace`（-O2）正常
- 校验产物无需开浏览器：`node -e "new WebAssembly.Module(require('fs').readFileSync('<wasm>'))"`，非法模块秒级报错
- **`godot-cpp` gen 目录跨平台共享（已修复，2026-10-16）**：`gen/` 的 builtin 尺寸按 arch 不同（wasm32 `String`=4 字节 vs x64 = 8），复用别平台布局会让 wasm32 侧模块出现步长错位——症状是 `PackedStringArray` range-for 崩溃（`String` 拷贝构造报 `unaligned accesses`/`OOB`）。**已由强制 `generate_bindings=yes` 修复**（见强制命令第 4 条）。若再遇同类崩溃，先查 `gen/include/godot_cpp/variant/string.hpp` 的 `STRING_SIZE` 是否匹配当前 arch（wasm32 应为 4），并用 `node -e "new WebAssembly.Module(...)"` 秒级校验产物
- **跨线程释放句柄（已修复，2026-10-17，只在 web shim 内解决）**：cross-env 里 `jsb_check(is_alive())` 触发的根因不是 Reset 语义，而是**跨线程误用句柄**——Godot 会从任意线程销毁带 instance binding 的对象 → `InstanceBindingCallbacks::free_callback` → `Environment::free_object()`，而那个 Environment 属于别的线程（worker），于是它的 `Global` 在**主 realm** 里被 `Reset()`。web 上一个 pthread 只能访问自己的 JS global，而句柄槽存在目标 realm 的 JS 堆里，所以这种 Reset 既可能误删本 realm 的同号句柄（`EngineID` 恒 0、按 realm 解析），也会在断言处炸。修复（只改 `src/runtime/impl/web/`）：`Isolate` 记录属主线程并持一个 `em_proxying_queue`；`Global::Reset()` 入口判 `Broker::owns_current_thread()`，跨线程时把「释放该槽（含清弱回调）」经 `emscripten_proxy_async` 投递回属主线程执行（属主线程已退出则丢弃，realm 随之消失），本线程只清 C++ 字段。bridge（`jsb_environment.*`）保持上游不动。
  实测证据：在分支内置 `jsb_checkf(false)` 会命中（该场景确定性发生）；强制走旧路径 3/3 失败（`FATAL: !(is_alive())`）；thunk 内置 `jsb_checkf` 报 `self == owner`（确认在属主线程执行）；干净构建下 web 腿 ×2 PASS，native（node 构建 / doctest 98\|98+1280\|1280 / host-node）全绿。另：此前试过的「`Reset()` 对 `IsEmpty()` 静默清空」已**回退**——它让 kStrong 分支的断言恒真并跳过 `jsbi_handle_Reset`（句柄槽泄漏），属于掩盖缺陷。
  排查提示：`Global::Reset()` / `Broker::owns_current_thread()` 这两处的 `JSB_WEB_LOG(Error/Warning, …)` 结果**不会出现在抓取到的日志里**（该时刻 logger 已不可用），只有 `jsb_checkf`/FATAL 可见 —— 不能用「日志没出现」推断「代码没执行」。
- **glue ensure 只在 `GlobalInitialize::init()` 顶部一处（2026-10-17 实测 ×2 PASS）**：`__jsb_web_lib` 在每个线程**各自的 JS global** 里，而 `GlobalInitialize` 构造函数的 static 守卫是**进程级**的（pthread 共享静态内存，全进程只构造一次）——所以构造函数内的 ensure 覆盖不到后续 worker 线程。而 `init()` 是**每线程最早的 JS 触点**：worker 线程先跑 `jsb_worker.cpp` 的 `impl::GlobalInitialize::init()` 再建 `Environment`，主线程先跑 `GodotJSScriptLanguage` 构造里的 `init()` 再建 `Environment`，`Isolate::Isolate()` 永远更晚。故按线程的 ensure 单点放在 `init()` 的 tls 分支**第一条语句**（连它自己的那行 `JSB_WEB_LOG` 都要在其后）；日志回调与 `Isolate::Isolate()` 内的重复调用已删除（2026-10-16 的"回调内也要 ensure"结论作废——当时的失败是把 `init()` 体内那一处也一并删掉了）
- **`_engine_alive` 守卫已被"释放时注销钩子"取代（2026-10-17）**：旧做法是 `FreeEngine` 置 `false` 并在 gc finalizer / 两个浏览器事件监听器里逐次判活，且必须在 `NewEngine` 复位（线程池复用线程时 `_jsbb_` 跨会话存活，漏复位则新会话守卫全跳过、worker 永不就绪）——这是"事件为什么会打进已死引擎"的兜底，不是根因。现改为在 `jsbb_Engine::Release()` 里**成对撤销注册**：`FinalizationRegistry.unregister(_token)` 批量注销该引擎的全部对象登记（`jsbb_Registry.Dispose()`），并 `removeEventListener` 掉 `SetHostPromiseRejectionTracker` 注册的具名 `unhandledrejection` / `error` 监听器（匿名函数无法移除，故改为具名并存入 `_host_event_hooks`）。注销后回调根本不会触发，三处判活与 `_engine_alive` 字段全部删除
- **桥内字符串/内存助手保持 `_jsbb_.wasmop`（2026-10-17 结论）**：`UTF8ToString`/`stringToUTF8`/`lengthBytesUTF8`/`_malloc`/`_free` 确实由 Emscripten 模块作用域提供，但**不需要**改成模块作用域常量：`_jsbb_.wasmop` 在 `init()` 里赋值，而 13 个调用点全部要求引擎已存在（引擎只在 `init()` 之后创建），不存在「`init()` 之前用到」的情况。曾短暂引入的 `jsbb_wasmop()` + `?? {…}` 兜底、以及模块作用域常量，均已回退到上游形态。

- `binding_mode=static|shared`（默认 `shared`，2026-09-20 起）时每次构建自动跑 codegen 单态发射（`misc/build/static_binding_codegen.py --binding-mode <mode>`），产出 `src/static_binding/gen/dispatch_*.gen.cpp`（glob 编译）；`binding_mode=dynamic` 不跑 codegen。切分支后 gen 目录残留 obj 会被覆盖，无需手动清理
- codegen 源数据 = godot-cpp 子模块内置 `third/godot-cpp/gdextension/extension_api-4-7.json`；`SConstruct` 的 `API_VERSION = "4.7"` 是唯一硬编码点——改它须同步 `.gdextension` 的 `compatibility_minimum`。**CI 无 api-dump job**
- 生成文件一律不入库（`*.gen.*`），详见 [../cpp/generated-files.md](../cpp/generated-files.md)
- lws 全平台启用（含 Linux）：预编译库来自我方 `GodotJS-Dependencies` CI，Linux 用 `CMAKE_POSITION_INDEPENDENT_CODE=ON` 构建，可链入共享库。**改依赖（含 lws / v8 / libnode 的下载源与版本）前先读 [dependencies.md](./dependencies.md)**

## dll 部署与验证（动态/静态两份）

- addons 只有**一份** gdextension（`godotjs-ext.gdextension`，见 [single-library](#单一库两产物2026-09-28)）；`target=editor` 与 `target=template_*` 的产物名不同（`godotjs-ext.<plat>.editor.<arch>` / `.template_<flavor>.`），替换验证时换**当前 target 那一份**

### 单一库两产物（2026-09-28）

- 同一 `SharedLibrary` 目标按 `env["target"]` 选源集合：`target=editor` 编 `runtime_globs + editor_globs`，模板腿只编 `runtime_globs`（editor 源需要 `TOOLS_ENABLED` godot-cpp 头，模板腿没有）
- 两侧 glob 有 19 个同名文件（api_tool / internal / compat 各若干），合并时**按 basename 保序去重**（`SConstruct` 的 `make_target_env`）；obj 落 `.build/runtime/`
- 宏 `JSB_WITH_EDITOR`（`SConstruct` 的 `jsb_defines` → `src/jsb.gen.h`）取值 `target == "editor"`，用于共享 TU 内的分支（`register_types.cpp` 的 editor 模块转调、测试入口、`jsb_script_language.cpp` 的 templates include）。它与 `JSB_TOOLS`（镜像 godot-cpp 的 `TOOLS_ENABLED`）并存不合并
- 唯一入口符号 `jsb_gdextension_init`（原 `jsb_editor_library_init` 已删）
- **进程残留锁 dll**：编译成功但 cp 报 "Device or resource busy" = 有 godot 进程未退出（含 SEGV 残留）。先 `taskkill /F /IM godot*` 再 cp；cp 后用 md5sum 确认两处一致
- 引擎加载的是 `project/addons/godotjs-ext.daylily-zeleen/bin/` 下的产物——改代码后必须 scons 再验证
- **dll 身份只认 md5 + 构建命令**：采数/验证前 `md5sum` 两处部署位；`staticBinding` 字段不可信，`det==0 flood` 只对 dynamic 有效。跨腿采数后切回另一腿必须重新 `scons`（两腿产物字节不同），并重新核对 md5——凭记忆判断当前部署的是哪条腿必然出错
- benchmark 采数对比必须固定同一 dll 做 A/B（如 `--gc` on/off）；dll 一变，所有旧数据作废

## 本地测试命令（速查）

本地测试命令速查（详细判据与陷阱见 [../test/index.md](../test/index.md) 与 [../test/doctest.md](../test/doctest.md)）：

- C++ 测试（需 `tests=yes` 构建）：`godot --path ./project --jsb-run-tests`（单套件；editor 用例在 `target=editor` 构建时并入同一注册表）
- TS 编译：`cd project && pnpm gen:types && node_modules/.bin/tsc`（`gen:types` 需要 `GODOT` 环境变量指向引擎；`tsconfig.json` 的 `typeRoots` 指向 `./typings`，缺 typings 必须先 `gen:types`。**不要用 `--noCheck`**——那会跳过类型检查，本项目要求测试项目真实过检）
- TS 集成测试：先生成 api 数据并编译 TS，再 `godot --path ./project --verbose`
- Benchmark：`godot --audio-driver Dummy --headless --path ./project -- --bench [--gc] [--only=<组>]`——**所有开关都是 user args**（`--` 之后）：`--bench`（`start.ts` 用 `OS.get_cmdline_user_args()` 判断，选中只跑 benchmark 场景）、`--gc`、`--only`（`benchmark.ts` 同源解析）。把 `--bench` 写在 `--` 之前引擎会试图解析它而测试项目收不到（判别：`START-DIAG benchOnly=false` 即未生效）；详细纪律见 [../test/index.md](../test/index.md) Benchmark 专项
