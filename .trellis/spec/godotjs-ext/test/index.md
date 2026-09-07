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
- `--gc`：每个 case 计时前请求一次全量 GC，消除前序组遗留 wrapper Variant 的 GC 压力污染。GC 入口为 JS 全局 `gc()`（`jsb_environment.cpp` 挂载 → `Builtins::_gc` → `Environment::gc()` → `LowMemoryNotification`，**同步语义**：返回即收集完成，禁止用 sleep/定时器等待）。未暴露 gc 入口的构建上自动降级 no-op + 一次性 WARNING；报告 JSON 有 `gcRequested` 字段供采数脚本核验 `--gc` 确实生效
- **采数纪律**：用 `python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix` 固化流程——脚本自动执行 dll md5 前后双查（后台 scons 中途完成会即时拦截）、按日志指纹（"static binding not found" 回退警告数）验证腿身份、`gcRequested` 字段验证 `--gc` 生效、COMPLETED/exit/invalid 逐轮核验，最后产出 `report.md` 中位数表。双腿/双开关对比必须各采 ≥3 轮取中位数，且全程同一 dll；报告 JSON 的 `staticBinding` 字段不可信，dll 身份只认 md5 + 构建命令
- 验收：exit code == 0 且无 Orphan StringName（`--verbose` 下 grep Orphan）
- 已知遗留（不视为失败）：remove_child / queue_free / add_child 各 1 个 orphan（start.ts 的 call_deferred 方法名字面量）

## TS 集成测试前提

- 先生成 api 数据（dump → api-generate，见 [codegen-baseline.md](./codegen-baseline.md) 触发链）并编译 TS（`cd project && node_modules/.bin/tsc --noCheck`），再 `godot --path ./project --verbose`
- 结尾哨兵：`GODOTJS_TEST_PROJECT_COMPLETED` 为成功、`GODOTJS_TEST_PROJECT_FAILED:` 为失败

## 质量检查（所有测试通用）

- [ ] 验收：exit code == 0、无资源泄漏（无未释放 Resource、无 Orphan StringName）
- [ ] 改 TS 后重跑了 `tsc --noCheck`（引擎加载的是编译产物）
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
