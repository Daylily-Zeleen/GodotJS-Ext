# design：全局类元数据走常驻工具进程

## 数据流

```
EditorFileSystem 扫描线程（后台）
  └ GodotJSScriptLanguage::_get_global_class_name(path)          [runtime]
      ├ extractor_ 未注册 → 返回空 Dictionary（+ 一条错误日志）
      └ 已注册 → EditorToolClient::request("global-class", {path})  [editor, 同步, ≤5s]
                   ├ ok  → 五字段直接返回
                   └ 失败 → 空 Dictionary（+ 一条错误日志）
```

没有第二条实现体：正则（`js_class_name_matcher1_`/`matcher2_`、动态 base matcher 及成员）
在本任务里删除。

## 分层与线程

- **runtime 不认识 editor**：`GodotJSScriptLanguage` 只存一个 `std::function`。
  editor plugin 在 NOTIFICATION_READY 注册、析构注销。首次扫描晚于 plugin ready
  （`jsb_editor_plugin.cpp:265`），注册先于任何扫描线程调用。
- **客户端线程安全**：现状 `request()` 的数据竞争点只在进程生命周期——
  `process_` 指针的读/换（`_teardown_process`/`_spawn`）与 `is_running()` 检查无锁。
  改法：`std::mutex lifecycle_mutex_` 包住「检查存活→重启→取句柄」；id 分配已在
  `mutex_` 内；应答等待本就用 `mutex_`+`cv_`。`write_stdin` 单行写原子（< PIPE_BUF），
  请求体本身一行 NDJSON，无交错风险。
- **失败要出声**：扫描线程逐文件调用，失败返回空——但必须打日志。这不是每文件常态，
  而是环境故障（工具未安装/进程死了/超时）；静默会把"工具没起来"伪装成"所有脚本都没有全局类"。
  客户端自身的崩溃恢复语义（报错+重启+重试一次）保持。

## 工具侧（`jsb.editor.tools.cts`）

新增 `case "global-class"`：

- 入参 `{ path }`（res://），进程持有 `--project` 根，可直接换 OS 路径读文件。
- `ts.createSourceFile(path, text, ScriptTarget.Latest, /*setParentNodes*/ false)`
  ——单文件、无 Program、无类型检查，毫秒级。
- 提取（与现正则契约一致，仅更稳）：
  - 遍历 AST 找 `ClassDeclaration`/`ClassExpression`：
    - `abstract` 修饰符 → `is_abstract`；
    - 装饰器列表匹配 `bind.icon` / `icon`，取第一个字符串字面量实参 → `icon_path`；
    - `@tool` → `is_tool`；
    - `heritageClauses` 的 `extends` 表达式文本 → `base_type`；
  - 导出判定：`.ts` 取 `export default class X ...`（ExportAssignment 指向该类）；
    `.js` 取 `class X extends Y` + 文件尾 `exports.default = X`（现正则两步契约）。
  - 多个类：取 default 导出的那个；没有 default → 空（与现状一致）。
- 应答 `{ok:true, ...}`；文件读不到/解析不出 → `{ok:false}`（C++ 侧走降级）。

## 测试

- 工具侧：`.trellis` 外不引新框架，直接复用 `jsb.tools` 现有自测方式
  （`node scripts/jsb.tools/...` 直跑断言脚本，参照 doc/signature 提取器的既有验证法）。
- C++ 侧：`test_jsb_script_language_queries.h` 同型——
  - 未注册钩子 → 空 Dictionary；
  - 注册后 → 用钩子返回值（伪造 extractor）；
  - 钩子返回 false（解析不出）→ 空 Dictionary。
- 编辑器实跑：最小工程放 AC1/AC2 脚本，`--headless --editor --quit` 后
  读 `ProjectSettings.get_global_class_list` 校验。

## 风险

| 风险 | 缓解 |
|---|---|
| 工具进程每文件一次请求，大工程首扫变慢 | 常驻+内存态，实测毫秒级；若超预期再议（不做缓存是用户裁决） |
| 扫描线程并发多文件同时请求 | 生命周期锁串行化 spawn；请求本身按 id 多路复用，天然并发安全 |
| plugin 就绪前已有扫描调用（理论窗口） | 钩子未注册即走正则，行为同现状 |
| `@bind.icon` 参数非字面量（变量） | 只认字面量，其余走降级——与正则契约等价，不扩大承诺 |
