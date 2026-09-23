# implement.md — 执行清单

前提：`design.md` §3 的策略决策已由用户确认（S1 或 S2）。

## 阶段 0：清理诊断残留（先做，避免污染后续构建）

1. 删除 `src/internal/jsb_dll_probe.h`、`src/internal/jsb_dll_probe.cpp`。
2. 还原 `src/runtime/register_types.cpp`：移除 `:28` 的 include 与
   `jsb_startup()` / `jsb_shutdown()` / `jsb_uninitialize_module()` 内的全部 `jsb_probe_*` 调用。
3. 确认工作区除本任务既有改动外无其他临时改动：`git status --porcelain`（只读，不提交）。

## 阶段 1：修缺陷 A（两腿通用，独立于 B）

4. `src/runtime/weaver/jsb_script_instance.cpp`：为 `temporary_property_list` 静态池增加释放路径，
   在 shutdown 中 `exchange(nullptr)` 后 `memdelete`；放入 `jsb_shutdown()`（早于
   `StringName::cleanup()`）。
5. 验证点：v8 腿 `project/` 下 `grep -c "Orphan StringName"` 由 2 → 0。

## 阶段 2：修缺陷 B（按用户选定策略）

**若 S1（消除 node 镜像加载期 2 个 surplus 引用）**
6. 动态定位调用者。可用手段（按成本升序）：
   - 探针在 `DllMain` 之前用 `LdrRegisterDllNotification` 记录时间线（回调内**只允许 PEB 助手**，
     严禁 `CreateToolhelp32Snapshot`/`OpenThread`/`GetProcAddress`，否则重入 loader 崩溃）。
   - 被动扫描：遍历所有模块 IAT + delay-IAT，统计指向本模块地址的 slot（无需挂钩、无 loader 重入）。
   - 候选机制核对：VEH（`AddVectoredExceptionHandler` 会让 loader 持有处理者所在模块引用，
     `RemoveVectoredExceptionHandler` 释放）、CRT 线程 pin、libnode 内部静态注册。
7. 按证据消除引用（例如在 shutdown 里对称释放），再跑裸宿主实验：node 镜像应从 3 → 1。
8. 端到端：node 腿 `project/` 下 orphan 由 102 → 0。

**若 S2（godot-cpp 类静态改为引擎静态）**
6. 在 `third/godot-cpp` 内让 `get_class_static()` / `GDEXTENSION_CLASS_ALIAS` 版本产出
   `p_static=true` 且**永不析构**的名字（`memnew` 常驻），避免 §1 的 `ERR_PRINT`。
7. 记录子模块补丁的落地方式（fork 提交 或 构建期 patch），确保可复现。
8. 端到端：两腿 `project/` 与 minproj 均 0。

## 阶段 3：验收

9. 重建**两腿干净产物**（无探针），记录 md5。
10. 两腿分别跑：`--headless --path ./project --verbose`（exit 0、`GODOTJS_TEST_PROJECT_COMPLETED`、
     `grep -c "Orphan StringName" == 0`）与 `--jsb-run-tests`（C++ doctest + TS 套件）。
11. 对比 AC3：无新增 ERROR/WARNING。
12. 还原 `project/.godot/extension_list.cfg` 与 minproj 的 `extension_list.cfg` 为两行。

## 阶段 4：spec 与收尾

13. `.trellis/spec/godotjs-ext/test/index.md`：写入「两腿同判据：`grep -c "Orphan StringName" == 0`」、
     自查命令、本次漏检教训；按实测修订存量「已知遗留 orphan」条目。
14. 结果写入 `.trellis/tasks/09-22-fix-orphan-stringname-v8-node/report.md`。
15. 收尾汇报：目标 → 动作+证据（两腿 md5 + 日志）→ 最终状态 → 遗留。

## 风险点

- `third/godot-cpp` 是 git submodule（pinned `d7b6162`，无仓内补丁机制）→ S2 需明确落地方式。
- 改 `wrapped.hpp` 会触发两腿全量重编（单腿约 60–190s）。
- **禁止**用自我强卸（S3）规避问题：违反 Requirement 4。
- 严禁 `scons --clean`。
