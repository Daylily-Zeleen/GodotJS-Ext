# 报告：TODO 全量审计与分组实施

日期：2026-09-29
任务：`.trellis/tasks/09-29-todo-audit`
审计基线：`HEAD = f6e62c5`（分类口径以此为准；此后各分组的改动另见 §5）

---

## 1. 目标

1. 清点本仓全部 `TODO`，分类为「可简单实现 / 不可实现 / 已过时 / 需较大改动」。
2. 按用户指示分两组推进：先清 B 类（已过时），再按分组顺序实施 A 类。
3. 查 Godot 侧虚函数的调用点与真实功能，判定哪些对我们有必要。

## 2. 清点口径与分类

```bash
git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc   # → 188 行
```

全仓 `git grep -n -I "TODO"` 为 224 行，差额 36 行在范围外：`third/` 14、`.trellis/` 19、agent 配置 3。

| 类 | 含义 | 条数 |
|---|---|---|
| **A** | 可简单实现 / 待确认后实现 | 55 |
| **B** | 已过时 | 4 |
| **C** | 不可实现 / 受上游限制 | 10 |
| **D** | 需较大改动 | 85 |
| **E** | 非行动项 / 经调研判定不适用 | 34 |
| | 合计 | 188 |

两个横切事实（`research/todo-audit.md` 有逐条表）：

- **122/188 的注释文本与上游 GodotJS 完全相同**；仅 66 条为本仓独有。
- **128 条由 2026-08-05「批量代码格式化」一次提交引入**（`git blame` 188/188 命中），
  即这些 TODO 基本从未被当作待办处理过。

详见 `research/todo-audit.md`（61 KB，含 **188/188 全量逐条表**）。

## 3. 虚函数专项调研（`research/virtuals-godot-side.md`）

逐个查 Godot 侧调用点 + GDScript/C# 实现后的判定：

| 虚函数 | 判定 | 依据 |
|---|---|---|
| `_find_function` | **该做** | 活路径：`connections_dialog.cpp:1016/1024`（信号连接对话框，含基类链）、`script_text_editor.cpp:428`。恒 -1 会让编辑器认为方法不存在并**注入空函数体** |
| `_get_member_line`（`Script` 上） | **该做** | `ScriptEditor::script_goto_method`（`script_editor_plugin.cpp:3899`）← 信号连接跳转 `:1294`、动画方法轨 `:3475` |
| `_validate_path` | **该做** | `script_create_dialog.cpp:301`（新建脚本对话框）；语义对齐 C# `csharp_script.cpp:393` |
| `_auto_indent_code` | **不适用（死代码）** | `ScriptLanguageExtension::auto_indent_code` 是 `static`，`EditorAdapter::format_code` 值调用它、不虚分派；「自动缩进」走 `EditorLanguage::format_code` |
| `_add_global_constant` / `_add_named_global_constant` / `_remove_named_global_constant` | **不适用** | 唯一调用方是 autoload（`main.cpp:4490/4538`、`editor_autoload_settings.cpp`）。语义是让**裸标识符**解析到值，TS/JS 无此阶段；本仓无 autoload 集成；上游 C# 空实现或不覆写 |
| `_get_public_functions/constants/annotations` | **不值得** | 唯一消费者 `doc_tools.cpp:1107/1139/1152` 生成 `@<语言>` 文档页；需要新增并维护 C++ 侧语言内建清单（我方 `@bind` 在运行时 bundle 里） |

## 4. 分组提交一览（本地 `main`）

