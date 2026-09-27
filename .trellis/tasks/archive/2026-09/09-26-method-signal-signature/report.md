# 进度：函数与信号签名解析（子任务）

任务目录：`.trellis/tasks/09-26-method-signal-signature/`
父任务：`.trellis/tasks/09-24-script-static-members/`（已完结、已提交 `30d06fb`）
状态：**planning**（未 `task.py start`）。本任务**只落地 R2（`get_method_argument_count`）**，
R1（签名清单 sidecar）与 A8/A9 为已裁决待实施。

> 改动**未提交**（用户未授权）。工作树 HEAD = `origin/feature/script_static_members` = `30d06fb`。

---

## 目标

让方法参数名/类型、返回值类型、信号参数类型可从 TS 源提取、由运行时消费。
硬约束：**不得要求作者写注解**（用户当轮原话「绝对不要写注解去反射」）。

**本轮的切片**：`get_method_argument_count` —— 引擎侧此前恒 0
（`jsb_script_instance.h:188` 的 `return 0; // TODO`），使 inspector / 调试器 / GDScript 分析器
拿不到真实参数个数。

---

## 裁决（全部有实测/源码取证，详见 `research/signature-approach.md` §5）

| # | 议题 | 结论 |
|---|---|---|
| Q1 | TS 类型别名（`int32` 等） | Compiler API 返回**别名名本身**，`aliasSymbol.declarations` 为空 ⇒ 类型映射必须查**已知别名表** |
| Q2 | 重载 | 采纳 **C# 先例**（列全部 / `get_method_info` 遇第二条退化为空 / 个数取首个匹配）；TS emit 会**擦除**重载（3 声明 → 2 签名）⇒ 清单必须显式记录 |
| Q3 | 提取器时机 | 与 `.paths_mapping` 同节奏（窗口获焦 / 编辑器就绪 / 安装完成），每处 MD5 空操作门控（**用户决策**） |
| Q4 | 参数个数语义 | 计**全部已声明参数**、**剩余参数不计**（对齐 `GDScriptFunction::_argument_count` 与 `Callable::get_argument_count()` 文档） |

**方案（R1，未实施）**：编辑器侧 TS Compiler API 提取 → sidecar 清单 →
与 `.paths_mapping` 同型的落盘/加载/打包/降级链路。否决正则（7 条具名脆弱点）、
否决 tree-sitter（导出形态无 `.ts`，同样拿不到类型）、否决读 `.d.ts`（`b = "z"` 被合并成 `b?`）。

---

## 本轮改动（4 个源文件 + 3 个测试文件）

### 1. `src/runtime/bridge/jsb_class_info.h` — 数据模型

`ScriptMethodInfo` 新增 `int argument_count = 0;`，注释写明「剩余参数不计」与为何不用 `Function.length`。

### 2. `src/runtime/bridge/jsb_class_info.cpp` — 解析期扫描器

- 匿名命名空间新增 `constexpr int k_ArgumentCountUnknown = -1;` 与
  `int _count_declared_parameters(const String &p_source)`：取 `Function.prototype.toString` 语义文本
  （`impl::Helper::to_string_without_side_effect`），定位首个 `(`，按深度配平到 depth-0 的 `)`，
  在顶层按 `,` 分段计数；字符串/模板字面量（含转义）与 `//`、`/* */` 注释整体跳过；
  顶层 `...` 开头的段标记为 rest 并**不计入**。
- 捕获点（`:559-560`）：`method_info.argument_count = _count_declared_parameters(impl::Helper::to_string_without_side_effect(isolate, prop_val));`
- **未知 ⇒ `-1`（`k_ArgumentCountUnknown`），不是 `0`**：三处「不可判定」出口
  （无 `(`、depth-0 遇非 `)` 的闭合符、形参表未闭合）都返回它，
  使消费端能区分「没有参数」与「读不出来」。

**已知盲点（注释已具名，属 best effort）**：① 正则字面量里的不平衡括号（`f(x = /a(b/) {}`）——
区分正则与除法需要完整文法；② `${}` 内嵌反引号的模板字面量（`f(a = \`x${\`y\`}z\`)`）；
③ V8 `NoSideEffectsToString` 在结果 >128 字符时**截断为前 111 字符 + `...<omitted>...` + 末 2 字符**
（`src/objects/objects.cc`，`IsJSFunctionOrBoundFunctionOrWrappedFunction` 分支），形参表若不在
这 111 字符内闭合即不可判定。

### 3. `src/runtime/weaver/jsb_script.{h,cpp}` — `Script` 层

新增私有 `int _get_own_method_argument_count(const StringName &p_method, bool *r_is_valid) const`：
- 名字映射与 `_has_method` 一致（`_` 前缀 → `NamingUtil::get_member_name`）；
- **只报自有**（对齐 `GDScript::get_script_method_argument_count`，`gdscript.cpp:370-383`）；
- `argument_count < 0` ⇒ `*r_is_valid = false`（名字解析到了、个数没解析到）。

`_get_script_method_argument_count` 改为 `is_valid ? Variant(count) : Variant()`：
**空 `Variant` 而非 INT 0** 是必要的 —— `ScriptExtension::get_script_method_argument_count`
（`script_language_extension.h:114-124`）只接受 `Variant::INT`，否则回退
`Script::get_script_method_argument_count`（读 `get_method_info()`）；返回 INT 0 会让**任意名字**
都像"我方的 0 参方法"，回退路径永不生效。

### 4. `src/runtime/weaver/jsb_script_instance.{h,cpp}` — 实例层

