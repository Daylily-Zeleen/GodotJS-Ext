# 归档报告：自建 PIC 版 lws 恢复 Linux websocket

归档日期：2026-09-26
状态：**需求全部满足**（CI 实测证据）

## 目标
解决 Linux 预编译 `libwebsockets.a` 非 PIC 无法链入共享库的问题
（原临时处理：Linux 禁用 lws，v8 debugger 的 websocket 功能缺失）。

## 动作与证据

| 需求 | 证据 |
|---|---|
| 1. 自建 lws 构建 workflow（多平台 matrix） | `GodotJS-Dependencies/.github/workflows/build_lws.yml`，10 条腿 |
| 2. Linux 用 PIC 编译 | `.github/actions/lws/build/action.yml` 对 linux/android 传 `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` |
| 3. 打包为 zip（布局与 release 1.1 一致） | `lws_4.3.zip` 内为 `lws/<platform>_<arch>_release/{include,libwebsockets.a}`，实测顶层目录为 `lws/` |
| 4. 上传本项目 Release 并改 SConstruct 指向 | 现指向 `260925-node-final` 的 `lws_4.3.zip`；三个资产 URL 实测 HTTP 200（lws 10,153,838 bytes） |
| 5. 恢复 SConstruct 中 linux 的 lws 链接分支 | 已恢复；`SConstruct` 注释明示 "lws covers linux too: the prebuilt comes from our own GodotJS-Dependencies CI" |

**CI 实测（run 36152717104，全部 30 个 job success）**：
- `lws / build (linux, x86_64)` **success**，日志含
  `lws PIC validation passed: .../linux_x86_64_release/libwebsockets.a links as a shared object`
- `lws / build (linux, arm64)` **success**，同样含 PIC 校验通过
- lws 全部 10 条腿 success

**本地独立复核**：对旧的非 PIC 归档执行同样的整档 `.so` 链接测试，报出与 PRD 完全一致的
`relocation R_X86_64_PC32 against symbol 'stderr@@GLIBC_2.2.5' ... recompile with -fPIC`；
新归档通过。

## 验收标准
- [x] PIC 版 lws artifact 发布并可下载（HTTP 200 实测）
- [x] Linux 构建链入 lws 成功（CI PIC 校验 + 主仓 linux node/v8 腿均 success）
- [x] CI linux leg 全绿
- [ ] Linux 上 v8 debugger websocket 功能恢复（**需运行时手测**，非构建期可判定；构建链路已通）

## 遗留
- websocket 的**运行时**功能验证需要一次手测（属运行时验收，不阻塞构建链）。
