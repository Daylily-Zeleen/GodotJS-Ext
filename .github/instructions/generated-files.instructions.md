---
description: "Use when: 涉及 .gen. 构建生成文件的修改。禁止直接编辑生成文件，应修改构建逻辑。"
applyTo: "**.gen.**"
---
# 构建生成文件保护规则

文件名中带有 `.gen.` 的文件（如 `*.gen.cpp`、`*.gen.h`、`*.gen.inc`）是**在构建过程中自动生成**的，直接修改这些文件是无效的 — 下次构建时会被覆盖。

## 禁止操作

- ❌ 不要直接编辑任何 `*.gen.*` 文件
- ❌ 不要尝试修复 `.gen.` 文件中的编译错误
- ❌ 不要向 `.gen.` 文件中添加新代码

## 正确做法

如果要修改 `.gen.` 文件的内容，应该找到**生成该文件的构建逻辑**，修改对应的源文件或生成脚本：

| 生成文件 | 应修改的构建逻辑 |
|---|---|
| `src/gen/doc_data.gen.cpp` | `SConstruct` 或 `GodotJS/SCsub` 中的生成逻辑 |
| `GodotJS/jsb.gen.h` | `SConstruct`（约 173 行起）中的 `jsb.gen.h` 生成逻辑 |
| `GodotJS/jsb_project_preset.gen.h`/`.cpp` | `SConstruct` 中的对应生成逻辑 |
| `GodotJS/weaver-editor/templates/templates.gen.h` | `GodotJS/weaver-editor/templates/SCsub` |
| `godot-cpp/gen/.../*.gen.inc` | `godot-cpp/binding_generator.py` |
| 其他 `*.gen.*` 文件 | 搜索项目中的 `SConstruct`、`SCsub` 或 Python 生成脚本 |

## 通用原则

1. 如果看到文件名包含 `.gen.` 立即停止编辑 — 找生成逻辑
2. 生成逻辑通常位于 `SConstruct`、`SCsub` 或 Python 脚本中
3. 如果需要新增生成规则，在对应的 `SCsub` 或 `SConstruct` 中添加
