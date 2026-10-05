# 执行计划：`_get_dependencies` 静态依赖扫描

> 顺序执行；每步的验收方式写在步内。未通过验收不得进入下一步。

## 0. 前置确认（10 分钟）

- [ ] 读 `src/runtime/weaver/jsb_resource_loader.cpp` 全文，确认 `recognized_extensions_` 覆盖 `.ts/.js/.mjs/.cjs`（决定扫描器要处理的扩展名集合）。
- [ ] 读 `src/runtime/bridge/jsb_path_util.*` + `src/runtime/tests/test_jsb_paths_mapping.h`，确认可复用的路径归一化/映射 API，避免自造第二套（spec: `cpp/architecture-constraints.md`）。
- [ ] 读 `src/runtime/impl/web/bridge/src/monolith.ts:946`（#121）仅作对照，本任务**不动**它。

## 1. 扫描器模块

- [ ] 新增 `src/runtime/weaver/jsb_script_deps.h/.cpp`（名字待定，遵循 `cpp/coding-standards.md`）：纯函数，输入源码 + 所在目录，输出 `res://` 路径数组。
- [ ] 实现单趟词法扫描（跳过注释/字符串/模板字面量），识别 `import` / `export … from` / `require("…")` / `import("…")` 的字面量说明符。
- [ ] 说明符解析：相对 → 归一化；`res://` → 原样；裸包名 → 忽略；别名 → 读 `res://.godot/godotjs_ext/.paths_mapping`（不存在则忽略该别名）。
- [ ] 扩展名补全 + 存在性检查；去重、保持首次出现顺序。
- [ ] **验证**：先写一个独立的小用例（临时脚本放 `./.agent_tmp/`），用几个真实 `.ts` 文件（含相对/`res://`/裸包名/注释里伪 import/字符串里的 "import"）对照预期输出。

## 2. 接入 loader

- [ ] `jsb_resource_loader.cpp:123-126` 改为：打开文件 → 读源码 → 调扫描器 → 返回；文件不存在/读失败返回空数组（不报错）。
- [ ] `p_add_types` 显式忽略并注释理由（对齐 GDScript）。
- [ ] **验证**：`scons platform=windows target=editor dev_build=yes tests=yes -j6`（v8）编译通过。

## 3. doctest

- [ ] 新增 `src/runtime/tests/test_jsb_resource_deps.h`，并在 `src/runtime/tests/jsb_test_main.cpp` 的 include 列表登记（与既有 `test_jsb_*.h` 同风格）。
- [ ] 新增测试资源（`project/tests/` 下，命名避免与既有 fixture 冲突）。
- [ ] 覆盖 AC1/AC2/AC3/AC4（见 `prd.md`）。
- [ ] **验证**：`D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe --headless --path ./project --jsb-run-tests` → `[doctest] Status: SUCCESS!`。

## 4. 全量门禁

```
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j6
scons platform=windows target=editor use_quickjs_ng=yes dev_build=yes tests=yes -j6
D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe --headless --path ./project --jsb-run-tests
D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe --audio-driver Dummy --headless --path ./project --quit-after 25000
```

- [ ] v8 + quickjs-ng 各构建一次（不重复编译；改头文件会触发全量重编，编一次算一次）。
- [ ] doctest 全绿（含既有 81 例 / 1224+ 断言）；smoke 打印 `GODOTJS_TEST_PROJECT_COMPLETED`。
- [ ] 本任务不触碰 `impl/jsc`，故不需要 jsc 语法门；若实现过程中改了 jsc/quickjs/web 的 shim，则必须补跑语法门。

## 5. 编辑器侧人工确认（可选但推荐）

- [ ] 打开编辑器（`--editor --path ./project`），在文件系统依赖面板/`ResourceLoader.get_dependencies` 场景下确认 `.ts` 依赖可见；若无法在编辑器内确认，则在 `report.md` 里写明「仅 doctest 覆盖，编辑器 UI 未实测」。

## 6. 收尾

- [ ] 若引入新约定（`.paths_mapping` 读取契约 / 扫描器的静态限制），写进 `.trellis/spec/`（`trellis-update-spec`）。
- [ ] 任务 `report.md` 记录：改动文件、AC 逐条证据、未覆盖项。
- [ ] 清理 `./.agent_tmp/` 临时脚本。
- [ ] 提交（英文 message，按仓库 commit 约定）；推送需用户逐次授权。

## 回滚点

- 回滚点 1（步骤 1 结束）：新模块可独立删除，无外部影响。
- 回滚点 2（步骤 2 结束）：恢复 loader 的 `return {};` 即可回到现状。
- 若最终决定不做（备选 C）：仅保留 PRD/design 记录，把 loader 的 `//TODO` 改为「已知限制」注释（含本 design 的影响面清单），无需测试。
