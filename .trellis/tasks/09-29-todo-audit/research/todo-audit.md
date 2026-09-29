# TODO 全量审计（188 条 / 基线 f6e62c5）

清点命令（可复现）：

```bash
git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc   # 基线 188 行
```

全仓 `git grep -n -I "TODO"` 为 224 行，差额 36 行不在范围内：`third/` 14、`.trellis/` 19、agent 配置 3。

配套调研：**[virtuals-godot-side.md](./virtuals-godot-side.md)** —— 逐个查清 Godot 侧对这些虚函数的调用点、GDScript 与 C# 的实现，用于判定 `jsb_script_language.h` / `jsb_script.h` 那一批 TODO 是否有必要做。

## 1. 分类统计

| 类 | 含义 | 条数 | 占比 |
|---|---|---|---|
| **A 可简单实现 / 待确认后实现** | 单文件/单函数，接口已存在 | 55 | 29% |
| **B 已过时** | TODO 的前提已不成立，应删注释或改写 | 4 | 2% |
| **C 不可实现 / 受上游限制** | 需 Godot/godot-cpp 未暴露的接口，或 JS 引擎无法提供的语义 | 10 | 5% |
| **D 需较大改动** | 跨层设计或架构级 | 85 | 45% |
| **E 非行动项 / 不适用** | 设计备忘、说明，或**经调研判定对 JS 不适用** | 34 | 18% |
| | **合计** | **188** | 100% |

> **处置进度（2026-09-29）**：B 类 4 条已清除（§7.1）；裸 TODO 2 条已删（§7.2）；**#16 的 TODO 未删除**——按要求就地补全实现说明（§7.3）。当前 `TODO` 计数 **182**。

> 用户对判定的纠正已并入：**#16 是 D 类**（检查构建能否导出到指定平台/有无对应动态库）、**#55 不是死代码**（先确认 `_rebind` 是否仍需要）、**#86 不是死代码**（考虑 `IsFunction` 转 `godot::Callable`）；并按 §8 的调研**重判 9 条**（`_find_function` 等）。

## 2. 两个横切事实

**2.1 三分之二来自上游。** 与本地上游 GodotJS 检出比对：122 条文本上游同样存在，仅 66 条为本仓独有。逐条表「上游」列标出。

**2.2 引入了很久。** `git blame` 归因：128 条由 2026-08-05「批量代码格式化」引入，18 条在 2026-09-22（`JSB_TOOLS` 宏重构）新写入，其余为 2026-06/07 初始导入。

## 3. A 类：可简单实现（按「能独立成一个 PR」分组）

### G1 weaver 脚本语言/脚本虚函数（`jsb_script_language.h`、`jsb_script.h`、`jsb_script_language.cpp`）

- #169 `src/runtime/weaver/jsb_script_language.h:213` — `_find_function`（活路径：`connections_dialog.cpp:1016/1024`、`script_text_editor.cpp:428`）。恒返回 -1 会让「连接信号到脚本已有方法」时编辑器**多写一个空函数**（`connections_dialog.cpp:1017-1034`）。可用本文件已有的正则设施定位方法定义行，单文件可做
- #176 `src/runtime/weaver/jsb_script_language.h:236` — `_validate_path`：唯一调用方 `script_create_dialog.cpp:301`（「新建脚本」对话框的路径校验）。GDScript 与 C# 都有实现。是否要做取决于本项目是否使用「新建 TS 脚本」流程
- #155 `src/runtime/weaver/jsb_script.h:215` — `_get_member_line`（`Script` 上，非语言上）：调用链 `ScriptEditor::script_goto_method`（`script_editor_plugin.cpp:3899`）← 信号连接后跳转（`connections_dialog.cpp:1294`）、动画方法轨双击（`animation_track_editor.cpp:3475`）。恒 -1 使「跳转到方法定义」静默失效；GDScript 查 `member_lines`，C# 也是 `TODO omnisharp` + -1。需先记录成员声明行号（可复用 `_get_global_class_name` 的正则设施）
- #163 `src/runtime/weaver/jsb_script_language.cpp:449` — `String icon_path; // TODO` 未赋值，却在 :488 写入结果字典：从 JS 类注释/装饰器解析 icon 后填入即可
- #164 `src/runtime/weaver/jsb_script_language.cpp:451` — `bool is_abstract{ false }; // TODO` 与 `ScriptClassFlags::Abstract` 已有标志位呼应，解析期置位即可
- #162 `src/runtime/weaver/jsb_script_language.cpp:402` — `_validate` 的 `//TODO parse error info`：可从编译异常提取行列并填 `errors` 数组，单函数可做
- #160 `src/runtime/weaver/jsb_script_language.cpp:193` — 脚本列表管理方式（按 `script.id` 引用）：`script_list_` 集中在本文件，可局部改造
- #152 `src/runtime/weaver/jsb_script.cpp:1039` — `_update_exports` 中逐个属性取默认值的行为确认：「是否预期」只需确认设计意图，确认后删注释或改为批量接口
- #140 `src/runtime/weaver/jsb_script.cpp:196` — 同一函数内：可用 `script_class_info_.base_script_module_id` 沿基类链比较 `p_script` 的模块 id，改动局限 `GodotJSScript::_inherits_script`
- #141 `src/runtime/weaver/jsb_script.cpp:246` — `_reload` 开头 `if (!loaded_) return OK;` 使未加载脚本无法重载。去掉该早退（或改为先 `load_module_immediately`）即可，属单函数语义修正

### G2 weaver 脚本实例与继承（`jsb_script.cpp`、`jsb_script_instance.cpp`）

- #147 `src/runtime/weaver/jsb_script.cpp:847` — `_get_rpc_config` 是否并入父类配置：只需按基类链合并 `rpc_config`，函数内可完成
- #156 `src/runtime/weaver/jsb_script_instance.cpp:405` — `to_string` 空实现（return {}）：注释里已给出目标格式，可直接实现
- #157 `src/runtime/weaver/jsb_script_instance.cpp:553` — 静态方法包装成 `Callable`：`jsb_script_instance.cpp` 内已有非静态分支，静态分支补齐即可
- #153 `src/runtime/weaver/jsb_script.h:101` — `placeholders` 由 `LocalVector` 改 `HashMap` 加快查找：单文件数据结构替换，调用点集中在 `jsb_script.cpp`/`jsb_script_language.cpp`
- #145 `src/runtime/weaver/jsb_script.cpp:280` — `_get_doc_class_name` 的 `//TODO not verified`：逻辑已完整，只差验证；补一条测试或实测后即可删注释
- #167 `src/runtime/weaver/jsb_script_language.cpp:833` — 内置（built-in）脚本重载靠重新加载整个场景：已注明低效，属优化项；可先量化再决定是否做

### G3 编辑器侧（导出/安装/REPL/进程/预设）

- #10 `src/editor/weaver-editor/jsb_editor_plugin.cpp:716` — INSTALL 阶段跳过 d.ts（改到 GENERATE 阶段）：`verify_file` 内已被注释掉的三行即可启用，本文件可做
- #15 `src/editor/weaver-editor/jsb_export_plugin.cpp:297` — 导出时处理 `.js` 文件的模块依赖：注释已给出 `export_compiled_script(p_path)` 的接法，单一分支补齐
- #12 `src/editor/weaver-editor/jsb_editor_plugin.h:161` — `Vector<String>` 改 `PackedStringArray`：签名与 8 处调用点同文件，机械替换
- #13 `src/editor/weaver-editor/jsb_editor_plugin.h:162` — 同上（`generate_resource_types`）
- #17 `src/editor/weaver-editor/jsb_repl.cpp:58` — REPL 列出所有 realm 实例并交互：需把 realm 列表暴露给 REPL（`GodotJSScriptLanguage` 已持有 shadow env），属编辑器局部功能
- #26 `src/internal/jsb_process.cpp:310` — `//TODO not tested on linux`（`ProcessImpl` 的 UNIX 分支）：CI 已有 linux 腿，补一次实测即可
- #11 `src/editor/weaver-editor/jsb_editor_plugin.cpp:1291` — `tsc` 子进程无控制台输出、需实现管道：`jsb_process.cpp` 的 `create` 已有参数位，属局部实现
- #24 `src/internal/jsb_preset_source.h:130` — preset 数据生成改为直接产 `PackedByteArray` 以避免 memcpy：需改生成端（`misc/`）+ 本头文件解压路径，范围可控

### G4 bridge 局部修改

- #31 `src/runtime/bridge/jsb_async_module_loader.cpp:86` — `func->Call(...).ToLocalChecked()` 缺 TryCatch：加 `impl::TryCatch` 并错误上报，单函数可做
- #45 `src/runtime/bridge/jsb_class_info.h:332` — `ScriptClassInfo::is_valid()` 恒 true 与 TODO「class object 是否存活」矛盾：可改为检查 `clazz` Global 是否为空，单函数可做
- #46 `src/runtime/bridge/jsb_environment.cpp:55` — `//TODO remove this` 指 `#include "../weaver/jsb_script.h"`，但同文件 :767/:773 正在使用 `Ref<GodotJSScript>`，该 include 不能删——注释已过时，应删除
- #62 `src/runtime/bridge/jsb_environment.h:179` — `//TODO remove this later` 指 `friend struct ScriptClassInfo`：收敛访问后可移除，局部改动
- #50 `src/runtime/bridge/jsb_environment.cpp:382` — `start_debugger` 按 Editor/Game 不同阶段调用：只需调整 init 内调用点，单函数范围
- #66 `src/runtime/bridge/jsb_essentials.cpp:180` — 去掉 `(double)(int64_t)` 强制转换需 V8 ≥ 12.6.221；当前 pin 为 `12.4.254.21`（`SConstruct:100`），故前置未满足——改动本身一行
- #69 `src/runtime/bridge/jsb_internal_module_loader.cpp:32` — `InternalModuleLoader::load` 的 `//TODO evaluate source from presets with file_name_`：用已持有文件名构造 source reader 即可，单函数可做
- #79 `src/runtime/bridge/jsb_reflect_binding_util.h:398` — `ReflectBuiltinMethodPointer` 构造路径把 ClassID 从 `info.Data()` 改为临时不用：仅需恢复一处取用，性能取向的小改
- #81 `src/runtime/bridge/jsb_shadow_realm.cpp:530` — cross-wrapper 函数「Freeze 或 proxy 防止被篡改」：单处 `v8::Object::Freeze` 即可
- #82 `src/runtime/bridge/jsb_shadow_realm.cpp:549` — `args` 由 `LocalVector` 改 `jsb_stackalloc` 栈分配：局部性能改动，仓库已有 `jsb_stackalloc` 设施
- #84 `src/runtime/bridge/jsb_transpiler.h:55` — `jsb_transpiler.h` 的 `//TODO test`：仅缺验证
- #87 `src/runtime/bridge/jsb_type_convert.cpp:610` — Promise 内嵌字段处理「用更优雅方式」：注释已有 HACK 说明，可用 `v8::Promise` 的类型查询替代手写偏移
- #88 `src/runtime/bridge/jsb_type_convert.cpp:655` — 整数类型判断的更好方式：`IsInt32`/`IsUint32` 组合即可，单函数可做
- #55 `src/runtime/bridge/jsb_environment.cpp:1282` — `crossbind` 中「对象已绑定却重新 crossbind」的分支：注释掉的是 `_rebind(isolate, context, p_this, p_class_id)`。**先确认 `_rebind` 是否仍需要**（该函数在 `jsb_environment.cpp:1368` 仍存在），再决定启用该分支还是删除这段代码——不是可以径直删掉的死代码

