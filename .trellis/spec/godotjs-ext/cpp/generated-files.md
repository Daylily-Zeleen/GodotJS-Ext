# 构建生成文件保护规则（`*.gen.*` / `*.def.*`）

文件名含 `.gen.`（如 `*.gen.cpp`、`*.gen.h`、`*.gen.inc`、`manifest.gen.json`）或 `.def.` 表头的文件是**构建过程自动生成**的：

- ❌ 不直接编辑、不修编译错误、不手工加代码
- ❌ 不提交入库（`.gitignore` 已排除 `*.gen.*`；静态绑定表同样不入库）
- ✅ 要改内容 → 改**生成逻辑**（下表），再重新构建重新生成

## 生成逻辑映射（现状）

| 生成文件 | 应修改的生成逻辑 |
|---|---|
| `src/runtime/jsb.gen.h` | `SConstruct` 的 `generate_jsb_gen_header()`（约 435 行起） |
| `src/runtime/jsb_project_preset.gen.cpp`、`src/editor/weaver-editor/jsb_editor_preset.gen.cpp` | `SConstruct` 的 `PresetTransformer` / `generate_code()`（嵌入 JS bundle） |
| `src/editor/weaver-editor/templates/templates.gen.h` | `misc/build/generate_templates_header.py`（扫描 `templates/**/*.ts.cs`） |
| `src/runtime/internal/jsb_primitive_operators.def.gen.h` | `misc/build/generate_primitive_operators.py`（消费 godot-cpp 内置 api json，无条件生成，dynamic 路径也用） |
| `src/static_binding/gen/*`（`dispatch_*.gen.cpp`、`registry.gen.h`、`string_names.gen.h`、`manifest.gen.json`） | `misc/build/static_binding_codegen.py`（`static_binding=yes` 时 SConstruct 每次构建自动调用；默认开启，日志见 `OK: static binding tables generated`） |
| `third/godot-cpp/gen/**`（`*.gen.inc` 等） | `third/godot-cpp/binding_generator.py` |
| `project/gen/**`、`project/typings/**`（运行期 TS 产物） | 引擎扩展 codegen：`godot --headless --editor --path ./project --generate-types`（入口 `jsb_editor_plugin.cpp` 的 `_generate_types_from_cmdline`） |

## 通用原则

1. 看到文件名含 `.gen.` 立即停止编辑，先查上表或搜索 `SConstruct` / `SCsub` / `misc/build/` 生成脚本
2. 新增生成规则写在对应 `SCsub` / `SConstruct` / `misc/build/` 脚本中
3. 静态绑定 codegen 属**构建流程代码生成**，构建接线见 [../build/scons-build.md](../build/scons-build.md)
4. 基线校验针对的是**编辑器运行期 TS 代码生成**（上表最后一行：`--generate-types` 产 `project/gen/`、`project/typings/`），验证改动编辑器生成代码后产物与改动前基线一致——与构建流程的代码生成无关；方法论见 [../test/codegen-baseline.md](../test/codegen-baseline.md)
