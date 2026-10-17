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

## 2026-10-15 · 提交 + 非 web 回归

- 提交 `0e66fbe feat(web): carry the JS bridge inside the extension side module`（13 文件；`tests/` 与 `third/quickjs-ng` 未入库；生成的 `.gen.h` 命中 `.gitignore:*.gen.*`，无需入库）。
- 非 web 回归（node 腿，改动只落在 web impl + SConstruct 生成器调用）：
  - 构建：`scons platform=windows target=editor use_node=yes dev_build=yes debug_symbols=yes tests=yes -j4` → rc=0（90s）
  - doctest：**98 cases / 98 passed / 1280 assertions**（`.agent_tmp/node_doctest_after_web.log`）
  - 集成：`GODOTJS_TEST_PROJECT_COMPLETED` = 1，`GODOTJS_TEST_PROJECT_FAILED` = 0（`.agent_tmp/node_integration_after_web.log`）

## OOB 下一步（本任务剩余唯一项）

- 现状：产物 wasm 的自定义段只有 `dylink.0` 与 `target_features`（实测，无 `name` 段）⇒ 栈里的 `wasm-function[1848]` 无法映射到符号。
- 计划 ①：用 `debug_symbols=yes`（必要时给 web 腿补 `-g`）重编 web 扩展一次，拿到函数名后再定位。
- 计划 ②（若 ① 拿不到名字）：在 web impl 的启动路径上按段插日志二分（每个循环 ~95s 构建 + ~300s 运行）。
- 已知线索：崩点在桥初始化、GL 上下文创建之后；栈首帧在扩展 wasm 内，由主模块调用进来；桥对内存增长已有重建逻辑（`monolith.ts:1514-1537`）。

## 2026-10-15 · OOB 定位（带符号重编 + name 段解析）

- `debug_symbols=yes`（godot-cpp `common_compiler_flags.py`：emcc 下加 `-gdwarf-4 -g3`）重编 web 扩展 → wasm 出现 `name` 段 + DWARF（55MB）。
- 浏览器栈出现符号；另用脚本解析 `name` 段把未命名的帧也映射出来（校验：功能号 2820 = `_init`、2822 = `populate_string_names_replacements`，与 Chrome 输出一致）。
- **崩点**：`jsb::static_binding::operator_unary_thunk<(godot::Variant::Operator)23, godot::PackedInt64Array, bool>`
  **调用链**：`GodotJSScriptLanguage::_init()` → `populate_string_names_replacements()` → `api_tool::list_utility_functions()`（拉起 JS 侧 API Tool）→ JS 访问触发静态绑定 operator thunk → OOB。
- 相关代码：`src/static_binding/thunks/builtin_operators.h:157` —— `left_backing_of<L>(info.This())` / 静态初始化的 `variant_get_ptr_operator_evaluator` / `eval(left_opaque, nullptr, &ret_slot)`。
- 候选原因：① `info.This()` 在 web 分片（`v8::Local` 模拟）下栈位无效；② 对象未带预期 internal data，绑定读到的指针异常；③ JS 误触发到该 thunk。
- 下一步：在该 thunk 内插日志（`This()` 的 stack pos、OpC、类型、`left_opaque`）→ 重编 → 重跑（约 95s + ≤300s/轮），据此判定 ①②③。
- 注：工程里当前装的 web wasm 是带符号诊断版（55MB），`bin/` 被 ignore，不影响仓库。

## 2026-10-15 · OOB：更正模块归属（上一轮的定位有误）

**探针结果（决定性）**
- 用 `JSB_WEB_LOG(Error, ...)`（web 腿经 `jsbi_log` → 浏览器 console，已被对照点验证可打印）在 `operator_unary_thunk` 全程打点，并在 `jsb_web_global_init.cpp` 的 `jsbi_init()` 之后加对照点。
- 结果：对照点打出 **1 条**，`operator_unary_thunk` 的探针 **0 条**（`.agent_tmp/web_run19.log`）⇒ 该 thunk 从未被调用。

**据此更正**
- 上一轮我把 `wasm-function[1848]` 按**我们模块的 name 段**命名，结论（静态绑定 operator thunk）**不成立**。
- 真正依据：栈里的模块哈希中，`0313f132`/`0d30869e`（每次构建都变）才是我们模块；崩帧所在的 `006200aa` **跨构建恒定**（run14/16/17/18/19 都一样）⇒ 那是**引擎的模块**。
- 即：OOB 发生在**引擎代码**里；链路是 `GodotJSScriptLanguage::_init()` → `populate_string_names_replacements()` → `godot::StringName::StringName(const String&)`（godot-cpp，在我们模块）→ 引擎内部（`String` → `StringName` 的转换/复制）→ 崩。
- 最可能形态：我们交给引擎的某个 **`String` 内部数据/指针异常**（引擎在复制时越界读）。注意：官方引擎模板没有 name/symbol 段，无法直接反查引擎帧。

**已回滚**：`src/static_binding/thunks/builtin_operators.h` 与 `jsb_web_global_init.cpp` 的临时探针已全部移除（`git status` 只剩 report.md 一处改动）。

**下一步**
- 在 `populate_string_names_replacements()` 内逐段/逐条打日志（同一通道），定位崩前最后处理的那个名字及其来源（暴露类列表 / singleton 列表 / `api_tool::list_utility_functions()` / 全局常量）。
- 若指向 `api_tool::*`（JS API Tool 返回值），再往 JS 侧追 `String`/`StringName` 的构造与桥交互。

## 2026-10-15 · OOB 收敛：根因在 sourcemap 处理路径

**过程**（每一步都是「构建 190~250s + 跑测 ≤300s」的实测循环，日志在 `.agent_tmp/web_run*.log`）
- 探针通道结论：`printf` 在 side module 到不了浏览器 console；`EM_ASM({console.warn(...)})` 与 `JSB_WEB_LOG` 都可以（后者走 jsbi_log）。
- 逐段插桩把 `populate_string_names_replacements()` 全程走完（`snr done` 打出），崩点随之后移，最终用带符号构建拿到命名栈：
  - `godot::RegEx::search(String const&, int, int)` ← `SourceMapCache::match` ← `process_source_position` ← `BridgeHelper::get_exception` ← `Environment::load` ← `GodotJSScript::load_module_immediately`
  - 之后又出现 `godot::String::utf8() const` ← 同一链路。
  - 期间控制台出现大量 `Unicode parsing error: Invalid unicode codepoint (2020200a / 74682820 …)`：**4 个 ASCII 字节被当成一个 UTF-32 码点**读写 ⇒ sourcemap 路径在 web 上拿到的是损坏的 `String`。
- **决定性实验**：临时在 `SourceMapCache::process_source_position()` 开头 `return p_stacktrace;`（绕过 sourcemap）后重编：
  - **OOB 完全消失**；引擎完整启动（`Build configuration: Emscripten 4.0.20, multi-threaded, GDExtension support.`）。
  - 新阻塞变成逻辑错误：`[src\runtime\bridge\jsb_godot_module_loader.cpp:172] godot class not found 'Node'`（JS 侧模块加载器查不到引擎类）。

**当前工作区的临时改动（未提交，需收敛）**
- `src/runtime/internal/jsb_source_map_cache.cpp`：sourcemap 绕过（临时）+ `RegEx` 有效性守卫 + 未使用的 `JSB_OOB_PROBE` 宏块。
- `src/runtime/weaver/jsb_script_language.cpp`：singleton 的 range-for 改成显式索引（曾把崩点从 singleton 段推走）。
- `src/api_tool/core/api_tool_loader.cpp`：`JSB_OOB_PROBE` 宏块（探针已清）。

