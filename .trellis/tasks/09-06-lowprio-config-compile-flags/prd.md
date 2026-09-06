# 编译参数控制 jsb.config.h 配置

> 来源：`.本地文档/低优先级.md` jsb.config.h 条目（P3，构建改进）。

## Goal

用 SCons 编译参数控制 `src/runtime/jsb.config.h` 的配置项（当前为手工编辑头文件），使引擎组合/特性开关可经构建命令行声明。

## Background

`jsb.config.h` 现含 `JSB_WITH_BIGINT`、`JSB_MAX_SAFE_INTEGER`、`JSB_UTF16_CONV_PREFERRED`、`JSB_STRICT_DISPOSE` 等配置宏（`DO NOT CHANGE` 标注的除外）。SConstruct 已有 `CompileDefines` 机制（引擎宏 `JSB_WITH_V8` 等即由构建注入），扩展同一机制即可。

## Requirements

1. 盘点 `jsb.config.h` 中可参数化宏：区分「可安全经命令行注入」与「必须保持头文件常量」（如 `JSB_MAX_SAFE_INTEGER` 有 `DO NOT CHANGE` 标注，涉及 ABI/序列化兼容）
2. 可注入项迁到 SConstruct `CompileDefines`（头文件保留默认值兜底）；`--help` 文案补说明
3. CI 矩阵不强行增加组合——本任务只提供能力，不改默认构建

## Acceptance Criteria

- [ ] 宏分类清单落档（可注入 / 必须保留）
- [ ] 至少一个宏实际迁为编译参数并验证（改参数构建生效、不改参数行为不变）
- [ ] CI 全绿（默认组合无行为变化）

## Notes

- `CompileDefines` 机制见 `SConstruct`（约 377 行起）
