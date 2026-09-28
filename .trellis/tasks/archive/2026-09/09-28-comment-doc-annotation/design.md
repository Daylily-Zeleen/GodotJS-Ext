# 技术设计：源文件注释 → 编辑器脚本文档（常驻 Node 编辑器工具进程）

> 依据：`prd.md`（R1–R9 / A1–A9）、`research/comment-parsing-options.md`（解析方式实测）、
> 用户 2026-09-28 的架构裁决（常驻进程 + IPC + 帮助文档不落盘）。
> `file:line` 均指本检出。

---

## 0. 本轮用户裁决（取代 `implement.md` 里的短命进程形态）

- **常驻 Node 进程**：编辑器工具（函数/信号签名提取 + 帮助文档提取）由一个**长驻**进程执行，
  通过**进程间通讯**按需触发，不再每次 `spawn` 一个短命进程。
- **帮助文档不进文件系统**：随问随答、缓存在**内存**里。若最终选择落盘，必须满足两条硬约束
  （不会被导出打包；能被 `cleanup_invalid_files()` 清理）。
- **`@bind.help()` 仍然有效**（用户早前裁决），且**显式声明优先**于注释。

---

## 1. 为什么"常驻 + IPC"是更优形态（相对现状 `Process::create` 短命进程）

现状（`jsb_editor_plugin.cpp:1400-1423`）：每次摘要变化就 `Process::create("signature", "node.exe", args)`，
进程跑完即退。**每次都要重新加载 TypeScript 编译器**（`require("typescript")`，数十 MB JS 解析 + JIT），
这是主要成本，且随触发次数线性重复。

常驻形态把这份成本**只付一次**，同时带来三个结构性好处：

| 好处 | 说明 |
|---|---|
| 消除重复加载 | TS 编译器只在进程启动时加载一次 |
| **帮助文档可以真的"随问随答"** | 短命进程做不到"打开某个脚本时立刻要它的文档"——每次都要起进程 |
| 宿主侧代码收敛 | 启动/重启/心跳/退出都落在一个组件里，不再散落在 4 个触发点 |

代价（如实记录）：多一个需要生命周期管理的子进程；IPC 两侧都要定协议；
进程崩溃要有重启路径。

---

## 2. 复用既有进程设施：`jsb::internal::Process` 已经够用

`src/internal/jsb_process.{h,cpp}` 已实现（09-26 任务为短命进程修过三处缺陷）：

- `Process::create(name, exe, args)` → 后台线程持续读取子进程 stdout（`jsb_process.cpp:171-175` / `:311-339`）；
- `stop()` 幂等、必然 join 读取线程（`:218-222`、`:370-379`）；
- 继承父进程 **stdin**（`STARTF_USESTDHANDLES` 只重定向 stdout：`:139`；POSIX 只 `dup2` STDOUT：`:288-290`）。

⇒ **宿主 → 工具的请求可以直接写子进程 stdin**，工具 → 宿主的答复走 stdout（已有读取线程）。
**不需要 socket、named pipe、共享内存**，也不引入任何新依赖。

需要给 `Process` 补的最小能力（`src/internal/jsb_process.h/.cpp`）：

```cpp
// 写子进程 stdin（行分隔报文）。返回 false 表示管道已不可用（子进程已退出）。
virtual bool write_stdin(const String &p_text) = 0;

// stdout 每读到一行就回调（默认实现为空 = 现有"只打日志"行为）。
virtual void on_stdout_line(const String &p_line) {}
```

### 2.1 线程安全：**必须用 `std::` 同步原语**（实测约束，不是风格偏好）

`on_stdout_line()` 在 `ProcessImpl` 的**读取线程**上被调用，而 `write_stdin()` 由**宿主线程**调用
⇒ 工具对象的应答缓冲区被两个线程触碰。