### G5 impl 各腿局部修改

- #93 `src/runtime/impl/jsc/jsb_jsc_data.cpp:49` — `Data::GetIdentityHash` 的 jsc 哈希改进：可用 `JSValueGetIdentityHash`，单函数可做
- #94 `src/runtime/impl/jsc/jsb_jsc_handle.h:156` — jsc `Global` 改用 `JSWeakRef`（头文件已在 `jsb_jsc_pch.h` 引入 `JSWeakPrivate.h`）：实现集中在 `jsb_jsc_handle.h`
- #96 `src/runtime/impl/jsc/jsb_jsc_isolate.cpp:472` — `_NotAllowedCallAsFunction` 中 `stack_dup` 是拷贝还是窃取：确认后改一行即可（`constructor call` 错误路径）
- #101 `src/runtime/impl/jsc/jsb_jsc_primitive.cpp:57` — jsc `Value::ToDetailString` 无等价实现而回退 `ToString`：可在回退前附加类型前缀，单函数可做
- #102 `src/runtime/impl/jsc/jsb_jsc_primitive.cpp:100` — `External::Value` 直接 `JSObjectGetPrivate` 而非 `JSValueToObject`：已 `jsb_check(_IsExternal)`，注释为待确认，可改写为结论
- #108 `src/runtime/impl/quickjs/jsb_quickjs_ext.h:118` — quickjs `jsb_quickjs_ext.h` 的 `//TODO unsafe eq check`：指针比较前可先比 tag，单函数可做
- #109 `src/runtime/impl/quickjs/jsb_quickjs_isolate.cpp:67` — JSObject 是否会被 realloc 的假设：注释已给出「若会 realloc 则需间接映射」的方案，属待实测确认
- #110 `src/runtime/impl/quickjs/jsb_quickjs_isolate.cpp:92` — 同上（第二处 `js_realloc`）
- #113 `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:332` — 属性标志是否保留 `JS_PROP_HAS_CONFIGURABLE`：与 129（web 同问题）一致，两处各改一行
- #115 `src/runtime/impl/quickjs/jsb_quickjs_primitive.cpp:223` — `Integer::NewFromUnsigned` 用 `JS_NewUint32` 而底层 tag 只有 INT/FLOAT64：可改用 `JS_NewInt32`/`JS_NewFloat64`，单函数可做
- #116 `src/runtime/impl/quickjs/jsb_quickjs_typedef.h:64` — `jsb_quickjs_typedef.h` 的「不知道能否正常工作」：属待验证
- #122 `src/runtime/impl/web/bridge/src/monolith.ts:1051` — web `GetGlobalObject` 临时返回浏览器 global：可指向桥接环境对象，单函数可做（需先确认沙箱语义）
- #124 `src/runtime/impl/web/bridge/src/monolith.ts:1526` — `i64` getter 是否受支持：`BigInt64Array` 可用性确认，属待验证
- #125 `src/runtime/impl/web/bridge/src/monolith.ts:1535` — `u64` getter 同上
- #127 `src/runtime/impl/web/jsb_web_helper.h:43` — web `SetDeleter` 未测试：补测即可
- #128 `src/runtime/impl/web/jsb_web_helper.h:67` — `jsb_web_helper.h` 的「copy from HEAP?」：内存来源确认，局部改动
- #129 `src/runtime/impl/web/jsb_web_object.cpp:199` — web 属性标志 `CONFIGURABLE` 是否移除：与 113 对称，一行改动
- #133 `src/runtime/tests/test_jsb_any_runtime.h:168` — node 构建创建 isolate/context 的路径与其它腿不同：属测试基建分支，`test_jsb_any_runtime.h` 内可对齐

## 4. B 类：已过时

- #46 `src/runtime/bridge/jsb_environment.cpp:55` — 原文 `//TODO remove this`
  - 处置：`//TODO remove this` 指 `#include "../weaver/jsb_script.h"`，但同文件 :767/:773 正在使用 `Ref<GodotJSScript>`，该 include 不能删——注释已过时，应删除
- #73 `src/runtime/bridge/jsb_module_resolver.cpp:97` — 原文 `//TODO set `require.cache``
  - 处置：`//TODO set require.cache` 已实现：`JavaScriptModuleCache::insert`（`jsb_module.cpp:86`）把模块写入 `cache_object_`，`_new_require_func`（`jsb_environment.cpp:1431`）把该对象挂到每次 `require` 上；注释已过时
- #100 `src/runtime/impl/jsc/jsb_jsc_pch.h:37` — 原文 `//TODO WARNING: ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY.`
  - 处置：`jsb_jsc_pch.h` 的「ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY」已过时：CI 已有 jsc 腿（`.github/workflows/ci.yml` 的 `engine: jsc`，覆盖 macos 与 ios），jsc.impl 已可构建
- #158 `src/runtime/weaver/jsb_script_instance.cpp:753` — 原文 `//TODO find the method named `_notification`, cal it with `p_notification` as `argv``
  - 处置：TODO 写「find the method named `_notification`, cal it with p_notification as argv」——紧随其后的 3 行**已经这么做了**（构造 `Variant value`、`argv`、`callp(_notification)`），注释已过时

## 5. C 类：不可实现 / 受上游限制

- #9 `src/compat/editor_settings.cpp:133` — `EditorSettings::set_restart_if_changed`：godot-cpp 中确认不存在（`gen/include/.../editor_settings.hpp` 无匹配），GDExtension 无法调用
- #18 `src/editor/weaver-editor/jsb_repl.cpp:159` — `set_disable_visibility_clip`：godot-cpp 中确认无（`gen/include` 全树无匹配）
- #27 `src/internal/jsb_variant_util.h:75` — `Dictionary::id()`：godot-cpp 只有 `int64_t hash()`，无 `id()`；GDExtension 无法取得容器身份（用 hash 是既有绕行）
- #28 `src/internal/jsb_variant_util.h:79` — `Array::id()` 同上（godot-cpp 只有 `hash()`）
- #83 `src/runtime/bridge/jsb_thread_safe_for_nodes_scope.h:31` — `jsb_thread_safe_for_nodes_scope.h` 的「godot 没有暴露相关接口」：godot-cpp 全树无 `set_thread_safe`/ThreadSafe 匹配，GDExtension 侧无法实现
- #106 `src/runtime/impl/quickjs/jsb_quickjs_data.cpp:113` — QuickJS 数值标签只有 `JS_TAG_INT`，无法区分 int32/uint32（引擎语义限制），`IsInt32`/`IsUint32` 只能同解
- #107 `src/runtime/impl/quickjs/jsb_quickjs_data.cpp:120` — 同上
- #132 `src/runtime/tests/jsb_test_helpers.h:75` — 测试里恢复 cwd 的 `OS::set_cwd`：godot-cpp 无 `set_cwd`/`get_cwd`（`gen/include/.../os.hpp` 无匹配），属 GDExtension 限制
- #151 `src/runtime/weaver/jsb_script.cpp:936` — 注释掉的 `EditorHelp::get_doc_data()` / `DocData` 代码：godot-cpp 未暴露（`get_doc_data` 在 `third/godot-cpp/gen/include` 全树无匹配），GDExtension 侧无法实现
- #168 `src/runtime/weaver/jsb_script_language.cpp:892` — `PlaceholderScriptInstance::property_set_fallback`：godot-cpp 全树无匹配（`property_set_fallback` 不存在），GDExtension 无法调用

## 6. E 类：非行动项 / 经调研判定不适用

- #1 `.clang-format:159` — `.clang-format` 中从上游 Godot 继承的注释（说明为何 `Minimum: 0`）；本仓格式化规范沿用同一工具链，保留即可
- #2 `scripts/jsb.runtime/src/godot.annotations.ts:214` — Godot 属性 hint API 的设计备忘（建议提供便捷构造方法）：属 API 设计方向，非缺陷
- #20 `src/internal/jsb_console_output.cpp:40` — `Vector<IConsoleOutput*>` 是否换 `LocalVector`：疑问式备忘，量级极小
- #32 `src/runtime/bridge/jsb_async_module_manager.cpp:147` — `//TODO module tree needed?` 是疑问式备忘，紧跟 `jsb_nop()`；在语义确定前不构成可执行项
- #33 `src/runtime/bridge/jsb_async_module_manager.cpp:148` — `//TODO GodotJS script needed?` 同上，疑问式备忘
- #38 `src/runtime/bridge/jsb_class_info.cpp:366` — `//TODO collect methods/signals/properties` 紧接其后的代码正在收集 prototype 成员，注释已被实现覆盖，建议删除或改写
- #39 `src/runtime/bridge/jsb_class_info.cpp:443` — `//TODO property categories`：紧随其后的注释已说明「解析期只登记名字、签名懒加载」，属取舍记录，建议保留
- #42 `src/runtime/bridge/jsb_class_info.h:103` — `//TODO RESERVED FOR FUTURE USE` 标注的预留字段，属说明
- #43 `src/runtime/bridge/jsb_class_info.h:234` — `struct ScriptMethodInfo // TODO: 为什么不复用 MethodInfo` 是疑问式备忘（Godot 的 MethodInfo 与脚本方法元信息字段不同），建议改写为结论而非 TODO
- #51 `src/runtime/bridge/jsb_environment.cpp:392` — 析构函数上的「not always safe」告警性说明，配合下方 Warning 日志，属防御性注释
- #58 `src/runtime/bridge/jsb_environment.cpp:1448` — `debugger_ready_future_.wait_for(...) // TODO` 裸 TODO，无说明内容，建议删除或补上意图 —— **本轮已按用户指示删除**
- #63 `src/runtime/bridge/jsb_environment.h:224` — 说明「导出的 default class 直接或间接继承原生 godot class」的条件，属设计说明
- #89 `src/runtime/bridge/jsb_type_convert.cpp:701` — 「JS 原始数组宽松转 Godot 数组是不是坏主意」——设计疑问，当前行为由 `JSB_IMPLICIT_PACKED_ARRAY_CONVERSION` 开关控制，建议改写为结论注释
- #90 `src/runtime/bridge/jsb_type_convert.h:87` — `jsb_type_convert.h` 说明 V8 内嵌字段数量随编译配置变化，属安全性说明（重复出现于两处重载）
- #91 `src/runtime/bridge/jsb_type_convert.h:100` — 同上
- #92 `src/runtime/impl/jsc/jsb_jsc_class.h:44` — `constructor_` 是否与 `prototype.constructor` 重复：jsc/quickjs/web 三处同一疑问，属待确认的备忘
- #99 `src/runtime/impl/jsc/jsb_jsc_object.cpp:226` — `JSValueUnprotect` 后的 `//TODO correct?`：需实测确认保护计数配对，属待确认
- #105 `src/runtime/impl/quickjs/jsb_quickjs_class.h:44` — quickjs `constructor_` 重复疑问：与 92/126 同一备忘
- #112 `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:135` — quickjs `HasOwnProperty` 的 `//TODO unsure`：待确认，无具体动作
- #114 `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:355` — 说明 quickjs 未实现 `key_conversion`，并与 jsc/web 保持一致的取舍（指向 monolith.ts）；建议保留为跨实现一致性说明
- #119 `src/runtime/impl/web/bridge/src/monolith.ts:447` — 裸 `//TODO`（`_throw_trivial` 之前），无说明内容，建议删除 —— **本轮已按用户指示删除**
- #120 `src/runtime/impl/web/bridge/src/monolith.ts:449` — `_throw_trivial`「not sure, temporarily throw all trivial errors」：临时策略说明，建议改写为结论 —— **本轮已按用户指示删除**
- #126 `src/runtime/impl/web/jsb_web_class.h:42` — web `constructor_` 重复疑问：与 92/105 同一备忘
- #134 `src/runtime/tests/test_jsb_shadow_realm.h:34` — 测试文件引用 `jsb_shadow_realm.cpp` 顶部 TODO 的测试背景说明，属文档性注释
- #135 `src/runtime/weaver/jsb_resource_loader.cpp:66` — 整段是 `// { … }` 注释掉的旧热重载方案（第 65–91 行），不参与编译，TODO 已无跟踪对象；建议整块删除
- #136 `src/runtime/weaver/jsb_resource_loader.cpp:71` — 同上注释块内的 TODO（重复出现），随整块删除
- #137 `src/runtime/weaver/jsb_resource_loader.cpp:79` — 同上注释块内的 TODO（重复出现），随整块删除
- #139 `src/runtime/weaver/jsb_script.cpp:195` — `_inherits_script` 的已知取舍说明（指出调用点仅 `Array::assign`）；当前实现返回 false 是可接受的现状，注释应保留为取舍记录
- #161 `src/runtime/weaver/jsb_script_language.cpp:392` — `_validate` 里用于说明期望字典键（functions/errors/warnings/safe_lines）的 TODO，属契约文档，建议保留（可改写为 TODO 以外的措辞）
- #166 `src/runtime/weaver/jsb_script_language.cpp:654` — 一行明确标注 NOT IMPLEMENTED 的 Verbose 日志，用于运行时可见性；注释无额外动作，建议保留（或改写为「已知不支持」措辞）
- #170 `src/runtime/weaver/jsb_script_language.h:219` — `_auto_indent_code` 在 extension 路径下**不会被调用**：`ScriptLanguageExtension::auto_indent_code` 是 `static`（`script_language_extension.h:573`），`EditorAdapter::format_code` 直接值调用它，不经过虚分派。「自动缩进」真正走 `EditorLanguage::format_code`（`script_text_editor.cpp:1789/1801`），GDScript 也走自己的 `EditorLanguage` 实现。→ 应删注释或写明为何不实现，**不是填实这个函数**
- #171 `src/runtime/weaver/jsb_script_language.h:220` — `_add_named_global_constant`：唯一调用方是 autoload（`main.cpp` 游戏启动、`editor_autoload_settings.cpp` 编辑器）。**对 JS 不适用**：TS/JS 没有裸标识符解析，注入全局名对 JS 无意义；本仓也无任何 autoload 集成（`git grep autoload src` 为空）。上游 C# **不覆写**此函数。→ 注释应写「不适用」而非「待实现」
- #172 `src/runtime/weaver/jsb_script_language.h:221` — `_remove_named_global_constant`：同上，唯一调用方是 autoload 删除路径（`editor_autoload_settings.cpp:567`）。**不适用**，C# 亦不覆写
- #177 `src/runtime/weaver/jsb_script_language.h:265` — `_add_global_constant`：唯一调用方是 autoload（`main.cpp:4490/4538` 游戏启动两遍）。GDScript 灌 `globals` 数组**给解析器做常量折叠**；JS 无此阶段。**对 JS 不适用**，上游 C# 为空实现（`/* TODO */ ... {}`）

