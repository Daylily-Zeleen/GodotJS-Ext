# 「被改过 → 又被复原」的 TODO：真实处理状态

调查范围：`git log --since="3 weeks ago"`（2026-09-22 .. 2026-10-12，**271 个提交**）。
方法：全量扫描每个提交的 TODO 增删（`git show --unified=0`），再对每条候选做
`git log -S "<原文>"` 三态（引入 → 删除 → 复原）追踪 + 当前文件实测。

判定口径沿用用户明确要求：**把 `//TODO` 改写成说明文字不算完成**。

---

## 0. 结论摘要

1. **复原批次只有两处**：`8b85a4b`（09-30，17 行）与 `5811f0b`（09-30，4 行），共 **21 行**。
2. 其中 **19 行仍在代码里**（HEAD `9887342`）；2 行后来被正确实现/删除。
3. **`8b85a4b` 把一处真 bug 修复一起回退了**：`jsb_jsc_isolate.cpp` 的
   `stack_val` → `stack_dup`（jsc 异常路径保护计数泄漏，`69fa066` 刚修过）。
   这是本次调查唯一的**功能性回退**。
4. **`STATUS.md` 的 B 类「已过时，已删」表 6 条全部失效** —— 它们都被复原回代码，
   至今仍在 HEAD 里，但清单仍记「已删」。
5. 复原**判错**的有 4 条（前提已被推翻，本应删除）：`not tested on linux`、
   `require.cache`、`may not be supported?`、`_notification`。

---

## 1. 复原清单（21 行）

`8b85a4b`（`fix(todo): put back the TODOs I had rewritten as prose`，17 行）：

| 文件 | 复原的 TODO |
|---|---|
| `src/internal/jsb_preset_source.h` | `// TODO: 调整生成的数据，考虑直接生成 PackedByteArray，避免内存拷贝` |
| `src/internal/jsb_process.cpp` | `//TODO not tested on linux` |
| `src/runtime/bridge/jsb_essentials.cpp` | `// TODO: V8 update. Once we update V8 past 12.6.221 we can skip the cast to double` |
| `src/runtime/bridge/jsb_module_resolver.cpp` | ``//TODO set `require.cache` `` |
| `src/runtime/bridge/jsb_reflect_binding_util.h` | `//TODO ClassID temporarily not used for possibly better performance…` |
| `src/runtime/bridge/jsb_shadow_realm.cpp` | `// TODO: Freeze 或 proxy, 防止被篡改` |
| `src/runtime/bridge/jsb_transpiler.h` | `//TODO test` |
| `src/runtime/bridge/jsb_type_convert.cpp` | `/** TODO: 用更优雅的方式处理 */`（Node Promise 内嵌字段） |
| `src/runtime/bridge/jsb_type_convert.cpp` | `//TODO find a better way to check integer type?` |
| `src/runtime/impl/jsc/jsb_jsc_handle.h` | `//TODO use JSWeakRef (JSWeakPrivate.h)` |
| `src/runtime/impl/jsc/jsb_jsc_isolate.cpp` | `//TODO copy or steal?` |
| `src/runtime/impl/jsc/jsb_jsc_pch.h` | `//TODO WARNING: ONLY FOR DEV, NOT SUPPORTED TO BUILD…` |
| `src/runtime/impl/web/bridge/src/monolith.ts` | `//TODO`（裸）+ `//TODO may not be supported?` |
| `src/runtime/weaver/jsb_script.h` | `placeholders; // TODO: 是否要改成 HashMap 加快查找？` |
| `src/runtime/weaver/jsb_script_instance.cpp` | `// TODO: Warp static method to Callable` |
| `src/runtime/weaver/jsb_script_instance.cpp` | ``//TODO find the method named `_notification`…`` |

`5811f0b`（`feat(runtime): realm-type predicate…`，用户侧并发改动，4 行）：

| 文件 | 复原的 TODO |
|---|---|
| `src/runtime/bridge/jsb_environment.cpp` | `//TODO remove this`（`#include "../weaver/jsb_script.h"` 上方） |
| `src/runtime/bridge/jsb_environment.cpp` | ``//TODO call `start_debugger` at different stages for Editor/Game Runtimes.`` |
| `src/runtime/bridge/jsb_environment.cpp` | `// //TODO may not work in this way`（被注掉） |
| `src/runtime/bridge/jsb_environment.cpp` | `…wait_for(debugger_connection_pool_duration); // TODO`（行尾） |

> `801f876`（09-29）与 `8b85a4b` 无关：它恢复的 7 条（REPL realm 选择、纯 JS 导出依赖遍历、
> d.ts GENERATE/INSTALL 分工、`InternalModuleLoader`、`friend ScriptClassInfo`）后来**都被真正实现**了
> —— `72ba665` / `a6320ad` / `96b2822` / `5811f0b`。这批是正确的 `[x]`，不是本报告的"过时"对象。

---

## 2. 逐条真实状态

