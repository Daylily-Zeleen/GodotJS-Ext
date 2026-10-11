# 退出期 `Check failed: node->IsInUse()`（未定位）

## 现象

进程收尾时（**doctest 摘要打印之后**）偶发 V8 GlobalHandles 的 CHECK 失败：

```
[doctest] Status: SUCCESS!
WARNING: Call ScriptInstance::callp() failed: env is null      ← 恒定 8 条，同一阶段
   at: GodotJSScriptInstance::callp (src\runtime\weaver\jsb_script_instance.cpp:791)
... (×8)
#
# Fatal error in , line 0
# Check failed: node->IsInUse().
#
#
#
#FailureMessage Object: 000000091D7FE990
```

- **rc 恒为 0**，`[doctest] 91/91 SUCCESS`，测试全过；它出现在**测试结果之后**的退出路径上。
- 复现率：**~1/5**（同一二进制连续跑 5 次，1 次命中）。竞态。
- gdb 下**不复现**：进程 `[Inferior 1 exited normally]`，无 IsInUse（确认时序竞态）。

## 如何复现

```
scons platform=windows target=editor use_node=yes dev_build=yes tests=yes debug_symbols=yes -j6
D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe ^
    --audio-driver Dummy --headless --path ./project --jsb-run-tests
```
连跑 5 次，统计输出里 `IsInUse` 出现次数（期望 0/5 算健康）。命令模板见 `.agent_tmp/`（本轮用过 `ii0..ii4`）。

## 已排除（证据）

- **不是跨环境错误上报引起的**：基线日志里就有，且时间戳早于相关改动：
  - `.agent_tmp/fin2_node_dt.err`（2026-10-09 01:32，`IsInUse=1`）
  - `.agent_tmp/ur_dt.err`（2026-10-09 05:09，`IsInUse=1`）
  - `.agent_tmp/die.err`（2026-10-09 10:34，`IsInUse=1`）
  - 对照 `.agent_tmp/rg_dt.err`（2026-10-09 06:48，`IsInUse=0`）
- **与 doctest 用例无关**：命中的那次摘要仍是 91/91 SUCCESS；`node->IsInUse()` 是 V8 GlobalHandles 在
  销毁 handle 时对节点引用状态的 CHECK，发生在引擎/语言 shutdown 阶段。

## 线索 / 下一步

- 同阶段恒定的 `Call ScriptInstance::callp() failed: env is null`（`jsb_script_instance.cpp:791`）说明退出时
  仍有脚本实例在调用，而环境已空——疑似 notify queue / 脚本语言 shutdown 的时序。
- `GodotJSScriptInstance::env_` 变为 null 的路径（`jsb_script_instance.cpp`：`env_->` 出现在 529 行；
  null 检查在 540/577/790）值得查：谁在什么时机把 `env_` 置 null、置空后还有谁在回调。
- 建议：给 `env is null` 的 8 次调用补一条带对象 id / 方法名的日志，定位这 8 次来自哪个对象/通知。
- 用 `--verbose` 或加临时探针记录 shutdown 顺序（`jsb_script_language` 的 `finish()` / notify queue flush）。

## 状态

未定位。与 `10-06-cross-isolate-error-reporting` 无因果，独立任务。

---

## 已定位并修复（2026-10-12，A/B 反证）

**根因**：`ShadowRealm::finish_all()` 里对静态句柄表的清理（`SymbolCrossUtils::clean()` /
`CrossWrapper::clean()`，**提交 `9718b87`**）之前不存在。缺了它，`jsb_shadow_realm.cpp` 的两个
**静态**表（`SymbolCrossUtils::cache_`、`key_for_funcs`、`CrossWrapper::flag_symbols_`）
会活到进程静态析构（`__run_exit_handlers`）；此时 isolate 已销毁，其中的 `v8::Global` / `TStrongRef`
析构 → 释放 V8 Global handle → `GlobalHandles::NodeSpace::Free()` 的
`CHECK(node->IsInUse())` 失败（`third/.../v8/src/handles/global-handles.cc:279`）。
`IsInUse()` = `state() != FREE`，即**同一个 handle 节点被释放了两次**。

**A/B 反证（决定性）**：把 `finish_all()` 里的两行 `clean()` 注释掉，只改这一处，同一二进制：
```
无 clean()：IsInUse 命中 8 / 12 次   （.agent_tmp/fals_*.log）
有 clean()：IsInUse 命中 0 / 42 次   （.agent_tmp/ii_probe_*/ii2_*/iii_*/final_*.log）
```
恢复 `clean()` 后重编再跑 5 次，0/5。**命中率差异把因果钉死在这两行上。**

**时间线吻合**：10-09 任务建立时（2026-10-09 01:32 的 `fin2_node_dt.err` 命中）`9718b87` 尚未落地；
该提交是本会话早前为修"静态析构时对已死 isolate 释放句柄 → SEGV"而加的，正好同源。

## 另一独立发现：退出期 8 次 `env is null`（不是本 CHECK 的原因，但值得单列）

给 `callp()` 的 null 分支加探针（`.agent_tmp/jsb_script_instance.cpp.bak` 为原样）后实跑，8 次调用是：
```
method=_enter_tree  owner=Node   script=res://tests/singleton/config-singleton.ts
method=_enter_tree  owner=Node2D script=res://tests/start.ts
method=_ready       owner=Node   script=res://tests/singleton/config-singleton.ts
method=_ready       owner=Node2D script=res://tests/start.ts
method=_draw        owner=Node2D script=res://tests/start.ts
method=_top_level_raise_self owner=Node2D script=res://tests/start.ts
method=_exit_tree   owner=Node2D script=res://tests/start.ts
method=_exit_tree   owner=Node   script=res://tests/singleton/config-singleton.ts
```
固定 8 条、每次相同、恒在 `[jsb] running tests result: 0` 之后（即主环境已 `dispose()` 之后）。
`env_` 由 `EnvironmentRef::reset()`（`~Environment` 尾部，`jsb_environment.cpp:437`）置空，
所有脚本实例共享同一 control block，所以一次 dispose 就让全部实例变 null。
含义：进程收尾时**仍有节点通知在派发**（`start.ts` 的 `_exit_tree` 等），而 JS 环境已销毁。
目前只是 `WARN_PRINT` + 返回 false，行为安全；但说明退出顺序上"语言 shutdown"早于"场景树通知排空"。
**未改行为**（属独立问题，需确认是否为引擎既定时序）。探针已还原。

## CI 确认（2026-10-12）

`620bf50` 推送 → run **38179774833 = success**（含 4 条 web 腿 `engine: web` ×2 + nothreads）。
本任务无代码改动（根因修复由 `9718b87` 承载），本条仅记录文档与诊断结论。