`GodotJSScriptInstanceBase::get_method_argument_count`（原 `return 0; // TODO` 纯虚默认实现）
改为**自己走 `base` 链**，对齐 `GDScriptInstance::get_method_argument_count`（`gdscript.cpp:1919-1931`）：
`Object::get_method_argument_count`（`object.cpp:772-781`）**先问实例且不走链**，
所以走链必须在这里。**总 miss 必须置 `*r_is_valid = false`**，否则会遮蔽 ClassDB 第二腿
（`object.cpp:783-793`）。

### 5. 测试（永久覆盖）

| 文件 | 断言 |
|---|---|
| `project/tests/static-members/static-members-target.ts` | 新增 `add(a: number, b: number = 2, ...rest: number[]): number` —— 形状刻意是 `Function.length` 报不出的（`length` = 1，声明数 = 2） |
| `src/runtime/tests/test_jsb_static_members.h` | 新用例 `script method argument count`：`greet` = INT 0、`add` = INT 2、未知名字 **不是 INT**；派生脚本的 `add` **不是 INT**（只报自有）；用 `load_source_code` + `set_path` 构造（不碰资源缓存、`loaded_ == false` 入口），`_is_valid()` 守卫使断言不因脚本加载失败而空转 |
| `project/tests/static-members/static-members-gdcheck.gd` | 实例侧：`D.new().get_method_argument_count("add")` = 2（走链）、`"greet"` = 0、未知名字 `has_method` = **1**（落到 ClassDB 第二腿，证明 miss 未被当成有效 0）；`Script` 侧：派生 `dscr.get_method_argument_count("add")` = 2、`scr` 的 `"add"` = 2、`"greet"` = 0 |

---

## 验证（全部实测，同一引擎 `Godot_v4.7.2-stable_win64_console.exe`）

| 项 | 命令 | 结果 | 日志 |
|---|---|---|---|
| 构建 | `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `RC=0` | `.agent_tmp/q4_last_build.log` |
| C++ 套件 | `--headless --path ./project --jsb-run-tests` | `RC=0`；runtime `55/55` cases、`712/712` assertions；editor `3/3`、`12/12` | `.agent_tmp/q4_last_cpp.log` |
| 全量验收 | `--audio-driver Dummy --headless --path ./project --verbose` | `RC=0`；**Orphan StringName = 0**、`COMPLETED = 1`、`FAILED = 0`、`STATIC-MEMBERS-GD-OK = 1` | `.agent_tmp/q4_last_accept.log` |
| codegen 基线 | `python misc/verify_codegen.py --godot "<GODOT>"` | `RC=0`、`✅ 校验通过: 生成产物与基线一致` | `.agent_tmp/q4_verify_codegen.log` |
| TS 编译 | `node node_modules/typescript/bin/tsc`（先删 `.godot/.tsbuildinfo`） | `RC=0` | — |

### 负向控制（两条，均**实测失败**后还原并复验绿）

| # | 削减 | 结果 | 日志 |
|---|---|---|---|
| ① 扫描器恒返 0 | `_count_declared_parameters` 首行 `return 0;` | `TESTS_RC=1`、`54 passed \| 1 failed`，失败项 `test_jsb_static_members.h(457): ERROR: (int64_t)defaults_and_rest == 2 values: 0 == 2` | `.agent_tmp/q4_negctl_tests.log` |
| ② 实例层链走断 | `get_method_argument_count` 内 `sptr = nullptr;` 取代 `sptr = sptr->base.ptr();` | `GODOTJS_TEST_PROJECT_FAILED=1`、`STATIC-MEMBERS-GD-OK=0`，失败项 `static-members(gd) inherited method argument count: expected 2, got 0` | `.agent_tmp/q4_negctl3_accept.log` |

> ②的**第一次**尝试（在循环后加 `return 0;`）是**空转**的——`return 0` 位于 `while` 之后，
> 走链仍先命中并返回。已识别并换成真正打断走链的削减形式。这条教训记在此处：
> **负向控制必须确认削减真的落在被测路径上**。

还原后两轮复验：`q4_final_accept.log`、`q4_final3_accept.log` 均
`orphan=0 / COMPLETED=1 / FAILED=0 / STATIC-MEMBERS-GD-OK=1`。

---

## 已知边界（如实记录，未改代码）

- **R1 签名清单未实施**：参数名/类型、返回值类型、信号参数类型仍不可得（A8/A9 未勾）。
  本轮只交付参数**个数**。
- **`_get_method_info()` 的 `args` 刻意不填**（裁决 R3.1）：填了会让
  `GDScriptAnalyzer::validate_call_arg`（`gdscript_analyzer.cpp:6142-6148`）把每个参数当**必填**
  （`PropertyInfo` 一律 `usage = PROPERTY_USAGE_DEFAULT`、无默认值扣除）⇒ 对外来脚本产生
  **新的 GDScript 解析错误**。`_get_script_method_argument_count` 是唯一通道。
- **扫描器的三个盲点**（见上文 §2），都返回 `k_ArgumentCountUnknown` 而非错误个数。
- **未在 jsc / quickjs / web 三腿实测**：本轮验证在 v8 腿。三腿的
  `to_string_without_side_effect` 都经 `ToDetailString`，但 jsc 的该实现是
  `//TODO no equivalent implementation` → `ToString`（**有副作用**）；若切腿需重验。
- **`GodotJSScriptLanguage::_finish()` 无新增释放点**：本轮的 `argument_count` 是 `int`，
  不持有 `StringName`/`Variant`，不涉及 orphan 缺陷 A 的同型处理。

## 清理

- 本轮探针已删：`.agent_tmp/probe_fn_src.mjs`、`probe_detail.mjs`、`probe_len.mjs`、
  `.agent_tmp/_del.py`。