「复原判定」= 8b85a4b/5811f0b 把它当"未完成"复原回去，这个判定对不对。

| 位置 | 复原判定 | 依据（已实测） | 真实状态 |
|---|---|---|---|
| `jsb_process.cpp` `not tested on linux` | ❌ **错** | `test_jsb_process.h:53` 在 `WINDOWS_ENABLED \|\| (UNIX_ENABLED && !__EMSCRIPTEN__)` 下注册两个用例；`ci.yml:772-794` 有 `ubuntu-22.04` 的 host-qjs / host-qjs-classic / host-node 三条腿跑 `--jsb-run-tests`（`:1050`） | **前提已推翻**，应删 |
| `jsb_essentials.cpp` V8 cast | ✅ 对 | `third/v8/include/v8-version.h`：12.4.254 < 12.6.221 | 未完成（等 V8 升级） |
| `jsb_module_resolver.cpp` `require.cache` | ✅ **对**（`7f51ac7` 删错） | 该处只 `module_obj->Set(filename/path)`；函数体内**无** set-cache。`cache_object_` 的写入点在 `jsb_module.cpp:86`（`JavaScriptModuleCache::insert`）与 `jsb_environment.cpp:363`（Environment 初始化时挂到全局 `require`）——**不同对象、不同时机**，`7f51ac7` 的"已实现"是错认 | 未完成 |
| `jsb_reflect_binding_util.h` ClassID | ✅ 对（取舍） | 被注掉的 `info.Data().As<v8::Uint32>()->Value()` 取法存在；取不取是性能取舍 | 未完成（取舍，须写判据） |
| `jsb_shadow_realm.cpp` Freeze/proxy | ✅ 对 | wrapper 双方都要写（`STATUS.md` #81 同判） | 未完成 |
| `jsb_type_convert.cpp` Promise「更优雅」 | ✅ 对 | Node Promise 的内嵌字段是 V8 实现细节 | 未完成 |
| `jsb_type_convert.cpp` integer type | ✅ 对 | `IsNumber()` 已覆盖 int32/uint32/float64 | 未完成（低价值） |
| `jsb_jsc_handle.h` `use JSWeakRef` | ⚠️ 可疑 | `JSWeakCreate/GetObject/Release` 已在 `:219/220/265/284/299/321/349` 使用；TODO 挂在 `Global<T>` 模板上方，措辞与实现已不符 | 需澄清（注释失真） |
| `jsb_jsc_isolate.cpp` `copy or steal?` | ❌ **错，且回退了真 bug 修复** | `69fa066` 用 `stack_val` 替换 `stack_dup`：`stack_dup` 会 `JSValueProtect` 一份且**无任何路径释放**（`jsb_jsc_isolate.h:265-269`），`ConstructorCallError` 是常驻槽（`jsb_jsc_isolate.cpp:175` 建、`:194` 统一 `JSValueUnprotect`）→ 异常路径保护计数泄漏 | **泄漏回归**（见 §3） |
| `jsb_jsc_pch.h` `ONLY FOR DEV…` | ❌ 可疑 | `ci.yml:492/575` 有 ios `engine: jsc` 构建腿、`misc/release/package.py` 发布 jsc；但 `report.md §22` 记 host-jsc 为 `continue-on-error`（informational） | 部分过时（jsc 已入 CI 构建矩阵） |
| `monolith.ts` `may not be supported?` | ❌ **错** | `bridge/tsconfig.json:14` `"target": "es2021"`；`BigInt64Array` 是 ES2020 内置；且 `i64` 在 `:655/:726` 是活路径 | **前提已推翻**，应删 |
| `jsb_script.h` placeholders HashMap | ⚠️ 复原后被正确删除 | `2271c68` 删掉，`STATUS.md` 记 `[~]`（保持 `LocalVector`，`045d274`） | 已裁定（`[~]`） |
| `jsb_script_instance.cpp` Warp static → Callable | ✅ 对 | `Callable(owner_, name)` 绑的是实例方法 | 未完成 |
| `jsb_script_instance.cpp` `_notification` | ❌ **错** | 下方 3 行（`:826-829`）就是该动作：`callp(jsb_string_name(_notification), argv, 1, error)` | **应删**（`7f51ac7` 判定正确） |
| `jsb_preset_source.h` PackedByteArray | ⚠️ 半对 | `60021eb` 已实现"避免内存拷贝"（持有 `decompress()` 结果，`uncompressed_data_` 现为 `PackedByteArray`）；"生成端产出 PackedByteArray"半个不适用 | **目标已达成**，注释过时 |
| `jsb_environment.cpp` `remove this` | ✅ 对 | 该文件直接用 `GodotJSScript`（9 处，`:654/675/925/931/938/2115/2286/2363/2373`） | 未完成（风格/冗余） |
| `jsb_environment.cpp` `start_debugger` 阶段 | ✅ 对 | — | 未完成 |
| `jsb_environment.cpp` `may not work in this way` | ✅ 对 | 仍被注掉 | 未完成 |
| `jsb_environment.cpp` wait_for 行尾 TODO | ✅ 对 | 现 `:1634` 带说明 | 未完成 |

