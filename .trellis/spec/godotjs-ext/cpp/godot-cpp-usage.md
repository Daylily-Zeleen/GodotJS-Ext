# godot-cpp 使用规范

> 适用范围：`src/` 中调用 godot-cpp 绑定的代码。
> `third/godot-cpp` 是**外部依赖子模块**（pinned 上游、只读、不由本仓维护），不设独立 Trellis 包；
> 本文件只约束**我方对它的使用方式**。构建接线见 [../build/scons-build.md](../build/scons-build.md)。

godot-cpp 的接口与 Godot 源码提供的接口**大不相同**，切勿直接照搬。

## API 适配

1. **虚函数 `_` 前缀** — godot-cpp 中类的虚函数大多带 `_` 前缀（`_process`、`_ready`、`_enter_tree`），不用 Godot 源码中无前缀的名称
2. **保留 virtual/override** — 修改虚函数时**必须保留或补充** `virtual` 与 `override` 关键字
3. **使用 godot-cpp 绑定机制** — 优先 `GDExtension` 宏与 `godot::` 命名空间类型（`godot::String`、`godot::Ref<>`），而非 Godot 核心类型
4. **接口表初始化** — 非 GDExtension 入口的代码默认没有 godot-cpp 接口表（见 [architecture-constraints.md](./architecture-constraints.md)）

## 质量检查

- [ ] 未直接修改 `third/godot-cpp/` 源码（例外须用户明确确认）
- [ ] 使用 `godot::` 类型而非 Godot 核心类型
- [ ] ptrcall 参数/返回内存按 `MaxSizeEncodeArgType` 分配（见 [ptrcall-encoding.md](./ptrcall-encoding.md)）

## 子模块事实

- 构建经顶层 `SConstruct` 的 `SConscript("third/godot-cpp/SConstruct", ...)` 接入；`API_VERSION = "4.7"` 由顶层传入，是唯一硬编码点
- codegen 源数据 = 子模块内置 `gdextension/extension_api-4-7.json`（我方静态绑定 codegen 与 godot-cpp 自己的 `binding_generator` 用同一文件）
- `third/godot-cpp/gen/**` 由 `binding_generator.py` 生成，勿手改（见 [generated-files.md](./generated-files.md)）