- ❌ **不能**用 `godot::Mutex` / `godot::Thread`：读取线程是**裸 OS 线程**（`jsb_process.cpp:170-172`
  在 Windows 上直接 `ReadFile`），而 godot-cpp 的 `Mutex` 在 `THREADS_ENABLED` 下是
  `_PRIVATE` 构造（`third/godot-cpp/include/godot_cpp/templates/mutex.hpp`）⇒ 宿主外的线程上不可用。
- ✅ 本项目已有 `std::mutex` / `std::atomic` 的先例（`src/api_tool/core/api_tool_detail_storage.{h,cpp}`、
  `src/runtime/bridge/jsb_environment.h:103`、`src/runtime/weaver/jsb_script_instance.cpp:296`）。

⇒ 管理器侧（阶段 C）用 `std::mutex` + `std::condition_variable` + `std::atomic<int64_t>`（工具对象
现有成员均为裸指针/`Ref<Thread>`，原子替换是唯一可行路径）；`Process` 的 stdin 写端也用 `std::mutex` 保护。

### 2.2 stdout 行回调的所有权

现状：读取线程用 `buffer[4096] + rd_line 累积` 拼行，**行尾即 `_flush()`，随后立即清空**
（`:96-113`、`:344-347`）⇒ 现在是把整行交给 `JSB_LOG` 就地消费，**没有保留**。
⇒ 现有"行缓冲与 stdout 的所有权都在读取线程"的设计必须保持：回调只能**立刻消费**该行
（复制进调用方自己的缓冲），不得保存指向 `rd_line` 的引用。

### 2.3 具体的 fd/HANDLE 方案（两端对称）

| | Windows | POSIX |
|---|---|---|
| stdout | 现有 `CreatePipe` + `SetHandleInformation(pipe[0], HANDLE_FLAG_INHERIT, 0)`（`:134-137`） | 现有 `pipe()` + `dup2(pipefd[1], STDOUT_FILENO)`（`:288-290`） |
| stdin | 再 `CreatePipe(&rd, &wr)`；**`SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0)`**（写端只在宿主）；`si.hStdInput = rd` | 再 `pipe(in)`；子进程 `dup2(in[0], STDIN_FILENO)`、`close(in[1])`；宿主 `close(in[0])` |
| 关断 | 子进程退出 ⇒ 读取线程的 `ReadFile` 失败退出 ⇒ **`on_stdout_line` 收 EOF 通知**（新增 `on_stdout_eof()`） | 同：`read()` 返回 0 / 错误 ⇒ EOF 通知 |

⚠️ 09-26 修的三处缺陷（use-after-free、幂等 `on_stop`、`pipefd` 初值 `{-1,-1}`）都是短命进程暴露的；
常驻进程会暴露**另一半**：`stop()` 必须能在"子进程还活着"时也干净收场（现在已满足），
并且**必须有一个明确的「进程已死」判据**（`_is_running()`）供调用方发现并重启。

---

## 3. 协议（宿主 ↔ 工具，NDJSON）

请求（宿主 → 工具，一行 JSON）：

```jsonc
{"id": 17, "op": "signatures"}                                  // 全项目签名提取（离线产物）
{"id": 18, "op": "doc", "path": "res://tests/foo.ts"}           // 单文件帮助文档（内存即答）
```

响应（工具 → 宿主，一行 JSON；`id` 回显）：

```jsonc
{"id": 17, "ok": true, "written": 18, "up-to-date": 0}          // 签名：写盘结果是结果的一部分
{"id": 18, "ok": true, "class": {"brief": "…", "description": "…"},
 "members": [{"kind": "method", "name": "greet", "brief": "…", "description": "…"},
             {"kind": "property", "name": "hp", "brief": "…", "description": "…"},
             {"kind": "signal",   "name": "changed", "brief": "…", "description": "…"},
             {"kind": "constant", "name": "MAX", "brief": "…", "description": "…"}]}
{"id": 18, "ok": false, "error": "…"}
```