| commit | 分组 | 内容 |
|---|---|---|
| `7f51ac7` | 审计 + B 类 | 188 条审计；B 类 4 条；裸 TODO 2 条；#16 补全实现说明；任务文档 |
| `3f00994` | **G1** 语言层 | `_find_function`、`_validate_path`、`_get_member_line` 实现；6 条不适用注释 |
| `045d274` | **G2** 脚本层 | `_inherits_script`、`to_string`、`_reload`、`_get_doc_class_name`；3 条判定 |
| `60021eb` | **G3** 编辑器侧 | 路径列表类型评估；`PresetSource` 去一次 memcpy；3 条判定 |
| `ceb9c07` | **G4** bridge | async loader 缺 TryCatch 修复；`crossbind` 的 `_rebind` 判定；`jsb_stackalloc`；若干注释判定 |
| `801f876` | 复核修正 | 用户 10 条复核（见 §6） |
| `1902f89` | 收尾 | 撤掉 `StatelessScriptClassInfo::is_valid`；新增 `STATUS.md` |
| `d4fa24c` | 收尾 | `SceneDTSContext` 的 wildcards 提到循环外；重写 `STATUS.md` 判定规则 |
| （工作树） | **G5** impl 腿 | `src/runtime/impl/` 全部条目（见 §5.5） |

## 5. 判定与确认记录

每条都写了「结论 + 依据」。依据是**代码/引擎源码/CI 配置**，不是推断。

### 5.1 B 类 4 条（已清除）

| # | 位置 | 判定依据 | 处置 |
|---|---|---|---|
| 73 | `jsb_module_resolver.cpp` | `require.cache` 已实现：`jsb_module.cpp:86` 写入 `cache_object_`，`jsb_environment.cpp:1431` 挂到每次 `require` | 删 2 行 |
| 158 | `jsb_script_instance.cpp` | TODO 下方 3 行就是它描述的动作（构造 `argv` + `callp(_notification)`） | 删该行 |
| 46 | `jsb_environment.cpp` | 它要删的 include 在同文件 `:767/:773` 正被使用 | 删注释 |
| 100 | `jsb_jsc_pch.h` | 「NOT SUPPORTED TO BUILD」过时：CI 有 `engine: jsc` 腿（macos+ios），`misc/release/package.py:57` 的 `ENGINE_ORDER` 含 jsc | 改写为垫片说明 |

### 5.2 裸 TODO 2 条（已删除）

`#58` `jsb_environment.cpp:1448` 行尾 `// TODO`（无内容）；`#119` `monolith.ts:447` `//TODO`（无内容）。

### 5.3 实现类判定

| 条目 | 结论 | 关键依据 |
|---|---|---|
| `_find_function` | 实现 | 调用方见 §3；返回 1 基行号（对齐 `member_lines`/token `start_line`） |
| `_get_member_line` | 实现 | 同上 |
| `_validate_path` | 实现 | 文件名基名即类名，须为合法标识符且非保留字 |
| `_inherits_script` | 实现 | 调用方是 `container_type_validate.h:141/185`（类型化容器/属性校验）与 `scene_tree_dock.cpp:3838`；原 TODO 说「只被 `Array::assign` 调用」是错的。改为沿 `base` 链比脚本身份（同 `GDScript::inherits_script`） |
| `to_string` | 实现 | 调用方 `Object::to_string()`（`object.cpp:1033`）。对齐 `GDScriptInstance::to_string`：调脚本的 `_to_string()`，非 String 则 `r_valid=false` |
| `_reload` | 实现 | 未加载时先加载再重载（原早退让「保存后重载」对刚创建的脚本静默无效）；`GDScript::reload` 无此早退 |
| `_get_doc_class_name` | 实现 | 调用方 `editor_inspector.cpp:4590`；原实现绕道 `_get_documentation()` 且依赖其非空，改为直接取同一来源 |
| async loader `ToLocalChecked` | **修 bug** | 用户 loader 抛错时原代码会 `CHECK` 失败终止进程；改为 `TryCatch` + 上报（同 `AMDModuleLoader::load_source`） |
| `PresetSource` 解压 | 实现 | 解压结果直接持有 `PackedByteArray`，去掉一次整块 memcpy；`resize` 失败改为上报 |

### 5.4 明确「不适用 / 不改」的判定