## 7. 已清除 / 已补全记录（2026-09-29）

### 7.1 B 类（4 条，已在工作树中实现）

| # | 文件 | 判定依据 | 处置 |
|---|---|---|---|
| 73 | `src/runtime/bridge/jsb_module_resolver.cpp` | `require.cache` **已实现**：`jsb_module.cpp:86` 写入 `cache_object_`，`jsb_environment.cpp:1431` 挂到每次 `require` | 删 2 行注释 |
| 158 | `src/runtime/weaver/jsb_script_instance.cpp` | TODO 下面 3 行**就是它描述的动作**（构造 `argv` + `callp(_notification)`） | 删该行；保留其下「call it at all type levels?」（D 类 #159） |
| 46 | `src/runtime/bridge/jsb_environment.cpp` | 指向的 `#include "../weaver/jsb_script.h"` 在同文件 `:767/:773` 正被使用，**不能删** | 删注释，include 按字母序前移 |
| 100 | `src/runtime/impl/jsc/jsb_jsc_pch.h` | 「NOT SUPPORTED TO BUILD」**已过时**：CI 有 `engine: jsc` 腿，`misc/release/package.py:57` 的 `ENGINE_ORDER` 也含 jsc | 改写为垫片用途说明 |

### 7.2 删除的无内容裸 TODO（2 条）

`#58` `jsb_environment.cpp` 行尾 `// TODO`；`#119` `monolith.ts` 中 `_throw_trivial` 前的 `//TODO`。

### 7.3 #16：TODO **未删除**，就地补全实现说明

`src/editor/weaver-editor/jsb_export_plugin.cpp` 的 `_supports_platform`，写入：平台名取
`get_os_name()` 小写（godot-cpp `editor_export_platform.hpp:68`）、映射到
`res://addons/godotjs-ext.daylily-zeleen/bin/<platform>/` 下实际产物、缺库返回 false 让编辑器禁用该平台；
并指向 `godotjs-ext.gdextension` 的 `[libraries]` 键与 `misc/release/package.py:leg_library_key()`。

**保留不动**（用户明确说明不是死代码）：**#55**（先确认 `_rebind` 是否仍需要）、
**#86**（考虑 `IsFunction` 分支转 `godot::Callable`）。

验证（重编后实测，dll md5 `ec757e9672fa838abd551f1574416e3b`，两处部署位一致）：

```
--headless --path ./project                 → rc=0  COMPLETED=1  FAILED=0  Orphan StringName=0
--headless --path ./project --jsb-run-tests → 73/73 cases, 1094/1094 assertions, rc=0
git grep -n -I "TODO" -- src scripts .clang-format SConstruct misc | wc -l  → 182
```


## 8. 全量逐条表（188/188）

基线为 `f6e62c5`；已清除/已改写的条目仍按基线列出以便回溯。编号与 `git grep` 输出顺序一致。
「上游」列 = 同文本是否也存在于上游 GodotJS 检出；「引入」列 = `git blame` 的提交年月。

### B1 · src/runtime/weaver/

