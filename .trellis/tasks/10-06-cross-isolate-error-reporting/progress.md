2026-10-08 注册表改 CrossWrapper* 裸指针 + purge 去 GetAlignedPointerFromInternalField + remove_cache 悬垂读修正；node/v8/qjs 全绿（node smoke x4，v8/qjs x2），jsc 语法门 rc=0
2026-10-08 修复 try_get_cache 语义（注册表存宿主侧包装器句柄 host_obj_，之前误返回 guest 对象）；add_cache 覆盖旧槽并回收；remove_cache 加属主判定；node 40/40 全绿，v8/qjs 全绿，jsc 语法门全绿；期间 AsyncHooks::FailWithCorruptedAsyncStack 抖动静默（HEAD 亦复现，非本任务引入）
2026-10-08 文档核查：确认 report 已记「guest 死后释放包装器未根治」；但「try_get_cache 复用路径缺针对性测试」未记 → 已补入 report（步骤 16 缺口小节）
2026-10-08 步骤17：新增 try_get_cache 复用断言（node doctest 91/91 SUCCESS），teeth check 通过（禁用复用 -> 断言确实失败）；顺带定性「宿主经 Proxy 读 guest 属性返回 undefined」为既有缺陷（_transfer_string 跨 isolate WriteUtf8），未修、已记录
2026-10-08 更正：AsyncHooks::FailWithCorruptedAsyncStack 非「假警报」——干净环境 3/40=7.5% 复现，栈总是 worker 的 node 事件循环，单场景复现不出（0/36），需整条套件；先前三个对照（stash 全绿 / 禁钩子 40/40 / 钩子 no-op 40/40）全部无效（前两者样本被污染，后两者让套件卡住没跑到崩溃点）。结论=未定性，需两完整构建 x N>=40 对比。
2026-10-08 修复 AsyncHooks 崩溃：node 未捕获钩子只装有转发目标的环境（主环境保留 node 默认行为）；CrossWrapper 记录 host_isolate_ 与 object_hash_（finalizer 不再解引用句柄，修掉 qjs 崩溃死循环）；remove_cache 加 SArray 索引有效性判定。修前 3/40 -> 修后 0/30，三腿 doctest+smoke 全绿。
2026-10-08 §18 修 _transfer_string 尾随 NUL（V8 WriteUtf8 含 NUL vs shim 不含）-> hostRead 断言；teeth check 通过；node/v8/qjs doctest+smoke 全绿。
2026-10-08 §18 更正：改为统一 quickjs/jsc/web 的 String::WriteUtf8 到 V8 语义（含 NUL 的返回值），调用点按 V8 契约使用；撤销调用点补丁。qjs teeth check：旧语义下该用例直接崩溃。
2026-10-09 AsyncHooks 真根因：capture callback 位于 node 自身 async 上下文作用域内，回调里跑 JS 会打乱栈 -> pop_async_context 断言。最终修法=回调只暂存 + 下一帧 update 里 capture/forward，且只对有转发目标的环境装钩子。40/40 全绿（修前 2/40）。已折叠进 9c633b2 转发提交。