**下一步**
1. 处理 `godot class not found 'Node'`（`jsb_godot_module_loader.cpp:172`，web 上类注册/查找链路）。
2. 决定 sourcemap 在 web 上的处置：gate 掉 vs 修根因（损坏 String 的来源仍未定位，怀疑 `jsb_format`/String 拼装路径）。

## 2026-10-15 · web 运行时打通（6 个真 bug）+ 剩余 1 个场景

**已修复（每个都有实测证据，日志在 `.agent_tmp/web_run*.log`）**

1. **`ApiLoader::update_base_dir()` 用 `globalize_path()`** ⇒ 打包运行时目录列举为 0（`count 0`）⇒ 类表空 ⇒ `godot class not found 'Node'`。改为保留 `res://`（`count 955`）。
2. **`GlobalInitialize::shutdown()` 只在非 emscripten 分支有定义** ⇒ web 构建符号缺失 ⇒ 引擎关闭时 `Aborted(undefined symbol …)`。补定义。
3. **`get_api_data_files()` 遍历「其它文件夹」时错用 `classes_dir_path`** ⇒ 生成的路径全是错的 ⇒ `builtin_classes/*.capi` **从未进包** ⇒ 运行时 `Failed to find primitive type: PackedStringArray`。改为 `DirFiles::get_files_at(dir_path)`（用 `dir_path`）。
4. **worker 线程没有 `GlobalInitialize::init()`**（它只挂在 `GodotJSScriptLanguage` 构造函数 = 主线程）⇒ worker 里桥未初始化 ⇒ `Cannot read properties of undefined (reading 'call_function')`。在 `Environment` 构造处按线程补（`#if JSB_WITH_WEB`，`init()` 在线程内幂等）。
5. **桥在 `init()` 之前使用 `wasmop`** ⇒ worker 早期抛错时 `undefined.UTF8ToString/_free` 直接崩。加 `jsbb_wasmop()` 兜底（模块作用域里的 `UTF8ToString` 等本来就在）。
6. **worker 就绪/加载链路**：`jsb_web_glue_ensure()` 补到 `Isolate` 构造 + 打印回调（防止 `_jsbb_` 未建立就被调用）。

**基础设施**：harness 的 console tail 80→4000；给 page 会话开 worker 自动附加（能看到 `[worker]` 日志）；`project.godot` 音频驱动 = Dummy；`--skip-builds` 需要预先 `pnpm -C project build`（否则用陈旧 JS 测）。

**当前状态**：引擎完整启动、类/内置类型绑定正常、worker 能加载脚本、15 个场景跑到第 15 个（cross-environment）。
**剩余**：`cross-environment` 场景失败——主线程 `instance_from_id(<主线程自己创建的 Node id>)` 返回 null（`owned: could not prebind receiver`）；worker 内同 id `validLocal=true`。即**对象经 web worker 转移后，发送侧的原生对象不再可解析**，怀疑 web worker 转移路径的所有权处理。

## 2026-10-16 · web 腿全绿：根因是桥的 GetOwnPropertyDescriptor 语义

**根因（已修复）**
`src/runtime/impl/web/bridge/src/monolith.ts` 的 `jsbb_Engine.GetOwnPropertyDescriptor` 在
`Object.getOwnPropertyDescriptor(obj, key)` 返回非 object（属性**不存在**）时 `_throw(TypeError("GetOwnPropertyDescriptor: unexpected"))`。
v8 语义要求此时返回 `undefined`（非空 MaybeLocal），而脚本解析器会**故意**查询不存在的自有属性：
`jsb_class_info.cpp:638` 的 `NS_MISSING is ignored (not an own property of the class)` 路径。
抛错把 web 腿带偏，后续 `Builtins::_require` 直接 `RuntimeError: memory access out of bounds`，
场景以 `GODOTJS_TEST_PROJECT_FAILED: start` 结束。
- 修复：缺失属性时 `return jsbb_StackPos.Undefined;`（附注释说明 v8 语义与调用方）。
- 顺带修正 `GetOwnPropertyNames` 里复制粘贴错的错误信息（`GetOwnPropertyDescriptor` → `GetOwnPropertyNames`）。
- 实测效果：`GetOwnPropertyDescriptor: unexpected` 与 `memory access out of bounds` 在修复后的日志中**各 0 次**
  （修前 `.agent_tmp/web_scene{1,2}.log` 各 1 次）。

**被证伪的假设（不要再走）**
- GC 释放相关：`internal_data_.remove_at()`（`.agent_tmp/web_diag_nr{1,2,3}.log` FAIL/PASS/FAIL）、
  web 端 valuetype 释放（`.agent_tmp/web_diag_nf{1,2,3}.log` FAIL/PASS/FAIL）、
  finalizer 的引擎存活守卫（`.agent_tmp/web_guard{1,2,3}.log` 3/3 FAIL，`skipped=0`）——三条全部与失败无关。
- 「static-members 单场景确定性失败」是**实验事故**：当时对 `project/tests/start.ts` 的改动手滑只替换了列表**第一项**
  （`resource/Resource` → `static-members/StaticMembers`），列表仍是 15 项、`static-members` 出现两次；
  第二次跑到时共享静态量 `score` 仍是上一轮 worker 回写的 2000 ⇒ `shared static initial value expected 0, got 2000`。
  `StaticMembersGd.tscn` 跑的是 `static-members-gdcheck.gd`（GDScript，不断言绝对值），原列表里 TS 测试只跑一次。

**终态证据（本轮最后一版产物）**
| 验证 | 命令/日志 | 结果 |
| --- | --- | --- |
| web 腿 ×3 | `.agent_tmp/web_final{1,2,3}.log` | rc=0，`web-browser: PASS`，`GODOTJS_TEST_PROJECT_COMPLETED`=1，`FAILED`=0 |
| web 腿 ×2（最终桥产物，含信息修正） | `.agent_tmp/web_ok{1,2}.log` | 同上 |
| web 构建 | `.agent_tmp/bridge_build_final.log` / `web_build_final.log` | rc=0 |
| node 构建 | `.agent_tmp/node_build_gofinal.log` | rc=0 |
| doctest | `.agent_tmp/doctest_gofinal.log` | `test cases: 98 \| 98 passed`、`assertions: 1280 \| 1280 passed` |
| 集成（host-node） | `.agent_tmp/node_integration_gofinal.log` | rc=0，`host-node: PASS`，`COMPLETED`=1，`FAILED`=0 |

**SetDeleter TODO（`jsb_web_helper.h:44`）处置**
已删除 TODO，改写为机制性注释（payload 由 `jsni_gc_callback()` 在浏览器 GC 回收该对象时消费，对象把 internal data
注册进桥的 FinalizationRegistry）。依据是实测：浏览器 FinalizationRegistry 真的回调进 C++，每轮 20 次
（类型 25=CALLABLE / 26=SIGNAL，`dup=0` 无双重释放），见 `.agent_tmp/web_dbg{1,2}.log`。

**遗留**
- `cross-environment` 的 shadowRealm：web 上以能力检测跳过（`require('godot.shadowRealm')` 失败则打印跳过原因）。
  **用户裁决（2026-10-16）：保持该方案（选项 B），不实现 web 侧 shadow realm。**
