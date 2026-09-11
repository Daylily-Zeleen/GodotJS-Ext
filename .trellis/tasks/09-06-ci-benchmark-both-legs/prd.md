# CI benchmark job 双腿对比回填

> 静态绑定收尾任务之一。

## Goal

CI benchmark job 恢复 static/dynamic 双腿对比数据：dynamic leg 因 C1 崩溃挂死（run 33946389411 已取消），job 本身的环境三层修复已完成。

## 现状

- 环境三层修复已完成并合入：896b942（scons 安装）、804e6f0（godot-cpp platform 名 linux）、b7a1bd4（shadow_realm 去 always_inline）
- C1 崩溃已结案：`09-06-c1-dynamic-crash` 已归档（根因修复 a5f0db9，ptrcall 缓冲区溢出），dynamic leg 阻塞解除

## 依赖

- 原阻塞项 `09-06-c1-dynamic-crash` 已归档结案（根因 a5f0db9），阻塞解除，CI 配置可动
- Comparison report step 的 Python 语法错误（`lines = [...]` 闭合后悬空 `]`）已于 2026-09-12 修正；剩余工作 = workflow_dispatch 触发 benchmark job 跑通双腿并回填报告

## Acceptance Criteria

- [ ] CI benchmark job 全绿（static + dynamic 两 leg 都跑完不挂死）
- [ ] bench 报告回填双腿对比数据（含体积对比）
- [ ] Operators 组扩展 case（来自 `bench-operators-expansion`）纳入双腿报告

## Notes

- CI 无 api-dump job；codegen 源 = godot-cpp 内置 json（见 `.trellis/spec/godotjs-ext/build/scons-build.md`）
