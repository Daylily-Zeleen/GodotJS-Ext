# 测试与校验规范（godotjs-ext）

> 适用范围：C++ doctest 测试、TS 集成测试、benchmark、代码生成基线校验。
> 构建接线（SCons、dll 部署）见 [../build/index.md](../build/index.md)。

## 开发前检查单

- [ ] 改 C++ 测试？读 [doctest.md](./doctest.md)（双套件机制、注册表规则、过滤语义）
- [ ] 改 TS / 编辑器生成代码 / 基线？读 [codegen-baseline.md](./codegen-baseline.md)（headless 触发链、校验方法论、TS 缓存陷阱）

## Benchmark 专项注意

- 命令：`cd project && godot --audio-driver Dummy --headless --path . -- --bench [--gc] [--only=<组>]`
- **所有开关都是 user args**（在 `--` 之后）：`--bench`（start.ts 用 `get_cmdline_user_args()` 判断，选择只跑 benchmark 场景）、`--gc`、`--only=<组>`（benchmark.ts 用 `get_cmdline_user_args()` 解析）。引擎参数区（`--` 之前）不放任何 bench 开关——把 `--bench` 写在 `--` 之前会让引擎试图解析它而测试项目收不到
- `cases.builtin.ts` 为**手维护**（原 `generate_benchmark_cases.py` 生成器已移除，新增 case 直接编辑该文件）；改动后 `cd project && node node_modules/typescript/bin/tsc --noCheck` 重编
- `--gc`：每个 case 计时前通过 JS 全局 `gc()` 请求回收（`Builtins::_gc` → `Environment::gc` → 各环境 `add_async_call(TYPE_GC_REQUEST)`）。同线程立即执行 `_on_gc_request`，其他线程入队；返回不保证 worker 已回收。跨线程生命周期回归应有界等待可观测状态，不用任意 sleep 代替完成信号。未暴露 gc 的 benchmark 构建降级 no-op 并 WARNING；`gcRequested` 只表示请求。
- **采数纪律**：用 `python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix` 固化流程——脚本自动执行 dll md5 前后双查（后台 scons 中途完成会即时拦截）、按日志指纹（"static binding not found" 回退警告数）验证腿身份、`gcRequested` 字段验证 `--gc` 生效、COMPLETED/exit/invalid 逐轮核验，最后产出 `report.md` 中位数表。双腿/双开关对比必须各采 ≥3 轮取中位数，且全程同一 dll；报告 JSON 的 `staticBinding` 字段不可信，dll 身份只认 md5 + 构建命令
- 验收：exit code == 0 且无 Orphan StringName（`--verbose` 下 grep Orphan）
- 已知遗留（不视为失败）：remove_child / queue_free / add_child 各 1 个 orphan（start.ts 的 call_deferred 方法名字面量）

## TS 集成测试前提

- 先生成 api 数据（dump → api-generate，见 [codegen-baseline.md](./codegen-baseline.md) 触发链）并编译 TS（`cd project && node_modules/.bin/tsc --noCheck`），再 `godot --path ./project --verbose`
- 结尾哨兵：`GODOTJS_TEST_PROJECT_COMPLETED` 为成功、`GODOTJS_TEST_PROJECT_FAILED:` 为失败

## 跨环境通信测试后端选择

### 范围
`project/tests/cross-environment` 的对象转移与基础消息往返共用后端调度，不再将基础会话固定为 Worker。

### 命令
`godot --headless --path project` 默认依次运行 worker、shadow。单后端使用 `-- --object-transfer-backend=worker` 或 `-- --object-transfer-backend=shadow`；参数从 `OS.get_cmdline_user_args()` 读取。

### 契约
每个后端先跑 owned/native-owned/refcounted/persistent，再跑三轮基础消息会话（Godot/JavaScript 载荷、Dictionary，第一轮另含 plain）。`--object-transfer-case=<场景>` 仅限制对象用例、跳过基础会话，但仍遍历所选后端。Worker 等待 onready，ShadowRealm 同步加载 startupScript；使用真实 peer，结束时 terminate。

### 校验与错误
非法 backend（包括空值）报告 FAILED，不启动任一后端。运行时不支持的传输必须失败，不得静默跳过或替换为 Worker。成功判据为 exit 0、COMPLETED 且无 FAILED；失败哨兵优先于 exit 0。

### 基例、好例、坏例
无参数是双后端全套；指定 shadow 加 owned 是 shadow 单场景；backend=invalid 是应拒绝的输入。

### 验证点
默认日志必须有两后端四个对象用例及三轮 session 的 done；显式单后端不得出现另一后端 start；仅 case 筛选仍须有两后端该用例 done。检查场景断言、完成哨兵与进程退出，不只检查 start。

### 错误与正确
错误：shadow 对象测试后继续创建 JSWorker 执行基础会话，声称双覆盖。正确：整个后端会话使用同一 peer 工厂和真实所选实现，所有共享断言都执行。

