# C++ 开发规范（godotjs-ext）

> 适用范围：`src/` 全部 C++ 代码（runtime / editor / api_tool / common / internal / compat / static_binding / testing）。
> 构建与测试命令见 [../build/index.md](../build/index.md)。

---

## 开发前检查单

- [ ] 读 [coding-standards.md](./coding-standards.md)（命名空间禁令、临时文件位置、格式化）
- [ ] 涉及 `.gen.` / `.def.` 文件？读 [generated-files.md](./generated-files.md) —— 禁止直接编辑
- [ ] 涉及 ptrcall / api_tool / 静态绑定？读 [ptrcall-encoding.md](./ptrcall-encoding.md)
- [ ] 涉及 GDExtension 生命周期 / 跨库共享 / DLL 拆分？读 [architecture-constraints.md](./architecture-constraints.md)
- [ ] 修改导出符号或公共头前，先用 LSP 查全部引用点

## 质量检查

- [ ] 头文件无 `using namespace`（局部作用域除外）
- [ ] 无临时文件散落在 `src/`、`project/` 等业务目录（一律 `./.agent_tmp/`）
- [ ] 未直接编辑任何 `*.gen.*` / `*.def.*` 文件
- [ ] ptrcall 参数/返回内存按 `MaxSizeEncodeArgType` 分配（新增 Variant 类型时同步 `all_encode_types`）
- [ ] 格式化通过 `misc/format_src.py`（遵循 `.clang-format`）
