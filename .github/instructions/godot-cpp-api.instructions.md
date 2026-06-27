---
description: "Use when writing or modifying godot-cpp extension code, adapting Godot source interfaces to godot-cpp style."
applyTo: "**/*.h", "**/*.cpp"
---
# Godot-CPP API 适配规则

## 核心原则

godot-cpp 的接口与 Godot 源码提供的接口**大不相同**，切勿直接照搬。

## 具体规则

1. **虚函数 `_` 前缀** — godot-cpp 中类的虚函数大多带有 `_` 前缀（如 `_process`、`_ready`、`_enter_tree`），不要使用 Godot 源码中无前缀的名称。
2. **保留 virtual/override** — 修改虚函数时**必须保留或补充** `virtual` 和 `override` 关键字，不能将其移除。
3. **使用 godot-cpp 的绑定机制** — 优先使用 `GDExtension` 宏和 `godot::` 命名空间下的类型（如 `godot::String`、`godot::Ref<>`），而非 Godot 核心类型。