### QuickJS-NG shadow 传输支持
`TransferableShadowRealmImpl::handle_post_message` 与 `_on_message` 已与 `Worker` 序列化路径对齐：统一走 `VariantSerializerDelegate`/`VariantDeserializerDelegate`（不再由 `#if JSB_WITH_V8` 门控），复用 `Worker::parse_transfer_list`，接收侧补 `ThreadSafeForNodesScope`，发送侧按 transfer_index 定序。QuickJS-NG 的 shadow 对象转移已全绿；worker 与 shadow 可并列声称双后端覆盖。

## 陷阱：TS 集成测试「挂死」（不是失败，是永不退出）

**症状**：`run-runtime-matrix.mts` 卡在 `[run] host-<engine> run` 无任何输出，可挂数小时不退出（CI 上表现为 job 卡到被取消）。

**机制**：`runCommand()` 用 `spawnSync` 同步等引擎进程，**自身无超时**。任何让引擎不退出 `SceneTree` 的错误都会变成无限挂起：

1. `start.ts` 的模块级 `import`（如 `@tests/...` 别名、`./test-status`）解析失败 → `start.js` 附加失败 → `_ready` 从未执行 → `finally { quit() }` 永不运行。注意模块级 import 在 `_ready` 的 `try` **之外**，不受其保护。
2. `start.ts` 的场景循环里某场景从不触发 `completeCallback`（该分支没有 `setTimeout` 兜底）→ 循环永久 await。

**排查要点**：

- tsc **不会重写** `paths` 别名。`tsconfig.json` 的 `paths`（如 `@tests/*` → `./tests/*`）只影响类型检查与智能提示，**输出里保留裸说明符**（`require("@tests/...")`），改由引擎运行时经 `.paths_mapping` 解析。因此 tsconfig 加了别名却缺 `.paths_mapping`，编译能过、运行必炸
- `.paths_mapping` 在 `project/.godot/godotjs_ext/` 下（与编译产物同目录），由**编辑器**插件写（`PathsMapping::generate_from_tsconfig`），运行时在 `GodotJSScriptLanguage::init` 里 **eager 加载一次**
- 该目录一旦整体缺失，`FileAccess` 不会创建父目录 → 写入静默失败（日志仅 `cannot open ... Can't open file`）。`rm -rf project/.godot`（CI 的 Linux 诊断步骤会做）即触发该形态
- 判别日志：`unknown module: @tests/...` + `failed to attach module res://.../tests/start.js`

**判别挂死而非慢**：`timeout 60 <engine> --audio-driver Dummy --headless --path project`，rc=124 即挂死。正常整轮约 1.5 分钟。

**修复方向**（按优先）：让生成端在缺目录时自建（`DirAccess::make_dir_recursive_absolute`，见 `jsb_codegen_generator.cpp` 惯例）；CI 侧给该 step 加 `timeout-minutes` 兜底。**不要**靠“重跑一次”绕过——只要 `.godot` 被清空就会复现。

## 陷阱：从 JS 构造 RefCounted 类会让 QuickJS 断言中止

**症状**：`new Curve2D()` / `new AStar2D()` 等 **RefCounted 派生类**的 JS 构造触发

```
ERROR: FATAL: Condition "!(isolate_ && weak_type_ == WeakType::kStrong && is_alive())" is true.
   at: SetWeak (src/runtime/impl/quickjs/jsb_quickjs_handle.h:278)
terminate called after throwing an instance of 'std::system_error'  what():  Resource deadlock avoided
```

引擎以 abort 退出（Linux 上 `exit code null`），整个测试腿 FAIL。Node 派生类（`TabBar`/`CodeEdit`）、builtin（`GArray`）不受影响。

**机制**：同一指针被 **弱化两次**。`bind_pointer()` 对 `is_js_owned()` 的对象已调用 `SetWeak`；随后引擎的 reference 回调（每对 inc/dec 各一次）在 DECREF 分支再次 `SetWeak` 同一 handle。V8 的 `Global::SetWeak` 在此**幂等**，但 QuickJS/JSC/Web 的 shim 实现要求 handle 当前必须是 strong，否则 `jsb_check` 中止（`JSB_WITH_CHECK` 仅在 `JSB_DEBUG` 下开，故 **release 构建不复现**）。

**判别**：instrument 两个调用点（`bind_pointer` 与 `reference_object` 的 DECREF 分支）打印 `p_pointer`，同址两次即命中。或直接构造最小复现：测试项目里 `new Curve2D()` 单独一句。

**修复方向**：用 `IsWeak()` 守卫 DECREF 分支的 `SetWeak`（shim 需补 `IsWeak()`——`v8::Global` 本就有，三个 shim 原先都缺）。不要改 `jsb_check` 或降级为告警。

## 质量检查（所有测试通用）

- [ ] 验收：exit code == 0、无资源泄漏（无未释放 Resource、无 Orphan StringName）
- [ ] 改 TS 后重跑了 `tsc --noCheck`（引擎加载的是编译产物）
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