- `project/` 下 glob `_*` = `[]`、`.agent_tmp*` = `[]`（无散落）。
- `.agent_tmp/` 其余为前序任务的既有产物（不在本轮范围）。

## 遗留 / 待用户拍板

- **未提交**：`.trellis/tasks/09-26-method-signal-signature/` 为 untracked，
  4 个源文件 + 3 个测试文件为工作树改动。提交需**当轮明确授权**。
- **`task.py start` 未执行**：任务仍 `planning`。

---

# 第二轮：提取器接线（用户点 3「按 design.md 实测可用的落地形态实现看看先」）

状态：**已完成并实测验证**（构建、两套 C++ 套件、全量验收、codegen 基线、编辑器端到端全绿）。
本轮**只做提取器 + 编辑器接线**；**C++ 消费侧（sidecar 读取器、`_get_method_info` 填充、
懒加载改造、重载、信号）仍未授权实施**。

## 落地形态（与 design.md §8 一致）

```
scripts/jsb.editor/src/signature/jsb.signature.extract.cts   ← 源码（untracked）
  └─ tsc -p tsconfig.signature.json ──► scripts/out/jsb.signature.extract.cjs
       └─ SConstruct PresetDefine ──► 内嵌进 editor 扩展
            └─ 编辑器安装到 res://.godot/ ──► Process::create 以 Node spawn
                 └─ 写 res://.godot/godotjs_ext/**/*.sig
```

**为什么是独立 Node 产物**：引擎内嵌的 AMD bundle 没有 Node 内建模块，而 `typescript.js`
依赖 `require("fs"/"path"/...)`。
**为什么装进项目数据目录而非 jsb 输出目录**：输出目录的清理规则是「没有对应 `.ts` 源即判陈旧」，
工具以 `.cjs` 结尾会被删；且 `.godot` 树不参与 EFS 扫描（`filesystem_cache10` 无
`::res://.godot/::` 条目）⇒ 不产生 `.ts.uid` 副作用。
**为什么参数值是绝对路径且不加引号**：子进程继承引擎 cwd（引擎启动时会 `chdir`，`main.cpp:1713`），
相对路径不可靠；而带尾反斜杠的值一旦被 `Process` 的引号包裹，`\"` 会被命令行走义吞掉分隔符。

## 本轮改动（12 个已跟踪文件 + 4 个新增）

| 文件 | 内容 |
|---|---|
| `SConstruct` | `PresetDefine("scripts/out/jsb.signature.extract.cjs", "jsb.signature.extract.cjs")` 进 ed 列表（不套 `AMDSourceTransformer`、不零终止） |
| `scripts/jsb.editor/package.json` | build = `tsc && tsc -p tsconfig.signature.json` |
| `scripts/jsb.editor/tsconfig.json` | 加 `"exclude": ["src/signature"]`（不得并进 AMD bundle） |
| `scripts/jsb.editor/tsconfig.signature.json`（新增） | 独立 Node 目标：`module: commonjs`、`rootDir: ./src/signature`、`outDir: ../out` |
| `src/jsb.config.h` | `#define JSB_SIGNATURE_EXT "sig"` |
| `src/internal/jsb_path_util.{h,cpp}` | `convert_signature_path()`（`<outDir>/<rel>.sig` → `res://<rel>.ts`） |
| `src/editor/weaver-editor/jsb_editor_plugin.{h,cpp}` | 见下 |
| `src/internal/jsb_process.cpp` | 见下 |
| `src/editor/weaver-editor/jsb_export_plugin.cpp` | `export_raw_file` 顺带打包同名 `.sig` |
| `src/editor/tests/test_jsb_editor_cleanup.h`（新增） | 清理白名单的永久断言 |
| `src/editor/tests/jsb_editor_test_main.cpp` | 注册上一个头 |
| `src/runtime/tests/test_jsb_path_util.h` | `convert_signature_path` 的 4 条断言 |

### `jsb_editor_plugin` 细节

- `_accumulate_typescript_stats(dir, count, max_mtime)`（匿名 ns，`#if JSB_USE_TYPESCRIPT`）：
  递归统计 `res://` 下 `.ts`，排除 `.d.ts`、`node_modules`、项目数据目录、**所有 `.` 开头目录**。
- `_regenerate_signatures()`（非 static）：门控摘要 =
  **`.ts` 数量 | 最大 mtime | tsconfig md5 | 工具自身 md5**。
  工具 md5 必须进摘要：升级扩展会换提取器而 `.ts`/tsconfig 都不动，不带上就永不重跑、留下旧版 sidecar。
  摘要相等即空操作；命中则先 `signature_tool_->stop()` 收尾上一次，再 `Process::create`。
  **不做 `is_running()` 复核**：短命进程在 `CreateProcessW` 返回后可能已自行退出，复核会把
  「跑完了」误判成「起不来」。
- 触发点 4 处，与 `_regenerate_paths_mapping()` 同节奏：窗口获焦、`NOTIFICATION_READY`、
  `try_install_project_files` 的 verify 全绿分支与 `force` 分支。
- `add_install_file({ JSB_SIGNATURE_TOOL_NAME, "res://" + get_project_data_dir_name(), CH_TYPESCRIPT })`。
- `collect_invalid_files` 新增 `.sig` 分支（有对应 `.ts` 保留、源没了判 invalid）；
  `cleanup_invalid_files` 对 sidecar 跳过无意义的 `+ ".map"` 删除。

### `jsb_process.cpp` 细节（为短命进程而必须修）

三个缺陷，都是「一次性子进程」这一使用方式暴露出来的：