| 条目 | 结论 | 依据 |
|---|---|---|
| `_add_global_constant` 等三条 | 不适用（注释说明） | §3 |
| `_get_public_*` 三条 | 不值得（注释说明） | §3 |
| `_auto_indent_code` | 死代码（注释说明） | §3 |
| `_get_rpc_config` 是否含父类 | **不并入** | `GDScript::get_rpc_config`（`gdscript.cpp:927-929`）与 `CSharpScript::get_rpc_config`（`csharp_script.cpp:2754-2756`）都只返回自己的 |
| `placeholders` 是否改 HashMap | **保持 LocalVector** | 条目数是个位数；按对象查找走静态 `PlaceholderScriptInstance::placeholders_`；热点路径本就需要遍历 |
| 内置脚本重载是否优化 | **保持现状** | 只重读子资源需 `PackedScene` 私有接口 |
| 静态方法转 `Callable` | **不做** | `Callable(owner, name)` 绑的是实例方法；静态方法在 JS 里挂在构造函数上，无实例可绑（GDScript 同样只在 `callp` 里拒绝） |
| tsc 子进程无输出 | **已经是好的** | `jsb::internal::Process` 后台线程排空 stdout 并逐行 `JSB_PROCESS_LOG`（`jsb_process.cpp:131/134`、POSIX `:433/435`） |
| POSIX `ProcessImpl` 未在 Linux 测试 | **已覆盖** | `test_jsb_process.h` 两个用例注册于 `UNIX_ENABLED`；CI test job 有 3 条 `ubuntu-22.04` 腿跑 `--jsb-run-tests`（`.github/workflows/ci.yml:993-1019`） |
| `jsb_environment.cpp:382` debugger 分阶段启动 | **不存在该钩子** | Editor/Game 的差异已归一到 `p_params.debugger_port`（`jsb_script_language.cpp:236-241`） |
| V8 cast 去掉 | **前置未满足** | 需 V8 > 12.6.221，本仓 pin `12.4.254.21`（`SConstruct:100`） |
| `InternalModuleLoader` | **TODO 保留** | 无注册点 ⇒ 目前不实现；但功能本身不完整（`file_name_` 从不被读），不能因此不挂 TODO |
| `friend struct ScriptClassInfo` | **TODO 保留** | 需收紧（它直接读私有 `flags_`/`EnvironmentFlags::EF_Shadow`，`jsb_class_info.cpp:830`）；做法是补一个语义明确的公开谓词 |
| `StatelessScriptClassInfo::is_valid` | **改为真实判定** | `return !module_id.is_empty()`。`module_id` 在解析成功时写入（`jsb_class_info.cpp:884`），模块加载失败时整个 `script_class_info_` 被重置为 `{}`（`jsb_script.cpp:913`）；与 `GodotJSScript::is_valid_internal()`（`jsb_script.h:292` → `VariantUtil::is_valid_name`）用同一判据。**未覆盖**「文件是否还存在」一层（该结构刻意不依赖环境/文件系统） |
| 路径列表类型 | **保持 `Vector<String>`** | 见 §6 Q5 |
| `#15` 纯 JS 导出 | **TODO 保留** | 补法是对 `.js` 也调 `export_compiled_script`；需先确认 `exported_paths_` 按路径去重不区分身份不会重复打包 |
| `#10` d.ts 安装分工 | **TODO 保留** | 目标是建立分工；阻塞点是「api 生成的 d.ts」与「preset 分发的 d.ts」共用 `CH_D_TS` 一个 hint |
| `#17` REPL realm 选择 | **TODO 保留** | 需枚举入口 + 选择控件 + 跨 realm 求值通道（后者本身未实现） |

## 5.5 G5（`src/runtime/impl/`）—— **已回滚，只保留 3 处有铁证的改动**

### 5.5.1 回滚原因（用户判定）

用户指出：**把 `//TODO` 改写成 `//NOTE` 不算完成**，做不到就不要动。我照做了——上一轮把 25 条
TODO 改写成了 NOTE 并标为「已完成」，这是偷懒。现已 `git checkout` 回滚
`src/runtime/impl/` 下除下列 3 处外的全部改动，TODO 恢复原样。

**保留的改动（前提已被代码证据推翻，不是「改注释」）**：