- `0e66fbe`（web glue 路线 B）**未推送**；工作区另有本轮未提交改动。未获授权不 commit/push。

## 2026-10-16 · 收尾清理：注释语言规范 + 死探针 + 复验

- 规范补记：`.trellis/spec/godotjs-ext/cpp/coding-standards.md` 新增「注释语言」节（新增注释一律简短中文、
  存量英文注释不强制改写、标识符保留原文、commit message 英文）；`.trellis/spec/guides/workflow-rules.md`
  的「文档语言」节原先只写了「代码注释惯例不变」，现在指向该节。（此前规范里确实没有这条。）
- 本轮新增的英文注释全部改为简短中文：`monolith.ts`（6 处）、`library_godotjs_jsbi.js`、`jsb_web_helper.h`、
  `jsb_web_isolate.cpp`、`jsb_web_handle.h`、`jsb_environment.cpp`（2 处）、`jsb_environment.h`、`jsb_worker.cpp`、
  `run-runtime-matrix.mts`（4 处）、`test-cross-environment.ts`（4 处）、`cross-environment-peer.ts`。
- 清掉测试文件里遗留的死探针（`test-cross-environment.ts` 的 `_full/_low/_res/_rid/_n` 与空 try/catch）。

**复验（最终源码状态）**

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web 构建 / bridge 构建 | `.agent_tmp/web_build_cmt.log` / `bridge_build_cmt.log` | rc=0 |
| web 腿 ×2 | `.agent_tmp/web_cmt{1,2}.log` | rc=0，`PASS`，`COMPLETED`=1，`FAILED`=0 |
| node 构建 | `.agent_tmp/node_build_cmt.log` | rc=0 |
| 集成 host-node | `.agent_tmp/node_cmt.log` | rc=0，`PASS`，`COMPLETED`=1，`FAILED`=0 |
| doctest | `.agent_tmp/doctest_cmt.log` | `98 \| 98 passed`、`1280 \| 1280 passed` |

## 2026-10-16：web 腿回归定位与修复（range-for + 工具链）

### 根因（两项，均已实测闭环）

1. **range-for 遍历 `PackedStringArray` 在 web 侧模块被错编**（用户指出方向）：
   `GodotJSScriptLanguage::populate_string_names_replacements()` 的单例循环在 web 侧模块（`-sSIDE_MODULE=1 -pthread`）下，
   `String` 拷贝构造触发 `RuntimeError: operation does not support unaligned accesses`。
   A/B（同为默认 -O2，仅循环形式不同）：range-for 复现 trap（`web_ab3.log`），下标索引 PASS（`web_final_idx.log`）。
   与循环元素写成 `const StringName&` 还是 `const String&` 无关——两种写法都崩，只有下标索引正常。
2. **`optimize=debug`（-Og）不该用于 web 侧模块**：emsdk 4.0.11 下 `-Og` 直接产出**非法 wasm**
   （V8 拒收：`GodotJSRuntimePreset::get_source` "expected 0 elements on the stack for fallthru"，node 校验秒级复现）；
   6.0.1 下产出可实例化但运行期错编的模块（同一 trap 的另一种表现）。规范命令不带 `optimize`，默认 `speed_trace`(-O2) 正常。

### 工具链

- 本机 emsdk 已 `activate 4.0.11`，与 CI 一致（`.github/actions/setup-godotjs-ext/action.yml` 的 `em-version` 默认值）；此前是 6.0.1

### 改动

- `src/runtime/weaver/jsb_script_language.cpp`：单例循环改下标索引 + `const String&`（注释写真实原因）
- `.trellis/spec/godotjs-ext/build/scons-build.md`：新增「web 侧模块」节（emsdk 版本、禁 `optimize=debug`、node 秒级校验产物、range-for 禁用）

### 验证（`.agent_tmp/`）

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web 构建（规范命令，4.0.11） | `web_build_final_idx.log` | rc=0；产物 node 校验 VALID |
| web 腿 | `web_final_idx.log` | rc=0，`PASS`，`COMPLETED`=1，`FAILED`=0 |
| node 构建 | `node_build_idx.log` | rc=0 |
| doctest | `doctest_idx.log` | `98\|98`、`1280\|1280` passed |
| host-node | `hostnode_idx.log` | rc=0，`COMPLETED`=1，`FAILED`=0 |

### 排查中排除的假设（证据）

- 陈旧生成物 `jsbb.impl.js`（gitignored）：重新生成后仍 FAIL → 非根因
- 产物哈希 `7f89999b`/`cbb28fe4`/`e9645c04` 依次重建仍 FAIL → 非单一产物问题
- 浏览器环境（隔离 profile、禁 ML/OptimizationGuide 组件）仍 FAIL → 非根因
- Edge 无法替代（harness CDP `Browser.getVersion` 超时）
- 崩溃栈固定：`index.side.wasm:589 ← ext String::String ← NamingUtil::get_class_name ← populate_string_names_replacements ← _init`

### 2026-10-16 补充：range-for 问题的机制取证（用户质疑后收敛）

用户质疑"项目里不止这一处对 PackedStringArray 用 range-for"，原"被错编"的说法过宽。补埋点实测（`.agent_tmp/web_diag10.log`）：

```
[singleton-diag] size=39 sizeof(String)=8 sizeof(PSA)=16 psa_ptr=51093248
[singleton-diag] i=0 it=(14833784)Performance      idx=(14833784)Performance   eq=1
[singleton-diag] i=1 it=(14840728)ProjectSettings  idx=(14965096)Engine        eq=0
[singleton-diag] i=2 it=(13040544)Time             idx=(14840728)ProjectSettings eq=0
...（i>=1 起两条路径元素序列完全分叉；迭代器指针跳跃、operator[] 出现重复地址）
RuntimeError: memory access out of bounds   ← 迭代器 ~i=21 走出数组边界
```

结论（实测范围）：
- 同一个 `PackedStringArray`，range-for（`begin()` 取一次基址 + 本地指针自增）与 `operator[]`（每次经 `packed_string_array_operator_index` 取）观测到**不同的元素序列**；迭代器基址中途失效，走出数组边界，之后 `String` 拷贝构造对垃圾地址做原子引用计数 → `unaligned accesses`。
- const/非 const 访问路径、`-Og`/`-O2` 均复现；下标索引每次重新取元素 → 稳定，全套测试绿。
- 范围修正：web 侧模块内既有代码对 `PackedStringArray` 的遍历（`api_tool_loader.cpp:60`、`amd_module_loader.cpp:50`、`jsb_builtins.cpp`）本来就是下标循环；range-for 遍历的多是 `Vector<String>`/`List`/`LocalVector`（godot-cpp 自有容器，无逐元素引擎调用）或 editor 腿代码（不编进 wasm）。因此 web 执行路径上真正跑到"PackedStringArray + range-for"的只有这一处——这就是只有它崩的原因。
- 接口层面（`packed_string_array_operator_index` 在 dlink+pthreads 侧模块下为何使基址失效）未定论，需上游取证。

### 2026-10-16 终：根因确认为 gen 头跨平台复用，构建层修复

