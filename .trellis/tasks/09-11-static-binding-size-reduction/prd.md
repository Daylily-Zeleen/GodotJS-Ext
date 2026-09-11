# 静态绑定库体积优化：相同函数签名共用 thunks

## Goal

优化静态绑定的库体积。参考现有 ReflectBuiltinMethodPointerCall 思路：让相同函数签名共用同一 thunks 函数，额外传入用于执行的 godot 函数指针（或 hash+函数名用于懒加载，待评估）以及函数默认参数作为 v8 自定义数据(callback data)。当前每个 API 方法实例化一个 builtin_method_thunk/class_method_thunk 模板，签名相同的重载各自展开，库体积膨胀。目标：消除按签名重复实例化，改用共享 thunk + 每方法自定义数据。

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
