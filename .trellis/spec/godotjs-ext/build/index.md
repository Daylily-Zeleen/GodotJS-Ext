# 构建规范（godotjs-ext）

> 适用范围：SCons 构建、静态绑定 codegen 接线、构建产物（dll）部署验证。
> C++ 测试、TS 集成测试、benchmark、代码生成基线校验见 [../test/index.md](../test/index.md)。

## 开发前检查单

- [ ] 编译前读 [scons-build.md](./scons-build.md)（强制命令、禁 clean、增量机制）
- [ ] 构建产物（dll）部署验证？读 [scons-build.md](./scons-build.md) 的部署节
- [ ] 构建后要跑测试？测试命令与验收标准见 [../test/index.md](../test/index.md)

## 质量检查

- [ ] 只用规范命令编译，未自行编造参数、未 `scons --clean`
- [ ] dll 替换验证：两份 gdextension 的 dll 都换，md5sum 确认一致
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
