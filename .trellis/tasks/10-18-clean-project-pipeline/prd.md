# PRD：初期流程报错与崩溃修复（干净 project 七步流水线）

## 背景

从干净初始 project 出发跑标准 TS 集成流程时，导入 / 生成阶段出现崩溃与报错。目标是把「编译插件 → 命令行导入 → 命令行生成 api 数据 → 命令行安装预设文件 → 命令行生成 typings → 编译 TS → TS 集成测试」这 7 步全部修到无崩溃、无非预期报错。

已知结构性缺口：headless 下 `try_install_project_files()` 只打日志并跳过安装（`jsb_editor_plugin.cpp` 的 `DisplayServer::get_name() == "headless"` 分支），`.godot/` 清空后 5 个 `.cjs` 一个都不装 → 需在 `src/editor/` 增加 headless 可触发的安装路径 / CLI 开关。

## 验收标准

1. 7 步每步可执行到底；除第 2 步外，每步日志中 `ERROR:` / `FATAL:` / `SCRIPT ERROR:` / `Condition "…" is true` / 未处理异常 / 崩溃退出码计数为 0
2. 第 2 步（导入）仅容忍「插件/DLL 未被识别或加载」相关报错，逐条归因；不得有引擎崩溃退出码
3. 第 4 步：headless 命令行下预设文件被真实写入（产物存在性验证），不是「跳过安装」的日志
4. 第 6 步 `tsc` 不加 `--noCheck` 且 exit 0
5. 第 7 步：exit 0、`GODOTJS_TEST_PROJECT_COMPLETED` == 1、`GODOTJS_TEST_PROJECT_FAILED` == 0、`Orphan StringName` == 0
6. 结果来自「先清理生成文件再从头跑」的复跑
7. 每处崩溃/报错有书面归因，无一处靠抑制/降级/跳过掩盖

## 边界

- 允许改：`src/editor/*`、`src/api_tool/*`、`src/internal/*`、`src/compat/*`、`misc/**`
- 不动：`third/godot-cpp`、`GodotJS-Dependencies/`、`SConstruct` 依赖 tag/版本常量、`.github/workflows/`
- 不改 `project/tests/**` 断言语义来绕过崩溃
- 不 commit / push / 还原文件

## 停止条件

- 总轮次 ≤ 100；同一问题不同解法 ≤ 5 次，超过即书面交用户
- 需要改依赖/godot-cpp 才能修 → 停并交用户
- 需要削弱断言才能变绿 → 停并交用户