| # | 文件:行 | 原文 | 类 | 上游 | 引入 | 理由 |
|---|---------|------|----|------|------|------|
| 135 | `src/runtime/weaver/jsb_resource_loader.cpp:66` | `//     //TODO a dirty but approaching solution for hot-reloading` | E | ✓ | 2026-08 | 整段是 `// { … }` 注释掉的旧热重载方案（第 65–91 行），不参与编译，TODO 已无跟踪对象；建议整块删除 |
| 136 | `src/runtime/weaver/jsb_resource_loader.cpp:71` | `//         //TODO need to handle duplicate scripts if GodotJSScript is implemented as thread-wide (not implemented yet)` | E | ✓ | 2026-08 | 同上注释块内的 TODO（重复出现），随整块删除 |
| 137 | `src/runtime/weaver/jsb_resource_loader.cpp:79` | `//             //TODO temporarily ignore it, we are trying to implement scripts in worker threads which may be better not to reuse an existing script reference` | E | ✓ | 2026-08 | 同上注释块内的 TODO（重复出现），随整块删除 |
| 138 | `src/runtime/weaver/jsb_resource_loader.cpp:148` | `//TODO` | D | ✓ | 2026-08 | `ResourceFormatLoaderGodotJSScript::_get_dependencies` 返回空。运行时侧已有 `Environment::get_module_direct_dependencies`，但要接上需要 editor→runtime 的取环境路径、以及「内建模块是否计入依赖」的语义定义，不是单文件改动 |
| 139 | `src/runtime/weaver/jsb_script.cpp:195` | `//TODO `inherits_script` seems to be called only by Array::assign, it's enough for now without an implementation.` | E | ✓ | 2026-09 | `_inherits_script` 的已知取舍说明（指出调用点仅 `Array::assign`）；当前实现返回 false 是可接受的现状，注释应保留为取舍记录 |
| 140 | `src/runtime/weaver/jsb_script.cpp:196` | `//TODO iterate the prototype chain, check if the current script inherits from `p_script`` | A | ✓ | 2026-09 | 同一函数内：可用 `script_class_info_.base_script_module_id` 沿基类链比较 `p_script` 的模块 id，改动局限 `GodotJSScript::_inherits_script` |
| 141 | `src/runtime/weaver/jsb_script.cpp:246` | `if (!loaded_) return OK; // TODO: 这里堵死了怎么 reload ?` | A | — | 2026-08 | `_reload` 开头 `if (!loaded_) return OK;` 使未加载脚本无法重载。去掉该早退（或改为先 `load_module_immediately`）即可，属单函数语义修正 |
| 142 | `src/runtime/weaver/jsb_script.cpp:257` | `//TODO discard the object and crossbind again, but for now we just reload it normally` | D | ✓ | 2026-08 | `p_keep_state==false` 时「丢弃对象并重新 crossbind」未实现，涉及脚本实例生命周期与已有原生对象，属热重载架构面 |
| 143 | `src/runtime/weaver/jsb_script.cpp:264` | `//TODO different env has different module state, we need to refresh the state in all envs when marking a module as dirty somewhere` | D | ✓ | 2026-08 | 跨多个 env 的模块状态刷新（worker/shadow realm 各自持有 module 状态），需要统一的 dirty 广播机制 |
| 144 | `src/runtime/weaver/jsb_script.cpp:267` | `//TODO `Callable` objects bound with this script should be invalidated somehow?` | D | ✓ | 2026-08 | `Callable` 失效策略未定：绑定到该脚本的 Callable 在重载后指向旧原型，需要设计失效/重绑定通道 |
| 145 | `src/runtime/weaver/jsb_script.cpp:280` | `//TODO not verified` | A | ✓ | 2026-08 | `_get_doc_class_name` 的 `//TODO not verified`：逻辑已完整，只差验证；补一条测试或实测后即可删注释 |
| 146 | `src/runtime/weaver/jsb_script.cpp:403` | `// TODO: 当前 ScriptInfo 中似乎不包含静态函数信息` | D | — | 2026-09 | `_has_static_method` 需要 `ScriptClassInfo` 侧补静态方法登记（当前 `ScriptMethodInfo`/解析流程均未区分静态），属数据结构扩展 |
| 147 | `src/runtime/weaver/jsb_script.cpp:847` | `return script_class_info_.rpc_config; // TODO: 是否需要包含父类？` | A | — | 2026-09 | `_get_rpc_config` 是否并入父类配置：只需按基类链合并 `rpc_config`，函数内可完成 |
| 148 | `src/runtime/weaver/jsb_script.cpp:898` | `//TODO a dirty but approaching solution for hot-reloading` | D | ✓ | 2026-08 | 重载后遍历 `instances_` 重绑（自认 dirty、依赖 `script_language` 互斥锁），属热重载架构面 |
| 149 | `src/runtime/weaver/jsb_script.cpp:899` | `//TODO will crash if reloading script instances in worker threads` | D | ✓ | 2026-08 | 该 dirty 方案在 worker 线程重载脚本实例会崩，需线程模型级改造 |
| 150 | `src/runtime/weaver/jsb_script.cpp:920` | `//TODO do not rely on ResourceLoader` | D | ✓ | 2026-08 | 「不依赖 ResourceLoader」：当前经 `ResourceLoader::load` 反查基类脚本资源，替换需要脚本身份解析的新通道 |
| 151 | `src/runtime/weaver/jsb_script.cpp:936` | `// TODO: 可以考虑调用 EditorFileSystem::update_file() 进行触发，是有必要吗？` | C | — | 2026-08 | 注释掉的 `EditorHelp::get_doc_data()` / `DocData` 代码：godot-cpp 未暴露（`get_doc_data` 在 `third/godot-cpp/gen/include` 全树无匹配），GDExtension 侧无法实现 |
| 152 | `src/runtime/weaver/jsb_script.cpp:1039` | `//TODO maybe this behaviour is not expected` | A | ✓ | 2026-08 | `_update_exports` 中逐个属性取默认值的行为确认：「是否预期」只需确认设计意图，确认后删注释或改为批量接口 |
| 153 | `src/runtime/weaver/jsb_script.h:101` | `LocalVector<PlaceholderScriptInstance *, int32_t> placeholders; // TODO: 是否要改成 HashMap 加快查找？` | A | — | 2026-08 | `placeholders` 由 `LocalVector` 改 `HashMap` 加快查找：单文件数据结构替换，调用点集中在 `jsb_script.cpp`/`jsb_script_language.cpp` |
| 154 | `src/runtime/weaver/jsb_script.h:177` | `// TODO: In the next compat breakage rename to `*_script_*` to disambiguate from `Object::has_method()`.` | D | ✓ | 2026-08 | `_has_method` 等重命名为 `*_script_*`：属对外 API 的 compat break，需与文档/上游同步节奏 |
| 155 | `src/runtime/weaver/jsb_script.h:215` | `virtual int32_t _get_member_line(const StringName &p_member) const override { return -1; } // TODO` | A | — | 2026-08 | `_get_member_line`（`Script` 上，非语言上）：调用链 `ScriptEditor::script_goto_method`（`script_editor_plugin.cpp:3899`）← 信号连接后跳转（`connections_dialog.cpp:1294`）、动画方法轨双击（`animation_track_editor.cpp:3475`）。恒 -1 使「跳转到方法定义」静默失效；GDScript 查 `member_lines`，C# 也是 `TODO omnisharp` + -1。需先记录成员声明行号（可复用 `_get_global_class_name` 的正则设施） |
| 156 | `src/runtime/weaver/jsb_script_instance.cpp:405` | `// TODO:` | A | ✓ | 2026-08 | `to_string` 空实现（return {}）：注释里已给出目标格式，可直接实现 |
| 157 | `src/runtime/weaver/jsb_script_instance.cpp:553` | `// TODO: Warp static method to Callable` | A | — | 2026-08 | 静态方法包装成 `Callable`：`jsb_script_instance.cpp` 内已有非静态分支，静态分支补齐即可 |
| 158 | `src/runtime/weaver/jsb_script_instance.cpp:753` | `//TODO find the method named `_notification`, cal it with `p_notification` as `argv`` | B | ✓ | 2026-08 | TODO 写「find the method named `_notification`, cal it with p_notification as argv」——紧随其后的 3 行**已经这么做了**（构造 `Variant value`、`argv`、`callp(_notification)`），注释已过时 |
| 159 | `src/runtime/weaver/jsb_script_instance.cpp:754` | `//TODO call it at all type levels? @seealso `GDScriptInstance::notification`` | D | ✓ | 2026-08 | `_notification` 是否在全部继承层级调用（对齐 `GDScriptInstance::notification`）：涉及脚本继承链的通知分发语义 |
| 160 | `src/runtime/weaver/jsb_script_language.cpp:193` | `// TODO: manage script list in a safer way (access and ref with script.id)` | A | — | 2026-08 | 脚本列表管理方式（按 `script.id` 引用）：`script_list_` 集中在本文件，可局部改造 |
| 161 | `src/runtime/weaver/jsb_script_language.cpp:392` | `// TODO` | E | ✓ | 2026-08 | `_validate` 里用于说明期望字典键（functions/errors/warnings/safe_lines）的 TODO，属契约文档，建议保留（可改写为 TODO 以外的措辞） |
| 162 | `src/runtime/weaver/jsb_script_language.cpp:402` | `//TODO parse error info` | A | ✓ | 2026-08 | `_validate` 的 `//TODO parse error info`：可从编译异常提取行列并填 `errors` 数组，单函数可做 |
| 163 | `src/runtime/weaver/jsb_script_language.cpp:449` | `String icon_path; // TODO` | A | — | 2026-08 | `String icon_path; // TODO` 未赋值，却在 :488 写入结果字典：从 JS 类注释/装饰器解析 icon 后填入即可 |
| 164 | `src/runtime/weaver/jsb_script_language.cpp:451` | `bool is_abstract{ false }; // TODO` | A | — | 2026-08 | `bool is_abstract{ false }; // TODO` 与 `ScriptClassFlags::Abstract` 已有标志位呼应，解析期置位即可 |
| 165 | `src/runtime/weaver/jsb_script_language.cpp:531` | `// TODO: Implement global mechanism.` | D | — | 2026-09 | `reload_scripts_internal` 里 `#if JSB_TOOLS` 下的空块「Implement global mechanism」：指全局（非节点内）脚本重载机制，属架构面 |
| 166 | `src/runtime/weaver/jsb_script_language.cpp:654` | `JSB_LOG(Verbose, "TODO [GodotJSScriptLanguage::_profiling_set_save_native_calls] NOT IMPLEMENTED");` | E | — | 2026-09 | 一行明确标注 NOT IMPLEMENTED 的 Verbose 日志，用于运行时可见性；注释无额外动作，建议保留（或改写为「已知不支持」措辞） |
| 167 | `src/runtime/weaver/jsb_script_language.cpp:833` | `// TODO: It would be nice to do it more efficiently than loading the whole scene again.` | A | — | 2026-07 | 内置（built-in）脚本重载靠重新加载整个场景：已注明低效，属优化项；可先量化再决定是否做 |
| 168 | `src/runtime/weaver/jsb_script_language.cpp:892` | `// placeholder->property_set_fallback(G.first, G.second); // TODO: Godot 未暴露接口` | C | — | 2026-07 | `PlaceholderScriptInstance::property_set_fallback`：godot-cpp 全树无匹配（`property_set_fallback` 不存在），GDExtension 无法调用 |
| 169 | `src/runtime/weaver/jsb_script_language.h:213` | `virtual int32_t _find_function(const String &p_function, const String &p_code) const override { return -1; } // TODO` | A | — | 2026-08 | `_find_function`（活路径：`connections_dialog.cpp:1016/1024`、`script_text_editor.cpp:428`）。恒返回 -1 会让「连接信号到脚本已有方法」时编辑器**多写一个空函数**（`connections_dialog.cpp:1017-1034`）。可用本文件已有的正则设施定位方法定义行，单文件可做 |
| 170 | `src/runtime/weaver/jsb_script_language.h:219` | `virtual String _auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const override { return p_code; } // TODO` | E | — | 2026-08 | `_auto_indent_code` 在 extension 路径下**不会被调用**：`ScriptLanguageExtension::auto_indent_code` 是 `static`（`script_language_extension.h:573`），`EditorAdapter::format_code` 直接值调用它，不经过虚分派。「自动缩进」真正走 `EditorLanguage::format_code`（`script_text_editor.cpp:1789/1801`），GDScript 也走自己的 `EditorLanguage` 实现。→ 应删注释或写明为何不实现，**不是填实这个函数** |
| 171 | `src/runtime/weaver/jsb_script_language.h:220` | `virtual void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override {} // TODO` | E | — | 2026-08 | `_add_named_global_constant`：唯一调用方是 autoload（`main.cpp` 游戏启动、`editor_autoload_settings.cpp` 编辑器）。**对 JS 不适用**：TS/JS 没有裸标识符解析，注入全局名对 JS 无意义；本仓也无任何 autoload 集成（`git grep autoload src` 为空）。上游 C# **不覆写**此函数。→ 注释应写「不适用」而非「待实现」 |
| 172 | `src/runtime/weaver/jsb_script_language.h:221` | `virtual void _remove_named_global_constant(const StringName &p_name) override {} // TODO` | E | — | 2026-08 | `_remove_named_global_constant`：同上，唯一调用方是 autoload 删除路径（`editor_autoload_settings.cpp:567`）。**不适用**，C# 亦不覆写 |
| 173 | `src/runtime/weaver/jsb_script_language.h:223` | `virtual TypedArray<Dictionary> _get_public_functions() const override { return {}; } // TODO: Vector<StackInfo>` | D | — | 2026-08 | `_get_public_functions`：唯一消费者是 `doc_tools.cpp:1107`（为语言生成 `@<语言名>` 文档页）。不是「填一个函数」——需要新增并维护一份 C++ 侧语言内建清单（我方 `@bind` 注解在运行时 bundle 里，不满足该同步接口）。上游 C# 为空且注释是 `TODO?` |
| 174 | `src/runtime/weaver/jsb_script_language.h:224` | `virtual Dictionary _get_public_constants() const override { return Dictionary(); } // TODO: Vector<StackInfo>` | D | — | 2026-08 | `_get_public_constants`：消费者 `doc_tools.cpp:1139`，同 173 |
| 175 | `src/runtime/weaver/jsb_script_language.h:225` | `virtual TypedArray<Dictionary> _get_public_annotations() const override { return {}; } // TODO: Vector<StackInfo>` | D | — | 2026-08 | `_get_public_annotations`：消费者 `doc_tools.cpp:1152`，同 173 |
| 176 | `src/runtime/weaver/jsb_script_language.h:236` | `virtual String _validate_path(const String &p_path) const override { return ""; } // TODO: 返回指定路径文件的错误信息（脚本创建对话框处使用）` | A | — | 2026-07 | `_validate_path`：唯一调用方 `script_create_dialog.cpp:301`（「新建脚本」对话框的路径校验）。GDScript 与 C# 都有实现。是否要做取决于本项目是否使用「新建 TS 脚本」流程 |
| 177 | `src/runtime/weaver/jsb_script_language.h:265` | `virtual void _add_global_constant(const StringName &p_name, const Variant &p_value) override {} // TODO` | E | — | 2026-09 | `_add_global_constant`：唯一调用方是 autoload（`main.cpp:4490/4538` 游戏启动两遍）。GDScript 灌 `globals` 数组**给解析器做常量折叠**；JS 无此阶段。**对 JS 不适用**，上游 C# 为空实现（`/* TODO */ ... {}`） |
| 178 | `src/runtime/weaver/jsb_script_language.h:270` | `virtual String _debug_get_error() const override { return ""; } // TODO` | D | — | 2026-09 | `_debug_get_error` 空：需要把 V8/JSC 运行时错误桥接进 Godot 调试器的 ScriptLanguageExtension 协议 |
| 179 | `src/runtime/weaver/jsb_script_language.h:271` | `virtual int32_t _debug_get_stack_level_count() const override { return 1; } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_count` 恒返回 1：同上，需要栈帧枚举 |
| 180 | `src/runtime/weaver/jsb_script_language.h:272` | `virtual int32_t _debug_get_stack_level_line(int32_t p_level) const override { return 1; } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_line` 恒返回 1：同上 |
| 181 | `src/runtime/weaver/jsb_script_language.h:273` | `virtual String _debug_get_stack_level_function(int32_t p_level) const override { return ""; } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_function` 空：同上 |
| 182 | `src/runtime/weaver/jsb_script_language.h:274` | `virtual String _debug_get_stack_level_source(int32_t p_level) const override { return ""; } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_source` 空：同上 |
| 183 | `src/runtime/weaver/jsb_script_language.h:275` | `virtual Dictionary _debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_locals` 空 Dictionary：需要作用域内变量枚举 + Variant 化 |
| 184 | `src/runtime/weaver/jsb_script_language.h:276` | `virtual Dictionary _debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_members` 空 Dictionary：同上 |
| 185 | `src/runtime/weaver/jsb_script_language.h:277` | `virtual void *_debug_get_stack_level_instance(int32_t p_level) override { return nullptr; } // TODO` | D | — | 2026-09 | `_debug_get_stack_level_instance` 返回 nullptr：同上 |
| 186 | `src/runtime/weaver/jsb_script_language.h:278` | `virtual Dictionary _debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO` | D | — | 2026-09 | `_debug_get_globals` 空 Dictionary：同上 |
| 187 | `src/runtime/weaver/jsb_script_language.h:279` | `virtual String _debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) override { return ""; } // TODO` | D | — | 2026-09 | `_debug_parse_stack_level_expression` 空：需要表达式求值桥接 |
| 188 | `src/runtime/weaver/jsb_script_language.h:280` | `virtual TypedArray<Dictionary> _debug_get_current_stack_info() override { return {}; } // TODO: Vector<StackInfo>` | D | — | 2026-09 | `_debug_get_current_stack_info` 返回空：同上（注释中的 `Vector<StackInfo>` 已是旧签名） |

