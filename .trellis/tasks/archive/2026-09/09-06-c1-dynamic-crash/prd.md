# C1 专项：dynamic 路径崩溃/挂死排查

> 静态绑定收尾任务之一。**专项优先级**。

## Goal

排查 dynamic 路径的 SEGV 139（Windows）/挂死（linux CI benchmark job）：ptrcall 参数缓冲区溢出修复（5af231d，即 a5f0db9）后 det==0 flood 已清零，但崩溃仍存在（记录于旧 TASK_STATUS 待办 #3）。

## 排查结论（2026-09-06）

**根因 = ptrcall 参数缓冲区溢出（a5f0db9 已修复）；"修复后仍崩溃"不成立——崩溃在 HEAD 上不可复现。**

证据链：

1. **CI 挂死现场跑在修复之前**：run 33946389411 触发于 b7a1bd4（09-05 13:07），溢出修复 a5f0db9 在其后（09-05 22:01）——挂死即溢出 bug 本身（提交信息：485 条 det==0 ERROR 后崩溃）
2. **修复后 CI 从未复测**：benchmark job 仅 `workflow_dispatch` 手动触发（ci.yml:71）；a5f0db9（run 33972120556）与 HEAD 903cb69（run 34004202891）均为 push 触发 → benchmark skipped
3. **HEAD 本地复现矩阵（26+ 轮）全绿**：dev/release × static/dynamic × 4.7.1/4.8.dev，全部 exit=0、COMPLETED、无泄漏、无 SEGV、无进程残留（日志 .agent_tmp/c1_*.log）
4. **阳性对照实验**（证明复现手段有效）：临时 `git checkout b7a1bd4 -- <3 个修复文件>` 重建 release dynamic dll，同款 bench（4.7.1 + `--audio-driver Dummy --headless --bench`）立即 signal 11 崩溃（栈 23 帧落在主 dll，.agent_tmp/c1_positive_bench.log）；恢复 HEAD 后同款 exit=0。手段能抓住该类崩溃 → 全绿结果有效
5. **用户本地复测确认不崩**（2026-09-06）

"修复后仍崩"的最可能来源：旧记录形成时的 dll **混合部署**（两份 gdextension 只换了一份，spec 已沉淀双 dll 判据）；`.agent_tmp` 无当时崩溃日志留存，无法进一步考证。

## 顺带产出

- `-- --bench` 命令形式纠错并沉淀 spec（误写会静默跑全量 TS 测试）
- 双 gdextension 部署判据沉淀 spec（只换主 dll 会被 editor dll 旧行为干扰）
- `staticBinding` benchmark 字段不可信（dynamic path 也注册 IN）
- benchmark 非法参数二次修复（本 PRD「已排除项」同类遗漏）：`Callable.unbind(0)` 与空 Array 目标 `get(0)` 各 485 条引擎 ERROR 污染计时——修 `generate_benchmark_cases.py`（METHOD_ARG_OVERRIDE / CTOR_OVERRIDE["Array"]），89 cases invalid=0、引擎 ERROR 行 0

## 遗留

- **CrossWrapper finalizer 无 isolate 存活检查**（jsb_shadow_realm.cpp:591/807 `value_.object_.Get(isolate_)`，isolate_ 指向可能已 dispose 的 worker/shadow env）——代码审查发现的真实隐患，但与本次崩溃无因果（worker 消息传输走 Variant 序列化，不走 CrossWrapper；ShadowRealm 路径本地无测试覆盖）。登记为 P3 backlog，不阻塞本任务

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

- [x] 根因定位并修复——ptrcall 参数缓冲区溢出（a5f0db9）；阳性对照证明手段有效，HEAD 复现矩阵全绿；非绕过
- [x] Windows 本地：bench 全量 + TS 集成测试 exit code == 0、无泄漏、无 SEGV（dev/release × static/dynamic × 4.7.1/4.8.dev 矩阵 26+ 轮）
- [x] `ci-benchmark-both-legs` 解除阻塞（"修复后仍挂死"被 CI 元数据 + 阳性对照 + 用户复测推翻；job 需手动触发）
- [x] 机制性结论沉淀 `.trellis/spec/godotjs-ext/build/scons-build.md`（bench 命令行形式、双 dll 部署判据、staticBinding 字段不可信）；CrossWrapper 隐患登记 P3 backlog（见「遗留」）

## Notes

- 诊断日志等临时产物放 `.agent_tmp/`
- 相关知识：`.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`、`.trellis/tasks/09-06-static-bindings-wrapup/design.md`
