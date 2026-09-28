# GodotJS-Ext 特性与差异清单（Research）

> 供文档写作取用的 file:line 事实清单。原始调研出自由主线从仓库直接读取的证据
> （`README.md`、`SConstruct`、`.github/workflows/*.yml`、`.trellis/spec/godotjs-ext/**`、
> `src/**`、`scripts/**`、`project/**`）。

## 1. 身份

- **GDExtension**，入口符号 `jsb_gdextension_init`
  （`project/addons/godotjs-ext.daylily-zeleen/godotjs-ext.gdextension`；`compatibility_minimum = 4.7`，
  `reloadable = false`，`[icons] GodotJSScript`）。
- 许可：根 `LICENSE` = LGPL-2.1；`third/GodotJS/LICENSE` 是上游 MIT 副本，该目录只剩 LICENSE
  （`.gitmodules` 只声明 `third/godot-cpp` 与 `third/quickjs-ng`）。
- 构建脚本 `SConstruct`：选项在 30–38（`use_quickjs` / `use_quickjs_ng` / `use_jsc` / `use_node`），
  `API_VERSION = "4.7"` 在 69，引擎选择与 `JSB_WITH_*` 在 368–436，静态绑定 codegen 接线在 887–918，
  单库源集合在 842–1037，tests glob 在 959–964。

## 2. 运行时分层（`src/runtime/`）

| 目录 | 内容 |
|---|---|
| `weaver/` | `ScriptLanguage` / `Script` / `ScriptInstance` / ResourceFormat loader+saver |
| `bridge/` | `Environment`、模块加载器与解析器、类型转换、worker、shadow realm、debugger、`ScriptDocStore` |
| `impl/{v8,quickjs,node,jsc,web}/` | 各引擎实现 |
| `internal/` | 共享工具 |

- `src/runtime/register_types.cpp`：唯一入口 + 启动回调、语言/loader/saver 注册、
  `JSB_WITH_EDITOR` 分支、api_tool init/shutdown。
- `godot` 模块由 `bridge/jsb_godot_module_loader.cpp` 从 api_tool 二进制库**惰性**合成，
  解析顺序：单例 → 工具函数 → 类。
- `godot-jsb` 由 `bridge/jsb_bridge_module_loader.cpp` 构造，导出
  `BINDING_MODE` / `BIGINT_FOR_64BIT` / `DEV_ENABLED` / `TOOLS_ENABLED` /
  `CAMEL_CASE_BINDINGS_ENABLED` / `version` / `impl` / `_new_callable` /
  `set_async_module_loader` / `$import`，以及 `jsb.internal.*`。
- 编译期功能矩阵见 `src/jsb.config.h`（`JSB_WITH_BIGINT`、`JSB_SHADOW_REALM_ENABLED` 等）。

## 3. 构建与产物

- 规范命令（`.trellis/spec/godotjs-ext/build/scons-build.md`）：
  `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6`；
  禁止 `scons --clean`。
- 叠加：`platform=`、`tests=yes`、`binding_mode=static|shared|dynamic`（`SConstruct:37`，默认 `shared`）、
  引擎标志。
- **单库两产物**：`target=editor` → `godotjs-ext.<plat>.editor.<arch>`；
  `target=template_*` → `godotjs-ext.<plat>.template_<flavor>.<arch>`。
- 产物落 `bin/<platform>/`，引擎实际加载 `project/addons/godotjs-ext.daylily-zeleen/bin/`；
  验证靠 md5 比对两处部署位。
- `scripts/` 是 pnpm workspace（`pnpm-workspace.yaml`：`.`、`./*`、
  `../src/runtime/impl/web/bridge`），`pnpm build` 产出 `scripts/out/`，由 scons 嵌入。

## 4. 引擎与发布

| 引擎 | scons | 包 | 平台 |
|---|---|---|---|
| v8 | 默认 | `v8` | Windows / Linux(x86_64+arm64) / macOS(arm64) / Android(arm64+x86_64) / iOS(arm64) |
| quickjs | `use_quickjs=yes` | ❌ | 仅本地 |
| quickjs-ng | `use_quickjs_ng=yes` | `qjs-ng` | 上述 + Web(threads / nothreads) |
| jsc | `use_jsc=yes` | `jsc` | macOS + iOS |
| node | `use_node=yes` | `node` | 3 个桌面 |
| web | `platform=web`（不加引擎标志） | `web` | 仅 Web |

- CI 构建矩阵 23 腿（`.github/workflows/ci.yml:403-561`），测试腿 5 个（744–760），
  Godot 宿主 `4.7.1-stable`（46–49）。
- 发布链：`release.yml`（workflow_run on CI → should-publish）→ `misc_release.yml`
  （`mode=full` / `upload-only`）→ `misc/release/package.py`（唯一派生点，按引擎 assemble + 1:1 自校验）。
- `.gdextension` 只认 `[configuration]` / `[libraries]` / `[dependencies]` / `[icons]`；
  `[information]` 是本项目自有元数据，打包时只改写 `version`。

## 5. 编辑器（`src/editor/weaver-editor/`）

- dock 名 `GodotJS-Ext`（`jsb_docked_panel.cpp:42`）：`TabContainer`，tab0 = REPL（`jsb_repl.h`：
  历史、补全、clear/gc/generate-types/install-files/tsc 按钮），tab1 = Statistics
  （`jsb_statistics_viewer.h`）。
