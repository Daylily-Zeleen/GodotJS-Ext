# 执行计划：跨隔离区错误上报

> 顺序执行；每步都写清"验收方式"。未通过验收不进下一步。

## 0. 前置：先复现崩溃（回归用例先行）

- [ ] 在 `src/runtime/tests/test_jsb_shadow_realm.h` 加用例：`globalThis.__realm.evaluate("(function () { throw new Error('boom'); })()")`，宿主侧 `try/catch` 断言拿到 `instanceof Error` 的对象。
- [ ] **验收**：修前必须复现崩溃（`report.md` §30 已实测过：SEH crash、栈顶 `jsb_shadow_realm.cpp:1252`、doctest 82→66）。把这次复现的输出存进本任务 `report.md`。
- [ ] 提交该用例（**先红**）或记为独立提交点——交由用户决定是否分两次提交（红 → 绿）。

## 1. 修前置缺陷（回滚点 1）

- [ ] `src/runtime/bridge/jsb_shadow_realm.cpp:1251-1252`：`ToLocalChecked()` → `ToLocal(...)` + 失败走错误分支（禁止 `jsb_check` 崩）。
- [ ] 错误分支不再 `jsb_throw(guest_isolate, ...)`：改为把错误交给"调用方 isolate"（本步先只做 host 侧抛出，记录形态见步骤 2）。
- [ ] 核对三 shim 的 `TryCatch` 结束语义（`impl/{jsc,quickjs,web}/jsb_*_catch.h:42`），必要时在错误分支显式结束异常；**逐个核对既有调用点**（很多地方是"捕获后丢弃"，语义不能变）。
- [ ] **验收**：步骤 0 的用例从"崩溃"变成"通过"；`scons ... tests=yes` + doctest 全绿（v8），且无其它用例回归。

## 2. 记录 / 重建 helper

- [ ] 新增错误记录采集（源侧）：从 `impl::TryCatch` 取 `message`/`stack`，补 `name`。
- [ ] 新增字段复制（§design 5.3）：白名单、深度/节点/字节上限、环检测、**不触发 getter**、`untransferred` 清单。
- [ ] 新增目标侧重建：在目标 realm `new Error` + 覆盖 `name`/`stack` + 挂 `extra`，并把未携带清单挂到 `Symbol.for("jsb.untransferred")` 上（见 `design.md` §5.4）。
- [ ] **验收**：单元用例——构造带自定义字段/嵌套/环/深链/getter 的错误对象，断言：核心字段正确、能转的字段到达、不能转的进清单、getter 未被触发、整体不失败。

## 3. ShadowRealm 三个入口（AC2/AC3）

- [ ] `evaluate`：宿主侧抛重建的 Error。
- [ ] `importValueSync`：同 `evaluate`（替换 `jsb_shadow_realm.cpp:1362` 的字符串抛出）。
- [ ] `importValue`：`resolver->Reject(...)` 用重建的 Error（替换 `:1316` 的字符串 reject）；顺带确认 `:1315-1316` 的 throw+reject 组合语义，避免双重报错。
- [ ] **验收**：三个入口各一条用例，断言 `instanceof Error`、`message`、`stack` 含 guest 栈文本。

## 4. Worker + TransferableShadowRealm（AC4/AC5/AC6）

- [ ] worker 启动失败：在启动/加载失败处 `post_message(Message(TYPE_ERROR, handle, <序列化记录>))`；成功路径仍发 `TYPE_READY`（`jsb_worker.cpp:611` 不动）。
- [ ] worker 侧回调抛错：`jsb_worker.cpp:286-292`、`:566-572`、`:839-844` 三处 catch → 发 `TYPE_ERROR`（保留原日志）。
- [ ] host 侧：`jsb_environment.cpp:761-767` 的 `onerror` 分支反序列化记录 → 重建 Error → 交给用户回调。
- [ ] `TransferableJSShadowRealm` 用**同一个 helper** 发 `TYPE_ERROR`（保证与 Worker 形态一致）。
- [ ] **验收**：worker 启动失败 / 回调抛错两条用例，断言主线程 `onerror` 收到的对象 `instanceof Error` 且字段集与 ShadowRealm 一致。

## 5. 定时器异常（AC12/R9）

- [ ] **worker 环境**：定时器回调异常 → 发 `TYPE_ERROR` 给 master（与步骤 4 同一 helper；worker 的定时器由 `jsb_worker.cpp:365` 的 `p_env->update(delta)` 驱动）。
- [ ] **主环境**：不改行为（`jsb_timer_action.cpp:73-75` 已捕获 + 日志）；把 `jsb_environment.cpp:532-533` 的过时 TODO 改写成"worker 环境需转发给 master"。
- [ ] **验收**：worker 内 `setTimeout(() => { throw ... })` → 主线程 `onerror`；主环境同用例只产生日志、不崩、不新增回调。

## 6. 全量门禁

```
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j6
scons platform=windows target=editor use_quickjs_ng=yes dev_build=yes tests=yes -j6
D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe --headless --path ./project --jsb-run-tests
D:\Dev\godot\godot\bin\godot.windows.editor.x86_64.console.exe --audio-driver Dummy --headless --path ./project --quit-after 25000
```

- [ ] v8 + quickjs-ng 各构建一次（改头文件会触发全量重编，别重复编）。
- [ ] doctest 全绿（基线：v8 82 例/1224 断言、qjs 84 例/1236 断言，新增用例后计数应增加）。
- [ ] smoke 打印 `GODOTJS_TEST_PROJECT_COMPLETED`。
- [ ] jsc：本机只跑语法门（`bash .agent_tmp/jscgate.sh <impl/jsc TU>`），运行期靠 CI 的 `Test (host-jsc, macos-latest)`。

## 7. 收尾

- [ ] `.trellis/spec/`：若"跨边界错误约定"（ErrorRecord 字段 / `onerror` 形态 / untransferred 清单）被判定为新约定 → 写入 spec。
- [ ] 本任务 `report.md`：改动文件、AC 逐条证据、未覆盖项。
- [ ] 清理 `./.agent_tmp/` 临时脚本与探针（本次分析用的探针已删除，勿再引入）。
- [ ] 提交（英文 message）；推送需逐次授权。

## 回滚点

- 回滚点 1（步骤 1 结束）：崩溃消失、错误分支可达——独立可交付。
- 回滚点 2（步骤 4 结束）：消息通道新增 `TYPE_ERROR` 发送端不影响既有 `TYPE_MESSAGE`/`TYPE_READY`。
- 若用户改主意只做 ShadowRealm：步骤 1-3 可独立成立，步骤 4-5 单独成任务。