**统计**：复原正确 11 / 复原错误 4 / 复原可疑 2 / 复原后已另行处置 2 / 半对 1。

---

## 3. 唯一的代码回退：jsc 异常路径保护计数泄漏

```
$ git show 69fa066 -- src/runtime/impl/jsc/jsb_jsc_isolate.cpp
-	//TODO copy or steal?
-	*exception = isolate->stack_dup(jsb::impl::StackPos::ConstructorCallError);
+	//NOTE 直接读保留槽，不要 `stack_dup`：…`stack_dup` 会再 `JSValueProtect` 一份且没有任何路径释放它
+	*exception = isolate->stack_val(jsb::impl::StackPos::ConstructorCallError);

$ git show 8b85a4b -- src/runtime/impl/jsc/jsb_jsc_isolate.cpp   # 又退回去
-	*exception = isolate->stack_val(jsb::impl::StackPos::ConstructorCallError);
+	//TODO copy or steal?
+	*exception = isolate->stack_dup(jsb::impl::StackPos::ConstructorCallError);
```

HEAD 实测：`jsb_jsc_isolate.cpp:517` 仍是 `stack_dup`；`:194` 的 `_release()` 只对 `bridge_calls_` 做
`JSValueUnprotect`，不含 `ConstructorCallError` → 每次"把构造函数当函数调用"都泄漏一个保护计数。

`8b85a4b` 的 message 写的是"only reverts the ones that were never implemented"，
但这一条是**已被实现且修了真 bug** 的，属于误伤。

---

## 4. `STATUS.md` 的失效项

### 4.1 B 类「已过时，已删」表 6 条**全部**已回到代码

| # | STATUS 记 | HEAD 实际 |
|---|---|---|
| 46 | `jsb_environment.cpp:55` 删注释 | `:56` `//TODO remove this` 在 |
| 73 | `jsb_module_resolver.cpp:97` 删 2 行 | `:98-99` ``//TODO set `require.cache` `` 在 |
| 100 | `jsb_jsc_pch.h:37` 改写为垫片说明 | `:35` `//TODO WARNING: ONLY FOR DEV…` 在 |
| 158 | `jsb_script_instance.cpp:753` 删该行 | `:824` `//TODO find the method named…` 在 |
| 94 | `jsb_jsc_handle.h:156` `JSWeak*` 已在用 | `:156` `//TODO use JSWeakRef` 在 |
| 124/125 | `monolith.ts` i64/u64 活路径 + ES2020 内置 | `:1526` `//TODO may not be supported?` 在 |

### 4.2 A 类「已完成」表两处与代码不符

| # | STATUS 记 | 实际 |
|---|---|---|
| 96 | `stack_dup`→`stack_val`，修异常路径保护计数泄漏（`69fa066`） | **已回退**，`8b85a4b` 复原为 `stack_dup`；泄漏仍在 |
| 10 | d.ts 分工「新增 `CH_GENERATED` 标记，`verify_file` 跳过生成产物」 | `CH_GENERATED` 在 HEAD 里 **0 处**；该做法已由 `96b2822` 改写——分工落在 `install_project_files()`，`verify_file()` 明确**不能**跳过（否则 `install_static_types()` 经 `filter_files()→verify_file()` 取不到文件）。目标达成，但记的实现方式是错的 |

### 4.3 未被清单覆盖的

`8b85a4b`/`5811f0b` 复原的 21 行里，**没有任何一条**出现在 `STATUS.md` 的 A/B/C/D/E 分组表里
（B 类那 6 条是被"复原覆盖"而非新登记）。即：清单当前**看不到**这批 TODO 的处置状态。

---

## 5. 建议

1. **优先**：修 `jsb_jsc_isolate.cpp:517` 的 `stack_dup` → `stack_val`（唯一真回退，jsc 腿 `continue-on-error` 掩盖了它）。
2. 删 4 条前提已被推翻的 TODO：`not tested on linux`、`require.cache`、`may not be supported?`、`_notification`（各自补一句事实性说明）。
3. 澄清 `use JSWeakRef`（注释与实现不符）；`jsb_preset_source.h` 的注释按已达成改写。
4. 更新 `STATUS.md`：B 类表整表重写；A 类 #96/#10 更正；把这 21 行纳入清单。
5. 保留待办（复原正确、确未做）：V8 cast、ClassID、Freeze/proxy、Promise「更优雅」、integer type、
   `Warp static method to Callable`、`remove this`、`start_debugger` 阶段、`may not work in this way`、
   `wait_for` 行尾、`jsc_pch` 措辞。

---

## 6. 追加核查（2026-10-12，第二轮）

### 6.1 `jsb_jsc_isolate.cpp` 回退已修