用户追问"wasm32 指针怎么是 8 字节"后定位到真根因：
- wasm32 指针就是 4 字节；8 字节是 godot-cpp 生成头里 `godot::String` 的 opaque 句柄（`STRING_SIZE` 取自 extension_api.json `builtin_class_sizes`，按 arch+精度选择）。
- 本机 `third/godot-cpp/gen/` 由 10-11 的 x64 构建生成（String=8），`generate_bindings` 默认 False、gen 目录跨平台共享 → web 构建复用 64 位 builtin 布局 → `PackedStringArray::Iterator` 按 sizeof=8 步长走引擎 4 字节步长缓冲 → 隔一个取一个（埋点 `it[i]==idx[2i]`）→ 越过缓冲区一倍 → 垃圾元素进 `String` 拷贝构造 → 崩。桌面 x64 两个步长一致，永不暴露。
- 接口层机制闭环，此前"被错编/待上游取证"的说法作废；`-Og` 非法 wasm 是独立第二问题（4.0.11 下 V8 拒收，仍禁用）。

修复：
- `SConstruct`：`env["generate_bindings"] = True`——每次构建按当前 arch 重新生成 gen（AlwaysBuild + SCons MD5 签名，同平台增量不受影响；切换平台时 godot-cpp 全量重编一轮，属预期）。
- `jsb_script_language.cpp` 保留下标索引（对 gen 状态鲁棒），注释更新为步长机制。

验证（`.agent_tmp/`）：

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web 构建（regen 后 STRING_SIZE=4） | `web_build_genyes.log` | rc=0，wasm VALID |
| web 腿 | `web_genyes.log` | PASS，COMPLETED=1，FAILED=0 |
| node 构建（regen 回 STRING_SIZE=8） | `node_build_genyes.log` | rc=0 |
| doctest | `doctest_genyes.log` | 98\|98、1280\|1280 |
| host-node | `hostnode_genyes.log` | PASS，COMPLETED=1，FAILED=0 |

### 2026-10-16 最终闭环：range-for 原样恢复，无需任何源码改动

`SConstruct` 强制 `generate_bindings=yes` 后（gen 按 wasm32 生成 `STRING_SIZE=4`），将 `jsb_script_language.cpp` 完全还原（用户保留自己的一行改动 `const StringName&`→`const String&` 的 range-for），web 腿直接全绿——证明崩溃根因 100% 是 gen 头跨平台复用，与循环写法无关。

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web 构建（range-for 原样 + STRING_SIZE=4） | `web_build_rf.log` / `web_build_rf2.log` | rc=0，wasm VALID |
| web 腿（StringName 形态） | `web_rf.log` | PASS，COMPLETED=1，FAILED=0 |
| web 腿（String& 形态，用户最终形态） | `web_rf2.log` | PASS，COMPLETED=1，FAILED=0 |
| node 构建（regen 回 STRING_SIZE=8） | `node_build_rf.log` | rc=0 |
| doctest | `doctest_rf.log` | 98\|98、1280\|1280 |

`jsb_script_language.cpp` 当前 diff = 用户自己的一行改动，无任何本轮残留。spec 已改为记录根因与诊断方法（查 gen 头 STRING_SIZE 是否匹配 arch），不再要求下标循环。

### 2026-10-16（三问答复轮）

1. `jsb_web_handle.h` 断言：已恢复原样（不再吞）。恢复后 web 跑测断言仍触发 → 证明存在真实缺陷：cross-env worker session 2 在**复用的线程池线程**上运行时，session 1 拆除阶段已 `Reset` 的强句柄被二次 `Reset`（`jsb_web_handle.h:211`）。属 worker 会话切换 + 线程池复用场景的 Global 生命周期/清理顺序问题，**未修复**（埋点证据与方法见下）。此前的"僵尸 Reset"缺陷（`Reset(isolate, 空 Local)` 留下 isolate_ 非空 + value_=-1 的状态）已顺带修掉。
2. `jsb_web_glue_ensure()`：根修完成——`ensure` 移到 `GlobalInitialize` 构造函数（安装日志回调**之前**，回调能触发的先决条件即已满足），三个 `_custom_print_*` 回调里的逐次调用已删，`init()` 内保留（`jsbi_init` 前置）。`jsb_web_glue_setup` 本身幂等（`__jsb_web_glue_ready` 守卫）。
3. `optimize=debug`：在修正 gen 布局后**独立复现**非法 wasm（同函数 #169 fallthru 校验错）→ 与 gen 问题无关，emcc 4.0.11 的 `-Og` 对本模块确实不可用，spec 禁用条目保留；已删除此前"6.0.1 错编模块"的错误归因（那实为 gen 步长问题）。"web 构建怎么会有 v8"：web 构建无任何 v8 代码（三份构建日志 0 命中），此前 spec 里"V8 拒收"指浏览器内核的 wasm 校验器，措辞已改。

诊断方法记录（可复现）：给 `jsbi_handle_New/Reset/IsValid` 临时加 owner 参数 + JS 侧 tombstone（创建/移除栈），slot-missing 时打印完整证据链；实测数据：句柄 306 由 0x56ea30 创建、0x56ed90 移除、0x56ed90 二次 Reset 触发断言；`store_gen_now=1` 排除 store 重建；`idgen_now` 单调排除 id 复用。

### 2026-10-16（末）：逐句柄 console 埋点不可行 + 当前失败面

逐句柄 `console.warn` 埋点（每次 new/remove 带 8 行栈）在 web 上**不可行**：上千条 console 输出把页面拖垮，worker 95s 未就绪（`web_diag19.log`），埋点本身改变时序。
清理全部 TEMP-DIAG 后的干净构建（`web_final4.log`）：本次未触发 is_alive 断言，改为 **`worker ready timed out after 95000ms`**（session 2 worker 未就绪）→ cross-env 在复用线程路径上有**两种失败表现**（is_alive 断言 / worker ready 超时），共同根因是 worker 会话切换 + 线程池复用下的生命周期问题，未修复。
native 腿全绿：node 构建 rc=0、doctest 98|98+1280|1280、host-node PASS。
下一步候选：① worker 就绪握手在复用线程上的失效点（`onready` 时序）；② session 拆除时 `Environment`/Global 的清理顺序；③ `-sPTHREAD_POOL_SIZE_STRICT=2` 观察行为差异。

### 2026-10-16（终²）：is_alive 断言根因闭环

恢复断言后复现 → 属主埋点（创建/移除/断言三方 owner + 栈）+ 源码比对，根因确认为**已知模式的缺失守卫**：
- 绑定对象：`jsb_environment.cpp:1021/1024` `handle->ref_.Reset(...)` + `SetWeak(..., &object_gc_callback, ...)`。
- GC 第一遍：`object_gc_callback` 里 `ref_` 已被 Reset（句柄清空）。
- C++ 包装对象第二遍释放：`~ObjectHandle` → `~ref_` → `Global::Reset()` → kStrong 分支 `jsb_check(is_alive())` false → FATAL。
- 同库既有先例：`jsb_environment.cpp:1089-1102` 对同一模式（"first-pass GC callback may already have reset ref_ while second-pass free is still pending"）已有注释 + `IsEmpty()` 守卫；崩溃点（web shim 的 `Global::Reset()`）缺等价语义。
- v8 语义：`Global::Reset()` 对空句柄/弱回调已清空的句柄是文档化 no-op；桌面平台 v8 真实现不炸，web shim 会炸。

修复方向（涉及 shim 语义，按边界需先批准）：`jsb_web_handle.h::Reset()` 对 `IsEmpty()`（含弱回调已释放）静默清空——与 `jsb_environment.cpp:1094-1102` 的既有守卫同构，非放宽断言。