### B2 · src/runtime/bridge/

| # | 文件:行 | 原文 | 类 | 上游 | 引入 | 理由 |
|---|---------|------|----|------|------|------|
| 29 | `src/runtime/bridge/jsb_async_module_loader.cpp:57` | `//TODO env -> enqueue async call` | D | ✓ | 2026-08 | `jsb_async_module_loader.cpp` 的 resolve/reject 未实现（直接 `jsb_not_implemented`），需要异步模块加载器的句柄模型 |
| 30 | `src/runtime/bridge/jsb_async_module_loader.cpp:65` | `//TODO env -> enqueue async call` | D | ✓ | 2026-08 | 同上（reject 分支） |
| 31 | `src/runtime/bridge/jsb_async_module_loader.cpp:86` | `//TODO JS try catch` | A | ✓ | 2026-08 | `func->Call(...).ToLocalChecked()` 缺 TryCatch：加 `impl::TryCatch` 并错误上报，单函数可做 |
| 32 | `src/runtime/bridge/jsb_async_module_manager.cpp:147` | `//TODO module tree needed?` | E | ✓ | 2026-08 | `//TODO module tree needed?` 是疑问式备忘，紧跟 `jsb_nop()`；在语义确定前不构成可执行项 |
| 33 | `src/runtime/bridge/jsb_async_module_manager.cpp:148` | `//TODO GodotJS script needed?` | E | ✓ | 2026-08 | `//TODO GodotJS script needed?` 同上，疑问式备忘 |
| 34 | `src/runtime/bridge/jsb_async_module_manager.h:32` | `//TODO handle parent module id in AsyncModuleManager?` | D | ✓ | 2026-08 | `AsyncModuleManager` 处理 parent module id：需要模块树/父链信息，与 32/33 同一设计域 |
| 35 | `src/runtime/bridge/jsb_bridge_module_loader.cpp:370` | `// TODO: Cache for our cache functions?:` | D | ✓ | 2026-08 | `_signal_getters` 等弱引用函数缓存：注释自己指出需要挂 GC finalizer 并避免长期持有 StringName |
| 36 | `src/runtime/bridge/jsb_bridge_module_loader.cpp:393` | `// TODO: Weak ref function cache` | D | ✓ | 2026-08 | 弱引用函数缓存（`() => Signal`）：与 35 同属缓存+GC 设计 |
| 37 | `src/runtime/bridge/jsb_class_info.cpp:34` | `//TODO it breaks the isolation of 'bridge'` | D | ✓ | 2026-07 | `jsb_class_info.cpp` 包含 `../weaver/jsb_script.h` 打破 bridge 隔离：按 spec `architecture-constraints.md` 该分层约束需架构级处理 |
| 38 | `src/runtime/bridge/jsb_class_info.cpp:366` | `//TODO collect methods/signals/properties` | E | ✓ | 2026-08 | `//TODO collect methods/signals/properties` 紧接其后的代码正在收集 prototype 成员，注释已被实现覆盖，建议删除或改写 |
| 39 | `src/runtime/bridge/jsb_class_info.cpp:443` | `//TODO property categories` | E | ✓ | 2026-08 | `//TODO property categories`：紧随其后的注释已说明「解析期只登记名字、签名懒加载」，属取舍记录，建议保留 |
| 40 | `src/runtime/bridge/jsb_class_info.cpp:878` | `//TODO maybe we should always add new GodotJS class instead of refreshing the existing one (for simpler reloading flow, such as directly replacing prototype of a existing instance javascript object)` | D | ✓ | 2026-08 | 「总是新建 GodotJS class 而非刷新已有类」以简化重载流：属类注册/重载架构决策 |
| 41 | `src/runtime/bridge/jsb_class_info.h:61` | `//TODO a FASTPATH implementation? avoid unnecessary Variant wrapping for special builtin primitives (from Vector2 to Color)` | D | ✓ | 2026-08 | builtin primitive（Vector2→Color）FASTPATH 避免 Variant 包装：注释自己指出 VALUE 绑定受线程安全池限制，属类型系统层面 |
| 42 | `src/runtime/bridge/jsb_class_info.h:103` | `//TODO RESERVED FOR FUTURE USE` | E | ✓ | 2026-08 | `//TODO RESERVED FOR FUTURE USE` 标注的预留字段，属说明 |
| 43 | `src/runtime/bridge/jsb_class_info.h:234` | `struct ScriptMethodInfo // TODO: 为什么不复用 MethodInfo` | E | — | 2026-08 | `struct ScriptMethodInfo // TODO: 为什么不复用 MethodInfo` 是疑问式备忘（Godot 的 MethodInfo 与脚本方法元信息字段不同），建议改写为结论而非 TODO |
| 44 | `src/runtime/bridge/jsb_class_info.h:286` | `//TODO we have no idea about it with javascript itself. maybe we can decorate the abstract class and check here?` | D | ✓ | 2026-08 | JS 侧无 abstract 语义，需靠装饰器标注再检查：属注解系统扩展 |
| 45 | `src/runtime/bridge/jsb_class_info.h:332` | `//TODO whether the internal class object alive or not` | A | ✓ | 2026-08 | `ScriptClassInfo::is_valid()` 恒 true 与 TODO「class object 是否存活」矛盾：可改为检查 `clazz` Global 是否为空，单函数可做 |
| 46 | `src/runtime/bridge/jsb_environment.cpp:55` | `//TODO remove this` | B | ✓ | 2026-07 | `//TODO remove this` 指 `#include "../weaver/jsb_script.h"`，但同文件 :767/:773 正在使用 `Ref<GodotJSScript>`，该 include 不能删——注释已过时，应删除 |
| 47 | `src/runtime/bridge/jsb_environment.cpp:84` | `//TODO check if it's not removed from `all_runtimes_` but being destructed already (consider remove it from the list immediately on destructor called)` | D | ✓ | 2026-08 | `all_runtimes_` 中「已进入析构但未摘除」的竞态：属环境生命周期/并发设计，注释重复出现 3 处 |
| 48 | `src/runtime/bridge/jsb_environment.cpp:96` | `//TODO check if it's not removed from `all_runtimes_` but being destructed already (consider remove it from the list immediately on destructor called)` | D | ✓ | 2026-08 | 同上（`access(p_runtime)` 分支） |
| 49 | `src/runtime/bridge/jsb_environment.cpp:123` | `//TODO check if it's not removed from `all_runtimes_` but being destructed already (consider remove it from the list immediately on destructor called)` | D | ✓ | 2026-08 | 同上（线程扫描分支） |
| 50 | `src/runtime/bridge/jsb_environment.cpp:382` | `//TODO call `start_debugger` at different stages for Editor/Game Runtimes.` | A | ✓ | 2026-08 | `start_debugger` 按 Editor/Game 不同阶段调用：只需调整 init 内调用点，单函数范围 |
| 51 | `src/runtime/bridge/jsb_environment.cpp:392` | `//TODO not always safe` | E | ✓ | 2026-08 | 析构函数上的「not always safe」告警性说明，配合下方 Warning 日志，属防御性注释 |
| 52 | `src/runtime/bridge/jsb_environment.cpp:526` | `//TODO be able to handle the uncaught exceptions in env (instead of being swallowed in the timer invocation).` | D | ✓ | 2026-08 | 定时器回调中未捕获异常被吞，需要转发到 worker master 的 `onerror`：属错误传播链路 |
| 53 | `src/runtime/bridge/jsb_environment.cpp:882` | `// TODO: 为 RefCounted 添加释放资源的函数考虑 Symbols.Dispose ?` | D | — | 2026-08 | 为 RefCounted 增加释放资源函数（`Symbols.Dispose`）：涉及符号表与 dispose 绑定 |
| 54 | `src/runtime/bridge/jsb_environment.cpp:968` | `//TODO do not clear the internal field if calling from JS GC` | D | ✓ | 2026-08 | JS GC 触发时不清理 internal field：属对象绑定生命周期细节，注释块已给出理由 |
| 55 | `src/runtime/bridge/jsb_environment.cpp:1282` | `// //TODO may not work in this way` | A | — | 2026-08 | `crossbind` 中「对象已绑定却重新 crossbind」的分支：注释掉的是 `_rebind(isolate, context, p_this, p_class_id)`。**先确认 `_rebind` 是否仍需要**（该函数在 `jsb_environment.cpp:1368` 仍存在），再决定启用该分支还是删除这段代码——不是可以径直删掉的死代码 |
| 56 | `src/runtime/bridge/jsb_environment.cpp:1357` | `//TODO a dirty but approaching solution for hot-reloading` | D | ✓ | 2026-08 | `Environment::rebind` 自注 dirty 的热重载方案 |
| 57 | `src/runtime/bridge/jsb_environment.cpp:1368` | `//TODO a dirty but approaching solution for hot-reloading` | D | ✓ | 2026-08 | `Environment::_rebind` 同上 |
| 58 | `src/runtime/bridge/jsb_environment.cpp:1448` | `std::future_status status = debugger_ready_future_.wait_for(debugger_connection_pool_duration); // TODO` | E | — | 2026-08 | `debugger_ready_future_.wait_for(...) // TODO` 裸 TODO，无说明内容，建议删除或补上意图 —— **本轮已按用户指示删除** |
| 59 | `src/runtime/bridge/jsb_environment.cpp:1623` | `//TODO try to compile?` | D | ✓ | 2026-08 | `validate_script` 直接 `return true`：真正校验需要「无副作用的编译」能力（见 65），两者同一问题 |
| 60 | `src/runtime/bridge/jsb_environment.cpp:1680` | `// TODO if a function returns a Promise for godot script callbacks (such as _ready), it's safe to return as nothing without error?` | D | ✓ | 2026-08 | Godot 脚本回调（`_ready` 等）返回 Promise 时的语义未定：属调用协议设计 |
| 61 | `src/runtime/bridge/jsb_environment.cpp:1910` | `// TODO: 支持静态函数的调用。 static calls are not supported` | D | — | 2026-09 | `call_script_method` 不支持静态函数调用：注释明确写出限制，需扩展调用通道 |
| 62 | `src/runtime/bridge/jsb_environment.h:179` | `//TODO remove this later` | A | ✓ | 2026-08 | `//TODO remove this later` 指 `friend struct ScriptClassInfo`：收敛访问后可移除，局部改动 |
| 63 | `src/runtime/bridge/jsb_environment.h:224` | `//TODO all exported default classes inherit native godot class (directly or indirectly)` | E | ✓ | 2026-08 | 说明「导出的 default class 直接或间接继承原生 godot class」的条件，属设计说明 |
| 64 | `src/runtime/bridge/jsb_environment.h:387` | `//TODO temp, get C++ function pointer (include class methods)` | D | ✓ | 2026-08 | `get_function_pointer` 标注 temp：真正方案需要静态绑定/函数指针表的完整集成 |
| 65 | `src/runtime/bridge/jsb_environment.h:453` | `//TODO is there a simple way to compile (validate) the script without any side effect?` | D | ✓ | 2026-08 | `validate_script` 的「无副作用编译校验」：与 59 同一问题 |
| 66 | `src/runtime/bridge/jsb_essentials.cpp:180` | `// TODO: V8 update. Once we update V8 past 12.6.221 we can skip the cast to double` | A | ✓ | 2026-08 | 去掉 `(double)(int64_t)` 强制转换需 V8 ≥ 12.6.221；当前 pin 为 `12.4.254.21`（`SConstruct:100`），故前置未满足——改动本身一行 |
| 67 | `src/runtime/bridge/jsb_essentials.cpp:252` | `//TODO the root 'import' function (async module loading?)` | D | ✓ | 2026-08 | 根 `import` 函数空块（async module loading）：属异步模块体系 |
| 68 | `src/runtime/bridge/jsb_godot_module_loader.cpp:84` | `//TODO check static bindings at first, and dynamic bindings as a fallback` | D | ✓ | 2026-08 | godot 模块加载器「优先静态绑定、动态绑定兜底」：需要两套绑定表协同查询 |
| 69 | `src/runtime/bridge/jsb_internal_module_loader.cpp:32` | `//TODO evaluate source from presets with file_name_` | A | ✓ | 2026-08 | `InternalModuleLoader::load` 的 `//TODO evaluate source from presets with file_name_`：用已持有文件名构造 source reader 即可，单函数可做 |
| 70 | `src/runtime/bridge/jsb_message.h:61` | `//TODO worker error (NOT IMPLEMENTED YET)` | D | ✓ | 2026-08 | `Message::TYPE_ERROR` 常量存在但无生产者，worker 错误语义未实现（与 6/7 的 `onerror` 同一链路） |
| 71 | `src/runtime/bridge/jsb_module.cpp:46` | `//TODO reload all related modules (search the module graph) ?` | D | ✓ | 2026-08 | 重载时沿模块图重载相关模块：属模块依赖图能力 |
| 72 | `src/runtime/bridge/jsb_module.cpp:47` | `//TODO inconsistent implementation, since the original time modified is read in module resolvers (SourceReader)` | D | ✓ | 2026-08 | 模块 mtime/hash 的读取路径不一致（resolver 的 SourceReader 与 `FileAccess` 各读一次）：属缓存一致性设计 |
| 73 | `src/runtime/bridge/jsb_module_resolver.cpp:97` | `//TODO set `require.cache`` | B | ✓ | 2026-08 | `//TODO set require.cache` 已实现：`JavaScriptModuleCache::insert`（`jsb_module.cpp:86`）把模块写入 `cache_object_`，`_new_require_func`（`jsb_environment.cpp:1431`）把该对象挂到每次 `require` 上；注释已过时 |
| 74 | `src/runtime/bridge/jsb_object_bindings.cpp:35` | `// TODO: Refactor. Violates isolation of bridge.` | D | ✓ | 2026-07 | `jsb_object_bindings.cpp` 包含 weaver 头打破 bridge 隔离：与 37 同一分层问题 |
| 75 | `src/runtime/bridge/jsb_object_bindings.cpp:49` | `/** TODO:` | D | ✓ | 2026-08 | 为 RefCounted 增加 `[Symbol.dispose]`/`[Symbol.asyncDispose]` 并同步生成文档：涉及绑定生成与文档管线 |
| 76 | `src/runtime/bridge/jsb_primitive_bindings.cpp:406` | `//TODO it's restricted since we don't know anything about the type` | D | ✓ | 2026-08 | `Object::_set/_get`「不知道类型」导致的限制：需要 Variant 类型信息贯通到 primitive 绑定层 |
| 77 | `src/runtime/bridge/jsb_primitive_bindings.cpp:698` | `//TODO hardcoded branches for fast method reflection wrapper` | D | ✓ | 2026-08 | vararg 快速反射包装的硬编码分支：注释与 `jsb_reflect_binding_util.h` 的手写特化同源，需代码生成方案 |
| 78 | `src/runtime/bridge/jsb_reflect_binding_util.h:191` | `// TODO: 以下内容改为在构建时根据 extension_api.json 生成，妈的哪来的傻逼全部硬编码` | D | — | 2026-08 | `jsb_reflect_binding_util.h` 的一大批手写 VS 类型特化「改为构建期按 extension_api.json 生成」：与已有 `misc/build/static_binding_codegen.py` 部分重叠，但本文件是另一套 fast-path 反射，需新生成器 |
| 79 | `src/runtime/bridge/jsb_reflect_binding_util.h:398` | `//TODO ClassID temporarily not used for possibly better performance in constructor (by avoiding info.Data())` | A | ✓ | 2026-08 | `ReflectBuiltinMethodPointer` 构造路径把 ClassID 从 `info.Data()` 改为临时不用：仅需恢复一处取用，性能取向的小改 |
| 80 | `src/runtime/bridge/jsb_shadow_realm.cpp:29` | `// TODO: 优化 mutex 的使用` | D | — | 2026-07 | `jsb_shadow_realm.cpp` 的 mutex 使用优化：涉及 shadow realm 并发模型 |
| 81 | `src/runtime/bridge/jsb_shadow_realm.cpp:530` | `// TODO: Freeze 或 proxy, 防止被篡改` | A | — | 2026-07 | cross-wrapper 函数「Freeze 或 proxy 防止被篡改」：单处 `v8::Object::Freeze` 即可 |
| 82 | `src/runtime/bridge/jsb_shadow_realm.cpp:549` | `// TODO: jsb_stackalloc` | A | — | 2026-07 | `args` 由 `LocalVector` 改 `jsb_stackalloc` 栈分配：局部性能改动，仓库已有 `jsb_stackalloc` 设施 |
| 83 | `src/runtime/bridge/jsb_thread_safe_for_nodes_scope.h:31` | `// TODO: godot 没有暴露相关接口` | C | — | 2026-07 | `jsb_thread_safe_for_nodes_scope.h` 的「godot 没有暴露相关接口」：godot-cpp 全树无 `set_thread_safe`/ThreadSafe 匹配，GDExtension 侧无法实现 |
| 84 | `src/runtime/bridge/jsb_transpiler.h:55` | `//TODO test` | A | ✓ | 2026-08 | `jsb_transpiler.h` 的 `//TODO test`：仅缺验证 |
| 85 | `src/runtime/bridge/jsb_type_convert.cpp:357` | `//TODO should auto convert a null/undefined value to a default (variant) counterpart?` | D | ✓ | 2026-08 | `js_to_gd_var` 是否把 null/undefined 自动转默认 Variant：属隐式转换语义设计（影响面广） |
| 86 | `src/runtime/bridge/jsb_type_convert.cpp:584` | `//TODO` | D | ✓ | 2026-08 | `js_to_gd_var` 中 `p_jval->IsFunction()` 的分支是空的：需考虑是否把 JS 函数转成 `godot::Callable`。涉及 Callable 的所有权/作用域与跨 env 失效语义，属类型转换语义扩展 |
| 87 | `src/runtime/bridge/jsb_type_convert.cpp:610` | `/** TODO: 用更优雅的方式处理 */` | A | — | 2026-08 | Promise 内嵌字段处理「用更优雅方式」：注释已有 HACK 说明，可用 `v8::Promise` 的类型查询替代手写偏移 |
| 88 | `src/runtime/bridge/jsb_type_convert.cpp:655` | `//TODO find a better way to check integer type?` | A | ✓ | 2026-08 | 整数类型判断的更好方式：`IsInt32`/`IsUint32` 组合即可，单函数可做 |
| 89 | `src/runtime/bridge/jsb_type_convert.cpp:701` | `//TODO is loose conversion check for JS primitive array as Godot array a bad idea?` | E | ✓ | 2026-08 | 「JS 原始数组宽松转 Godot 数组是不是坏主意」——设计疑问，当前行为由 `JSB_IMPLICIT_PACKED_ARRAY_CONVERSION` 开关控制，建议改写为结论注释 |
| 90 | `src/runtime/bridge/jsb_type_convert.h:87` | `* TODO: v8 能够根据编译配置改变 v8::Promise，v8::ArrayBuffer，v8::ArrayBufferView 的内嵌字段数量` | E | — | 2026-08 | `jsb_type_convert.h` 说明 V8 内嵌字段数量随编译配置变化，属安全性说明（重复出现于两处重载） |
| 91 | `src/runtime/bridge/jsb_type_convert.h:100` | `* TODO: v8 能够根据编译配置改变 v8::Promise，v8::ArrayBuffer，v8::ArrayBufferView 的内嵌字段数量` | E | — | 2026-08 | 同上 |