`stack_dup` → `stack_val`（`jsb_jsc_isolate.cpp:520`），并把 `69fa066` 的注释扩写为就地说明
（常驻槽 + `_release()` 统一 unprotect + 不能改用 `_GetError()` 的理由）。

**泄漏链条（实测核对）**：
- `emplace_`（`jsb_jsc_isolate.h:347-352`）**不** protect，只登记槽位；`ConstructorCallError`
  由 `jsb_jsc_isolate.cpp:175` 建立。
- `_release()`（`:204-206`）对每个保留槽各做**一次** `JSValueUnprotect`。
- `stack_dup`（`jsb_jsc_isolate.h:265-269`）= `stack_val` + 额外 `JSValueProtect`，**无配对释放**
  → 每次 `_NotAllowedCallAsFunction`（把构造函数当函数调用）泄漏一个保护计数。
- 对照：`_ThrowError`/`_GetError` 成对（`:454`/`:464` protect、`:466` unprotect），`set_stack_copy`
  也是 protect 新的 + unprotect 旧的 —— 只有 `stack_dup` 的调用点没有配对。

**验证**：`clang++ -fsyntax-only`（`.agent_tmp/jsc_args.rsp`，jsc 腿 PCH + stub 头）**rc=0**。
该 gate 的有效性用「故意注入 `int x = ;`」反证过：同一命令行 **rc=1** 并报 `expected expression`
（且必须与被测文件同目录，否则会因为 `#include "jsb_jsc_isolate.h"` 找不到而误报）。
jsc 运行期无法本机执行（只在 macOS/iOS 链接运行），CI 的 `host-jsc` 腿是 `continue-on-error`。

### 6.2 `jsb_preset_source.h:130` — 目标已达成，TODO 过时

```cpp
// TODO: 调整生成的数据，考虑直接生成 PackedByteArray，避免内存拷贝
PackedByteArray compressed;
memcpy(compressed.ptrw(), (const uint8_t *)data_, (int)data_size_);
uncompressed_data_ = compressed.decompress(uncompressed_size_, FileAccess::COMPRESSION_DEFLATE);
```

- `60021eb` 已经实现了「避免内存拷贝」的那一半：`uncompressed_data_` 由 `Vector<uint8_t>` 改为
  **直接持有** `decompress()` 的返回值（`PackedByteArray`），去掉了「解压结果再 memcpy 到第二个缓冲区」
  那一整次拷贝（见 `:47` 的类型与 `:136-139`）。
- 剩下的 `memcpy`（compressed 数据，`jsb_runtime_preset.gen.cpp:411` 给出 `23929` 字节）是**必需的**：
  生成端产出的是静态 `const char *`（`PresetSource(p_filename, (const char*)data+0, …)`），
  而 `decompress()` 的入参必须是 `PackedByteArray`。
- 「生成端直接产出 PackedByteArray」这半个不成立：`.gen.cpp` 是编译期静态数据，
  改成 PackedByteArray 也得在运行期构造一次，省不掉这一拷。

**结论**：TODO 的目标已达成，注释应当删除或改写为事实。

### 6.3 `jsb_node_bridge.cpp:42` — 剩余 3 项**全部已实现**，TODO 过时

```
/**
 * TODO: 参考 Gode
 *  3. 通过ExportPlugin确保额外的二进制文件被打包
 *  4. 添加 node 相关的测试
 *  5. 添加 CI 验证文件货配置完整性
 */
```

- 原本有 5 项；`199f1c5`（08-11）已删掉 1、2（node.dll 手动加载 + 供 fork 用的独立可执行文件）
  —— 即当时已确认这两项做完。剩下的 3/4/5 从那时起原封不动。
- **item 3**：`199f1c5` 同时引入了 `add_node_runtime_helpers`（`jsb_export_plugin.cpp:322-351`）
  + `add_node_runtime_helper_shared_object` / `stage_macos_helper_framework`，
  导出时把 `bin/<platform>/godotjs-ext(.exe)` 打进包。
- **item 4**：`7821acf` 补了 node 专属用例 —— `test_jsb_runtime_api.h:364`
  `native_probe_executable resolves the bundled fork helper`、`:460`
  `child_process.fork runs the helper instead of the host`。
- **item 5**：CI 有 node 构建腿（`ci.yml:558/564/569`）与 3 条 `host-node` 测试腿
  （`:783-794`，windows/ubuntu/macos）。

**结论**：3 项都已完成，TODO 应删。

### 6.4 `jsb_node_runtime.cpp:132` — 真未完成，但数字已过时

```cpp
// TODO: 找不到node 构建在退出进程时的 89 个 Orphan StringName 怎么处理，orz。
```

- 三态：`fd0324c`/`5ae7e2a`（08-11）引入；`7f51ac7` 只在审计表里登记（该提交**没有**改这个文件），
  至今未动。
