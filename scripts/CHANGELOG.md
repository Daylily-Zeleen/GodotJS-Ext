# Changelog

## 1.1.0

### Minor Changes

-   6abe171: web 后端完整落地：ShadowRealm（web shim 多 engine）与 web 导出链路（export glue）。
    worker 接收消息时向宿主环境转发未捕获异常；跨线程释放桥内句柄改为在持有线程执行；
    补齐 web 后端的 `GlobalInitialize::shutdown()`；`IsInt32`/`IsUint32` 对齐 v8 语义；
    未处理 Promise 错误向宿主转发；source map 的 web 构建正则修正。

### Patch Changes

-   bada6d5: 修复 step4/step6 退出期 0xC0000005：不再把引擎对象的 instance binding 留到
    本 DLL 卸载之后——资源扫描跳过 `.gdextension` 清单；`TypeDB::load_singletons()` 不再
    为每个引擎单例建 wrapper（store 命中读数据、缺失走原始接口直读类名）；`require` 到
    引擎单例时登记 instance_id，CORE 反初始化时校验存活后统一归还（43a2944 拆分为独立文件）。
-   0a9fe10: 值类型常量局部变量改为值初始化（NDK clang 对 `const` 默认初始化直接报错，
    曾导致 android template_release 构建矩阵失败）。
-   4bf2653: headless CLI 步骤（install-project-files / generate-types）能够干净收尾并返回
    正确退出码。
-   13d69a2: api store 未生成时以 Warning 取代 ERROR 误报；编辑器侧 TS 诊断加固。
-   60ef55d: codegen TypeDB/DocCache 改为值语义存储，移除手工所有权与冗余登记；修复
    FileSplitter 写出器的泄漏。
-   5c7b7bc: 只允许 ts 脚本执行 `_request_codegen`；代码生成过程中临时实例化的节点即时释放。
-   78289a6: jsc 后端把 -0.0 视为非整数；script docs 避免未校验的字典读取。
-   cbd7d02/f9e81d4: api_tool 的 `base_dir_` 固定为 res:// 路径；修复 `get_api_data_files`
    无法遍历文件夹的问题。
-   627820f/f245107/b1e9886: CI 调整——push 只在长期分支触发（PR 分支由 pull_request
    事件承担）、测试矩阵支持 `GODOT_BIN` 覆盖宿主引擎、web 步骤按 web 腿门控。

## 1.0.2

### Patch Changes

-   69bb524: Release one archive per JS engine (v8, qjs-ng, jsc, node, web) instead of a single
    v8/qjs-ng pair, and ship a `.gdextension` whose `[libraries]`/`[dependencies]`
    entries match the files actually present in the archive.

    The engine list, the legs each archive contains and the packaged
    `.gdextension` are all derived from the CI build matrix by
    `misc/release/package.py` - the same script the `verify-release-artifacts` gate
    runs - so jsc and node can no longer be silently left out of a release, and the
    browser (pure-web) wasm can no longer end up inside the v8 archive.

## 1.0.1

### Patch Changes

-   bf246db: Align the Changesets release flow with the migrated scripts workspace.

All notable changes to the GodotJS extension are documented in this file.