1. `Process::stop()` 原先 `if (!is_running()) return;` —— 子进程自行退出后，读取 stdout 的
   后台线程仍持有 `this` 裸指针，跳过 `on_stop()`（即跳过 join）后析构 `ProcessImpl` 就是
   **use-after-free**。改为恒调 `on_stop()`。
2. 两个平台的 `on_stop()` 改为**幂等**：先取 `const bool was_running = _is_running();` 再置
   `is_closing`（顺序反了会让活进程永不被终止 ⇒ 管道不关 ⇒ 读取线程 `ReadFile` 永久阻塞 ⇒
   join 挂死）；已退出则跳过 `TerminateProcess`/`kill`；句柄/`rd_pipe` 关后置空；
   `thread.is_valid() && thread->is_started()` 才 join。
3. UNIX 侧：`pipefd` 初值 `{0,0}` → `{-1,-1}`（`0` 是 stdin，按 fd 有效性清理会误关标准输入）；
   `on_stop()` **不**关 `pipefd[0]`（唯一所有者是读取线程，各关一次是双重 close）；
   仅在无读取线程（启动失败）时才由宿主关两个 fd。

## 验证（全部实测，`Godot_v4.7.2-stable_win64_console.exe`）

| 项 | 命令 | 结果 | 日志 |
|---|---|---|---|
| 构建 | `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5` | `RC=0` | `.agent_tmp/sig_build6.log` |
| C++ 套件 | `--headless --path ./project --jsb-run-tests` | `RC=0`；runtime **55/55** cases、**716/716** assertions；editor **4/4**、**27/27** | `.agent_tmp/sig_tests6.log` |
| 全量验收 | `--audio-driver Dummy --headless --path ./project --verbose` | `RC=0`；**Orphan StringName = 0**、`COMPLETED = 1`、`FAILED = 0` | `.agent_tmp/sig_accept3.log` |
| codegen 基线 | `python misc/verify_codegen.py --godot "<GODOT>"` | `RC=0`、`✅ 校验通过` | `.agent_tmp/sig_verify_codegen.log` |
| **编辑器端到端** | `--headless --editor --path ./project --quit-after 900` | `RC=0`；装出 `project/.godot/jsb.signature.extract.cjs`（md5 == `scripts/out/` 那份）并 spawn；删光 sidecar 后重跑 ⇒ `scripts=98 written=18 up-to-date=0 empty=80`、18 个 `.sig` 重新生成且与本轮早先 `--dump` 的产物**逐字节一致** | `.agent_tmp/sig_editor_final.log` |
| 增量 | 同上再跑一次 | `written=0 up-to-date=18`（摘要门控生效） | `.agent_tmp/sig_editor2.log` |
| 打包规则自证 | 对真实产物目录按 `export_raw_file` 的推导式反查 | 103 个 `.js` 中 18 个有 sidecar、推导路径全部命中；18 个 sidecar 对应的 `res://<rel>.ts` **全部存在**（清理白名单不变量） | 会话内脚本 |

### 负向控制（两条，均实测失败后还原复验绿）

| # | 削减 | 结果 |
|---|---|---|
| ① `.sig` 白名单分支失效 | `else if (it_path.ends_with("." JSB_SIGNATURE_EXT))` → `else if (false && …)` | `TESTS_RC=1`、editor `3 passed \| 1 failed`、失败项 `test_jsb_editor_cleanup.h(110): ERROR: !invalid.has(...paired.sig) values: false` ⇒ 新测试**非空转** |
| ② 摘要门控失效 | `NOTIFICATION_READY` 里连调 `_regenerate_signatures()` 两次（删光 sidecar 制造必跑条件） | 提取器**只运行一次**（`written=18`），第二次被摘要挡下 ⇒ 会话内门控成立 |

> ①的第一版测试写错了：把「源 `.ts`」也放进 `<outDir>` 下，而 `convert_signature_path` 的落点是
> `res://<rel>.ts`（**项目根**，不是 outDir 内）⇒ 「应保留」的断言恒假。**测试自己先失败才暴露出
> 我的布局理解错误**；同时暴露了 `convert_signature_path` 的**真实 off-by-one**
> （`std::size(JSB_SIGNATURE_EXT) + 1` 多减一个字符，`test.sig` → `res://test.t`），已修。

## 已知边界（如实记录）

- **sidecar 尚无消费者**：运行时不读 `.sig`，参数名/类型仍不可得（A1–A3/A6/A7 未实现）。
  本轮的可见产物只有磁盘上的清单文件与被打包进导出包的能力。
- **导出打包未端到端实测**：本机 `%APPDATA%/Godot/export_templates` 为空（无导出模板）
  ⇒ 只做到「推导规则对真实产物目录逐个反查命中」，未跑真实导出。
- **jsc / quickjs / web 三腿未实测**（沿用上轮记录）。
- `.godot/jsb.signature.extract.cjs` 与 18 个 `.sig` 是**编译产物**（在 `.godot/` 内），
  不构成 `project/` 散落。

## 清理

- 已删上一轮遗留的 pnpm 存活探针：`project/node_modules/.godotjs/{m.txt,keepme.txt,jsb.signature.extract.cjs}`、
  `project/node_modules/jsb.signature.extract.cjs`（连同空目录 `.godotjs`）。
- 本轮日志全部在 `.agent_tmp/`（`sig_*.log`）。

## 遗留 / 待用户拍板（第二轮）

- **未提交**（用户当轮未授权提交）。本轮工作树改动 = 上表 12 个已跟踪文件 + 4 个 untracked
  （`design.md`、`src/signature/`、`tsconfig.signature.json`、`test_jsb_editor_cleanup.h`）。