- 实测：`.agent_tmp/dsn_node.log`、`e_A.log`（均 2026-09-23）各有 **99** 条 `Orphan StringName`
  —— 已不是 89。10 月的日志（`win_full_suite.log`/`win_final.log`/`win_dt2.log` 等最新 60 个）
  **0 条**，但那些运行不一定走到 node 的退出路径，不能据此判定已解决。

**结论**：真未完成；注释里的「89」是过时数字，且缺少「怎么复现/怎么算失败」的判据。

---

## 7. 第三轮（2026-10-12）：jsc 修复的可测性、node 三处结论修正

### 7.1 jsc 修复：**无法写测试**（不是没写，是测不到）

原计划「写测试覆盖 jsc 修复然后推 CI」，核查后确认不可行，依据如下：

1. **该路径在 HEAD 上是死代码**。`_NotAllowedCallAsFunction` 只在 `Isolate::_NewConstructor`
   里注册（`jsb_jsc_isolate.cpp:556`），而 `_NewConstructor` 在 `src/` 内**零调用点**
   （全仓 2 处命中 = 定义 + 声明；`.agent_tmp`/worktree/git 对象里的命中都是副本）。
   jsc 的 native class 走 `jsb_jsc_class_builder.h:289` 的 **JS class factory**
   （`class { constructor(...args){ return cpp_allocator(new.target, ...args) } }`），
   把构造函数当函数调用时由**引擎**抛原生 TypeError，不会进 `_NotAllowedCallAsFunction`。
2. **保护计数不可观测**。JSC 公开 API 没有 protect/refcount 查询
   （对照：quickjs 有 `QuickJS::_RefCount`，jsc 侧没有对应物）。`JSValueProtect` 的计数
   只存在于引擎内部，`JSValueIsUndefined` 之类探针读不到。
3. **跨后端断言不可行**。v8 侧同一场景走 `jsb_transpiler.h:293` 的
   `jsb_checkf(info.IsConstructCall(), ...)`，而 `jsb_checkf` = `CRASH_COND_MSG`
   （`src/internal/jsb_macros.h:61`）→ 进程 abort。所以「无 new 调用构造函数」在 v8 腿
   是崩溃而不是可断言行为，写成跨腿测试会直接把 v8 腿打挂。
4. 本机无法运行 jsc（jsc 只在 macOS/iOS 链接运行），只能靠 CI 的 `host-jsc` 腿
   （已确认为 enforcing，`continue-on-error` 只剩一个诊断步骤）。

**结论**：修复本身是「把死代码里的泄漏改掉」，正确但**无法用测试固化**。
可选处置（需用户决定）：删除死掉的 `_NewConstructor` / `_NotAllowedCallAsFunction`
（约 20 行），或保留并注明它是为将来的 native-class 路径预留。修复已由
`clang++ -fsyntax-only`（rc=0，gate 用注入语法错反证过）验证。

### 7.2 `jsb_node_bridge.cpp:42` — 我上一轮说错了

用户指出：Gode 测的是它的 **node module（原生 addon）** 功能，用 npm 原生模块
（llama.cpp 之类）的独立 fixture 工程，不是我说的「fork helper + node 内建模块」。

核实：
- 审计报告 §5.6 自己就写着 Gode 的 `test/run_npm_native_smoke.py` +
  `test/fixtures/npm_native_llama/`：**独立 fixture 工程**，fork 探针子进程。
- 本仓实测**没有任何**原生 addon 测试：`grep -rniE "\.node\b|dlopen|native module|
  node-gyp|node-addon-api|llama" src/runtime/tests/ project/tests/ scripts/test/` → 0 命中。
- 我上一轮说的「item 4 已由 `7821acf` 完成」不成立：`7821acf` 测的是 fork helper 的
  `process.execPath` 重定向 + node 内建模块（`node:path` 等），**没碰原生 addon**。

**修正后的结论**：`jsb_node_bridge.cpp` 的 TODO 里
- 「通过 ExportPlugin 确保额外二进制被打包」→ **已完成**（`199f1c5` 引入
  `add_node_runtime_helpers`，`jsb_export_plugin.cpp:322-351`）。
- 「添加 node 相关的测试」→ **部分完成**：fork helper + 内建模块有覆盖，
  **原生 addon（node module）没有**，这才是 Gode 对标的缺口。
- 「CI 验证配置文件完整性」→ **未完成**（无 `.gdextension` 库路径与产物一致性的 CI 校验）。

已按此改写 `jsb_node_bridge.cpp:41-48` 的注释（保留真缺口，删掉已完成项）。

### 7.3 `jsb_node_runtime.cpp:132` — TODO 过时，问题已修

用户指出：Orphan 问题已通过改 `GodotJS-Dependencies` 处理过，但 TODO 还在。