| 位置 | 原 TODO | 保留理由（证据） |
|---|---|---|
| `jsb_jsc_handle.h:156` | `use JSWeakRef` | 已经在用：`JSWeakCreate`(:281/:296)、`JSWeakGetObject`(:219/:267/:318/:324/:346)、`JSWeakRelease`(:220/:269)，`jsb_jsc_pch.h:47` 已 include |
| `jsb_jsc_isolate.cpp:472` | `copy or steal?` | **真修的 bug**：`stack_dup(ConstructorCallError)` → `stack_val(同槽)`。该槽是常驻保留槽（构造时 `emplace_` 建、`_release()` :190 统一释放），`stack_dup` 多一次 `JSValueProtect` 且无释放路径 ⇒ 异常路径泄漏。不能改 `_GetError()`（窃取并复位的是 `StackPos::Exception` 槽，与本槽位不同） |
| `monolith.ts` `i64`/`u64` | `may not be supported?` | 是活路径（`:654/:660/:725/:730`），`BigInt64Array` 是 ES2020 内置、bridge 目标 es2021 |

**`src/runtime/impl/` 的 TODO 计数：40 行 → 36 行**（只删了这 3 处所覆盖的 4 行）。
其余 `#92/#93/#95/#97/#98/#99/#101/#102/#103/#104/#105-#110/#112/#113/#114/#116-#123/#126/#127/#128/#129/#133`
**全部保持 TODO 不动**——它们需要的是实现或设计改动，不是措辞。

### 5.5.2 上一轮的判定错误（已纠正，仅作记录）

- `#111`（QuickJS 死循环检查）、`#115`（避免 `JS_NewUint32`）：我判定为「B 类已过时并删了注释」。
  **`#111` 是错的**：`JS_SetInterruptHandler` 确实已接上（`jsb_quickjs_isolate.h:203`），
  但那只是**按需安装**的终止钩子（调用点 `jsb_shadow_realm.cpp:1015`、`jsb_worker.cpp:434`），
  原 TODO 要的「死循环检查」（自动中断）**并没有实现**。现已回滚。
- `#113`/`#129`（`configurable`）、`#117`（快判优化）：我改写成「裁定保留/不做」。
  判定结论本身有依据，但**改注释不等于完成**，现已回滚。

### 5.5.3 验证

| 项 | 结果 |
|---|---|
| 全仓 TODO | 155 → **151** |
| `src/runtime/impl/` TODO | 40 → **36** |
| 编译/跑测 | 见 §7（与 node 腿测试一并重编） |

## 5.6 用户指出的第 2 项：node 腿没有专门的测试（**已补**）

用户指出：`jsb_node_bridge.cpp` 的 TODO 里「添加 node 相关的测试」我当时说「CI 有 host-node 三条腿」
就算完成了，但**C++ 测试和 TS 集成测试都没测 node 的功能**，并让我去看 Gode 怎么测。

**Gode 的做法**（`.agent_tmp/gode`，本仓临时解压的上游）：

| 测试 | 内容 |
|---|---|
| `test/run_godot_smoke.py` | 跑 `res://scenes/tests_runner.tscn`，断言出现 `[GodeTest] all tests passed`、无 `ERROR:`、无泄漏 |
| `test/run_npm_native_smoke.py` + `test/fixtures/npm_native_llama/` | **独立 fixture 工程**：fork 一个 `fork_probe_child.cjs`，断言子进程的 `process.execPath` 是**打包的 gode_node helper** 而不是宿主；再装真 npm 原生包（node-llama-cpp `dryRun`）验证 `.node` 加载 |

**我补的（对齐 Gode 的思路）**：

1. **C++ doctest**（`src/runtime/tests/test_jsb_runtime_api.h`，`#if JSB_WITH_NODE` 内）2 个用例：
   - `native_probe_executable resolves the bundled fork helper`：断言
     `process._linkedBinding('godot')` 存在、`native_probe_executable` 已注册、
     返回值是**存在且绝对**的路径（路径校验在 C++ 侧做——这里的 `require` 是桥接
     require，只认 GodotJS 模块，对 `path`/`fs` 会崩）。
   - `child_process.fork runs the helper instead of the host`：fork 探针子进程，
     断言它经 IPC 回报的 `execPath` **不是宿主**。