### 2026-10-16（终³）：修复落地 + 双向验证

- `jsb_web_handle.h::Reset()`：改为 v8 语义——`IsEmpty()`（空句柄/弱回调已释放）时静默清空，活句柄正常 Reset。消除"GC 第一遍 Reset + C++ 二遍释放"合法序列上的断言。
- `jsb_web_global_init.cpp`：glue ensure 按线程根修落地（构造函数 thread_local 一次 + `init()` 内按线程一次 + 回调内按线程幂等 + `Isolate::Isolate()` 兜底）；`worker.cpp:362` 的 `GlobalInitialize::init()` 保留（负责 `jsbi_init` 协议注册，与 glue 无关）。
- `jsb_web_handle.h::Reset(Isolate, 空 Local)` 的语义回退为原始实现（isolate_ 照常绑定）——此前误改。

验证（`.agent_tmp/`）：

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web 构建 | `web_build_v8b.log` | rc=0，wasm VALID |
| web 腿（修复后连续多次） | `web_v8sem1/2.log`、`web_v8b.log` | 三次全部 PASS，COMPLETED=1，FAILED=0 |
| node 构建 | `node_build_v8sem.log` | rc=0 |
| doctest | `doctest_v8sem.log` | 98|98、1280|1280 |
| host-node | `hostnode_v8sem.log` | PASS，COMPLETED=1，FAILED=0 |


### 2026-10-16（补）：glue ensure "冗余"质疑的实测结论（用户问对了）

曾以"Isolate::Isolate() 已调用 + 构造函数已调用"为由删除 `init()` 体与日志回调内的 `jsb_web_glue_ensure()` → 换新 worker 线程（pthread 池新起）后首条日志即 `TypeError: reading 'jsbi_init'`，worker 永不就绪（`web_g1.log`）。恢复后 PASS（`web_g2/g3/g4.log`）。

机制（实测确认）：`GlobalInitialize` 构造函数的函数级 static 守卫位于共享静态内存，全进程只构造一次（首个线程）；`__jsb_web_lib` 却在各线程各自的 JS global。故**按线程的 ensure 不可省**：回调内（任意线程首条打印）、`init()` tls 分支内（worker `jsbi_init` 前）、`Isolate::Isolate()`（兜底）。构造函数内的 ensure 只覆盖首线程，无跨线程价值（未加）。

同轮验证 `_engine_alive`：`FreeEngine` 置 false 后 `NewEngine` 未复位 → 复用线程上 session 2 所有守卫跳过；已在 `NewEngine` 补 `this._engine_alive = true`。


### 2026-10-17：用户质询的三处"防御性补丁"改为根因修法

用户三条质询：① `Isolate::Isolate()` 难道不是每线程最早吗？中间层的 ensure 还有必要吗；② `_engine_alive` 要解决的是"engine 死掉后事件为什么还会调进来"，不是逐次判活；③ `_jsbb_.wasmop` 为什么要考虑未初始化的情况，`_jsbb_.init()` 不是已经初始化了吗。

**Q1 glue ensure 单点化（`init()` 顶部唯一一处）**

- 事实核对：`init()` 才是每线程最早的 JS 触点——worker 线程 `jsb_worker.cpp:362` 的 `impl::GlobalInitialize::init()` 先于 370 行的 `Environment` 创建；主线程 `GodotJSScriptLanguage` 构造（`jsb_script_language.cpp:168-172`）先 `init()` 再建 Environment；`Isolate::Isolate()` 永远更晚。
- 改动：删除三个 `_custom_print_*` 回调内与 `Isolate::Isolate()` 内的 `jsb_web_glue_ensure()`；`init()` 的 tls 分支里把 ensure 提到**第一条语句**（连它自己那行 `JSB_WEB_LOG` 都在其后）。全仓调用点只剩 `jsb_web_global_init.cpp:204`（定义在 `jsb_web_glue.cpp:36`）。
- 2026-10-16 那条"回调内也必须 ensure"的结论**作废**：当时失败的真正原因是我把 `init()` 体内那一处也一并删掉了。

**Q2 `_engine_alive` → 释放时注销钩子**

- 改动（`monolith.ts` 桥源码，重建后落到 `js/jsbb.impl.js`）：
  - `jsbb_Registry` 新增 `private _token = {}`，`Add()` 用 `this.watcher.register(obj, internal_data, this._token)`；新增 `Dispose()`（`unregister(this._token)` + `_count = 0`）。
  - `jsbb_Engine` 新增 `private _host_event_hooks: Array<[string, EventListener]> = []` 与 `_clear_host_event_hooks()`；`SetHostPromiseRejectionTracker` 改为**具名** `on_unhandled_rejection` / `on_error` 并登记；重复注册前先 `_clear_host_event_hooks()`（幂等）。
  - `Release()`：`_clear_host_event_hooks()`（成对 `removeEventListener`）→ `_registry.Dispose()` → 再置 undefined。
  - 删除 `static _engine_alive`、`NewEngine` 里的 `= true`、`FreeEngine` 里的 `= false`、finalizer 里的 `skipped gc callback for a released engine` 分支、两个监听器里的 `!self._stack || !_jsbb_._engine_alive` 判断。全仓 `_engine_alive` 计数 0。
- 语义收益：注销后回调**根本不会触发**，`NewEngine` 也无需再复位标志（原先漏复位会让复用线程的 session 2 守卫全跳过、worker 永不就绪——已随字段一同消失）。

**Q3 `wasmop` 取模块作用域**

- `UTF8ToString`/`stringToUTF8`/`lengthBytesUTF8`/`_malloc`/`_free` 本就来自 Emscripten 模块作用域，桥被 eval 时即已就绪，与 `init()` 无关。
- 改动：删除 `jsbb_wasmop()` 函数（含 `?? { UTF8ToString, ... }` 兜底）、`static wasmop` 字段、`init()` 里的赋值与 wasmop trace 循环；新增模块作用域 `const wasmop: WasmProtocol = { UTF8ToString, stringToUTF8, lengthBytesUTF8, _malloc, _free };`（行 1527），13 处 `jsbb_wasmop().` → `wasmop.`（634/644/645/707/724/1083/1114/1136/1147/1223/1326/1383/1653）。

验证（`.agent_tmp/`）：

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| bridge 重建（tsc） | `bridge_q123.log` | rc=0；`jsbb.impl.js` 含 `_token`/`Dispose`/`_host_event_hooks`，`_engine_alive` 计数 0 |
| web 构建 | `web_q123.log` | rc=0；`node` 校验 `wasm VALID` |
| web 腿 第 1 次 | `web_q123_1.log` | rc=0、`web-browser: PASS`、COMPLETED、FAILED=0 |
| web 腿 第 2 次 | `web_q123_2.log` | rc=0、`web-browser: PASS`、COMPLETED、FAILED=0 |
| node 构建 | `node_build_q123.log` | rc=0 |
| doctest | `doctest_q123.log` | 98|98、1280|1280 |
| host-node | `hostnode_q123.log` | rc=0、`host-node: PASS`、COMPLETED |

补充证据（被测字节 = 新构建字节）：`tests/bin/web-browser-export/godotjs-ext.web.template_debug.wasm32.wasm` 与 `project/addons/.../godotjs-ext.web.template_debug.wasm32.wasm` sha256 相同（`f1f5b0e1…`）；在导出 wasm 内可直接检索到新桥代码字符串 `_host_event_hooks`(7) / `_token`(6) / `unregister`(3)，旧守卫字符串 `skipped_gc` 计数 0——证明两次 PASS 跑的是本次改动后的产物，而非陈旧导出。