- 工具子菜单名 `GodotJS-Ext`（`jsb_editor_plugin.cpp:439`），条目（:440-455）：
  `Generate API Data`、`Install Project Files`、`Generate Types`、`Config Enabled TS Classes`
  （对话框标题 `Config Enabled Classes Bindings`，`jsb_config_classes_dialog.cpp:143`）、
  `Generate All Scene Nodes Types`、`Generate All Resource Types`、`Cleanup Invalid Files`。
- 命令行：`--generate-types`、`--godotjs-api-generate <extension_api.json>`（230–246, 322–341）。
- 安装文件清单（`add_install_file(...)`，471–499）：`tsconfig.json`、`jsconfig.json`、`package.json`、
  三处 `.gdignore`、`godot.minimal.d.ts`、`godot.mix.d.ts`、`godot.shadowRealm.d.ts`、
  `godot.worker.d.ts`、`jsb.editor.bundle.d.ts`、`jsb.runtime.bundle.d.ts`、
  `jsb.bundle.d.ts`（obsolete）与 `type.extension.d.ts`（obsolete——JS 类型扩展已移除，
  该条目只用于在安装时删除旧项目里的残留），以及 `jsb.editor.tools.cjs` / `jsb.signature.extract.cjs` /
  `jsb.doc.extract.cjs`（落项目数据目录，`jsb_editor_pch.h:45` 定义入口名）。
- 项目数据目录：`internal::settings::get_project_data_dir_name()`（`.godot` 或 `godot`）；
  TS 输出目录 `get_jsb_out_dir_name()` = `<数据目录>/godotjs_ext`（`jsb_settings.cpp:37-44`）。
- codegen：`src/editor/codegen/jsb_codegen_generator.h`（GodotTSDGenerator / SceneTSDGenerator /
  ResourceTSDGenerator），产出 `typings/godotN.gen.d.ts` 与工程 `gen/godot/**`。
- 源码注释 → 编辑器脚本文档：常驻 Node 工具进程（见
  `.trellis/tasks/archive/2026-09/09-28-comment-doc-annotation/report.md`）。
- ExportPlugin：`jsb_export_plugin.h`（忽略 `res://tsconfig.json` 等，`.sig` 边车随导出打包）。

## 6. 用户侧 API 面

- `godot.annotations`：新式 `createClassBinder()`（`scripts/jsb.runtime/src/godot.annotations.ts:714+`）
  与旧式装饰器并存（全部标 `@deprecated Use createClassBinder() instead.`）。
- `ClassBinder` 类型（`scripts/typings/godot.generated.d.ts:64603+`）：
  `tool` / `icon` / `export`（含 `multiline`、`range`、`range_int`、`file`、`dir`、`global_file`、
  `global_dir`、`exp_easing`、`array`、`dictionary`、`object`、`enum`、`flags`、`cache`）/
  `signal` / `rpc` / `onready` / `deprecated` / `experimental` / `help` / `exposed.const` / `exposed.shared`。
  `ExportOptions = { class?, hint?, hint_string?, usage? }`；
  `RPCConfig = { mode?, sync?, transfer_mode?, transfer_channel? }`。
- `godot.worker`（`scripts/typings/godot.worker.d.ts:24-56`）：`JSWorker` / `JSWorkerParent`，
  **无 `ontransfer`**（上游 worker.md 里有）。
- `godot.shadowRealm`（`scripts/typings/godot.shadowRealm.d.ts`）：`JSShadowRealm` /
  `TransferableJSShadowRealm`；纯 Web 构建不提供。
- `godot-jsb` 常量与 `$import`（`scripts/typings/godot.minimal.d.ts`）。
- `GArray.create` / `GDictionary.create` / `.proxy()` / `ProxyTarget` / `Signal.as_promise()`
  （`jsb.inject.ts:381`）/ `Callable.create`（`godot.generated.d.ts:478-499`）。
- 预设文件：`scripts/presets/{tsconfig.json,jsconfig.json,package.json,gdignore}.txt`，
  占位符 `__OUT_DIR__` / `__BUILD_INFO_FILE__` / `__MODULE__`（node16）/ `__TYPE_ROOTS__`
  在 `jsb_editor_plugin.cpp:683-687` 展开。

## 7. 测试与基准

- `project/tests/` 20 组场景：benchmark、cross-environment、default-args、extend、gen_dts_test、
  indexed-props、int64、numeric、operators、os-executor、papaparse、paths_test、resource、singleton、
  static-members、`中文路径`、start/test-status 等。
- C++：doctest 单套件，`--jsb-run-tests`（`src/tests/jsb_test_runner.h:165`）。
- benchmark：`-- --bench [--gc] [--only=<组>]`（`project/tests/benchmark/benchmark.ts`，
  `misc/bench_matrix.py` 做双腿对比 + md5 身份校验）。
- TS 集成矩阵：`scripts/test/run-runtime-matrix.mts`，哨兵 `GODOTJS_TEST_PROJECT_COMPLETED` /
  `GODOTJS_TEST_PROJECT_FAILED:`。

## 8. 上游未覆盖、本仓独有的主题

引擎矩阵（含 node / web、qjs 的"保留不出包"）、按引擎分包与 1:1 校验、
`[information]` / `[icons]`、`binding_mode` 静态绑定、api_tool 惰性布局、
源码注释文档、签名边车、`@bind.exposed.const/shared`、BigInt/int64 语义、benchmark 流程、
跨环境测试、单库两产物。
