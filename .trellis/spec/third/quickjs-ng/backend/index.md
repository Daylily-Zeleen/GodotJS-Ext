# quickjs-ng 子模块使用规范

> 包路径：`third/quickjs-ng`（git submodule）。这是可选 JS 引擎之一（`use_quickjs_ng=yes`），源码只读；构建时仅少量 C 文件被直接编入（`SConstruct` 的 `quickjs_src_descs`：dtoa.c / libregexp.c / libunicode.c / quickjs.c）。

## 引擎选择机制

- 默认引擎 v8；`use_quickjs_ng=yes` 切换，`JSB_WITH_QUICKJS` + `JSB_PREFER_QUICKJS_NG` 宏生效
- 运行时实现位于 `src/runtime/impl/quickjs/`（不在此子模块内）
- Windows MSVC 下 quickjs-ng 源需要 `/experimental:c11atomics`（SConstruct 已处理）

## 规则

- ❌ 不修改 `third/quickjs-ng/` 源码；适配写在 `src/runtime/impl/quickjs/` 或 `src/compat/`
- ❌ 无前端/Web 代码——本包没有 frontend 层
- ✅ 引擎相关 bug 先区分：quickjs-ng 引擎自身问题（上游）vs 我方桥接层问题（`src/`）
