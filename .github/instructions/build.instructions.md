---
description: "Use when: GodotJS-Ext 项目编译、构建、测试相关操作。定义 SCons 编译的强制规则和限制。"
applyTo:
  "**"
---

# GodotJS-Ext 编译规则

## 编译命令（强制）

当需要编译 GodotJS-Ext 项目时，**必须且只能**使用以下命令：

```
scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6
```

### 关键约束

1. **绝对不要自行编造编译命令** — 只使用上述参数组合
2. **绝对不要调用 `scons --clean`** — 这会导致全量编译，极其耗时
3. 利用增量编译机制，避免不必要的清理步骤
4. 如需重新编译，直接运行上述命令即可（增量构建会自动跳过未更改的部分）

### 为什么这些规则很重要

- 该项目的编译非常耗时，全量重建可能需要很长时间
- 增量编译已经足够高效，不需要手动清理
- 任何偏离上述参数的尝试都可能导致构建失败或性能下降