**cross-environment 路径确实被跑到（否则本轮验证不成立）**：harness 只在失败时打印 console tail，故再加一次带 `JSB_TEST_DUMP_TAIL=1` 的运行（`web_q123_dump.log`，rc=0、PASS）。导出页面的 console tail 显示：
- `[cross-environment-test] worker:session:1:*` 与 `worker:session:2:*` 全部 start/done（session 2 即线程池复用路径，本任务的核心场景）；
- 10+ 条 `[worker] [jsbb] Initialized GodotJS bridge`——每个 worker 线程各自完成一次按线程桥初始化（Q1 的单点 ensure 生效），无 `Cannot read properties of undefined (reading 'jsbi_init')`；
- `[worker] [jsbb] uncaught_error Error: peer throw` / `ErrorEvent` 与 `error-reporting:onmessage-throw`、`error-reporting:timer-throw` 均 start→done——正是 Q2 关心的"宿主 onerror"通道（具名监听器路径）。


### 2026-10-17：assert 根因定位（用户三问后的实测闭环）

用户的三个质疑全部成立，其中两条我改错了、已回退：

1. **Q2（`GodotJSEditorPreset::get_source` 是否混进 web template）**：**没有**。实测 web wasm：`jsb.runtime.bundle.js` 出现 1 次（runtime preset，本来就要），`jsb.editor.bundle.js` 出现 1 次但来自 `jsb_environment.cpp:462` 的 `kEditorBundleFile`（只注册空占位模块名），而 editor preset 独有的文件名 `tsconfig.json`/`jsconfig.json`/`jsb.signature.extract.cjs`/`.gdignore`/`jsb.ts.ast.cjs` **全部 0 次**；`SConstruct:1044` 只在 `target=editor` 时把 `editor_globs` 并入源列表，且 editor preset 的定义体另包在 `#if JSB_TOOLS`。构建日志里的出现仅 codegen 写文件。→ 上一轮把两个同形函数混谈是错的，`-Og` 的失败函数只是 **runtime** preset。
2. **Q1（`SetHostPromiseRejectionTracker` 被调用多次？）**：不会。唯一调用点 `jsb_environment.cpp:328`（`Environment` 构造），web 上 1 Environment ↔ 1 engine（`NewEngine` 拒绝第二个 engine）。我加的 `_clear_host_event_hooks()`（注释写"同一引擎重复注册"）是为不存在的场景防御 —— **已删除**；`Release()` 里的成对注销保留（那才是真正需要的：监听器挂在按线程常驻的 `globalThis` 上）。
3. **Q3（`Reset()` 里 `if (!is_alive()) return` 让 kStrong 断言失效）**：确实如此 —— 该提前返回使 `jsb_check(is_alive())` 恒真，且会跳过 `jsbi_handle_Reset` 造成 JS 句柄槽泄漏。对照 quickjs（`if (!isolate_) return;` + kStrong `jsb_check(is_alive())` + kWeak `if (is_alive())`）与 jsc（同入口、kStrong 无断言），该改法**已回退**为与 quickjs 同构。

回退后的实测：`web_assert_back.log` / `web_clean_run2.log` → `web-browser: FAIL`，`FATAL: Condition "!(is_alive())" is true. at Reset (jsb_web_handle.h:211)`，触发点是 `[cross-environment-test] worker:session:2:runFullRoundTrip:javaScript`（线程池复用线程）。即断言打的是真实缺陷，不是误报。

#### 缺陷根因（两次探针实测）

探针一（JS 侧，同 engine 内按 id 记录生命周期）：`DIAG slot missing 306 now_op= 51005496 ... history: [NEW op=51005496] [RM op=51005496]` —— 创建与断言在**同一 engine opaque**，306 建过、被移除过；排除跨会话。

探针二（C++ 侧属主环，进程级静态、pthread 共享）：

```
DIAG handle 306 tag C owner 81837020 self 5696896
DIAG handle 306 tag C owner 5696048  self 5696896
DIAG handle 306 tag C owner 78682988 self 5696896
DIAG handle 306 tag S owner 5696912  self 5696896
DIAG report id 306 self 5696896 hits 4
FATAL: Condition "!(is_alive())" is true.  (jsb_web_handle.h:211)
```

**同一个 handle id 306 被三个不同的 `Global` 各"新建"了一次**，而单线程 store 的 `jsbb_Handles._idgen` 严格单调 —— 唯一解释是**存在多个 store，各自从 0 编号**：`_jsbb_`（含 `jsbb_Handles`）是**按线程 JS global 各一份**，每个 engine 的句柄 id 空间独立。

叠加 `NewEngine` 里 `new jsbb_Engine(0, opaque)` 把 `EngineID` **硬编码为 0**，而 `_jsbb_.GetEngine(engine_id)` 只在**当前线程的 `_jsbb_`** 里解析 —— 于是**线程 A 铸造的句柄 id 会被拿到线程 B 的 store 里解析**。id 空间各自从 0 起，必然撞号（306 在多个 store 里都存在），后果：
- 别的线程的 `Global` 做 `Reset()` 会**误删本线程无关的句柄槽**（探针里的 `tag S` 就是这次误删）；
- 随后真正的属主再 `Reset()` 时槽已不在 → `is_alive()` false → `jsb_check` FATAL。

这同时解释了为什么只在 cross-environment（跨线程传对象/句柄）出现、以及为什么 id 稳定复现为 306。

#### 待定修法（涉及 shim 语义，需用户批准）

- (a) **让句柄带上所属 engine 身份**：C++ 侧 `Isolate` 记录 engine opaque，`jsbi_handle_*` 传该 opaque；与当前线程 engine opaque 不符时按"异线程/异 engine 句柄"明确报错而不是在本 store 里查 id。
- (b) **句柄 id 进程级唯一**：把 id 生成器移到模块级（所有线程/engine 共用递增），彻底消除撞号；单独用 (b) 只能把"误删"降级为"查不到"（断言仍会响），需与 (a) 配合。
- (c) **从生命周期上禁止 `Global` 跨线程存活/使用**（最接近 v8 语义，但改动最大）。

当前状态：`Reset()` 已还原（断言在）、Q1 的防御已删、全部临时探针（JS `_trace`/C++ `handle_diag`）已移除，web 产物为干净重建（`wasm VALID`），web 腿因该真实缺陷仍 FAIL；native 三腿未受影响。


### 2026-10-17（续）：判别实验 —— 是**跨线程**，不是同线程跨会话

用 TLS 变量地址作线程标识、把 isolate 指针一起记入属主环（`web_diag8_run.log`）：

```
DIAG handle 306 tag C owner 81872476 thread 73384996 isolate 76377600 self 5696912
DIAG handle 306 tag C owner 5696048  thread 11788972 isolate 51086600 self 5696912
DIAG handle 306 tag C owner 78807692 thread 51167460 isolate 67412328 self 5696912
DIAG handle 306 tag S owner 5696896  thread 11788972 isolate 67412328 self 5696912   ← 异线程 isolate
DIAG report id 306 self 5696912 thread 11788972 hits 4 pos 271
FATAL: Condition "!(is_alive())" is true. (jsb_web_handle.h kStrong Reset)
```

