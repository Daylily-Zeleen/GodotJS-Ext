# 补 uint64/bigint 静态绑定 codegen

> 来源：`.本地文档/低优先级.md` bigint/uint64 条目（P3，类型映射评估）。

## Goal

JS BigInt 与 Godot int64/uint64 的转换目前是不对称且有损的：Variant→JS 经 `new_integer`（int64 出口，> 2^53-1 转 BigInt），但 JS BigInt→Variant 只调 `Int64Value()`——超过 `INT64_MAX` 的 uint64 值有损。评估完整映射方案或明确记录限制。

## Background（2026-09-06 代码核实）

- `src/runtime/bridge/jsb_type_convert.cpp:543`：`p_jval->IsBigInt()` 分支只调 `v8::BigInt::Int64Value()`，无 `Uint64Value()` 出口（全仓库 grep `Uint64Value` 仅 `impl/web/jsb_web_interop.h` 有声明）
- `src/runtime/impl/v8/jsb_v8_helper.h:153`：`new_integer(int64_t)` 出口——Variant::INT 本身是 int64，无符号来源
- static binding codegen `misc/build/static_binding_codegen.py` 的 `PARAM_TYPE_MAP`/`OPERAND_CPP_MAP`：`"int" → int64_t`（api json 的 int 参数全映射 int64）
- `JSB_MAX_SAFE_INTEGER = 2^53-1`（`jsb.config.h:139`，标注 DO NOT CHANGE）只控制 gd→js 方向的 BigInt 化阈值

## 需要评估的问题

1. **引擎侧上限**：Godot Variant::INT 是 int64；api json 中是否存在真正 uint64 语义的参数/返回（还是全部按 int64 语义）？以 extension_api-4-7.json 实测为准
2. **BigInt→Variant**：JS BigInt 超 INT64_MAX 时——按无符号读（`Uint64Value` 后转 double/报错）还是按现状截断？错误路径如何暴露给脚本作者
3. **codegen**：api json 若出现 uint64 参数（如某些低层 API），`PARAM_TYPE_MAP` 需要新增映射；thunk 层的 `marshal`/`produce` 路径同步

## Requirements

- 以实测数据（api json 全量扫描）代替猜测；无 uint64 实参则本任务结论收敛为「记录限制」即可关闭
- 若实施映射变更：static 与 dynamic 两条路径行为必须一致（bench consistency gate 会拦截分歧）

## Acceptance Criteria

- [ ] api json 的 int/uint64 使用统计落档
- [ ] 结论：实施映射（含 static+dynamic 双路径）或记录限制（写入 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md` 或类型转换相关 spec）
- [ ] 若实施：bench + C++/TS 测试全绿，consistency gate 通过

## Notes

- ptrcall 编码背景见 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`