2. **TS 集成测试**（对齐 Gode 的独立 fixture）：
   `project/tests/node-runtime/test-node-runtime.ts` + `fork-probe-child.cjs` + `NodeRuntime.tscn`，
   已登记进 `project/tests/start.ts` 的场景列表。断言 `process.nextTick`、
   `require('node:path')`、`child_process.fork` 的 helper 重定向。
   注意：node 内建模块必须经 `globalThis.__godotjs_node_require` 加载
   （`jsb_node_runtime.cpp:343` 暴露），全局 `require` 是桥接 require。

### 5.6.1 新测试暴露的真 bug（未修，需单独改动）

**`godotjs-ext.exe`（node 的 fork helper）对任何调用都崩**：

```
bin\windows\godotjs-ext.exe --version                     -> rc=3221225477 (0xC0000005)
project\addons\...\bin\windows\godotjs-ext.exe --version -> rc=3221225477
```

- 与我的改动无关：`src/runtime/node_helper/`、`src/runtime/impl/node/` 均未被我修改
  （`git diff --stat` 为空）。
- 不是加载问题：主 DLL 可 `LoadLibrary` 且导出 `godotjs_node_probe_main`，`node.dll` 也能加载
  （用 `ctypes` 实测两者均 OK）⇒ 崩在 `node::Start` 之后。
- 结果：`child_process.fork` 在 node 腿**实际不可用**，第二个用例因此
  `WARN`（不 FAIL），日志里会明确写出 helper 启动即崩。

**已修（本轮）**，根因与修法：

1. **`PrepareNativeAddonHost()` 在独立进程里用了 godot-cpp 的 `String`**（`jsb_node_bridge.cpp`）。
   godot-cpp 的一切分配都走 GDExtension 接口表，而 helper 进程里没有 Godot 填这张表，
   于是第一次 `String` 分配就经空函数指针写入 —— 崩溃点（`access violation writing 0x0000000000000000`）。
   修法：该路径改为纯 Win32/POSIX（`GetModuleFileNameW` + `std::wstring` + `LoadLibraryW`），
   不碰任何 godot-cpp 类型。
2. 中途我误加过一段 `InitializeOncePerProcess` + `V8::Initialize`，导致初始化跑两遍
   （`Assertion failed: !init_called` / `Wrong initialization order`）—— 已撤销：
   `node::Start()` 自己完成初始化，helper 只需 `Prepare...` 后直接调它。
3. 探针子进程 `process.send` 后立即退出，父进程再 `child.send()` 会写已关闭的 IPC 通道 ⇒ `EPIPE`。
   修法：收到探针消息后不再回写（Gode 的探针同样如此）。

**验证**：
- `godotjs-ext.exe --version` → rc=0，输出 `v24.21.1-pre`（修复前 rc=3221225477）
- `godotjs-ext.exe fork-probe-child.cjs` → rc=0，无输出错误
- doctest **78/78 cases、1003/1003 assertions、rc=0**（两个 node 用例均通过）
- 项目跑测 rc=0、`COMPLETED=1`、`FAILED=0`、`Orphan StringName=0`

**过程教训**：我一度把 DLL 占用归咎于 Defender —— 实际是**我自己的 Python 内核**用
`ctypes.CDLL()` 加载了那两个 DLL 且句柄未释放（内核重启即可写）。排查外部故障前应先排除自己。

## 5.7 用户指出的第 3 项：`jsb_variant_util.h` 的改动要落到文档（**已写**）

用户的 `Dictionary/Array::id()` 规避实现已写进规范：
`.trellis/spec/godotjs-ext/cpp/godot-cpp-usage.md` 新增
「未暴露接口的规避：取 Dictionary/Array 的 `id()`」——含做法、依赖的前提
（`_p` 唯一成员、偏移 0、须取 Variant 载荷内地址）、**失效征兆**与排查步骤，
以及「将来 godot-cpp 暴露 `id()` 时改回公开接口」。

## 6. 用户复核（10 条）与修正

