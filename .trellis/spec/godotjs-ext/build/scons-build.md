# SCons 构建规范

## 强制命令

编译 GodotJS-Ext **必须且只能**使用：

```
scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6
```
其他平台/引擎组合按需在规范命令上叠加参数（示例：全量测试构建 `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`；测其他 JS 引擎加 `use_quickjs_ng=yes` / `use_node=yes` / `use_quickjs=yes`；引擎可执行文件名随 target/platform 变化，产物在 `bin/<platform>/`）。

1. **绝不自行编造编译命令**——只用上述参数组合
2. **绝不 `scons --clean`**——全量重建极耗时；增量编译自动跳过未更改部分
3. 需要重编就直接跑上述命令（增量构建）

## 构建机制要点

- 构建前提：`third/godot-cpp` 子模块已初始化（`git submodule update --init`），否则 SConstruct 报错退出
- **不要回退旧引擎**：旧二进制（如 7-28 的 editor.dev）加载不了新构建扩展（godot-cpp ABI 不匹配，插件实例化即崩）；官方 4.8.dev 宿主会挂死。
- **C++ 测试宿主（CI 同款）用官方 4.7.1**（`Godot_v4.7.1-stable_win64.exe`）；**不要用** `bin/windows/` 下自己构建的 godotjs-ext 可执行文件（headless 必崩，与改动无关）。
- `--godotjs-api-generate` 会【消费删除】`project/extension_api.json`；需要资源声明 gen（`extension_api.json.gen.ts`）时，先把官方引擎自带的 `extension_api.json`（随引擎分发，在其安装目录下）复制回 `project/` 再重跑 `--generate-types`。
- `project/.godot` 删除后的重建三件套：①手工写 `project/.godot/extension_list.cfg`（两行：runtime 与 editor 的 .gdextension 路径）；②重新编译 TS；③重新生成 api 数据。
- **测试项目重置**（需要完整初始环境时，先删除）：`./project/.godot`、`./project/gen`、`./project/typings`（如果有）；`./project/tsconfig.json` **只有**要执行 `GodotJSEditorPlugin::try_install_project_files()` 的测试才删除（git 跟踪的预设文件）。

- `static_binding=yes`（默认）时每次构建自动跑 codegen（`misc/build/static_binding_codegen.py`），产出 `src/static_binding/gen/dispatch_*.gen.cpp`（glob 编译）；切分支后 gen 目录残留 obj 会被覆盖，无需手动清理
- codegen 源数据 = godot-cpp 子模块内置 `third/godot-cpp/gdextension/extension_api-4-7.json`；`SConstruct` 的 `API_VERSION = "4.7"` 是唯一硬编码点——改它须同步 `.gdextension` 的 `compatibility_minimum`。**CI 无 api-dump job**
- 生成文件一律不入库（`*.gen.*`），详见 [../cpp/generated-files.md](../cpp/generated-files.md)
- Linux 平台 lws 被禁用（预编译库非 PIC；Linux 上 v8 debugger 的 websocket 功能失效，不影响构建/测试）——自建 PIC 版 lws 的修正方案见 Trellis 任务 backlog（lws PIC）

## dll 部署与验证（动态/静态两份）

- addons 有两个 gdextension：`godotjs-ext.gdextension`（主，v8）与 `godotjs-ext-editor.gdextension`（editor），各自指向 `bin/windows/` 下不同 dll——**替换验证时两份都要换**，只换主 dll 会被 editor dll 的旧行为干扰
- **进程残留锁 dll**：编译成功但 cp 报 "Device or resource busy" = 有 godot 进程未退出（含 SEGV 残留）。先 `taskkill /F /IM godot*` 再 cp；cp 后用 md5sum 确认两处一致
- 引擎加载的是 `project/addons/godotjs-ext.daylily-zeleen/bin/` 下的产物——改代码后必须 scons 再验证
- **dll 身份只认 md5 + 构建命令**：采数/验证前 `md5sum` 两处部署位；`staticBinding` 字段不可信，`det==0 flood` 只对 dynamic 有效。跨腿采数后切回另一腿必须重新 `scons`（两腿产物字节不同），并重新核对 md5——凭记忆判断当前部署的是哪条腿必然出错
- benchmark 采数对比必须固定同一 dll 做 A/B（如 `--gc` on/off）；dll 一变，所有旧数据作废

## 本地测试命令（速查）

本地测试命令速查（详细判据与陷阱见 [../test/index.md](../test/index.md) 与 [../test/doctest.md](../test/doctest.md)）：

- C++ 测试（需 `tests=yes` 构建）：`godot --path ./project --jsb-run-tests`（editor 构建同时跑 runtime + editor 两套件）
- TS 编译：`cd project && node_modules/.bin/tsc --noCheck`
- TS 集成测试：先生成 api 数据并编译 TS，再 `godot --path ./project --verbose`
- Benchmark：`godot --headless --bench --path ./project [-- --only=<组>]`——`--bench` 是**引擎参数**（`start.ts` 用 `OS.get_cmdline_args()` 读取），必须放在 `--` **之前**；`--only`/`--calls` 是 user args（`benchmark.ts` 用 `OS.get_cmdline_user_args()` 读取），必须放在 `--` **之后**。误写 `-- --bench` 会静默跑全量 TS 测试而非 benchmark（判别：日志出现 `Loading scene` 即全量）