- **下一阶段（未授权）**：C++ sidecar 读取器 → `_get_method_info` 填 `args`/`default_args`/
  `return`/`flags` → 懒加载改造（design §7）→ 重载（§11.1）→ 信号（§12）。

---

# 第三轮：消费者侧（阶段 0 / 1 / 3 —— 签名清单真正被运行时消费）

状态：**已完成并实测验证**（构建、两套 C++ 套件、全量验收、codegen 基线全绿；2 条负向控制已还原复验）。
本轮把上一轮的 sidecar 从"只有文件"变成**运行时真实签名**。

## 改动（7 个已跟踪文件 + 3 个新增）

| 文件 | 内容 |
|---|---|
| `src/runtime/bridge/jsb_signature.{h,cpp}`（新增） | sidecar 二进制读取器 + 类型映射 + 方法表构造 + 回退扫描 |
| `src/runtime/bridge/jsb_class_info.h` | `ScriptMethodSignature` / `ScriptArgumentCount`（三态）/ `ScriptSignalInfo::arguments` / `ScriptMethodInfo::overloads`；`argument_count` 默认值改 `NotComputed` |
| `src/runtime/bridge/jsb_class_info.cpp` | **删掉解析期签名扫描**（`_count_declared_parameters` 整段移除）；只登记名字 |
| `src/runtime/weaver/jsb_script.{h,cpp}` | `_ensure_signature_manifest()`（懒加载、编辑器下每次重读）、`_resolve_method_argument_count()`；`_get_method_info` / `_get_script_method_list` / `_get_script_signal_list` 消费清单 |
| `src/runtime/weaver/jsb_script_instance.cpp` | `make_temporary_method_list()` 按重载展开 |
| `src/runtime/tests/test_jsb_signature.h`（新增） | 读取器/映射/方法表/拒绝路径的**密闭**用例（自造清单字节，不依赖构建产物） |
| `src/runtime/tests/test_jsb_static_members.h` | 清单**确实被消费**的端到端断言 |
| `src/editor/weaver-editor/jsb_export_plugin.cpp` | 修 sidecar 名字推导的 off-by-one |
| `project/tests/static-members/static-members-gdcheck.gd` | 运行期调用形态回归 |

### 关键实现点

- **三态**：`NotComputed(-2)` / `Unknown(-1)` / `>= 0`。**负值一律不得当成 0**（那会把"读不出来"谎报成"没有参数"）。
- **懒加载**：解析期零签名工作；首次查询才读清单。编辑器（`is_editor_hint()`）下不缓存 —— 作者改完 `.ts` 保存后立刻看到新签名。
- **不缓存失败**：`signature_manifest_loaded_` 记的是"尝试过"（无类成员的脚本不产出清单，缺失是常态）。
- **回退路径保留**：没有清单时退回函数源文本扫描（复用脚本类信息里的 `method_cache`，与 `Environment::call_script_method` 同机制），行为与改动前一致。
- **类型映射唯一判定点**在 C++：引擎别名 → `get_original_name()` 反查原始类名 → `Variant::get_type_name` 反向表（现构）→ `ClassDB::class_exists` → NIL。
- **void 与不可映射**：返回值位 NIL **带** `PROPERTY_USAGE_NIL_IS_VARIANT` = 不可映射；**不带** = 真 `void`。
- **重载**：`_get_method_info` 遇 >1 签名返回空字典（C# 同形）；`_get_script_method_list` 展开全部；重载取首个 —— 键查找逻辑未动，`StatelessScriptClassInfo` 公开形态不变。

## 修掉的两个真实缺陷（均由新测试先失败暴露）

1. **读取器 off-by-one**：`std::size("js")` 计入结尾 NUL ⇒ `length - std::size(...)` 连点一起吃掉
   （`x.js` → `xsig`）。导出侧同一处推导式同型错误。**两边都修为 `std::size(...) - 1`**。
2. **字符串池错位**：池里**每一项**都带 `[len][bytes]`（索引 0 也不例外），从索引 1 起步会漏掉一个长度字节。

## 验证（全部实测）

| 项 | 结果 | 日志 |
|---|---|---|
| 构建 | `RC=0` | `.agent_tmp/sig_build20.log` |
| C++ 套件 | `RC=0`；runtime **59/59** cases、**797/797** assertions；editor **4/4**、27/27 | `.agent_tmp/sig_tests17.log` |
| 全量验收 | `RC=0`；**Orphan StringName = 0**、`COMPLETED = 1`、`FAILED = 0`、`STATIC-MEMBERS-GD-OK = 1` | `.agent_tmp/sig_accept_final.log` |
| codegen 基线 | `RC=0`、`✅ 校验通过` | `.agent_tmp/sig_verify_codegen_final.log` |
| sidecar ↔ 运行期 | 独立解码 `static-members-target.sig`：`add` = ret `number` / defaults 1 / 参数 `(a:number, b:number)` / vararg；`test_01.sig` 的 `test_signal` = 信号 + 参数 `value:number` | 会话内脚本 |
| 运行期消费（方法 + 信号） | `test_jsb_static_members.h` 直接查 `_get_script_method_list()`（`add` 的 `args`/`default_args`/VARARG/`return`）与 `_get_script_signal_list()`（`test_signal` 的 `value: float`）—— 均为**清单填充后才可能出现**的形态；`test_01.ts` 走 `ResourceLoader` 取缓存实例（`load_source_code` 会撞 "Another resource is loaded"） | 同上 |
| 无清单回退 | 移走全部 18 个 `.sig` 后全量验收仍绿（`GD-OK=1`）⇒ 回退路径未被破坏；随后经编辑器重建 | `.agent_tmp/sig_nomanifest.log` |