- 一行一条、UTF-8、以 `\n` 结束；非 JSON 行（例如 `require` 的杂音）**忽略并记日志**，不影响协议。
- **`op: "doc"` 只读、幂等、可任意时刻调用**——这就是"用到再去提取"。
- `op: "signatures"` 保持既有的"有摘要门控、增量写盘"语义（见 §5），只是触发方从"起进程"变成"发一行"。

---

## 4. 帮助文档：**直接写进 `ScriptClassInfo`**，不落盘（用户 2026-09-28 裁决）

用户原话：「**获取时直接写到 ScriptClassInfo 里就行了吧，只是还得另外实现刷新机制。**」

⇒ 不新增 sidecar、不新增扩展名、不改打包、不改 `collect_invalid_files()` 白名单。
文档的运行期落点就是**既有的数据模型**：`stateless_script_class_info.doc`
（`jsb_class_info.h:292-293`）与各 `ScriptMethodInfo::doc` / `ScriptPropertyInfo::doc`、
`ScriptSignalInfo::doc` / `ScriptConstantInfo::doc`（后两者本任务新增），
本任务为它们补 `description` 字段（§7）。

### 4.0 方向：**编辑器推**，不是运行时拉（跨库约束决定的）

`runtime` 库不能调用 `editor` 库（依赖方向只有 editor → runtime）。而 `_get_documentation()`
在 runtime 侧、由引擎在脚本加载路径上调用 ⇒ **不能在运行时里"按需向工具要文档"**。

⇒ 由**编辑器**在已知的时机取回文档，再调用 runtime 导出的入口写进 `ScriptClassInfo`：

```
编辑器触发（与 _regenerate_signatures 同节奏）
  └─ 向常驻工具发 {"op":"doc","all":true}（工具用与签名提取同一套枚举找 Godot 脚本）
       └─ 应答 {"docs":{"res://<rel>.ts":{class:{...},members:[...]}, ...}}
            └─ runtime 导出入口 apply_script_docs(module_id, payload)
                 └─ 写入 stateless_script_class_info.doc / methods[*].doc / properties[*].doc
                     / signals[*].doc / constants[*].doc（`@help` 优先，见 §4.1）
```

**这同时满足"用到再去提取"的诉求**：引擎侧 `_get_documentation()` 仍是唯一的读取点，
只是数据已在脚本加载前由编辑器推入（脚本加载 → 读文档之间没有窗口，§4.3）。

### 4.1 回填的优先顺序（R6，用户裁决 **`@help` 优先**）

- `_parse_script_doc(…)` 已在解析期把 `@bind.help` 写进 `doc.brief_description`
  （类 `jsb_class_info.cpp:407` / 方法 `:450` / 属性 `:575`）；
- 推入的源注释只在**该字段为空**时补写 `brief_description`，**绝不覆盖非空值**；
  `description` 始终来自源注释（`@bind.help` 没有全文这一档）。
  ⇒ 旧注解行为完全不变。

### 4.2 刷新机制（用户指出的必要件）

文档写进 `ScriptClassInfo` 后是常驻的，所以要有明确的失效判据。编辑器侧按 `source_md5` 记账：

| 层 | 触发 | 判据 | 动作 |
|---|---|---|---|
| L1 源文件变了 | 编辑器保存 / `EditorFileSystem` 重扫 | 编辑器记录 `path -> 上次推送时的源 md5`，与当前 md5 不等 | 重新请求该文件的文档并**覆盖**已写入的字段 |
| L2 类信息被重建 | 模块重新解析（hot-reload / `_reload()`） | `ScriptClassInfo` 是新建的 ⇒ 文档字段为空 | 走 L1 的同一路径（字段为空即需推送）自然补上 |
| L3 工具重启 / 请求失败 | 工具崩溃后重建、请求超时 | 编辑器侧记账里**只有成功应答才写入 md5** | 未成功 ⇒ 下次触发重问；**不把"没拿到"记成"已同步"** |

工具侧同样按 `source_md5 -> 文档结果` 做**进程内缓存**：md5 未变时立即回缓存，
编辑器反复重问不会重复解析。

