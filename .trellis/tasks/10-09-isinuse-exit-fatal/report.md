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