### 负向控制（2 条，均实测失败后还原复验绿）

| # | 削减 | 结果 |
|---|---|---|
| ① 禁用清单加载（`_ensure_signature_manifest` 提前 return） | 精确失败 **4 条**新断言：`args.size() == 2 values: 0 == 2`、`default_args.size() == 1 values: 0 == 1`、`VARARG != 0 values: 0 != 0`、`return.type == FLOAT values: -1 == 3` | 证明端到端断言**非空转** |
| ② 漏填 `default_arguments` | **仍然全绿** ⇒ 我先写的那三行 GDScript arity 断言是**空的**，已删除并如实记录在 gdcheck 注释里 | 见下 |

> ②的教训：GDScript 对**脚本实例上的方法调用不做 arity 检查**（arity 检查只发生在能拿到 `MethodInfo`
> 的静态分析路径），所以"文件能加载"根本不是 `default_arguments` 的证据。**没有失败过的守卫不授权任何结论**
> ——这条已按 spec 要求执行并留痕。

## 已知边界（如实记录）

- **`default_arguments` 的运行期 arity 效果仍未证实**（design §14-2）：本机没有触发它的 GDScript 形态
  （见②）。填充本身已由 C++ 用例钉住（`MethodInfo.default_arguments.size() == 1`），但它在 GDScript
  分析期的可观测后果**未验证**。
- **导出打包未端到端实测**：`%APPDATA%/Godot/export_templates` 为空（无导出模板）；只做到推导规则反查命中。
- **jsc / quickjs / web 三腿未实测**（沿用前轮）。
- 读取器的畸形输入防护（长度上限、越界即失败）**只有构造式覆盖**，未做 fuzz。

## 遗留 / 待用户拍板（第三轮）

- **未提交**（用户当轮未授权提交）。
- 工作树改动 = 已跟踪 21 文件（+598/−171）+ untracked：`design.md`、`src/signature/`、
  `tsconfig.signature.json`、`test_jsb_editor_cleanup.h`、`jsb_signature.{h,cpp}`、`test_jsb_signature.h`。

# 第四轮：生成收紧（目录忽略 + 单文件 Godot 判据 + default_count 修正）

状态：**已完成并实测验证**（提取器编译、sidecar 集合前后对比、C++ 构建、两套套件、全量验收、codegen 基线全绿）。

## 改动

| 文件 | 内容 |
|---|---|
| `scripts/jsb.editor/src/signature/jsb.signature.extract.cts` | 新增 `shouldIgnorePath`（Godot 忽略规则）、`isGodotScript` + `classifyClassName`（单文件判据）、`countTrailingOptional`（`default_count` 修正）；统计行加 `ignored=` / `non-godot=` |
| `src/editor/weaver-editor/jsb_editor_plugin.cpp` | `_accumulate_typescript_stats` 同步同一套忽略规则（含**文件本身**以 `.` 开头） |

### 1. 目录忽略规则（照抄 `EditorFileSystem`，不自创）

出处：引擎 `editor/file_system/editor_file_system.cpp:3511-3531`（`_should_skip_directory`）与
`:1478-1485`（`_scan_fs_changes` 的隐藏判据）：

- 任一路径段以 `.` 开头 ⇒ 忽略。**文件本身也要参与**（引擎有两条独立判据：文件走
  `current_is_hidden()`、目录走 `begins_with(".")`）⇒ 最后一段同样生效；
- 祖先目录含 `.gdignore` ⇒ 忽略（含其下全部内容）；
- 祖先目录含 `project.godot`（嵌套项目）⇒ 忽略；
- tsconfig 的 `outDir`（编译产物）⇒ 忽略。

用 `Map<string, boolean>` 缓存祖先目录的判定，避免同一目录下每个文件重复 stat。
C++ 侧的 `_accumulate_typescript_stats` 同步同一套 —— **两侧必须一致**，否则门控摘要与提取器
看到的集合对不上（例如隐藏文件的增删只改一边）。

### 2. 单文件 Godot 判据（三态，刻意宽松）

运行期判据是 `exports.default` + `class_obj[Symbol(ClassId)]`（`jsb_class_info.cpp:820-853`）——
**第三条要 JS 求值**，静态只能近似。故三态：

| 形态 | 判定 |
|---|---|
| 基类标识符来自 `"godot"` 模块 import | **是** |
| 无默认导出 / 默认导出不是类 / 类无 `extends` / 基类链在**本文件内**可证不是 | **不是**（不产出，并 `unlink` 旧清单） |
| 跨文件继承（`extends Child`）、`extends Mixin(Base)` 表达式、基类不可解析 | **未知 ⇒ 按"是"处理** |

按"是"放行的理由：判错的代价是那个脚本**永远没有清单**（静默退回源文本扫描，丢掉全部参数名/
类型/返回值），比多写一份没人读的清单严重得多。

**刻意不做跨文件传播**：需要图遍历 + 环处理 + tsconfig `paths` 反解，而实测收益为零
（`project/` 下 18 份清单全是 Godot 脚本）。
Godot 类名**不维护类名表** —— 所有引擎类都从 `"godot"` 模块 import，按**来源模块**判即可。

### 3. `default_count` 修正（真缺陷）

原先 `params.filter(optional).length` —— 实测 TS **只对 `?`** 强制"必填不能跟在可选之后"
（TS1016），**带初始值的参数不受约束**：`f(a: number = 1, b: number)` 是合法代码，而 `b` 必填。
把它算进 `default_count` ⇒ 消费端认为最小 arity = 2-1 = 1 ⇒ `f(1)` 被判合法，实际 `b` 拿 `undefined`。

