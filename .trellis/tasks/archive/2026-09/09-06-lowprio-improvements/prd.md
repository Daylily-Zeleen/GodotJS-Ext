# 低优先级改进集（父任务）

> 长期 backlog 任务树（全 P3）。

## Goal

承载低优先级改进项的任务地图。均为长期愿望/上游依赖型工作，无当前排期压力；父任务不做直接实现，待各子任务启动时再看是否需要 design.md。

## 任务地图

| 子任务 | 内容 | 类型 |
|---|---|---|
| ~~`lowprio-lws-pic`~~ | ~~lws 非 PIC（Linux）~~ | ~~构建/上游重打包~~ **已完成（2026-09-26，已归档）：自建 -fPIC 的 lws 发布为 lws_4.3.zip，恢复 SConstruct 的 linux lws 链接分支；CI PIC 校验通过（run 36152717104）。遗留：Linux 上 websocket 的运行时功能需一次手测** |
| ~~`lowprio-hermes-engine`~~ | ~~添加 Hermes 引擎与 NAPI~~ | ~~新功能（大）~~ **已取消（2026-09-21）** |
| ~~`lowprio-tsc-compiler-api`~~ | ~~TSC Compiler API 替代外部 tsc~~ | ~~编辑器优化~~ **已取消（2026-09-21）** |
| ~~`lowprio-scenetree-quit-mainloop`~~ | ~~quit SceneTree <- MainLoop~~ | ~~原始愿望~~ **已关闭（2026-09-24，无对应实现）** |
| ~~`lowprio-node-orphan-stringname`~~ | ~~node 构建 Orphan StringName 泄漏~~ | ~~缺陷排查~~ **已结案（2026-09-26，已归档）：根因为 node 腿的扩展 DLL 在 StringName::cleanup() 前未卸载（v8 腿能卸载）；依赖仓侧以 libuv 控制台线程可关闭（patch_libuv_console.py）解除卸载阻塞** |
| ~~`lowprio-tree-sitter-ast`~~ | ~~tree-sitter 解析 AST 替代正则~~ | ~~编辑器优化~~ **已否决（2026-09-28）：不引入，代价 ~9 MB 语法源码 + 六平台接线；类型信息走既有注解路线** |
| ~~`lowprio-config-compile-flags`~~ | ~~编译参数控制 jsb.config.h~~ | ~~构建改进~~ **明确不做（2026-09-28）：手工编辑已满足需要；头文件含 ABI/序列化敏感项，参数化收益低于成本** |
| ~~`lowprio-uint64-bigint-codegen`~~ | ~~bigint/uint64 codegen~~ | ~~类型映射评估~~ **已完成（已归档）：由 09-24-uint64-bigint-* 系列（arg / ctor-operators / return / switch）承接并完成** |

## Acceptance Criteria

- [x] 各子任务完成或明确关闭（附理由） —— 全部 5 个子任务均已归档并附处置结论（2026-09-28）

---

# 父任务收尾（2026-09-28）

长期 backlog 归组任务关闭：全部 5 个子任务均已归档，且各自附处置结论
（`archive/2026-09/` 下对应目录的 `prd.md` / `report.md`）。

| 子任务 | 处置 |
|---|---|
| `lowprio-lws-pic` | 已完成（2026-09-26）—— 自建 -fPIC 的 lws，恢复 linux 链接分支 |
| `lowprio-tree-sitter-ast` | 已否决（2026-09-28）—— 评估后不引入 |
| `lowprio-node-orphan-stringname` | 已结案（2026-09-26）—— 根因定位 + 依赖仓侧解除 |
| `lowprio-config-compile-flags` | 明确不做（2026-09-28） |
| `lowprio-uint64-bigint-codegen` | 已完成 —— 由 09-24-uint64-bigint-* 系列承接 |

验收标准「各子任务完成或明确关闭（附理由）」已满足。
