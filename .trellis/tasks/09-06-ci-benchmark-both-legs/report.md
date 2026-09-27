# CI benchmark job 双腿对比回填 — 执行记录

## 结论

**AC1 达成、AC2 达成**：`benchmark` job 在 CI 上首次全绿，双腿都跑完、不再挂死，
报告回填了双腿对比数据（含体积对比）。AC3（Operators 组扩展 case）未动，仍开放。

## 根因（2026-09-25 定位）

此前记为「引擎 teardown 的既有 SEGV」——**记录有误**。真因是 job 自身缺陷：
它从不生成项目的运行时状态，JS 运行时**根本起不来**，于是没有任何东西会退出：

```
ERROR: No loader found for resource: res://tests/start.ts
Error: unknown module: @tests/paths_test/paths-test
Error: godot class not found 'Node'
```

`start.ts` 的 import 在其 `finally { quit() }` 之前求值；import 一抛，引擎就空转到
6h job 上限。而「teardown SEGV」是**崩溃**（进程退出），根本不会造成挂死——两回事。

缺的三件产物，都是 `test` job 有、本 job 没有的：

| 缺件 | 后果 | 生成者 |
|---|---|---|
| `.godot/extension_list.cfg` | 扩展从未注册，没有任何 JS 脚本加载器 | 编辑器插件 |
| `.godot/godotjs_ext/.paths_mapping` | `@tests/*` 别名解析失败（`start.ts:10`） | 编辑器插件 `_regenerate_paths_mapping` |
| `.godot/.api_dumping/*.capi` | JS 运行时解析不了引擎类（`godot class not found 'Node'`） | `--godotjs-api-generate` |

三者全部出自**编辑器库**，而该 job 连编辑器库都没有：`benchmark-build` 只 stage 运行时
`.so`，job 只下载 `benchmark-*`。

## 为何长期未暴露

- job 是 `workflow_dispatch` 专属 → 所有 push run 里都是 `skipped`，从未真正执行过
  （回查 40 个 run，无一例外）。
- GitHub 不为「永不结束的 step」落日志（API 返回 `BlobNotFound`），**挂住把原因也藏了**。

## 修复

- commit `5faab4d`：`needs: [environment, benchmark-build, build]`（取编辑器库）+
  `Download editor extension (linux)` + `Stage extension libraries` +
  `Generate project runtime state`（dump api → api-generate，并断言三个产物）+
  `timeout 300` + `timeout-minutes: 45` + 启动失败时明确报错。
- commit `14c2dad`：只在新路径才暴露的第二个缺陷——一致性门禁跨两个**进程**比较
  `Node.get_instance_id(0)`（`static=31021073896` vs `dynamic=31071405544`），
  天然永不相等。新增 `processDependent` 标记（harness 带进 `BENCH_JSON`），门禁豁免该类。

## 复现与验证

Linux（WSL + Godot 4.7.1 + CI 真实产物）逐层复现：

| 状态 | 结果 |
|---|---|
| 新鲜 checkout（只有 `tsc` 产物） | `No loader found for resource: res://tests/start.ts` → 300s 超时（= CI 的 6h 上限） |
| + `extension_list.cfg` + `.paths_mapping` | 下一层错误：`[API Tool] Initialize error (File not found)` |
| 完整生成的 `.godot` | **双腿通过**（82s / 78s，exit 0，有 `BENCH_JSON`） |
| 修复后的完整序列（新鲜 checkout → 生成 → 双腿） | `paths_mapping OK: @tests/*=tests/*`、`leg=static OK bytes=24880`、`leg=dynamic OK bytes=24901`、**BOTH LEGS OK** |

**CI 实测（run `36114551388`，binding_mode=shared）**：`benchmark` job 16/16 step success，
日志含 `paths mapping: @tests/*=tests/*`、
`consistency gate passed (234 cases, 1 process-dependent skipped: Node.get_instance_id(0))`，
产物 `benchmark-report` 含 `report.md` + 双腿 json + gdb log。

报告摘要：

```
# Static vs Dynamic binding benchmark (ns/call, lower is better)
engine: 4.7.1-stable (official)  |  invalid cases skipped
## Binary size
- dynamic gdextension: 35.33 MiB
- static gdextension: 69.20 MiB
| Dictionary.size(0) | 24.3 | 118.1 | 0.21x |
```

## 验收

- [x] AC1：CI benchmark job 全绿（static + dynamic 两 leg 都跑完不挂死）
- [x] AC2：报告回填双腿对比数据（含体积对比）
- [ ] AC3：Operators 组扩展 case（来自 `bench-operators-expansion`）纳入双腿报告 —— **未做**