改为 `countTrailingOptional`：只统计**形参表末尾连续可选**的那一段。

## 实测证据

| 项 | 结果 |
|---|---|
| 提取器编译 | `tsc -p tsconfig.signature.json` RC=0 |
| 实写（清空后重建） | `scripts=28 written=18 up-to-date=0 empty=2 **ignored=70** **non-godot=8**` |
| 增量二次运行 | `written=0 up-to-date=18`（门控未误触发） |
| sidecar 集合前后对比 | **18 → 18，`LOST=[]` `NEW=[]`**（收紧未影响任何真实 Godot 脚本） |
| 忽略规则夹具 | `_sigprobe/.hidden/h.ts`、`.gdignore` 目录下的 `gdig/g.ts` **均未产出**；`plain/derived.ts`（跨文件继承 ⇒ 未知）**照旧产出** |
| 判据夹具 | `util.ts`（无默认导出）、`plain.ts`（无 extends）**未产出** |
| `default_count` 解码验证 | `f(a=1, b)` ⇒ **0**（修复前 1）；`g(x, y?)` ⇒ 1；`h(a?, b?)` ⇒ 2 |
| C++ 构建 | scons RC=0 |
| C++ 套件 | runtime **59/59** / **797/797**；editor **4/4** / 27/27 |
| 全量验收 | RC=0、**Orphan=0**、`COMPLETED=1`、`FAILED=0` |
| codegen 基线 | RC=0、`✅ 校验通过` |
| 项目清洁 | `project/` 无 `_*` / `.agent_tmp*` 残留；sidecar 仍 18 |

日志：`.agent_tmp/sig_build_extractor.log`、`sig_build21.log`、`sig_tests18.log`、
`sig_accept_final2.log`、`sig_verify_codegen_final2.log`。

## 已知边界

- **跨文件继承链判定缺失**（`extends Child extends Node` 中 `Child` 的 Godot 性不传播）：
  当前按"是"放行 ⇒ 判据偏松，但**不会丢签名**。等 `project/` 里真出现"带方法的非 Godot 脚本"、
  且 sidecar 数明显超出脚本数时再上精确版（图遍历 + 不动点）。
- **`.gdignore` 在提取器里是"祖先目录"检查**：若 `.gdignore` 放在项目根自身，引擎也整项目忽略，
  与本实现一致。
- 隐藏文件判据用"路径段以 `.` 开头"近似 `current_is_hidden()`（含 OS hidden 属性）⇒
  **Windows 上被设了 hidden 属性但名字不以 `.` 开头的文件，引擎忽略而本提取器不忽略**。
  项目里无此形态；真出现时补 `fs.statSync` 的属性检查即可。

## 遗留

- **未提交**（用户未授权）。
- 本轮新增改动：`jsb.signature.extract.cts`、`jsb_editor_plugin.cpp`（其余为前三轮积累，仍未提交）。


# 第五轮：GDScript 探针（打印 Godot 侧实际看到的签名）

起因：前四轮一直在**解码二进制** + **C++ 断言**验证，验的是数据层。用户指出应直接打印 Godot 侧实际拿到的
东西 —— 更直观，也更接近作者视角。本轮照此执行。

诊断脚本（保留，可复跑）：（一次性诊断脚本，已删除）、（一次性诊断脚本，已删除）
运行：`"<引擎>" --headless --path ./project --script ../（一次性诊断脚本，已删除）`

## 打印结果（`project/` 实机，18 份 sidecar 在位）

```
=== 带方法签名的脚本  res://tests/static-members/static-members-target.ts ===
  [方法表] get_script_method_list()
    greet      ()                    -> FLOAT          default_args=0  flags=NORMAL
    add        (a: FLOAT, b: FLOAT)  -> FLOAT          default_args=1  flags=NORMAL|VARARG
  [信号表] get_script_signal_list()
    (空)

=== 带信号的脚本  res://test_01.ts ===
  [方法表]
    _ready     ()                    -> NIL(void/任意)   default_args=0  flags=NORMAL
    test       ()                    -> NIL(void/任意)   default_args=0  flags=NORMAL
  [信号表]
    no_arg         ()
    test_signal    (value: FLOAT)

  [实例] Object::get_method_argument_count()
    greet -> 0    add -> 2    __unknown__ -> 0

  [真实调用形态]
    greet() -> 1      add(1,2) -> 3      add(1) -> 3
    add(1,2,3) -> 4   add(1,2,3,4) -> 5
```

参数名、参数类型、可选参数个数、VARARG、返回值类型 —— **全部到位**。这正是 inspector / 远程调试器 /
GDScript 分析器读的那一份（`Script` 的反射钩子，不是我们私有的 API）。

## 负向控制（最有价值的一步）

**移走全部 18 份 sidecar 后重跑同一探针**：

```
    greet      ()  -> NIL(void/任意)   default_args=0  flags=NORMAL
    add        ()  -> NIL(void/任意)   default_args=0  flags=NORMAL
    no_arg         ()
    test_signal    ()
```

⇒ 参数名/类型、默认值个数、VARARG、返回值类型**全部塌回缺省**，信号参数表变空。
**证明打印出来的东西确实来自清单，不是别处带来的。**
（清单已还原，重跑确认回到上一节的输出。）

**同时留下一条当时未深究的观察**：`[真实调用形态]` 那段在**无清单时完全不变**
（`add(1,2,3,4) -> 5` 照旧）。本轮曾据此"印证"第三轮的旧结论，并进一步归因为
「GDScript 对脚本实例上的方法调用不做 arity 检查」——**该归因已被第六轮证伪**：
真正原因是这段用的是**继承**方法的实例（分析器拿不到 `MethodInfo`），与清单无关。
正确判据见第六轮。

