# member-form-operators

## Goal

以成员函数形式实现内置类型运算符替代当前静态函数形式（绑定与编辑器代码生成）：vec.add(other) 替代 Vector2.OP_ADD(a, b)，绑定层把 this 作为左操作数；JS 原生基础类型（bool/int/float）无成员形态，仍以静态函数实现。编辑器 d.ts 同步生成成员签名。

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
