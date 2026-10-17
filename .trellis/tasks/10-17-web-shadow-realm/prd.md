# Web 后端 ShadowRealm 支持

## Goal

web 后端支持 ShadowRealm：解除 web shim 单 engine 限制，按 realm/线程管理多 engine，让 cross-environment 的 ShadowRealm 用例在 web 上可用

## Requirements

- web shim（`src/runtime/impl/web/`）支持**同一 JS realm 内多个 engine**：
  - `_jsbb_` 持有 engine 表（按 id），`NewEngine(opaque)` 分配真实 id（= isolate opaque，每个 C++ Isolate 唯一），
    `GetEngine(id)`/`FreeEngine(id)` 按表解析；不再有"同 realm 只允许一个 engine"的限制。
  - 引擎级状态（stack/handles/registry/host_event_hooks）保持在 `jsbb_Engine` 实例内；realm 级状态
    （`interop`/`wasmop`/worker 消息缓存/内存视图）保持单份共享。
- 打开 web 的 `JSB_SHADOW_REALM_ENABLED`（`src/jsb.config.h`，当前对 `JSB_WITH_WEB` 强制 0）。
- ShadowRealm 的跨线程/跨 realm 释放沿用 2026-10-17 的属主线程投递机制（`em_proxying_queue`），
  payload 携带 engine id（opaque），thunk 在属主线程上按 engine 解析。
- 不改 `jsb_environment.*` 的非 web 逻辑；web 的差异只落在 web shim 与既有 `JSB_SHADOW_REALM_ENABLED` 门控内。
- 完成后更新文档站（docs 站点中 web 后端能力矩阵 / shadow realm 相关页面）。

## Acceptance Criteria

- [ ] web 构建启用 `JSB_SHADOW_REALM_ENABLED` 且可编译（`JSB_WITH_WEB` + `JSB_SHADOW_REALM_ENABLED` 同时为 1）
- [ ] cross-environment web 腿：`require('godot.shadowRealm')` 可用，shadow 后端不再被跳过，
      `object-transfer-*` 与 `error-reporting` 场景在 shadow 后端下通过（web 腿 PASS）
- [ ] 多 engine：主 env 与 shadow env 同时存活时，各自的 `jsbi_*` 调用解析到正确的 engine
      （`Global::Reset()` 跨线程投递仍按 engine id 解析）
- [ ] native（node 腿）回归：构建 rc=0、doctest 98|98+1280|1280、host-node PASS
- [ ] 文档站已更新并本地可构建/预览
- [ ] CI（pull_request run）全绿

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