核实（均已实测，非复现）：
- 归档任务 `.trellis/tasks/archive/2026-09/09-22-fix-orphan-stringname-v8-node/`
  就是修这个的，其 `prd.md` 写明两个独立缺陷：
  - **缺陷 A（两腿各 2 个：`health`/`attack`）**：`jsb_script_instance.cpp` 的进程级静态池
    `temporary_property_list` 持有 `PropertyInfo::name`，活过 `StringName::cleanup()`。
  - **缺陷 B（仅 node，~99 个）**：node 侧 DLL 卸载不掉 —— 一次 `LoadLibraryExW` 需要
    **3 次** `FreeLibrary`（v8 镜像只需 1 次），引擎唯一那次调用减不到 0。
- **两者都已修**：
  - `49c0722 fix: release temporary property list pool to avoid orphan StringName`（09-23）
  - `ff8f3c5 build(deps): consume the 260925-node-final dependency release`（09-26），
    依赖仓把 node 镜像加载期多余引用消除（`Daylily-Zeleen/GodotJS-Dependencies`
    分支 `fix/libuv-console-shutdown`，release `260925-node-final`）。
  - 两者均已确认为 HEAD 的祖先（`git merge-base --is-ancestor` → YES）。
- 本机新构建（node 腿，`godotjs-ext.windows.editor.x86_64.dll` 10-12 06:47）实跑
  `--path ./project` 非 headless：**0** 条 `Orphan StringName`、无 `unclaimed` 行。
  （09-23 的 `.agent_tmp/dsn_node.log` 是**最小工程 + 非 headless** 下的 99 条，属修复前。）

**结论**：该 TODO 描述的问题**已解决**，TODO 应删（数字「89」本来也不对，实测是 99）。

---

## 8. 第四轮（2026-10-12）：归档任务复查 + 文档补充

### 8.1 复查方法

取近两周归档的任务（`archive/2026-10/*` 全部 + `archive/2026-09/` 中 09-24 之后修改的），
在任务文档里提取它**提到过的源码位置**，再逐个核对当前位置的 TODO 是否与任务的结论一致。

### 8.2 发现（1 处真过时）

| 归档任务 | 声称 | HEAD 实际 | 判定 |
|---|---|---|---|
| `09-24-script-static-members`（completed） | Q1=(c) **不做静态函数**，理由是「静态函数属于哪个隔离区无法确定、副作用不可知」；R5.2 明确 `_has_static_method()` 恒 `false` | `jsb_environment.cpp:2092` 仍是 `// TODO: 支持静态函数的调用。 static calls are not supported` | **过时**：任务是「已裁定不做」，注释却仍写「待做」。已改为 NOTE（与用户 `3656cea` 对 `_has_static_method` 的处理同型） |

### 8.3 复查为「非过时」的

| 位置 | 结论 |
|---|---|
| `jsb_resource_loader.cpp:124` | **有意保留**。`10-05-resource-loader-deps` 结论是「不实现，转已知限制」，且用户明确要求「留着 TODO，只把内容写成已知限制」。注释已含依据与核对日期（2026-10-05）。**不动** |
| `jsb_jsc_primitive.cpp:57` `no equivalent implementation` | 仍成立：jsc 确实没有 `ToDetailString` 的等价物（`09-26` 报告已记录该差异，并提示切腿需重验） |
| `jsb_script_language.h` 的 `_get_public_*` / `_add_*_global_constant` | 仍成立：`09-24` 只处理静态**成员**（常量/变量），这几个钩子是 `_get_public_*` 文档面与 autoload 暴露，任务未覆盖 |
| `jsb_environment.cpp:56` / `:385` / `:1440` / `:1634` | 仍成立：由 `8b85a4b`/`5811f0b` 复原，确未实现（见 §2） |
| 10-06 声称改写的 timer 注释 | **无法核对**：`jsb_environment.cpp` 里已无「定时器异常被吞」的注释，`jsb_timer_action.cpp:79` 是已捕获并日志的实现，与 10-06 的结论一致。未发现残留 |

### 8.4 文档站补充（用户要求）

`jsb_script.cpp:437` 的 NOTE 说明「静态变量能过、静态函数不能，因为前者在 C++ 侧是一个所有隔离区
公认的 Variant，后者无法确定属于哪个隔离区」。该说明原先只在代码里，文档站（`annotations.md`
的「静态成员暴露」一节）只说了「静态函数不支持」而没给出这个区分依据。已在
`docs/scripting/annotations.md` 与 `docs/en/scripting/annotations.md` 各补一节
「从 Godot / GDScript 一侧看」，含能力对照表与原因。`vitepress build` rc=0。

---

## 9. 第五轮（2026-10-12）：npm 原生模块（native addon）的真实状态

### 9.1 先说做法上的错

我最初**自己写了一个 `addon.c` + 构建脚本**当 fixture。这是错的：用户要的是照 Gode 做，Gode 用的是
`node-llama-cpp`。已删除。第二次又**新建了一个独立 Godot 工程**
（`scripts/test/fixtures/npm-native/`）——同样错，仓库已经有 `project/` 与
`project/tests/node-runtime/`。也删除了。正确做法是把探针放进**已有的** node 测试里。

### 9.2 顺带修掉的两个真 bug（已改，未提交）

