# 低优先级改进集（父任务）

> 长期 backlog 任务树（全 P3）。

## Goal

承载低优先级改进项的任务地图。均为长期愿望/上游依赖型工作，无当前排期压力；父任务不做直接实现，待各子任务启动时再看是否需要 design.md。

## 任务地图

| 子任务 | 内容 | 类型 |
|---|---|---|
| `lowprio-lws-pic` | lws 非 PIC（Linux） | 构建/上游重打包 |
| `lowprio-hermes-engine` | 添加 Hermes 引擎与 NAPI | 新功能（大） |
| `lowprio-tsc-compiler-api` | TSC Compiler API 替代外部 tsc | 编辑器优化 |
| `lowprio-tree-sitter-ast` | tree-sitter 解析 AST 替代正则 | 编辑器优化 |
| `lowprio-scenetree-quit-mainloop` | quit SceneTree <- MainLoop | 原始愿望（代码无对应实现，启动前先评估必要性） |
| `lowprio-node-orphan-stringname` | node 构建 Orphan StringName 泄漏 | 缺陷排查 |
| `lowprio-config-compile-flags` | 编译参数控制 jsb.config.h | 构建改进 |
| `lowprio-uint64-bigint-codegen` | bigint/uint64 codegen | 类型映射评估 |

## Acceptance Criteria

- [ ] 各子任务完成或明确关闭（附理由）
