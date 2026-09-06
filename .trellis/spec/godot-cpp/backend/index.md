# godot-cpp 子模块使用规范

> 包路径：`third/godot-cpp`（git submodule）。本目录规范约束**我方仓库中对 godot-cpp 的使用方式**；godot-cpp 自身源码原则上只读。

## 开发前检查单

- [ ] 使用 godot-cpp API？读 [godot-cpp-api.md](./godot-cpp-api.md)（`_` 前缀虚函数、virtual/override、`godot::` 类型）
- [ ] ptrcall 相关？读 [ptrcall-encoding.md](../../godotjs-ext/cpp/ptrcall-encoding.md)（EncodeT 语义与缓冲区规范）
- [ ] 改 godot-cpp 源码本身？先获得用户确认——上游依赖，改动影响兼容性

## 质量检查

- [ ] 未直接修改 `third/godot-cpp/` 源码（除非用户明确要求）
- [ ] 使用 `godot::` 类型而非 Godot 核心类型
- [ ] ptrcall 参数内存按 `MaxSizeEncodeArgType` 分配

## 子模块事实

- 构建经 `SConstruct` 的 `SConscript("third/godot-cpp/SConstruct", ...)` 接入；`API_VERSION = "4.7"` 由顶层传入
- codegen 源数据 = 子模块内置 `gdextension/extension_api-4-7.json`（静态绑定 codegen 与 godot-cpp 自己的 binding_generator 用同一文件）
- `third/godot-cpp/gen/**` 由 `binding_generator.py` 生成，勿手改