| # | 问题 | 证据 | 修法 |
|---|---|---|---|
| 1 | `__godotjs_node_require` 只解析内建模块 | 改前 `require("papaparse")` → `Cannot find module`。bootstrap 里 `globalThis.__godotjs_node_require = function(id){ return require(id) }` 的 `require` 在**调用时**已解析成 GodotJS 的桥接 require（`Builtins::_require`），不是 node 的 | 改用 `Module.createRequire(...)`。真 node 实测：`papaparse OK`、`node:path OK` |
| 2 | node 已解析的模块仍被 GodotJS resolver 再编译 | `.node` 二进制 → **崩溃**（`jsb_environment.cpp:1409` 的 `resolver->load`）；bare specifier → `module compile failed` | node 返回 exports 后直接采用，不再走 resolver |

### 9.3 修到的真缺陷：node.dll shim 缺 N-API 入口点（已修）

`misc/build/generate_node_shim.py` 用正则从 libnode 头文件抓 `napi_xxx(` 声明。但 addon 的
**ABI 入口点**是宏拼接出来的（`node_api.h:56`：`NAPI_MODULE_INITIALIZER_BASE napi_register_module_v`
`## NAPI_MODULE_VERSION`），任何头文件里都不会以 `napi_register_module_v1(` 的形式出现，
所以**从未被转发**（node.def 145 → 164 符号）。node 靠 `napi_register_module_v1` 初始化 addon。

修法：按头文件同样的方式推导（读 `NAPI_MODULE_VERSION`，拼出
`napi_register_module_v1` 与 `node_api_module_get_api_version_v1`），版本升级也不会失配。

### 9.4 最终阻塞：`llama-addon.node` **延迟加载 `NODE.EXE`**

逐层剥到根因（全部实测，非推断）：

```
llama-addon.node
  PE 导入表   : llama-common.dll / llama.b10068.dll / ggml.b10068.dll / ggml-base.dll / KERNEL32 / MSVC 运行库
  PE 延迟导入 : NODE.EXE        ← 69 个符号，全是 napi_*
```

- 普通导入的那 4 个 llama DLL 都在包内，**不是**问题。
- `NODE.EXE` 是**延迟加载**，所以普通导入表里看不到它（这也是我前面误判成 `node.dll` 的原因）。
- 系统 `node.exe` 能满足它（同名可执行文件已加载 → 延迟加载直接命中其导出），
  所以**在系统 node 里 `getLlama({dryRun:true})` 成功**（实测 `llama._bindings` 有 Addon* 类，
  即 addon 确实被加载了）。
- Godot（`godot.windows.editor.x86_64.exe`）与我们的 helper（`godotjs-ext.exe`）**名字不同、
  也没有导出 `napi_*`**，延迟加载解析失败 → `0xC06D007F`（ERROR_PROC_NOT_FOUND）。

试过的修法：在 addon 目录放一个名为 `NODE.EXE` 的转发器（转发到主 DLL）。**失败**，
仍是 `0xC06D007F` —— 说明延迟加载器要的是"已加载模块里名为 NODE.EXE 的那个"，而不是同目录的一个文件。

**结论**：要支持这类 addon，需要让宿主进程的某个**已加载模块**以 `NODE.EXE` 为名导出 `napi_*`
（等价于 node 的 `--force-node-api-uncaught-exceptions-policy` 那类宿主契约），或在独立 helper 里
把该模块按 `NODE.EXE` 加载。这超出本轮范围，需要单独任务。

### 9.5 因此本轮**没有**落地 native addon 测试

一个必然失败的测试比一个写明缺口的注释更糟，所以 `project/tests/node-runtime/` 未加探针；
`project/package.json` 的 `node-llama-cpp` 依赖也已还原。

`jsb_node_bridge.cpp:42` 的 TODO 已按**实测结论**改写：原生 addon（node module）功能**确实没有覆盖**，
且现在写明了它卡在哪（延迟加载 `NODE.EXE`），而不是笼统写「没有测试」。

---

## 10. 更正（2026-10-12）：§9.2 的两处改动已回退

用户质询「你改 node 的 bootstrap 干什么」。核对后确认这两处**没有真实消费者**，是我为了让
测不成的 fixture 能跑而引入的投机改动，已全部回退：

| 改动 | 为什么不该留 |
|---|---|
| `jsb_node_runtime.cpp`：`__godotjs_node_require` 改 `Module.createRequire` | 该 helper 只服务于 `NodeRuntime::NodeRequire`，而 `NodeRequire` 是 `Builtins::_require` 的 node 兜底 —— **内建模块本来就走得通**（现有测试 `require("node:path")` 在改动前就通过）。「包/原生 addon 加载」从未接线，只被我那个 fixture 需要。没有消费者就不该改 |
| `jsb_environment.cpp`：删掉 node 分支的 `resolver->load` | 同因：那段是既有设计，其行为在现有测试里已正确。只在 `.node`/裸包名场景下才暴露，而那个场景本轮没做成 |

