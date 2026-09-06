# 添加 Hermes 引擎与 NAPI

> 来源：`.本地文档/低优先级.md` Hermes 条目（P3，新功能，工程量大）。

## Goal

新增 Meta Hermes 作为可选 JS 引擎：`impl/hermes/` 实现层 + NAPI 适配，与既有 v8 / quickjs / quickjs-ng / jsc / web / node impl 并列。

## Requirements

- 参考 `src/runtime/impl/quickjs/`（轻量 C API 风格，最接近 Hermes）与 `src/runtime/impl/v8/`（NAPI 相关处理）的实现结构
- SConstruct 增加 `use_hermes=yes` 开关与第三方库描述（`ThirdPartyDescriptor`），宏 `JSB_WITH_HERMES`
- `bridge` 层类型转换复用现有 `TypeConvert`；引擎相关差异（GC/句柄生命周期/字符串编码 utf16）在 impl 层消化
- 测试项目 TS 全套（runtime + editor 套件）在 hermes 模式下通过

## Acceptance Criteria

- [ ] `use_hermes=yes` 构建通过（windows/linux 至少一平台）
- [ ] C++ doctest + TS 集成测试 exit 0、无泄漏
- [ ] benchmark 可在 hermes 腿上运行

## Notes

- 前置阅读：`.trellis/spec/godotjs-ext/index.md` 项目结构、`.trellis/spec/godotjs-ext/cpp/coding-standards.md`
- 大工程，启动前先写 design.md（impl 接口清单、内存所有权、预编译库获取方式）