## `default_count` 修正的 Godot 侧效果（临时夹具，已删除）

临时夹具 `project/_sigdefcheck.ts` → `tsc` → 提取器 → 打印：

| 声明 | Godot 侧 `default_args` | 最小 arity |
|---|---|---|
| `f(a: number = 1, b: number)` | **0** | 2 ✅（修正前是 1 ⇒ 会误放行 `f(1)`） |
| `g(x: number, y?: number)` | 1 | 1 ✅ |
| `h(a?: number, b?: number)` | 2 | 0 ✅ |

夹具与产物（`_sigdefcheck.ts` / `.js` / `.js.map` / `.sig`）**已全部删除**，
`project/` 残留检查为**干净**，sidecar 仍为 **18**。

## 本轮新确认的两个事实（此前只有源码推断）

1. **`Script::get_method_info` 与 `ScriptExtension::get_script_method_argument_count` 都没有 ClassDB 绑定**
   —— GDScript 侧**调不到**（实测 `Nonexistent function 'get_method_info' in base 'GodotJSScript'`）。
   ⇒ GDScript 侧可用的只有三个：`get_script_method_list()` / `get_script_signal_list()` /
   `Object::get_method_argument_count()`（后者是 Object 的绑定，内部走链到 Script 钩子）。
   这条同时印证了 `architecture-constraints.md` 里记的「`Script::get_method_info` 无 ClassDB 绑定」。
2. **方法表 / 信号表会走基类链，与四个"只报自有"的钩子不同** ——
   实测 `static-members-derived.ts`（自身无方法）列出了 `greet` / `add`（来自基类）。
   这是 `jsb_script.h:244-246` / `:259-261` 的 `base->get_script_method_list(...)` 递归所致。
   **不是缺陷**：这两个表的设计就是走链；而 `_get_constants` / `get_members` / `has_method` /
   `get_method_info` 四个钩子才是"只报自有"（见 `architecture-constraints.md`）。

## 验证状态

本轮**未改任何业务代码**，纯验证。第四轮的构建/套件/验收/codegen 结论**不受影响**（仍是
scons RC=0、runtime 59/59·797/797、editor 4/4、Orphan=0、COMPLETED=1、FAILED=0、codegen ✅）。

## 遗留

- 未提交。
- 诊断脚本留在 `.agent_tmp/`（gitignore），未在 `project/` 加永久用例 ——
  同样的断言已由 `test_jsb_static_members.h` 覆盖且负向控制已证明非空转，不重复建。


# 第六轮：`default_arguments` / VARARG 的分析期效果 —— **已证实**（推翻第三轮的"未证实"）

第三轮留的「`default_arguments` 在 GDScript 分析期的效果未证实」**是错的**，本轮实测推翻。
判别信号不是"是否报错"，而是**报错消息里的数字**。

## 判决性证据

夹具 `add(a: number, b: number = 2, ...rest: number[])` ⇒ arguments=2、default_count=1 ⇒ 最小 arity = 1。

| 调用 | 实测结果 |
|---|---|
| `add()` | `Parse Error: Too few arguments for "add()" call. **Expected at least 1** but received 0.` |
| `add(1)` / `add(1,2)` | 通过 |
| `add(1,2,3,4,5)` | 通过，**无** "Too many arguments"（VARARG 生效，max arity 不检查） |

报的是 **at least 1**，不是 2 ⇒ **GDScript 分析器确实读了 `default_arguments`**。

## 归因对照（临时夹具 `_sigdefcheck.ts`，已删除）

| 声明 | default_count | 最小 arity | 实测报错 |
|---|---|---|---|
| `f(a: number = 1, b: number)` | 0 | 2 | `Expected at least 2 but received 1` |
| `g(x: number, y?: number)` | 1 | 1 | `Expected at least 1 but received 0` |
| `h(a: number, b: number = 2)` 无 rest | 1 | 1 | `h(1,2,3,4,5)` → `Too many arguments … Expected at most 2 but received 5` |

⇒ 最小 arity **严格等于** `arguments.size() - default_count`，且 **VARARG 决定是否检查上限**
（有 rest 的 `add` 放行 5 个实参，无 rest 的 `h` 不放行）。
**第四轮的 `default_count` 修正在 Godot 侧有可见后果**：修正前 `f(a=1,b)` 会报 `at least 1`（错误放行 `f(1)`）。

## 顺带修正一条此前的错误归因（重要）

第三轮曾写「GDScript 对**脚本实例上的方法调用不做 arity 检查**」，并据此删掉了 gdcheck 里三行断言、
注明"不是 A3 的证据"。**该归因是错的**。

真实原因是**探针选错了脚本**：那三行用的是 `derived_instance`（`StaticMembersDerived` 实例），而 `add`
是**基类**的方法。按定案，`Script::get_method_info` **只报自有方法** ⇒ 分析器对派生脚本的 `add`
**拿不到 `MethodInfo`** ⇒ 不检查 arity。改用**自有**该方法的脚本（`StaticMembersTarget`）后，
arity 检查立刻生效（见上表）。

⇒ 判据不是"GDScript 不检查"，而是"**分析器能否为该方法拿到 `MethodInfo`**"：
自有方法 ⇒ 检查；继承方法 ⇒ 不检查（静默放行）。这与「四个 Script 钩子只报自有、走链是调用方的事」
那条架构定案**完全一致**，本轮只是补齐了它的一个可观测后果。

（gdcheck 里那三行的注释仍写着旧归因。用户已指示**不固化**本轮用例，故既不改那三行也不新增断言；
正确归因留在本节。）