### B3 · src/runtime/impl/

| # | 文件:行 | 原文 | 类 | 上游 | 引入 | 理由 |
|---|---------|------|----|------|------|------|
| 92 | `src/runtime/impl/jsc/jsb_jsc_class.h:44` | `//TODO may unnecessary, should be identical with prototype.constructor?` | E | ✓ | 2026-08 | `constructor_` 是否与 `prototype.constructor` 重复：jsc/quickjs/web 三处同一疑问，属待确认的备忘 |
| 93 | `src/runtime/impl/jsc/jsb_jsc_data.cpp:49` | `//TODO improve jsc value hash` | A | ✓ | 2026-08 | `Data::GetIdentityHash` 的 jsc 哈希改进：可用 `JSValueGetIdentityHash`，单函数可做 |
| 94 | `src/runtime/impl/jsc/jsb_jsc_handle.h:156` | `//TODO use JSWeakRef (JSWeakPrivate.h)` | A | ✓ | 2026-08 | jsc `Global` 改用 `JSWeakRef`（头文件已在 `jsb_jsc_pch.h` 引入 `JSWeakPrivate.h`）：实现集中在 `jsb_jsc_handle.h` |
| 95 | `src/runtime/impl/jsc/jsb_jsc_isolate.cpp:152` | `//TODO dead loop checker` | D | ✓ | 2026-08 | jsc 死循环检查（`JS_SetInterruptHandler` 空置）：需要中断回调与执行超时策略 |
| 96 | `src/runtime/impl/jsc/jsb_jsc_isolate.cpp:472` | `//TODO copy or steal?` | A | ✓ | 2026-08 | `_NotAllowedCallAsFunction` 中 `stack_dup` 是拷贝还是窃取：确认后改一行即可（`constructor call` 错误路径） |
| 97 | `src/runtime/impl/jsc/jsb_jsc_isolate.cpp:541` | `//TODO delete FunctionData in a thread safe way` | D | ✓ | 2026-08 | jsc `_delete_cfunction` 的线程安全释放：涉及 pending_delete 队列与跨线程所有权 |
| 98 | `src/runtime/impl/jsc/jsb_jsc_isolate.cpp:542` | `//TODO JSValueUnprotect(data.data);` | D | ✓ | 2026-08 | `JSValueUnprotect(data.data)` 未执行：需要与 97 一起确定 FunctionData 的生命周期 |
| 99 | `src/runtime/impl/jsc/jsb_jsc_object.cpp:226` | `//TODO correct?` | E | ✓ | 2026-08 | `JSValueUnprotect` 后的 `//TODO correct?`：需实测确认保护计数配对，属待确认 |
| 100 | `src/runtime/impl/jsc/jsb_jsc_pch.h:37` | `//TODO WARNING: ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY.` | B | ✓ | 2026-07 | `jsb_jsc_pch.h` 的「ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY」已过时：CI 已有 jsc 腿（`.github/workflows/ci.yml` 的 `engine: jsc`，覆盖 macos 与 ios），jsc.impl 已可构建 |
| 101 | `src/runtime/impl/jsc/jsb_jsc_primitive.cpp:57` | `//TODO no equivalent implementation` | A | ✓ | 2026-08 | jsc `Value::ToDetailString` 无等价实现而回退 `ToString`：可在回退前附加类型前缀，单函数可做 |
| 102 | `src/runtime/impl/jsc/jsb_jsc_primitive.cpp:100` | `//TODO we know val must be an instance of External. uncertain whether it's reasonable not using `JSValueToObject` here?` | A | ✓ | 2026-08 | `External::Value` 直接 `JSObjectGetPrivate` 而非 `JSValueToObject`：已 `jsb_check(_IsExternal)`，注释为待确认，可改写为结论 |
| 103 | `src/runtime/impl/node/jsb_node_bridge.cpp:42` | `* TODO: 参考 Gode` | D | — | 2026-08 | `jsb_node_bridge.cpp` 顶部「参考 Gode」：涉及 node 构建的二进制打包/ExportPlugin 策略（注释本身已列出 3 条方案） |
| 104 | `src/runtime/impl/node/jsb_node_runtime.cpp:128` | `// TODO: 找不到node 构建在退出进程时的 89 个 Orphan StringName 怎么处理，orz。` | D | — | 2026-08 | node 构建退出时约 89 个 Orphan StringName：归档任务 `09-22-fix-orphan-stringname-v8-node` 处理过同类问题，需先复核现状再定；注释未标注结论 |
| 105 | `src/runtime/impl/quickjs/jsb_quickjs_class.h:44` | `//TODO may unnecessary, should be identical with prototype.constructor?` | E | ✓ | 2026-08 | quickjs `constructor_` 重复疑问：与 92/126 同一备忘 |
| 106 | `src/runtime/impl/quickjs/jsb_quickjs_data.cpp:113` | `//TODO we can not determine whether it's int32 or uint32` | C | ✓ | 2026-08 | QuickJS 数值标签只有 `JS_TAG_INT`，无法区分 int32/uint32（引擎语义限制），`IsInt32`/`IsUint32` 只能同解 |
| 107 | `src/runtime/impl/quickjs/jsb_quickjs_data.cpp:120` | `//TODO we can not determine whether it's int32 or uint32` | C | ✓ | 2026-08 | 同上 |
| 108 | `src/runtime/impl/quickjs/jsb_quickjs_ext.h:118` | `//TODO unsafe eq check` | A | ✓ | 2026-08 | quickjs `jsb_quickjs_ext.h` 的 `//TODO unsafe eq check`：指针比较前可先比 tag，单函数可做 |
| 109 | `src/runtime/impl/quickjs/jsb_quickjs_isolate.cpp:67` | `//TODO JSObject would never be reallocated, true?` | A | ✓ | 2026-08 | JSObject 是否会被 realloc 的假设：注释已给出「若会 realloc 则需间接映射」的方案，属待实测确认 |
| 110 | `src/runtime/impl/quickjs/jsb_quickjs_isolate.cpp:92` | `//TODO JSObject would never be reallocated, true?` | A | ✓ | 2026-08 | 同上（第二处 `js_realloc`） |
| 111 | `src/runtime/impl/quickjs/jsb_quickjs_isolate.cpp:127` | `//TODO dead loop checker` | D | ✓ | 2026-08 | quickjs 死循环检查：与 95 同属中断/超时机制 |
| 112 | `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:135` | `//TODO unsure` | E | ✓ | 2026-08 | quickjs `HasOwnProperty` 的 `//TODO unsure`：待确认，无具体动作 |
| 113 | `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:332` | `int flags = JS_PROP_HAS_ENUMERABLE \| JS_PROP_HAS_CONFIGURABLE; //TODO consider remove 'configurable' flags` | A | ✓ | 2026-08 | 属性标志是否保留 `JS_PROP_HAS_CONFIGURABLE`：与 129（web 同问题）一致，两处各改一行 |
| 114 | `src/runtime/impl/quickjs/jsb_quickjs_object.cpp:355` | `// `key_conversion` is not implemented here, matching the jsc and web shims (see the TODO in` | E | — | 2026-09 | 说明 quickjs 未实现 `key_conversion`，并与 jsc/web 保持一致的取舍（指向 monolith.ts）；建议保留为跨实现一致性说明 |
| 115 | `src/runtime/impl/quickjs/jsb_quickjs_primitive.cpp:223` | `//TODO avoid using Uint32 because the underlying tag is INT or FLOAT64` | A | ✓ | 2026-08 | `Integer::NewFromUnsigned` 用 `JS_NewUint32` 而底层 tag 只有 INT/FLOAT64：可改用 `JS_NewInt32`/`JS_NewFloat64`，单函数可做 |
| 116 | `src/runtime/impl/quickjs/jsb_quickjs_typedef.h:64` | `//TODO do not know whether it works properly or not` | A | ✓ | 2026-08 | `jsb_quickjs_typedef.h` 的「不知道能否正常工作」：属待验证 |
| 117 | `src/runtime/impl/web/bridge/src/monolith.ts:236` | `//TODO fastpath to verify a UniversalBridgeClass` | D | ✓ | 2026-07 | web `monolith.ts` 的 UniversalBridgeClass 校验 fastpath：属 web.impl 桥接优化 |
| 118 | `src/runtime/impl/web/bridge/src/monolith.ts:330` | `//TODO gc finalizer is run after the object dead, it's impossible to get a valid internal_data in gc_callback` | D | ✓ | 2026-07 | web gc finalizer 在对象已死时无法取得有效 internal_data：需要句柄生命周期重设计 |
| 119 | `src/runtime/impl/web/bridge/src/monolith.ts:447` | `//TODO` | E | ✓ | 2026-07 | 裸 `//TODO`（`_throw_trivial` 之前），无说明内容，建议删除 —— **本轮已按用户指示删除** |
| 120 | `src/runtime/impl/web/bridge/src/monolith.ts:449` | `//TODO not sure, temporarily throw all trivial errors` | E | ✓ | 2026-07 | `_throw_trivial`「not sure, temporarily throw all trivial errors」：临时策略说明，建议改写为结论 —— **本轮已按用户指示删除** |
| 121 | `src/runtime/impl/web/bridge/src/monolith.ts:946` | `//TODO filter and key_conversion are not implemented (or not supported?)` | D | ✓ | 2026-07 | web `GetOwnPropertyNames` 的 `filter`/`key_conversion` 未实现：与 114 同源，需要 v8 shim 语义完整化 |
| 122 | `src/runtime/impl/web/bridge/src/monolith.ts:1051` | `//TODO temporarily use browser global object` | A | ✓ | 2026-07 | web `GetGlobalObject` 临时返回浏览器 global：可指向桥接环境对象，单函数可做（需先确认沙箱语义） |
| 123 | `src/runtime/impl/web/bridge/src/monolith.ts:1072` | `//TODO if async module is implemented, we can use dynamic scripts which support debugging in browser devtools` | D | ✓ | 2026-07 | web 同步 eval 与「async module 实现后可用 dynamic scripts（可在 devtools 调试）」：属异步模块体系（与 67 同域） |
| 124 | `src/runtime/impl/web/bridge/src/monolith.ts:1526` | `//TODO may not be supported?` | A | ✓ | 2026-07 | `i64` getter 是否受支持：`BigInt64Array` 可用性确认，属待验证 |
| 125 | `src/runtime/impl/web/bridge/src/monolith.ts:1535` | `//TODO may not be supported?` | A | ✓ | 2026-08 | `u64` getter 同上 |
| 126 | `src/runtime/impl/web/jsb_web_class.h:42` | `//TODO may unnecessary, should be identical with prototype.constructor?` | E | ✓ | 2026-08 | web `constructor_` 重复疑问：与 92/105 同一备忘 |
| 127 | `src/runtime/impl/web/jsb_web_helper.h:43` | `//TODO [web.impl] SetDeleter: not tested` | A | ✓ | 2026-08 | web `SetDeleter` 未测试：补测即可 |
| 128 | `src/runtime/impl/web/jsb_web_helper.h:67` | `//TODO copy from HEAP?` | A | ✓ | 2026-08 | `jsb_web_helper.h` 的「copy from HEAP?」：内存来源确认，局部改动 |
| 129 | `src/runtime/impl/web/jsb_web_object.cpp:199` | `int flags = jsb::impl::PropertyFlags::ENUMERABLE \| jsb::impl::PropertyFlags::CONFIGURABLE; //TODO consider remove 'configurable' flags` | A | ✓ | 2026-08 | web 属性标志 `CONFIGURABLE` 是否移除：与 113 对称，一行改动 |

