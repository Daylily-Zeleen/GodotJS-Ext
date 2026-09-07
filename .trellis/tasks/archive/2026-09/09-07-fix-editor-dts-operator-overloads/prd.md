# fix-editor-dts-operator-overloads

## Goal

编辑器 d.ts 生成器把双元运算符的 right=Variant(NIL) 重载输出为缺少右参的一元签名（如 static OP_EQUAL(left)），与实际双参调用语义不符。修正 jsb_codegen_writer.cpp 的 operator_ 输出逻辑：区分一元（NEGATE/POSITIVE/NOT/BIT_NEGATE）与双元运算符，双元一律带 right 参数（right=NIL 输出 right: Variant | null）。低优先级；重跑编辑器代码生成并全量校验 typings。

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
