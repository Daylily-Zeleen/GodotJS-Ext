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
| `src/runtime/internal/jsb_primitive_operators.def.gen.h` | `misc/build/generate_primitive_operators.py`（消费 godot-cpp 内置 api json，无条件生成，**两腿共用**：只发 `JSB_TYPE_BEGIN`/`JSB_DEFINE_*` 宏调用声明，不含实现；"静态方法"还是"成员方法"由消费者侧宏实现决定——`jsb_primitive_bindings.cpp` 两腿各挂 `class_builder.Instance()`，与生成文件内容无关） |
| `src/static_binding/gen/*`（`dispatch_*.gen.cpp`、`registry.gen.h`、`string_names.gen.h`、`manifest.gen.json`） | `misc/build/static_binding_codegen.py`（`binding_mode=static|shared` 时 SConstruct 每次构建自动调用以 `--binding-mode` 传参；默认 `shared`（2026-09-20 起），日志见 `OK: static binding tables generated`。`binding_mode=dynamic` 不跑 codegen） |
| `third/godot-cpp/gen/**`（`*.gen.inc` 等） | `third/godot-cpp/binding_generator.py` |
| `project/gen/**`、`project/typings/**`（运行期 TS 产物） | 引擎扩展 codegen：`godot --headless --editor --path ./project --generate-types`（入口 `jsb_editor_plugin.cpp` 的 `_generate_types_from_cmdline`） |

## 注解类型表（`jsb.runtime.gen.d.ts` 的 `ClassBinder` 等）

`project/typings/jsb.runtime.gen.d.ts` 里 `declare module "godot.annotations"` 的 `ClassBinder` /
`ExportOptions` / `RPCConfig` 由 **`src/editor/codegen/jsb_codegen_annotations.cpp`** 的
`get_annotation_types()` 以类型描述符（`Dictionary`）声明，`jsb_codegen_generator.cpp` 的
`emit_runtime_gen()` 序列化。**改注解签名 = 改这个文件**，不是改生成物。

**类型描述符 DSL 的能力边界（2026-09-25 实测）**：

- `DescriptorType`（`jsb_codegen_defs.h`）**没有 rest 参数概念**：`make_func` 的 `parameters` 里写
  `"...names"` 只是**名字**带三个点，元素类型要自己给（如 `string[]`）。既有先例：`export.range` 的
  `...extra_hints: ExportRangeExtraHint[]`。
- **无法在描述符里表达"函数重载"**（一个函数多个签名）。需要重载时用 `make_intersection()` 把多个
  `make_func()` 拼成交集 —— TS 对"交集的多个调用签名"按重载解析。
- **顺序敏感**：当某个签名（如 `(...names: string[])`）也接受零实参时，零参签名必须排在**前面**，
  否则无参调用会解析到 rest 签名。
- 例：`exposed.const` 需同时支持 `()`（成员装饰器）与 `(...names: string[])`（类装饰器）⇒
  `make_intersection({ make_func(Array(), make_godot_args("ClassMemberDecorator", {make_godot("StaticMemberDecoratorContext")})), make_func(names_params, class_decorator_func) })`。
  **泛型实参**用 `make_godot_args(name, args)`（先例：`export.object` 的
  `make_godot_args("ClassValueMemberDecoratorContext", instance_args)`）。成员形态的上下文收窄到
  `StaticMemberDecoratorContext`（`= ClassFieldDecoratorContext & { static: true }`，声明在
  `godot.annotations.ts`）——**这是唯一能让错位注解（instance field/accessor/method、`static accessor`）
  变成编译错误的地方**，因为 Godot 侧只读类对象的自有属性，错位成员会被静默丢弃。
  该类型名同时被生成物与 `scripts/typings/godot.generated.d.ts` 引用 ⇒ 改它必须同步镜像。
- **`scripts/typings/godot.generated.d.ts` 是同一声明的"独立检出"镜像**：`godot.minimal.d.ts` 经
  `///<reference>` 引入它，`godot.annotations.ts` 靠它编译（`createClassBinder()` 用 `Object.assign`
  产出交集类型，故该模块的 `ClassBinder` 声明必须与生成物一致）。**改注解签名须同时改该镜像**，
  否则独立检出编译失败。
- **对应关系是双向的**：`godot.annotations.ts` 的实现签名必须能被该 `ClassBinder` 接受，但**运行期
  实现走的是真实函数**——对象字面量方法**不能**承载重载签名，所以需要重载的注解要在
  `createClassBinder()` 里写成**局部 `function` 重载声明**，再在返回的对象字面量里引用它。

## 通用原则

1. 看到文件名含 `.gen.` 立即停止编辑，先查上表或搜索 `SConstruct` / `SCsub` / `misc/build/` 生成脚本
2. 新增生成规则写在对应 `SCsub` / `SConstruct` / `misc/build/` 脚本中
3. 静态绑定 codegen 属**构建流程代码生成**，构建接线见 [../build/scons-build.md](../build/scons-build.md)
4. 基线校验针对的是**编辑器运行期 TS 代码生成**（上表最后一行：`--generate-types` 产 `project/gen/`、`project/typings/`），验证改动编辑器生成代码后产物与改动前基线一致——与构建流程的代码生成无关；方法论见 [../test/codegen-baseline.md](../test/codegen-baseline.md)