### 4.3 为什么不需要悬空窗口保护

宿主入口是脚本加载路径（`_ensure_signature_manifest()` / `_get_documentation()`），
引擎侧由 `EditorFileSystem::_update_script_documentation()` 驱动，它**先 `ResourceLoader::load()`
拿到脚本对象**（`editor_file_system.cpp:2265`）再读文档 ⇒ 写 `ScriptClassInfo` → 读文档之间
没有可插入的窗口。

---

## 5. 签名提取：不变的部分

- 输入：源文件（`.ts` / `.js`，见 §6）；判据 `isGodotScript` 不变；
- 产物：`<outDir>/<rel>.sig`（`JSB_SIGNATURE_EXT`），**打包与清理白名单照旧**
  （`jsb_export_plugin.cpp:173-190`、`collect_invalid_files` 的 `.sig` 分支、
  `test_jsb_editor_cleanup.h`）；
- 门控摘要：仍由宿主算、宿主判（`.ts`+`.js` 数 | 最大 mtime | 配置文件 md5 | 工具 md5），
  未变化时**连请求都不发**（与现状"摘要相等即空操作"等价）；
- 增量：提取器内部仍按 `source_md5` 跳过未变文件。

⇒ **本任务不改变签名产物的任何契约**，只把"谁来跑"从"短命进程"换成"常驻进程"。

---

## 6. 注释解析与关联规则（用户早前裁决，不变）

1. 取「声明之前的**最后一段** `/** … */`」，其中「声明」= **其全部装饰器（`@decorator`）与修饰符 + 声明本体**
   ⇒ 注释在装饰器**之上**或**之下**都算；同时取所有注解的末端与声明起点作为上界。
2. 注释末端到该上界之间出现**空行** ⇒ 判为没有文档。
3. **首行 = brief，全文 = description**。
4. 覆盖对象：类、方法、属性、getter/setter、信号（`accessor` + `@bind.signal()`）、常量/静态成员。
5. **只取紧邻声明的那一段**，不看文件头部的无关注释（用户明确要求）。
6. 成员名 = **源码成员的 JS 名逐字**，与运行时 `script_class_info_.methods` / `.properties` 的登记键
   同源（运行时按 `prototype` 的自有属性名与注解给出的 `name` 登记，
   `jsb_class_info.cpp:440-453` / `:556-575`）⇒ 不需要映射层。
   （既有 `_` 前缀的暴露名差异属签名路径的既存偏差，本任务不扩围。）

**实测**（ts 6.0.3）：`node.jsDoc` 在"注释位于装饰器之后"时为空，`ts.getJSDocCommentsAndTags` 同样为空，
⇒ 两个位置必须走**同一条源码文本扫描**，不能依赖 AST 上的 JSDoc。

---

## 7. 运行时消费（`_get_documentation()`）

数据模型（`jsb_class_info.h`）：

| 结构 | 新增 |
|---|---|
| `ScriptBaseDoc` | `String description;`（语义 = `DocData::*Doc::description`） |
| `ScriptSignalInfo` / `ScriptConstantInfo` | 各挂 `doc`（`ScriptBaseDoc` 派生） |
| `ScriptMethodInfo` / `ScriptPropertyInfo` | 已有 `doc`，补 `description` 填充 |

权威顺序（用户裁决 **`@help` 优先**）：

```
_parse_script_doc(...)         // 装饰器先写（类 :407 / 方法 :450 / 属性 :575）
        ▼
文档来源（sidecar 或工具的应答）只在 brief 为空时补写，绝不覆盖非空值
```

`_get_documentation()` 按 `DocData::from_dict` 的键面组装（`core/doc_data.h:698-860`）：
类补 `description`；`properties[]`/`methods[]` 补 `description`（消掉 `// TODO: 填充完整函数文档`）；
新增 `signals[]` / `constants[]`。

