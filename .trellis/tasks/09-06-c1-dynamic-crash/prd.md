# C1 专项：dynamic 路径崩溃/挂死排查

> 静态绑定收尾任务之一。**专项优先级**。

## Goal

修复 dynamic 路径的 SEGV 139（Windows）/挂死（linux CI benchmark job）：ptrcall 参数缓冲区溢出修复（5af231d）后 det==0 flood 已清零，但崩溃仍存在。

## 已排除项（勿重复排查）

- a2a2880 / f894f06——未动 dynamic 文件
- 非法参数 case——已修（6f643dd / 62944f5）
- 71f7545 基线同样崩 → **既有问题**，非本分支引入
- ptrcall 参数缓冲区溢出——已修（5af231d），flood 485→0

## 现象

- 崩溃点在 worker-test 场景后、场景实例化期
- `wrap_cross_env_value` 路径待排查
- linux CI benchmark job 表现为挂死（run 33946389411 已取消），Windows 为 SEGV 139

## Acceptance Criteria

- [ ] 根因定位并修复（不是绕过：不得用超时杀进程/捕获 SEGV 类手段掩盖）
- [ ] Windows 本地：bench 全量 + TS 集成测试 exit code == 0、无泄漏、无 SEGV
- [ ] 修复后 `ci-benchmark-both-legs` 子任务解除阻塞
- [ ] 若涉及新的机制性结论，沉淀到 `.trellis/spec/godotjs-ext/cpp/`（用 trellis-update-spec 流程）

## Notes

- 诊断日志等临时产物放 `.agent_tmp/`
- 相关知识：`.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`、`.trellis/tasks/09-06-static-bindings-wrapup/design.md`
