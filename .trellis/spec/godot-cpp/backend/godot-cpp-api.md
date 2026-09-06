# godot-cpp API 适配规范

> 适用范围：在 `src/` 中使用 godot-cpp 绑定、把 Godot 源码接口适配到 godot-cpp 风格的代码。

godot-cpp 的接口与 Godot 源码提供的接口**大不相同**，切勿直接照搬。

1. **虚函数 `_` 前缀** — godot-cpp 中类的虚函数大多带 `_` 前缀（`_process`、`_ready`、`_enter_tree`），不用 Godot 源码中无前缀的名称
2. **保留 virtual/override** — 修改虚函数时**必须保留或补充** `virtual` 与 `override` 关键字
3. **使用 godot-cpp 绑定机制** — 优先 `GDExtension` 宏与 `godot::` 命名空间类型（`godot::String`、`godot::Ref<>`），而非 Godot 核心类型
4. **接口表初始化** — 非 GDExtension 入口的代码默认没有 godot-cpp 接口表（见 [architecture-constraints.md](../../godotjs-ext/cpp/architecture-constraints.md)）
