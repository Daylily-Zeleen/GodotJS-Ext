# implement：全局类元数据走常驻工具进程

阶段顺序即依赖顺序；每阶段末有独立验证点。

## 阶段 1：工具侧 op（`scripts/jsb.tools`）

1. `jsb.editor.tools.cts`：`switch` 加 `case "global-class"`；头注释协议表补一行。
2. 新建 `jsb.global-class.extract.cts`（与 `doc.extract`/`signature.extract` 同型）：
   `extract(path, projectRoot) -> {ok, class?, base_type?, icon_path?, is_tool?, is_abstract?}`，
   按 design 的 AST 规则实现。
3. 验证：`node` 直跑断言脚本（AC1/AC2/AC3 的样例源文件放 `.agent_tmp`），
   含跨行 extends、字符串里假命中、`@bind.icon` 变量实参（应 `{ok:false}` 或空 icon）。

## 阶段 2：客户端线程安全（`src/editor/weaver-editor/jsb_editor_tool.{h,cpp}`）

1. 加 `std::mutex lifecycle_mutex_`；`request()` 里「存活检查→重启→取句柄」入锁；
   `_teardown_process()` 同锁。
2. 头注释的「只有编辑器主线程触达」改为准确描述：任意线程可 `request()`，
   生命周期互斥；`start/stop` 仍主线程。
3. 验证：重编 rc=0；现有 `signatures`/`doc` 两调用点回归（触发一次重生成，日志正常）。

## 阶段 3：runtime 钩子 + 删正则（`src/runtime/weaver/jsb_script_language.{h,cpp}`）

1. `GodotJSScriptLanguage` 加
   `using GlobalClassExtractor = std::function<bool(const String &, Dictionary &)>;`
   `set_global_class_extractor(GlobalClassExtractor)` / 清理。
2. **删正则**：`js_class_name_matcher1_` / `js_class_name_matcher2_` 成员及其构造处的
   `RegEx::create_from_string(...)` 初始化、`_get_global_class_name` 里的两段匹配与
   动态构造的 base matcher 全部删除（约 60 行）。
3. `_get_global_class_name` 新函数体：钩子未注册 → 空 Dictionary + 一条错误日志；
   注册 → 调钩子，false → 空 Dictionary + 错误日志。
4. 验证：`test_jsb_script_language_queries.h` 加用例（未注册→空、注册→用其值、false→空）。

## 阶段 4：editor 注册（`src/editor/weaver-editor/jsb_editor_plugin.cpp`）

1. NOTIFICATION_READY 里 `_ensure_tool_client()` 成功后注册钩子，
   lambda 里 `tool_client_->request("global-class", {path}, resp)`，
   `resp.ok` 才返回 true。
2. plugin 析构/NOTIFICATION_EXIT_TREE 注销（置空钩子），避免悬垂。
3. 验证：编辑器实跑最小工程（AC6），`get_global_class_list` 比对。

## 阶段 5：收尾

1. AC5：`target=template_release` 编译 rc=0（钩子代码在 runtime，必须无编辑器依赖可编）。
2. doctest 全绿 + `Orphan StringName`=0。
3. `.trellis/spec/godotjs-ext/cpp/` 补约定条目（AC8）：runtime↔editor 经钩子解耦、
   扫描线程同步请求工具进程、失败必降级不刷屏。
4. STATUS/TODO 清理：`jsb_script_language.cpp:541/543` 两条 TODO 移除（已被本任务覆盖）。

## 验证命令

- 构建：`scons platform=windows target=editor use_node=yes dev_build=yes debug_symbols=yes tests=yes -j6`
- doctest：`godot --audio-driver Dummy --headless --path ./project --jsb-run-tests`
- 工具单测：`node <断言脚本>`（.agent_tmp）
- 模板：`scons platform=windows target=template_release use_node=yes -j6`