### B4 · editor / internal / scripts / tests / 其它

| # | 文件:行 | 原文 | 类 | 上游 | 引入 | 理由 |
|---|---------|------|----|------|------|------|
| 1 | `.clang-format:159` | `## Godot TODO: We'll want to use a min of 1, but we need to see how to fix` | E | — | 2026-06 | `.clang-format` 中从上游 Godot 继承的注释（说明为何 `Minimum: 0`）；本仓格式化规范沿用同一工具链，保留即可 |
| 2 | `scripts/jsb.runtime/src/godot.annotations.ts:214` | `// TODO: Godot's property hints make for a poor API. We should provide convenience methods to build them.` | E | ✓ | 2026-07 | Godot 属性 hint API 的设计备忘（建议提供便捷构造方法）：属 API 设计方向，非缺陷 |
| 3 | `scripts/jsb.runtime/src/godot.annotations.ts:272` | `//TODO but we barely know anything about the enum types and int/float/StringName/... in JS` | D | ✓ | 2026-07 | 注解中 enum / int / float / StringName 的类型信息不足：属注解系统类型建模 |
| 4 | `scripts/jsb.runtime/src/godot.annotations.ts:354` | `//TODO more general and unified way to handle all types` | D | ✓ | 2026-07 | `godot.annotations.ts` 中「更通用统一的类型处理」：与 3/5 同一类型建模问题 |
| 5 | `scripts/jsb.runtime/src/godot.annotations.ts:931` | `//TODO more general and unified way to handle all types` | D | ✓ | 2026-07 | 同上（第二处） |
| 6 | `scripts/typings/godot.shadowRealm.d.ts:62` | `//TODO not implemented yet` | D | ✓ | 2026-07 | `godot.shadowRealm.d.ts` 的 `onerror` 标注 not implemented：运行时确为占位实现——`TransferableShadowRealmImpl::register_class` 把 `onerror` 绑到 `_placeholder`（`jsb_shadow_realm.cpp:1754`），且无错误消息通路（见 70/7），属完整错误传播链路缺失 |
| 7 | `scripts/typings/godot.worker.d.ts:42` | `//TODO not implemented yet` | D | ✓ | 2026-07 | `godot.worker.d.ts` 的 `onerror` 同上：`Worker` 的 `onerror` 亦为 `_placeholder`（`jsb_worker.cpp:745`），`Message::TYPE_ERROR` 无生产者 |
| 8 | `src/api_tool/api_tool_types.h:770` | `godot::LocalVector<ApiClassMethod> methods; // TODO: 拆分出虚函数（纯定义，没有hash）` | D | — | 2026-08 | api_tool `ApiClass::methods` 拆分虚函数（纯定义、无 hash）：涉及 api store 的数据布局（见 spec `api-tool-lazy-layout.md`） |
| 9 | `src/compat/editor_settings.cpp:133` | `// editor_settings->set_restart_if_changed(p_setting, p_restart_if_changed); // TODO: EditorSettings 未暴露该接口` | C | — | 2026-08 | `EditorSettings::set_restart_if_changed`：godot-cpp 中确认不存在（`gen/include/.../editor_settings.hpp` 无匹配），GDExtension 无法调用 |
| 10 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:716` | `//TODO skip all d.ts files during the INSTALL phase (do it in the GENERATE phase)` | A | ✓ | 2026-08 | INSTALL 阶段跳过 d.ts（改到 GENERATE 阶段）：`verify_file` 内已被注释掉的三行即可启用，本文件可做 |
| 11 | `src/editor/weaver-editor/jsb_editor_plugin.cpp:1291` | `//TODO no console output in this way, implement pipes here` | A | ✓ | 2026-08 | `tsc` 子进程无控制台输出、需实现管道：`jsb_process.cpp` 的 `create` 已有参数位，属局部实现 |
| 12 | `src/editor/weaver-editor/jsb_editor_plugin.h:161` | `static void generate_scene_nodes_types(std::function<void(bool)> complete, const Vector<String> &p_paths); // TODO: Vector<String> 改为 PackedStringArray` | A | — | 2026-08 | `Vector<String>` 改 `PackedStringArray`：签名与 8 处调用点同文件，机械替换 |
| 13 | `src/editor/weaver-editor/jsb_editor_plugin.h:162` | `static void generate_resource_types(std::function<void(bool)> complete, const Vector<String> &p_paths); // TODO: Vector<String> 改为 PackedStringArray` | A | — | 2026-08 | 同上（`generate_resource_types`） |
| 14 | `src/editor/weaver-editor/jsb_export_plugin.cpp:273` | `//TODO when exporting for web.impl, need to reorganize all scripts into a monolithic script (like webpack)? and preload it before everything get run.` | D | ✓ | 2026-08 | web.impl 导出需把脚本重组为单体并预加载（类 webpack）：属 web 打包架构 |
| 15 | `src/editor/weaver-editor/jsb_export_plugin.cpp:297` | `//TODO handle module deps if it's a .js file ?` | A | ✓ | 2026-08 | 导出时处理 `.js` 文件的模块依赖：注释已给出 `export_compiled_script(p_path)` 的接法，单一分支补齐 |
| 16 | `src/editor/weaver-editor/jsb_export_plugin.cpp:310` | `//TODO` | D | ✓ | 2026-08 | `_supports_platform` 应检查**当前构建能否导出到指定平台**：平台名（`get_os_name()` 小写）+ 架构 → `res://addons/.../bin/<platform>/` 下的实际产物；缺库则返回 false，让编辑器在导出面板禁用该平台。数据源：`.gdextension` 的 `[libraries]` 键、`misc/release/package.py:leg_library_key()`（平台+target+arch→文件名，macOS `universal`、iOS xcframework 无 arch 标签）。**TODO 已就地补全说明（保留在代码里）** |
| 17 | `src/editor/weaver-editor/jsb_repl.cpp:58` | `//TODO list all created realm instances in REPL, interact with the currently selected one.` | A | ✓ | 2026-08 | REPL 列出所有 realm 实例并交互：需把 realm 列表暴露给 REPL（`GodotJSScriptLanguage` 已持有 shadow env），属编辑器局部功能 |
| 18 | `src/editor/weaver-editor/jsb_repl.cpp:159` | `// TODO: GDExtension: set_disable_visibility_clip not available in godot-cpp, skip` | C | — | 2026-08 | `set_disable_visibility_clip`：godot-cpp 中确认无（`gen/include` 全树无匹配） |
| 19 | `src/editor/weaver-editor/jsb_repl.cpp:272` | `//TODO we haven't implemented the js function invocation from outside of Realm, just temporarily call as source code eval` | D | ✓ | 2026-08 | 从 Realm 外调用 JS 函数未实现（REPL 暂以源码 eval 代替）：需要跨 realm 调用通道 |
| 20 | `src/internal/jsb_console_output.cpp:40` | `Vector<IConsoleOutput *> outputs_; // TODO: LocalVector?` | E | — | 2026-08 | `Vector<IConsoleOutput*>` 是否换 `LocalVector`：疑问式备忘，量级极小 |
| 21 | `src/internal/jsb_logger.h:112` | `//TODO cache messages from background threads to avoid messing up the output` | D | ✓ | 2026-08 | 日志在后台线程乱序：需要在 `Logger` 侧缓存/排队，涉及多线程输出模型（三处同一注释） |
| 22 | `src/internal/jsb_logger.h:117` | `//TODO cache messages from background threads to avoid messing up the output` | D | ✓ | 2026-08 | 同上（`_default_print_line`） |
| 23 | `src/internal/jsb_logger.h:122` | `//TODO cache messages from background threads to avoid messing up the output` | D | ✓ | 2026-08 | 同上（`_default_print_error`） |
| 24 | `src/internal/jsb_preset_source.h:130` | `// TODO: 调整生成的数据，考虑直接生成 PackedByteArray，避免内存拷贝` | A | — | 2026-08 | preset 数据生成改为直接产 `PackedByteArray` 以避免 memcpy：需改生成端（`misc/`）+ 本头文件解压路径，范围可控 |
| 25 | `src/internal/jsb_process.cpp:198` | `//TODO use async io instead of threading;` | D | — | 2026-08 | `jsb_process.cpp` 用线程而非 async io：属进程 IO 模型改造 |
| 26 | `src/internal/jsb_process.cpp:310` | `//TODO not tested on linux` | A | ✓ | 2026-08 | `//TODO not tested on linux`（`ProcessImpl` 的 UNIX 分支）：CI 已有 linux 腿，补一次实测即可 |
| 27 | `src/internal/jsb_variant_util.h:75` | `return HashMapHasherDefault::hash(d.hash()); // TODO: Godot 没有暴露 Dictionary::id()` | C | — | 2026-08 | `Dictionary::id()`：godot-cpp 只有 `int64_t hash()`，无 `id()`；GDExtension 无法取得容器身份（用 hash 是既有绕行） |
| 28 | `src/internal/jsb_variant_util.h:79` | `return HashMapHasherDefault::hash(a.hash()); // TODO: Godot 没有暴露 Array::id()` | C | — | 2026-08 | `Array::id()` 同上（godot-cpp 只有 `hash()`） |
| 130 | `src/runtime/internal/jsb_runtime_settings.cpp:37` | `// TODO: 摆脱 editor 依赖` | D | — | 2026-08 | `init_runtime_settings` 想「摆脱 editor 依赖」：涉及 runtime/editor 分层与设置读取路径 |
| 131 | `src/runtime/internal/jsb_runtime_settings.cpp:62` | `// TODO: 考虑挪到 jsb_editor_setting 中，并移除 godot-jsb 模块 (BridgeModuleLoader) 中的依赖，让runtime不再需要` | D | — | 2026-08 | 把 runtime 相关设置挪到 `jsb_editor_setting` 并让 runtime 不再依赖 `BridgeModuleLoader`：与 130 同一分层改造 |
| 132 | `src/runtime/tests/jsb_test_helpers.h:75` | `// CHECK(OS::get_singleton()->set_cwd(env.original_working_dir) == OK); // TODO: gde 没办法改变当前的工作目录` | C | — | 2026-08 | 测试里恢复 cwd 的 `OS::set_cwd`：godot-cpp 无 `set_cwd`/`get_cwd`（`gen/include/.../os.hpp` 无匹配），属 GDExtension 限制 |
| 133 | `src/runtime/tests/test_jsb_any_runtime.h:168` | `// TODO: Build with Node has different path to create isolate and context.` | A | — | 2026-08 | node 构建创建 isolate/context 的路径与其它腿不同：属测试基建分支，`test_jsb_any_runtime.h` 内可对齐 |
| 134 | `src/runtime/tests/test_jsb_shadow_realm.h:34` | `// 测试背景（见 src/bridge/jsb_shadow_realm.cpp 顶部 TODO 注释）：` | E | — | 2026-08 | 测试文件引用 `jsb_shadow_realm.cpp` 顶部 TODO 的测试背景说明，属文档性注释 |