**保留的**（有独立依据，与 fixture 无关）：

- `misc/build/generate_node_shim.py` 补 N-API ABI 入口点 —— 这是**实测确认的缺口**：
  `node.dll` 转发器从未导出 `napi_register_module_v1`（正则抓不到宏拼接出来的符号），
  node.def 145 → 164。任何 N-API addon 都会因此拿不到入口点。
- `jsb_node_bridge.cpp:42` 的 TODO 按实测改写（写清卡在延迟加载 `NODE.EXE`）。
- `jsb_environment.cpp:2092` 的 TODO → NOTE（与用户 `3656cea` 同型）。
- jsc 死代码注释 + `stack_dup`→`stack_val`。
- 文档站静态成员说明。

**回归**：重编 rc=0；doctest **93/93、1209/1209**。

**教训**：为了让一个自己臆造的测试跑起来而改生产代码，是反的。应当先确认「这个能力有没有真实
消费者/需求」，没有就不改；改动被回退说明我在 §9.2 把"能让 fixture 通过"当成了"修 bug"。

---

## 11. 更正 + 真正修好：原生 addon 是需求，且已能加载（2026-10-12）

### 11.1 我前两轮的判断都错了

- §9.2 说"包加载从未接线、没有消费者" → **错**。用户明确要原生 addon。
- §10 回退了 `__godotjs_node_require` 与 `jsb_environment.cpp` 的改动 → **回退得对**（见 11.4），
  但结论"不需要原生 addon"是错的。
- §9.3 说 `node.dll` 缺 `napi_register_module_v1` 是"真实缺口" → **也是错的**（见 11.3）。

### 11.2 真正的根因（逐层实测）

用一个最简单的原生 addon（`@reflink/reflink-win32-x64-msvc`，Rust + `napi-sys`）复现：

```
Node-API symbol has not been loaded
  at napi-sys-2.4.0/src/functions.rs:7
```

PE 表实测：该 addon **不导入任何 node 模块**，也**没有延迟导入**。它的 `napi-sys`
用 `GetProcAddress(GetModuleHandle(NULL), ...)` —— 在**宿主可执行文件**上取符号。

| 宿主 | 导出 napi_* | 结果 |
|---|---|---|
| 系统 `node.exe` | **161** | ✅ |
| `godot.windows.editor.x86_64.exe` | 0 | ❌ |
| `godotjs-ext.exe`（我们的 fork helper） | **0**（修复前） | ❌ |

即：**addon 找的是宿主 exe 的导出表**，而我们的 helper exe 什么都不导出。

### 11.3 修法（已落地，实测通过）

让 `godotjs-ext.exe` 导出那 161 个 N-API 符号。**成本为零**：helper 本来就通过 import library
引用主库的 `napi_*`，exe 里已有这些符号，只差一个导出表。

- `SConstruct`：新增生成 `bin/<platform>/node-exe.def`（**只写符号名，不写 `=target.symbol` 转发**），
  在 helper 的 MSVC 链接分支加 `/def:`。exe 静态链接主库，符号是"自己"的，所以不需要转发。
- **`napi_register_module_v1` 故意不列入**：那是 **addon 自己定义**的符号，`node.exe` 也不导出它
  （实测 node.exe 的 161 个里没有）；列入会导致 `LNK2001`。

实测结果：

```
reflink addon LOADED: object reflinkFile,reflinkFileSync,ReflinkError,AsyncReflink
llama  addon LOADED: object AddonGgufMetadata,AddonModel,AddonModelLora
```

Godot 内跑测试：`[node-runtime] native addon OK (object)`。

### 11.4 §9.2/§9.3 的改动保持回退（它们不是修法）

| 改动 | 为何仍应回退 |
|---|---|
| `__godotjs_node_require` → `Module.createRequire` | 探针改用**绝对 OS 路径 + `require`**，不需要它；且它没有别的消费者。改了就是多余面 |
| 删 `jsb_environment.cpp` 的 `resolver->load` | 同因；不动既有设计 |
| `generate_node_shim.py` 加 ABI 入口点 | **前提是错的**：主库**不导出** `napi_register_module_v1`（实测 162 个 napi 符号里没有），转发它必然 `LNK2001`。真实 `node.exe` 也不导出。已回退（162 符号 = 原状） |

### 11.5 测试落点

放进**已有的** `project/tests/node-runtime/`（不再新建工程）：
- `native-addon-probe.cjs`：在 forked helper 里 `require` llama 的 `llama-addon.node`，结果写文件
  （fork 重定向不保证 IPC 可用，文件也更便于手工诊断）。
- `test-node-runtime.ts`：`_testNativeAddon()` + `forkNativeAddonProbe()`，断言 `ok`。
- `project/package.json`：加 `node-llama-cpp@3.19.1`（与 Gode 同版本）。

**回归**：重编 rc=0；doctest **93/93、1209/1209**；`Orphan StringName`=0。

