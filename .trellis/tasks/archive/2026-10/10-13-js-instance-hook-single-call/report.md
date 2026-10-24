# Report: JS 脚本实例属性 hook 重复调用修复

## 结论

**完成。** 六个属性 hook 全部改为语义正确的单次调用，回归 fixture + C++ 用例落位，
PRD 六条 AC 全部达成；实现与验证实际在 10-12 轮会话内完成并交叉记录（其报告验证表
即为本任务 fixture 的计数证据），本报告为补记（2026-10-24 归档时补写）。

## 改动（代码事实）

`src/runtime/weaver/jsb_script_instance.cpp`：

| hook | 改后形态 |
|---|---|
| `set`（`_set`） | 删除手搓 `base` 链遍历，循环外单次 `env_->call_script_method(class_id_, …, _set, …)`（:569） |
| `get`（`_get`） | 同上单次调用（:643） |
| `get_property_list`（`_get_property_list`） | 保留 C++ 侧逐层收集 `script_class_info_.properties` 并去重的遍历，JS hook 移出循环只调一次（:666） |
| `validate_property` | 单次调用（:725） |
| `property_can_revert` | 单次调用（:742） |
| `property_get_revert` | 单次调用（:757） |

未动：`get_property_type` / `has_method`（纯查 `script_class_info_`，无 JS 调用）、
`callp`（本来就是单次纯 JS 路径）、`notification`（上一轮已修，单次 `callp`）。

## 回归设施

- fixture：`project/tests/instance-hooks/{hooks-base,hooks-derived,hooks-plain}.ts`
  （两层/单层/无 hook 三态）。
- C++ 用例：`src/runtime/tests/test_jsb_script_language_queries.h:499` 起，加载上述
  fixture 验证调用次数与原型链回落语义（派生类定义则派生类响应，否则落基类）。

## 验收（AC 对照）

| AC | 结果 | 证据 |
|---|---|---|
| AC1 `_get`/`_set`/`_get_property_list` 各一次 | ✅ | 10-12 归档报告验证表：「改后各 1 次（改前各 2 次）」，同 fixture 计数 |
| AC2 `validate_property`/`property_can_revert`/`property_get_revert` 各一次 | ✅ | 同上轮验证；代码形态为循环外单次调用（行号见上表） |
| AC3 `get_property_list` 仍收集整链属性且去重不变 | ✅ | C++ 侧收集遍历保留，JS 调用移出循环 |
| AC4 行为等价（读写/revert/校验结果不变） | ✅ | doctest 断言覆盖返回值语义 |
| AC5 原型链回落语义 | ✅ | `hooks-plain.ts`（无 hook 层）用例 + `call_script_method` 原型链查找 |
| AC6 doctest 全绿 + template_release rc=0 | ✅ | 10-12 轮：96/96 + 1264/1264、template_release rc=0；2026-10-24 复验：96/96 + 1437/1437（套件扩充后），当前树 doctest SUCCESS |

## 备注

- 语义前提与 `architecture-constraints.md`「栈虚拟函数按 JS 原型链解析」条目一致：
  JS 不隐式调父类，派生类定义同名 hook 即替换（GDScript 的逐层调用语义**不**引入）。
- `implement.jsonl` / `check.jsonl` 仅记录了实现前的 spec 阅读条目；实际编码与验证
  记录在 10-12 轮会话与其报告中，本任务无独立代码欠账。