结论：
- id 306 由**三个不同线程**各自在自己的 store 里铸造（每个 realm 的 `jsbb_Handles._idgen` 从 0 起且单调）。
- 断言发生在 thread 11788972；而**删除 306 的那次 `Reset()` 所携带的 `isolate` 是 67412328 —— 属于 thread 51167460**。
- 即：**一个属于别线程的 `Global` 在本线程上执行了 `Reset()`**。web 侧 `NewEngine` 把 `EngineID` 硬编码为 0，`_jsbb_.GetEngine(0)` 只在**当前 realm**解析 → 别线程的句柄 id 落到本线程 store，**误删与本线程无关的句柄**；随后本线程真正的属主（self 5696912）再 `Reset()` 时槽已不存在 → `jsb_check(is_alive())` FATAL。

即根因 = **C++ 侧跨线程使用/释放 `Global`（其 `isolate_` 属于别的线程）** × **web shim 的 engine 身份被抹平成常量 0、按 realm 解析句柄 id**。不是进程问题；一个页面一份线性内存，线程由 Web Worker 模拟，每线程一个 realm（导出预设 `threads/emscripten_pool_size=8`）。

清理：属主环与全部探针已移除（`grep handle_diag|DIAG` 于 web 源为空），web 产物干净重建（`wasm VALID`）。web 腿因该真实缺陷仍 FAIL；native 三腿未受影响。


### 2026-10-17：根因修复落地（按批准顺序 1 → 4 → 2，不做 3）

**步骤 1（a）：engine 身份 = isolate opaque**（`bridge/src/monolith.ts`）
- `NewEngine(opaque)` 返回 `opaque` 作 engine id（不再返回常量 0），`jsbb_Engine._id = opaque`；
  `FreeEngine`/`GetEngine` 改走 `_owns(id)`：`(this.engine.opaque >>> 0) === (id >>> 0)`，不匹配则打印
  `engine id does not belong to this realm <id> current <opaque>` 并返回 undefined。
- `js/ library_godotjs_jsbi.js`：句柄类入口（`jsbi_handle_IsValid/ClearWeak/SetWeak/Reset/New`）先确认 engine 归属，
  异 engine 时退化为安全空操作（`IsValid`→false、`New`→-1），**不再落到本 realm 的 store 上误删无关句柄**。
- 效果（实测）：同一处误用从"静默删错句柄 + 若干帧后才炸"变为"当场可见 + 精确到 isolate"。

**步骤 4：定位并修复跨线程释放点**（`src/runtime/bridge/jsb_environment.{h,cpp}`）
- 定位手段：`debug_symbols=yes` 构建 + 在异 engine 报错处抓 `new Error().stack`，栈直接点名：
  ```
  v8::Global<v8::Object>::Reset()
   ← jsb::Environment::free_object(void*, jsb::FinalizationType)
   ← jsb::InstanceBindingCallbacks::free_callback(void*, void*, void*)
   ← Godot 引擎（主模块）释放实例绑定
  ```
- 机制：`free_callback` 的 `p_token` 是**创建绑定的 env**（可能是 worker env），但它在**调用线程**（本次是主线程）
  上直接跑 `env->free_object()`，于是该 env 的 `ObjectHandle::ref_`（v8 Global，属 worker isolate）在**主 realm** 里被 Reset。
  改 (a) 之前：主 realm 的 handle id 恰好撞号 → 静默误删 → session 2 真属主再 Reset 时槽已空 → `jsb_check(is_alive())` FATAL。
- 修法（F1）：把 `free_object()` 里"碰 JS"的那一步从调用线程拆出去：
  - 新增异步类型 `AsyncCall::TYPE_RELEASE_JS_REF`，payload 是堆上的 `v8::Global<v8::Object>`；
  - `free_object()` 仍**立即**在本线程做 native 摘除（`remove_object_binding`，满足"指针必须立刻摘掉、不能投递"的原注释），
    但把 `ref_` move 到堆对象后交给新增的 `Environment::_release_js_ref()`：同线程走 `add_async_call` 内联执行，
    跨线程入队、由**属主 env 自己的线程**消费（`exec_async_call` 新 case 里 `Reset()` + `memdelete`）；
  - 属主 env 正在销毁 / 无处投递时（`is_disposing()`）：既不 Reset 也不析构（析构即 Reset），存入 `orphan_js_refs_`
    随 realm 一起抛弃（不构成可用 JS 引用的泄漏）。
  - 语义与顺序与原来一致（强引用仍活到本函数结束、仍在 `remove_object_binding` 之后再释放）。

**步骤 2：守卫边界（realm 级 vs pthread 级）**
- JS `_jsbb_.init()`：`interop` 已安装时**直接返回**（原来是打 `Already initialized, do not call it twice` 然后覆盖）。
  realm（线程的 JS global）会被池化 worker 复用，而 C++ 回调指针在同一 wasm 实例内不变，重复安装即 no-op；
  realm 级守卫就是 `this.interop`（另有 `jsb_web_glue_setup` 的 `__jsb_web_glue_ready`）。
- C++ `GlobalInitialize::init()`：保留 `tls_js_bridge_initialized` 作为 **pthread 级快速路径**，并把两套生命周期的
  边界写进注释（pthread 反复创建销毁 ↔ realm 池化复用；正确性由 realm 级幂等保证）。
- 未做（按你的决定）：不把 `GlobalInitialize::init()` 挪回 `Environment::Environment()`。

验证（`.agent_tmp/`）：
| 验证 | 日志 | 结果 |
| --- | --- | --- |
| 步骤1 诊断生效 | `web_id_run.log` | `engine id does not belong to this realm 58008440 current 51006016`（拦下误用） |
| 根因点名 | `web_st_run.log` | 符号化栈：`Global::Reset ← Environment::free_object ← InstanceBindingCallbacks::free_callback` |
| F1 后 web | `web_f1_run.log` | PASS，COMPLETED，sentinel 0 |
| 清理诊断后 web ×2 | `web_f1_clean1/2.log` | 两次 PASS |
| 步骤2 后 web ×2 | `web_s2_1/2.log` | 两次 PASS |
| native | `node_f1.log` / doctest / host-node | 见下节 |

当前：`Reset()` 断言保持在位（未被削弱）；诊断探针（JS `_engine_history`/栈、C++ isolate 日志）已全部移除；
产物为默认（无 `debug_symbols`）构建且 `wasm VALID`。


#### F1 修正（v2）：只在跨线程时走新路径

第一版把 `free_object()` 里的 `ref_` **无条件**搬到堆对象再延迟释放 —— native 立刻回归：
```
# Check failed: Handle not reset in first callback. See comments on |v8::WeakCallbackInfo|.
[doctest] test cases: 15 | 14 passed | 1 failed   （run 提前崩）
host-node: FAIL（Environment 析构链上崩溃）
```
原因：v8 要求**弱回调第一遍必须在原句柄上 Reset**；把带弱注册的句柄搬走后，`object_gc_callback` 里的
`object_handle->ref_.Reset()` 变成空操作 → 触发 v8 的检查。

