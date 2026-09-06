# TypeScript Compiler API 替代外部 tsc

> 来源：`.本地文档/低优先级.md` TSC Compiler API 条目（P3，编辑器优化）。

## Goal

评估并用 TypeScript Compiler API（进程内）替代当前外部 `tsc` 子进程调用，消除子进程启动开销与 `.tsbuildinfo` 增量缓存陷阱。

## Background

当前编译测试项目 TS 的方式：`cd project && node_modules/.bin/tsc --noCheck`（外部进程）。
已知陷阱（`.trellis/spec/godotjs-ext/test/codegen-baseline.md`）：`.godot/.tsbuildinfo` 只看源码哈希、不检查输出文件是否存在——产物被删后 tsc 不会补发，需手动删缓存重编。

## Requirements

- 先做可行性评估：Compiler API 能否覆盖现有 tsc 用途（仅 `--noCheck` 类型转译 + emit；类型检查未被使用）
- 评估 editor 扩展内嵌 Compiler API 的载体（Node 侧运行时已有 `scripts/jsb.editor/` bundle 体系）
- 若可行：新 emit 路径需与 tsc 产物字节级兼容（或至少语义等价），并处理增量状态（可保留 tsbuildinfo 或改为输出存在性检查）
- 若不可行：记录评估结论并关闭本任务，保留外部 tsc

## Acceptance Criteria

- [ ] 可行性结论落档（含 Compiler API 版本与所需 Node API 面）
- [ ] 若实施：产物与 tsc 输出对比通过；`.tsbuildinfo` 陷阱消除或缓解
- [ ] 若放弃：结论与本任务关闭理由记录于归档

## Notes

- 相关：`09-06-lowprio-tree-sitter-ast`（同为解析/编译链优化）
