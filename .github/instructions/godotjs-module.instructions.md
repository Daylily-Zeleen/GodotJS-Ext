---
description: "Use when: 修改 GodotJS 子模块代码。限定修改范围仅限于 GodotJS/ 目录及其内部文件，禁止修改 godot-cpp/、src/、project/ 等其他子模块。"
applyTo:
  "GodotJS/**"
---

# GodotJS 子模块修改规则

## 修改范围限定

当需要对 GodotJS 子模块进行修改时，**必须且只能**在 `GodotJS/` 目录及其子目录内操作。

### ✅ 允许修改的文件

- `GodotJS/` 下的所有文件和子目录
- `GodotJS/bridge/` — JS 桥接层
- `GodotJS/compat/` — 兼容性层
- `GodotJS/docs/` — 文档资源
- `GodotJS/impl/` — 运行时实现（jsc、quickjs、v8、web）
- `GodotJS/internal/` — 内部工具与辅助类
- `GodotJS/lws/` — LWS 库
- `GodotJS/quickjs/` — QuickJS 引擎
- `GodotJS/quickjs-ng/` — QuickJS-NG 引擎
- `GodotJS/scripts/` — 构建与测试脚本
- `GodotJS/tests/` — 测试代码
- `GodotJS/v8/` — V8 引擎
- `GodotJS/weaver/` — Weave 模块
- `GodotJS/weaver-editor/` — Weave 编辑器插件
- `GodotJS/config.py`、`GodotJS/SCsub` — 构建配置
- `GodotJS/register_types.cpp`、`GodotJS/register_types.h` — 注册逻辑

### ❌ 禁止修改的文件（除非明确要求）

- `godot-cpp/` — Godot C++ 绑定库（第三方/依赖）
- `SConstruct` — 顶层构建脚本（除非需要调整 GodotJS 的集成方式）
- `.github/instructions/` — 指令文件本身

## 关键约束

1. **不要修改 `godot-cpp/`** — 这是 Godot 的官方 C++ 绑定库，修改它会影响整个扩展系统的兼容性
3. **不要修改 `project/`** — 这是测试用的 Godot 项目，除非明确要求调整项目配置
4. **第三方库文件夹视为只读** — `lws/`、`quickjs/`、`quickjs-ng/`、`v8/` 是第三方库，原则上不应修改其源码。如需适配，应在 `GodotJS/impl/` 或 `GodotJS/compat/` 中编写兼容层

## 通用原则

- GodotJS 是一个独立的脚本引擎集成模块，封装在 `GodotJS/` 目录下
- 所有与 JavaScript/QuickJS/V8 引擎相关的功能都应在此目录内实现
- 如果需要暴露新的 Godot API 绑定，通过 `GodotJS/bridge/` 层完成，不要直接修改 `godot-cpp/`
