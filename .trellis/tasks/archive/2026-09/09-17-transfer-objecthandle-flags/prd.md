# 修复跨 Environment 对象转移的生命周期语义

## 目标与授权

保留不可由类型恢复的 JS-owned/persistent 语义，正确配对持久计数及 RefCounted 环境引用；修复本地 C++ 测试退出崩溃。用户要求 V8 与 QuickJS-NG 本地构建、完整集成验证，保留改动人工审查，不提交推送。

## 当前契约（用户 2026-09-18 调整）

- `TransferData::flags` 完整快照/恢复，不是独立 bool 或 OR 合并。
- `void prepare_transfer_out(...)` 要求源对象有效且已有 binding，不自动建立源句柄。
- 接收正常绑定后，若源含 PERSIST 先 mark，再整体恢复 flags；mark 不幂等。
- `int32_t persistent_object_count_` 非 atomic；None 解绑注销计数，不调用 native finalizer。
- RefCounted 传出保留的源环境引用由接收释放一次，消息 Variant 保活。
- web 对象列表走共享 prepare；自动 clone 分支排除 OBJECT。
- EnvironmentRef bool 检查 env 而非仅控制块。TransferableShadowRealm 的 guest Global 在 guest isolate 销毁前通过 dispose_environment 清空。

## 实测与假设结论

- 源 handle 必定存在：与当前 worker 自动递归后代的实现矛盾。当前双引擎在未访问子节点回归中断言中止，不能沿用旧版通过结论。
- 连续转移：存在，测试三轮往返并验证接收复用已有绑定。
- 非 RefCounted 的 JS-owned 句柄保持 strong，不能用 GC 作为删除触发；测试以 peer terminate/环境 teardown 为删除触发。
- gc 同线程执行、跨线程入队。worker teardown 用未转移 control Node 失效确认释放路径已执行。
- RefCounted ObjectID 的 JS number 可能丢精度，回归用 Godot WeakRef 判定存活，不用不安全数字查 ObjectDB。

## 验收记录

> 2026-09-18 用户 runtime 修复（transfer flags `528f3b5`、worker 后代移除 `42063a4`、shadow dispose `2e218c2`）后，此前阻断项已解除；本表按最新跑测状态更新。

- [x] 旧 DLL owned 泄漏已复现；修复后接收 teardown 删除 JS-owned Node。
- [x] native-owned：`42063a4` 移除 worker 自动收集 Node 后代后，prepare 不再遇到未绑定子节点；双后端（worker/shadow）四用例均通过。
- [x] RefCounted 连续往返、已有绑定复用、native holder 与 WeakRef 回收通过。
- [x] persistent：首次接收入双后端均通过；`transfer_in_bind` 的 flags 恢复 + `mark_as_persistent_object(instance, true)` 正确配对计数（此前非递归写锁重入已解除）。
- [x] 当前 V8 C++：runtime 50 / editor 3 全通过，进程 exit 0。
- [x] 当前 QuickJS-NG C++：runtime 51 / editor 3 全通过，进程 exit 0。
- [x] 完整 TS：双后端（worker+shadow）各 4 对象用例 + 3 轮基础会话全绿，COMPLETED，exit 0；`tsc --noEmit` 类型检查干净。
- [x] V8/QuickJS shadow：四用例 + 3 轮会话全绿（此前 V8 shadow persistent 超时、QuickJS shadow owned 因 serializer 未脱 V8 门控失败，均已解除）。
- [ ] web worker 运行验收：本地无 web 构建产物，未执行；不得以 native/shadow 替代。
- [ ] worker 内部 persistent 计数直接观测：现有监控只暴露主环境，当前只能从主环境迁移差值与 teardown 行为验证。

## 保留问题与审查状态

完整 TS 双后端跑测已全绿，`tsc --noEmit` 干净。C++ 两引擎仍各有 8 次 `ScriptInstance::callp: env is null`（既有告警，未判定为任务缺陷；进程 exit 0）。任务待人工审查后归档。
