# godotjs-ext 主代码规范索引

> 包路径：仓库根（`src/`、`SConstruct`、`misc/`、`project/`、`scripts/`）。
> 默认包。spec 层按主题组织在 `cpp/` 与 `build/`。

## 层与指南索引

| 层 | 文件 | 内容 |
|---|---|---|
| cpp | [index.md](./cpp/index.md) | C++ 开发前检查单与质量检查 |
| cpp | [coding-standards.md](./cpp/coding-standards.md) | 头文件命名空间禁令、临时文件位置、格式化 |
| cpp | [generated-files.md](./cpp/generated-files.md) | `*.gen.*`/`*.def.*` 生成文件保护与生成逻辑映射 |
| cpp | [ptrcall-encoding.md](./cpp/ptrcall-encoding.md) | ptrcall EncodeT 编码、缓冲区溢出修复规则 |
| cpp | [architecture-constraints.md](./cpp/architecture-constraints.md) | GDExtension `~` 副本陷阱、共享库约束、桥接规则 |
| build | [index.md](./build/index.md) | 构建接线检查单（编译命令、dll 部署） |
| build | [scons-build.md](./build/scons-build.md) | 强制编译命令、静态绑定 codegen 接线、dll 部署验证 |
| test | [doctest.md](./test/doctest.md) | C++ doctest 双套件机制与验收标准 |
| test | [codegen-baseline.md](./test/codegen-baseline.md) | headless 触发链、基线校验方法论、TS 缓存陷阱 |

## 项目结构速览

```
src/runtime/          运行时（Script/ScriptLanguage、Environment、bridge、impl/<引擎>）
src/editor/           编辑器扩展（Plugin/Dock/REPL/ExportPlugin、codegen 编排）
src/api_tool/         api 文档缓存与 ptrcall 调用（dynamic 路径核心）
src/static_binding/   静态绑定（gen/ 为构建生成，不入库）
src/internal/         内部工具（含双侧编译的无状态工具）
src/compat/           兼容层（双侧编译）
src/tests/            测试公共设施（header-only runner，仅 tests=yes 编入）
third/godot-cpp       godot 绑定子模块（只读）
third/quickjs-ng      可选 JS 引擎子模块（只读）
misc/build/           构建期代码生成脚本（静态绑定、运算符表、模板头）
project/              测试用 Godot 项目（TS 测试、benchmark）
scripts/              TS 运行时源码（jsb.runtime / jsb.editor bundle 源）
```

## 检出环境事实

codegen 黄金基线放 `<检出根>/.codegen-baseline/`（gitignore，本地数据）；校验脚本 `misc/verify_codegen.py` 已入库。执行构建/测试前先读 spec 的 build/test 层，不要凭记忆拼参数。新检出需先 `git submodule update --init`。
