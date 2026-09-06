# 测试与校验规范（godotjs-ext）

> 适用范围：C++ doctest 测试、TS 集成测试、benchmark、代码生成基线校验。
> 构建接线（SCons、dll 部署）见 [../build/index.md](../build/index.md)。

## 开发前检查单

- [ ] 改 C++ 测试？读 [doctest.md](./doctest.md)（双套件机制、注册表规则、过滤语义）
- [ ] 改 TS / 编辑器生成代码 / 基线？读 [codegen-baseline.md](./codegen-baseline.md)（headless 触发链、校验方法论、TS 缓存陷阱）

## Benchmark 专项注意

- 命令：`cd project && godot --audio-driver Dummy --headless --path . --bench [--only=<组>] [--calls=<N>]`
- `--bench` 是**引擎参数**（不带 `--` 分隔符；start.ts 用 `get_cmdline_args()` 判断）；`--only=<组>` / `--calls=<N>` 是 user args（在 `--` 之后，benchmark.ts 用 `get_cmdline_user_args()` 解析）。混用位置会导致参数不生效（全量跑或过滤失效）
- 验收：exit code == 0 且无 Orphan StringName（`--verbose` 下 grep Orphan）
- 已知遗留（不视为失败）：remove_child / queue_free / add_child 各 1 个 orphan（start.ts 的 call_deferred 方法名字面量）

## TS 集成测试前提

- 先生成 api 数据（dump → api-generate，见 [codegen-baseline.md](./codegen-baseline.md) 触发链）并编译 TS（`cd project && node_modules/.bin/tsc --noCheck`），再 `godot --path ./project --verbose`
- 结尾哨兵：`GODOTJS_TEST_PROJECT_COMPLETED` 为成功、`GODOTJS_TEST_PROJECT_FAILED:` 为失败

## 质量检查（所有测试通用）

- [ ] 验收：exit code == 0、无资源泄漏（无未释放 Resource、无 Orphan StringName）
- [ ] 改 TS 后重跑了 `tsc --noCheck`（引擎加载的是编译产物）
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