**`constants` 的额外改动**（相对早前一版设计）：常量按注解名收集（`ClassConstants`），
与 `MemberDocMap` 无关联 ⇒ 需要**多一个查询：`jsb.internal.get_script_doc(target, name, field)`**，
让绑定层把常量的 doc 一并放进类信息。这是本任务唯一新增的 JS↔C++ 接口。

---

## 8. 兼容性与降级矩阵

| 情形 | 行为 |
|---|---|
| 工具进程**崩溃 / 已退出** | **不静默**：`ERR_PRINT` 报出原因 → `JSB_LOG(Error, "restarting …")` → 重启并重试本次请求（每请求至多 1 次）→ 仍失败则 `ERR_PRINT` 声明"已放弃本次 op"（§9.1） |
| 请求**超时**（>5s） | **不静默**：`ERR_PRINT` 报出 `op` / 文件 / 超时值 → 丢弃该 in-flight（**不写 md5**）→ 不 `stop()` 进程（§9.1） |
| 项目未装 `typescript` | 与现状一致：`JSB_LOG(Warning)` 并跳过（`jsb_editor_plugin.cpp:1388-1391`）。**已裁决**：`.js` 项目同样要求该包（§9） |
| `JSB_USE_TYPESCRIPT=0` | 工具不启动；文档为空（与现状一致） |
| 导出后的游戏 | `TOOLS_ENABLED`/`JSB_TOOLS` 编掉 ⇒ 无工具、无文档请求；`.sig` 照包里那份读 |
| 工具启动失败 | `Process::on_start` 自身 `ERR_FAIL_COND_V_MSG`（既有）；管理器再补一条含 exe/参数的 `ERR_PRINT` |

---

## 9. 已裁决（用户 2026-09-28）

| # | 议题 | 结论 |
|---|---|---|
| 1 | 帮助文档传输形态 | **直接写进 `ScriptClassInfo`**（不落盘、不新增产物）；另实现刷新机制（§4） |
| 2 | `js` 项目的解析器 | **一律要求 `typescript` 包**（与现状同一前置；只改 jsconfig 预设与提取器后缀过滤） |
| 3 | 常驻进程形态 | **新建单独的工具进程**（与现有 `tsc` 监听进程无关） |
| 4 | **工具进程崩溃** | **不静默跳过**：打印错误信息 + 明确告知用户"即将重启工具进程"，然后**重启并继续尝试本次工作** |
| 5 | **请求超时** | **阻塞宿主，上限 5s**；超时即放弃本次请求，并**输出错误告知用户发生了什么**（哪个操作、哪个文件、超时值） |
| 6 | `@deprecated` / `@experimental` 的 JSDoc tag | **暂不做**（保持 `@bind.deprecated()` / `@bind.experimental()` 单一路径） |

### 9.1 崩溃/超时的具体语义（对应裁决 4/5）

**崩溃检测**：`Process::_is_running()` 为假、或 `write_stdin()` 返回 false、或读到 stdout EOF。

```
发现工具进程不在 → ERR_PRINT("jsb editor tool process exited unexpectedly: <原因>")
                 → JSB_LOG(Error, "restarting the editor tool process and retrying: <op>")
                 → 重建进程（清零在途请求）→ 重发**本次**请求（每请求至多重试 1 次）
                 → 若重启后仍失败 → ERR_PRINT 说明"已放弃本次 <op>，文档/签名将为空"
```

**超时**（阻塞上限 5s，`op` 级别）：

```
等待第 id 条应答超过 5s → ERR_PRINT("editor tool request timed out after 5s: op=<op> path=<path…>")
                       → 丢弃该 in-flight 记录（**不写 md5** ⇒ 刷新机制 L3 保证下次会重问）
                       → 不 stop() 进程（进程可能只是慢；让它把当前这轮跑完）
```

**绝不静默**：所有失败路径都至少有一条 `ERR_PRINT`/`JSB_LOG(Error)` 提到「哪个 op / 哪个文件 / 为什么」。
