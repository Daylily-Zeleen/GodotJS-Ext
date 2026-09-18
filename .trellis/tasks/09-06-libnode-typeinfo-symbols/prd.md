# libnode V8 Delegate typeinfo 符号缺失

> 来源：`.本地文档/低优先级.md` libnode 条目 + `ci.yml` node 矩阵注释（P3，上游依赖）。

## Goal

解决 node 模式在 macOS/Linux 的链接失败：`moluopro/libnode` 的 `libnode.a` 缺失 V8 Delegate 基类的 RTTI typeinfo 符号，导致 CI 矩阵中 node 的 macos/linux leg 被注释禁用、无测试覆盖。推动上游修复后解除禁用。

## Background（已核实的事实）

链接报错：

```
Undefined symbols:
  "typeinfo for v8::ValueSerializer::Delegate"   (referenced by jsb::Serialization::VariantSerializerDelegate)
  "typeinfo for v8::ValueDeserializer::Delegate" (referenced by jsb::Serialization::VariantDeserializerDelegate)
```

- 根因：`libnode.a` 根本没有这两个符号（`_ZTIN2v815ValueSerializer8DelegateE` / `_ZTIN2v816ValueDeserializer8DelegateE`）；我方 `src/runtime/jsb_environment.h` 的 delegate 子类 vtable 引用基类 typeinfo
- `--whole-archive` / `-force_load` 无效——符号不在库里，链接器无从解析
- 当前 CI 状态：node 仅 windows leg 启用（`ci.yml` build 矩阵注释块、test 矩阵 host-node 仅 windows）

## Requirements

1. 向 `moluopro/libnode` 提 issue，附上述符号名与链接错误；说明需要的修复方向：
   - 构建 libnode 时不对 V8 使用 `-fno-rtti`；或
   - 链接/编译配置强制保留这两个 Delegate 类的 RTTI 元数据（如 `-Wl,--no-gc-sections` 或显式引用）
2. 上游发布含符号的新版本后：更新 `SConstruct` 的 libnode 版本，恢复 `ci.yml` 中被注释的 node macos/linux build leg
3. 在 `test` job 补 node macos/linux 测试 leg（参考 `ci-macos-test-legs` 任务的 macos leg 方案）

## Acceptance Criteria

- [ ] 上游 issue 已提并得到响应（或自行 fork 修复）
- [ ] node macos/linux 构建恢复并全绿
- [ ] `test` job 的 node 测试 leg 扩展到 macos/linux
- [ ] `ci.yml` 中的禁用注释块删除

## Notes

- 本任务为**上游依赖**型，自身无可本地实施的代码改动（除提 issue）；上游未响应前保持 planning
- 关联：`09-06-ci-macos-test-legs`（macos 测试 leg 方案复用）、`09-06-ci-release-packaging`（node 打包）
- 临时产物放 `.agent_tmp/`