v2（当前实现，`jsb_environment.cpp:1146+`）：
```cpp
	v8::Global<v8::Object> *detached_ref = nullptr;
	if (is_caller_thread()) {
		object_handle->ref_.Reset();                 // 同线程：与原实现完全一致（native 零回归）
	} else {
		if (object_handle->ref_.IsWeak()) {
			object_handle->ref_.ClearWeak();          // 先摘弱回调，保住 v8 的"第一遍必须 Reset"约定
		}
		detached_ref = memnew(v8::Global<v8::Object>(std::move(object_handle->ref_)));
	}
	...
	remove_object_binding(object_handle, p_pointer);  // 仍然立即摘除（不改）
	...
	if (detached_ref != nullptr) {
		_release_js_ref(detached_ref);                // 跨线程：投递回属主 env 的线程释放
	}
```
`_release_js_ref()`：`is_disposing()` → 存入 `orphan_js_refs_`（既不 Reset 也不析构，随 realm 抛弃）；
否则 `add_async_call(TYPE_RELEASE_JS_REF, payload)`（同线程内联、跨线程入队），由 `exec_async_call` 新 case
在属主线程上 `Reset()` + `memdelete`。

最终验证（本轮全部实测）：
| 验证 | 日志 | 结果 |
| --- | --- | --- |
| web（F1 v2） | `web_f1v2_run.log` | PASS |
| web 最终 ×2 | `web_final1/2.log` | 两次 PASS（COMPLETED、sentinel 0） |
| native 构建 | `node_f1v2.log` | rc=0 |
| doctest | `doctest_f1v2.log` | 98\|98 passed、1280\|1280 |
| host-node | `hostnode_f1v2.log` | PASS |
| 残留 | — | web 源内 `DIAG` 计数 0；产物默认构建且 `wasm VALID` |

未做：步骤 3（不把 `GlobalInitialize::init()` 挪回 Environment 构造）——按你的决定。


### 2026-10-17：A/B 判定 —— 上游"engine id 硬编码 0"为什么本来就能跑多线程 worker

问题：上游（`.agent_tmp/upstream-godotjs/impl/web/bridge/src/monolith.ts:1513`）就是 `new jsbb_Engine(0, opaque)` / `GetEngine(0) → this.engine`，
凭什么 web 的 worker（多线程）也能用？

**机制**：`0` 不是全局身份，而是"**本 realm 的那一个 engine**"的地址。
- 每个线程一个 JS realm（worker 一个 Web Worker = 一个 `globalThis`），因此各有各的 `_jsbb_`，各有各的 `jsbb_Engine`；
  `NewEngine` 返回 0 → C++ 存进 `Isolate::rt_` → 之后所有 `jsbi_*(engine_id=0, ...)` 都在**当前 realm** 的 `_jsbb_` 里解析。
- 所以"只支持一个 engine"是"**一个 realm 同时一个 engine**"，不是"一个进程/应用一个"；多线程 worker 由"每线程各自一个 realm"天然满足。
- 它能成立的前提是一个不变量：**一个 `Isolate`（以及它的 `Global`/句柄）只在创建它的线程/realm 上使用** —— 这本来就是 v8 对 `Global` 的语义要求。
  上游并没有"支持跨线程句柄"，它只是**不违反**这个不变量。

**本次缺陷正是对这个不变量的违反**：`InstanceBindingCallbacks::free_callback` 的 `p_token` 是创建绑定的 env（worker env），
却在**调用线程（主线程）**上直接跑 `env->free_object()` → worker isolate 的 `Global` 在 main realm 里被 Reset。
因为 id 恒为 0、按 realm 解析，这种违反**无法被上游发现**，只会静默命中本 realm 的同号句柄并把它删掉（→ 后续 session 2 真属主 Reset 时槽已空 → `jsb_check(is_alive())` FATAL）。

**A/B 实测**（把 (a) 退回上游 0 形态、保留 F1，`web_ab_run.log`）：

| 组合 | web 腿 |
| --- | --- |
| 上游 id=0 + F1 | **PASS**（`web_ab_run.log`，COMPLETED、sentinel 0） |
| 上游 id=0 + 无 F1（原始代码） | FAIL（`jsb_web_handle.h:211` FATAL） |
| (a) id=opaque + F1 | PASS（`web_final1/2.log`） |

⇒ **F1 是修正确性的那一改；(a) 只是"让违反当场可见"的检测器/护栏**（把"静默删错句柄"变成"报错 + 拒绝操作"），
不是多线程能用的前提。是否保留 (a) 属于"要不要这道护栏"的取舍，待定。


### 2026-10-17（终）：撤销 F1，跨线程释放在 web shim 内部解决

决定（用户）：**撤掉 bridge 里的 F1**，非 web 后端保持最小改动；跨线程释放问题在 web shim 里解决。

实现（全部落在 `src/runtime/impl/web/`）：
- `jsb_web_broker.{h,cpp}`：新增 `owns_current_thread(isolate)` / `proxy_release_handle(isolate, value, weak)` 两个转发。
  （放在 `Broker` 的原因：`jsb_web_handle.h` 里 `Isolate` 是不完整类型，`Reset()` 不能直接调它的成员。）
- `jsb_web_isolate.{h,cpp}`：`Isolate` 构造时记 `owner_thread_ = pthread_self()` 并创建 `em_proxying_queue`；
  `owns_current_thread()` = `pthread_equal(owner_thread_, pthread_self())`；
  `proxy_release_handle()` 分配 payload(`isolate/handle/weak`) 后 `emscripten_proxy_async(queue, owner_thread_, thunk, payload)`
  （投递失败＝属主线程已退出，直接丢弃：该 realm 已随线程消失）；
  `_release_handle_thunk()` 在**属主线程**上执行 `SetWeakCallback(nullptr)`（若原为弱句柄）+ `jsbi_handle_Reset(get_engine(isolate), handle)`；
  `_release()` 里销毁队列。
- `jsb_web_handle.h :: Global::Reset()`：入口先判归属；跨线程 → 投递 + `_clear()` 返回；同线程 → 原路径（含 `jsb_check(is_alive())`，语义未改）。
- bridge 侧 `jsb_environment.{h,cpp}` 已回到上游（剩余 diff 只有本轮之前就存在的 `forward_error_to_master` 与 deleter `#if` 调整）。

验证（探针全部临时、已移除；这两个点上 `JSB_WEB_LOG(Error/Warning, …)` 在日志里看不到，故改用 `jsb_checkf` 硬失败取证）：

| 验证 | 日志 | 结果 |
| --- | --- | --- |
| 该场景是否确定性发生、且被识别 | `web_hard_run.log:1643` | 分支内置 `jsb_checkf(false)` → 命中 `DIAG cross-thread Reset reached` |
| 旧路径是否必崩 | `web_off1/2/3.log` | 强制 `owns_current_thread()==true` → **3/3 FAIL**（`jsb_check(is_alive())` FATAL），每次 `would-defer` 2 次 |
| thunk 的执行线程 | `web_thunk_run.log:1641` | `DIAG thunk executed on owner thread (self=76302688 owner=76302688)` ⇒ **属主线程** |
| 干净构建 web ×2 | `web_clean_s1/2.log` | 两次 PASS（COMPLETED、sentinel 0） |
| native 回归 | `node_shim.log` / `doctest_shim.log` / `hostnode_s.log` | rc=0、98\|98+1280\|1280、host-node PASS |

**调试注意点（记录）**：在 `Global::Reset()` / `Broker::owns_current_thread()` 这两处，`JSB_WEB_LOG(Error/Warning, …)` 的运行结果**不会出现在抓取的日志里**（该时刻 logger 已不可用），只有 `jsb_checkf`/FATAL 可见 —— 排查同类问题时，不能用"日志没出现"推断"代码没执行"。