## 9. 结论与建议推进顺序

1. ~~先删 B 类~~ —— **已完成**（§7.1）；另删 2 条无内容的裸 TODO（§7.2）。
2. **`jsb_script_language.h` 那批空实现不能一刀切填实**——调研（见 [virtuals-godot-side.md](./virtuals-godot-side.md)）给出三条不同结论：
   - **该做（A 类）**：`_find_function`、`_validate_path`、`_get_member_line`；
   - **不适用（改注释即可）**：`_add_global_constant`、`_add_named_global_constant`、
     `_remove_named_global_constant`（唯一调用方是 autoload，JS 无裸标识符解析，上游 C# 亦空/不覆写）；
   - **先判定是否为死代码**：`_auto_indent_code`（被 `static` 遮盖，extension 路径永不被调用）；
   - **不值得（D 类）**：`_get_public_functions/constants/annotations`（需新增并维护语言内建清单）。
3. **#16 的实现在代码 TODO 里已写明**；**#55 / #86 先做判定**（用户已给方向）。
4. **C 类不动代码**，建议改写为 `// NOTE:` 使其不再出现在 `TODO` 清单里，同时保留信息。
5. **D 类（85 条）不建议本轮触碰**：集中在热重载 / 调试器栈帧 / 异步模块 / bridge 分层四个主题。

### 关于「清除所有 TODO」这个目标本身

`TODO` 在本仓有三种互不相同的用法：

- **待办**（A 类等）——可执行。
- **取舍记录 / 已知限制**（C、多数 D）——删除会丢失信息。
- **随上游搬迁滚进来的历史注释**——122 条与上游文本相同，删改会产生持续 diff 成本。

**只有裸 TODO（仅 `TODO` 二字、无任何说明）与「经查证不适用」的空实现注释**才是真正该处理的：
前者应补全意图或删除，后者应改写为「不适用 + 原因」。