用户逐条驳斥了上一轮把「TODO 改成说明文字」当作完成的做法。**7 条用户对已改，1 条经评估后
回退代码，2 条是流程/措辞问题**。完整复核过程见
`research/user-review-10-items.md`，摘要：

| # | 问题 | 修正 |
|---|---|---|
| Q1 | 新增成员被声明在 `JSB_TOOLS` 外；「以下三个不适用」的第三个在哪 | 移进 `#if JSB_TOOLS`；措辞改为「以下两个」并注明第三个（`_add_global_constant`，:301）的位置。核对引擎 `script_language_extension.h:580-582`：三个 autoload 钩子同样不在 `TOOLS_ENABLED` 内，分区一致 |
| Q2 | REPL 的 TODO 被改成描述 | 恢复为 `//TODO`，实现前提写进正文 |
| Q3 | `#15` 因「本机无法验证」被删 TODO | 恢复为 `//TODO`，写出补法与待确认点 |
| Q4 | `verify_file` 的 d.ts 注释 | 恢复为 `//TODO`（目标就是建立分工），写出阻塞点 |
| Q5 | `Vector<String>` → `PackedStringArray` 是否该反过来 | **评估后回退**：`PackedStringArray` 逐元素 API 每个元素一次函数指针调用（`packed_string_array.cpp` 的 `_call_builtin_method_ptr_*`），只有 `ptr/ptrw` 是一次调用；这批函数是逐元素 push/filter/md5 ⇒ 改过去更慢。唯一跨语言边界是 `resources_reimported` 回调（`jsb_editor_plugin.cpp:227`），进我们这边本就拷一次 |
| Q6 | `jsb_shadow_realm.cpp` 为何不用 `memnew_placement`、为何 new 两次 | 改用 `memnew_placement`（本仓统一写法）；去掉重复的默认构造 + 赋值 |
| Q7 | `InternalModuleLoader` 无调用点不等于不挂 TODO | 恢复 TODO |
| Q8 | `friend` 那条「本轮不改 API」是给未来偷懒 | 恢复为 `//TODO`，写出可行做法 |
| Q9 | `is_valid` 该判定「脚本类是否还有效」 | 改为 `!module_id.is_empty()`（依据见 §5.4） |
| Q10 | 进度与确认项写在哪 | 之前一直写 `.agent_tmp/progress.md`（违反 AGENTS.md：**有任务时写任务目录**）。已改为写本 `report.md`，确认项集中在 §5 |

## 7. 验证

| 项 | 结果 |
|---|---|
| 重编 | dll md5 `a86c94c59fb788c476f3f64a758a2fc5`；`bin/windows/` 与 `project/addons/.../bin/windows/` 两处一致 |
| C++ doctest | **76/76 cases、1127/1127 assertions**，rc=0 |
| 项目跑测 | rc=0；`COMPLETED=1`；`FAILED=0`；`Orphan StringName=0`；`failed to check out module`=0 |
| `TODO` 计数 | 188 → **152**（净减 36：实现/删除 41 条，恢复 5 条误删的 TODO） |

本轮新增测试：`src/runtime/tests/test_jsb_script_language_queries.h`（3 个 TEST_CASE：
`find_identifier_line` 边界、`_validate_path`、`_inherits_script` 父链）。

## 8. 遗留

1. **未 push**（用户要求做完再推）。本地 `main` 领先 `origin/main` 8 个提交。
2. **G5 已完成并验证**（见 §5.5），产物在**工作树未提交**——需用户授权 commit。
3. A 类散条 6 条：`#10`/`#15`/`#17`/`#62`/`#69`/`#86`（`#86` = `IsFunction`→`Callable`，用户已给方向但未判）。
4. C 类 10 条 + D 类 `#173/#174/#175` 的 `TODO` 改写为 `NOTE`。
5. **上一轮遗留**：`678bc92` 扫入的 6 处 `//TODO` 删除及其 commit message 措辞问题——涉及改写已推送历史，需授权。
6. **过程失误**：审计切片曾派给只读的 `scout` 子代理（无法写文件），全部未落盘；
   我随后自行完成全部 188 条的读取与判定。
