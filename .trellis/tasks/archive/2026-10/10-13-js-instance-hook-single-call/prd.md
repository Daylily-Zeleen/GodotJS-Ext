# JS 脚本实例属性 hook 重复调用修复

## Goal

`GodotJSScriptInstance` 的若干属性 hook 在 C++ 侧手搓 `script_->base` 链，但循环内**每轮都以最派生类 `class_id_` 调用同一个 JS 函数**。而 `Environment::call_script_method(class_id_, ...)` 内部的 `prototype.Get(...)` 本身就走 JS 原型链查找 ⇒ 同一函数被重复调用 N 次（N = 链上声明了它的层数）。

已实测复现（fixture `Derived extends Base`，两层都定义 hook）：

| hook | 实测调用次数 |
|---|---|
| `_get` | 2 |
| `_get_property_list` | 2 |
| `_set` | 2 |

对照语义基准（引擎源码实读）：

- **C#**（`csharp_script.cpp`）：**无链遍历**，每个 hook 一次 `CSharpInstanceBridge_Call`，交给 C# 虚分派解析。
- **GDScript**（`gdscript.cpp`）：有 `while (sptr)` 遍历，但调的是**该层的** `E->value`（`sptr->member_functions.find(...)`），且多数 hook 命中即 `return` —— 每层最多调一次，且只调声明了它的那层。
- **JS（现状）**：遍历 × 每轮都调最派生类 ⇒ **错**。

## Requirements

1. **无循环内额外操作的 hook：直接单次 `call_script_method`，删掉手搓链。**
   适用：`set` 的 `_set` 部分、`get` 的 `_get` 部分、`validate_property`、`property_can_revert`、`property_get_revert`。
2. **循环内有额外操作的 hook：保留遍历，但 JS hook 只调一次。**
   适用：`get_property_list` —— 循环同时承担"逐层收集 `script_class_info_.properties` 并去重"，这部分是 C++ 侧静态元数据，`call_script_method` 替不掉。
3. **纯查表的接口不动**：`get_property_type`、`has_method` 只查 `script_class_info_`，不调 JS，无重复调用问题。
4. **语义仍按 JS 原型链**：派生类定义同名 hook 即替换基类那份，不隐式调用父类。不得为"对齐 GDScript"而引入逐层调用（上一轮 `notification` 已在此处踩过，见 `architecture-constraints.md`）。
5. **不改 `callp` 本身** —— 它已经是纯 JS 路径。

## Acceptance Criteria

- [x] AC1：`_get` / `_set` / `_get_property_list` 在"两层都定义"的 fixture 下**各只被调一次**（实测日志计数）。
- [x] AC2：`validate_property` / `property_can_revert` / `property_get_revert` 同样只调一次。
- [x] AC3：`get_property_list` 仍能收集**整条链**上的 `script_class_info_.properties`（含基类），且去重行为不变。
- [x] AC4：行为等价性 —— 属性读写、revert、校验的**结果**与改前一致（`_get`/`_set` 的返回值语义、`_get_property_list` 的最终列表内容）。
- [x] AC5：`_get` 只在派生类定义时被派生类响应；派生类不定义时落到基类（JS 原型链语义）。
- [x] AC6：doctest 全绿（含新增回归用例）；`target=template_release` 编译 rc=0。

## Notes

- 回归测试放 `src/runtime/tests/`，fixture 放 `project/tests/`（既有约定，见 `project/tests/notification/`）。
- 上一轮的 `notification` 修复（`callp` 单次）已确立"JS 不隐式调父类"这一前提，本任务与之同源。
