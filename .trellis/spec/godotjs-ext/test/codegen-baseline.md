# 代码生成基线校验规范

> 适用范围：**编辑器运行期 TS 代码生成**（`--generate-types` 产 `project/gen/`、`project/typings/`）的基线校验。
> 与构建流程的代码生成（SCons 期 `static_binding_codegen.py` 等产出 `src/static_binding/gen/*`）无关。
> 工具：`misc/verify_codegen.py`（入库）；基线：`<检出根>/.codegen-baseline/`（gitignore 本地自持，不入库）。

## 基线与脚本

- 校验脚本 `misc/verify_codegen.py` 与基线数据解耦：脚本入库，基线（`gen/`、`typings/`、`tsconfig.json`）放 `<检出根>/.codegen-baseline/`（gitignore；本地数据，不入库、不跨检出自动同步）
- 脚本用「当前检出根」定位基线（`JSB_CHECKOUT_ROOT` 可覆盖），不硬编码其他检出的路径

## 何时需要刷新基线（快照时机）

基线 = 「当前生成器 + 当前引擎 + 当前测试项目」在**改动前**的确定性输出。以下时点必须先固化基线再动手：

1. **修改编辑器代码生成相关逻辑之前**（`src/editor/codegen/`、类型生成、资源声明生成等）——这是基线存在的意义：改动后跑同一触发链，diff 应只剩你预期中的变化
2. 引擎版本升级、测试项目 TS 结构变化之后（输入漂移落地为新基线）
3. 首次在新检出建立基线（该检出从未有过基线）

反过来说：**不要在改动 codegen 逻辑之后才补基线**——那时产物已含改动，无法区分"你改的"和"意外的"。

## 固化基线的分步规程

前置：扩展 DLL 已构建并安装到 addon（先 scons）；测试项目 TS 已编译（`cd project && pnpm gen:types && node_modules/.bin/tsc`，不用 `--noCheck`）；确认当前 codegen 逻辑**尚未**包含待验证的修改。

```bash
# 步骤 1（可选但推荐）：确认现状干净——与现有基线 diff 应全绿或差异全部可归因为输入漂移
python misc/verify_codegen.py --godot <引擎路径> --diff-only

# 步骤 2：跑触发链并把产物快照为新基线（脚本内部：清理 → dump → api-generate → 放回 json → generate-types → 快照 → 自校验）
python misc/verify_codegen.py --godot <引擎路径> --update-baseline

# 步骤 3：双轮确定性验证——立刻重跑一次全流程（不 update），应全绿
python misc/verify_codegen.py --godot <引擎路径>
```

- `--update-baseline` 自带快照后自校验（快照 vs 现场 diff 全绿），但**第 3 步不可省**：它重新清理并再生，证明"从干净状态出发能确定性重生出基线"——这是"基线可信"的唯一授权来源
- 双轮严格做法是交叉比对两轮产物（轮 1 快照 vs 轮 2 快照）；同检出内轮 2 全绿即满足日常标准，跨检出/刷大基线时补交叉比对

## 改动 codegen 逻辑后的校验流程

1. 改 `src/editor/codegen/`（或相关生成逻辑）→ 构建（scons，dll 自动装到 addon）→ 编译 TS
2. `python misc/verify_codegen.py --godot <引擎路径>` 跑全流程
3. **把每个 diff 归因**：你的改动预期内 → 通过；预期外 → 排查（回归或副作用）；全部归因后如需接受新输出，重跑 `--update-baseline`

## headless 触发链（顺序敏感，脚本已固化，手工跑时按此序）

```text
0. （仅当 `project/.godot` 不存在时）先首次导入：`godot --headless --editor --path ./project --import`
1. <godot> --headless --editor --path ./project --dump-extension-api-with-docs   # 产 extension_api.json
2. <godot> --headless --editor --path ./project --godotjs-api-generate extension_api.json  # 建 api store，【消费删除】json
3. （放回第 1 步的 json 备份）
4. <godot> --headless --editor --path ./project --godotjs-install-project-files   # 装预设文件（.cjs 工具 + tsconfig + npm install）
5. cd project && node node_modules/typescript/bin/tsc                             # 不加 --noCheck
6. <godot> --headless --editor --path ./project --generate-types                 # 消费 json → gen/ + typings/
```

- **第 4 步的开关是 `--godotjs-install-project-files`**：菜单项走 `try_install_project_files()`，
  它在 GUI 下弹确认框、在 headless 下**只打日志并跳过**（`DisplayServer::get_name() == "headless"`
  分支），所以纯命令行流程永远装不上 `.godot/*.cjs` 工具脚本。该开关以 `force = true`
  触发安装，绕过对话框。判成败用产物存在性：`project/.godot/jsb.*.cjs` 共 5 个、且 `node_modules/typescript` 已落盘
  （2026-10-23 定稿：`node_modules` 由命令行入口 `_install_project_files_from_cmdline()` 在
  `try_install_project_files()` **之前**用 `OS::execute()` **同步**装好；`ensure_tsc_installed()`
  保持 HEAD 的 fire-and-forget 语义、且 headless 下不起进程——两处同时起 npm 会并发写同一份 `node_modules`。
  该步之后紧接的 `generate_types()` 会起工具进程 `require('typescript')`，所以等待必须在它之前）
- **第 5 步必须早于第 6 步**：见「TS 编译要排在 generate-types 之前」
- **不要改回"移动式"暂存**——api-generate 找不到输入会静默产出空 store，下游报 `godot class not found 'Node'`，看似代码回归实为管线事故。正确做法：dump 后**复制备份** → api-generate 消费原件 → generate-types 前还原
- **GodotJSEditorHelper 必须 `GDREGISTER_CLASS`（exposed）注册**：`--generate-types` 的场景/资源生成在 JS 运行时经 api store 解析该类；internal 注册的类不进 extension_api.json（8-22+ 引擎起严格跳过 unexposed 类），缺失即抛 `godot class not found 'GodotJSEditorHelper'`。exposed 不会污染产物——生成器由 `NamingUtil::get_omitted_original_classes()` 硬编码过滤
- 前置条件：测试项目 TS 已编译，否则场景/资源 `.gen.ts` 退化成裸 Node 类型且日志报 `xxx.js is missing`
- 长跑必须后台（generate-types 全量可超 5 分钟，前台超时会杀进程留下半成品）；脚本每步超时 900s
- 脚本容忍引擎退出崩溃码（0xC0000005），但配合产物存在性检查——产物没落盘仍判失败

## 退出码与崩溃（2026-10-18 实测更新）

**只剩 `--dump-extension-api-with-docs` 会崩，且是引擎固有**：全新空项目（只有 `project.godot`，
连 `addons/` 都没有）跑它 **3/3 崩**（0xC0000005），先 `--import` 也一样。同命令去掉 `-with-docs`
（`--dump-extension-api`）**3/3 exit 0**，但产物只有 6.9MB（无文档），typings 的 JSDoc 依赖文档，
不能替代。

`--godotjs-api-generate` / `--godotjs-install-project-files` / `--generate-types` 三个开关
**都是 exit 0**；前提是退出期那个引擎固有崩溃已经按下面「已知：headless CLI 退出期的
`exit(0xC0000005)`」一节的取舍处理过。

判 dump 成败仍用产物存在性：`project/extension_api.json` >11MB（with-docs）、
`project/.godot/.api_dumping/header.capi`、`project/gen/**/*.ts` 非空。

## 已知：headless CLI 退出期的 `exit(0xC0000005)` 是引擎 cleanup 顺序问题

**症状**：`--generate-types` / `--godotjs-install-project-files` 完成后进程以 `0xC0000005` 结束
（产物其实已写好）。`--dump-extension-api-with-docs` 同样崩，且**全新空项目、不装任何插件
也崩**（3/3），那一档与扩展无关。

**实测机制**（cdb first-chance AV 现场 + `llvm-symbolizer` 符号化）：

1. `Main::cleanup()` 的 `GDExtensionManager::deinitialize_extensions()` 会 **FreeLibrary 卸载扩展 DLL**
2. **卸载之后**，引擎仍在销毁由本扩展创建的对象
3. 销毁要调用挂在对象上的 godot-cpp binding free callback（实测崩点
   `<Unloaded_~godotjs-ext...>+0x1562560` = `godot::Resource::_gde_binding_free_callback`，
   `third/godot-cpp/gen/include/godot_cpp/classes/resource.hpp:50`），而那段代码**已随 DLL 消失**
4. 该 AV 写坏随机堆内存 —— 实测把 `OS::_exit_code` 覆盖成 `0xC0000005`，于是 `main` 返回它，
   进程 `exit(0xC0000005)`

**四个候选规避手段的代价（全部实测 ≥3 次，没有一条同时满足无崩溃 + orphan==0）**：

| 手段 | 崩溃 | `Orphan StringName` |
|---|---|---|
| 原样 | ✗ | 0 |
| `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN)` 钉住 DLL | ✓ | **100** |
| `_exit()` 立即终止（跳过 cleanup） | ✓ | 跳过清理 |
| 删掉 `GlobalInitialize::shutdown()` 里的 `platform.reset()` | ✓ | **100** |

- orphan 侧的机制：不卸载 DLL 时，V8 平台的工作线程持有该 DLL 的引用，`FreeLibrary` 引用计数
  不归零 → 卸载被推迟到进程终止 → 引擎在 `StringName::cleanup()` 时它仍映射 → 其 godot-cpp
  类名全被判 orphan（与 libuv/`LdrAddRefDll` 那条同一机制）

**判定**：这是 Godot 4.7.2 引擎的 cleanup 顺序问题，**在扩展代码内无法两全** ——
要"无崩溃"就得接受 orphan 或跳过 cleanup。改动前请先与维护者确认取舍。

**排查提示（避免重走弯路）**：
- 别用 ctypes 自写调试器：它抓到的"异常"是 `ntdll!LdrpDoDebuggerBreak`（**进程启动断点**），
  不是崩溃现场；据此得到的"模块只剩 13 个"等结论全是假的
- `Main::cleanup()` 开头会 `CoreGlobals::print_ready = false`，**之后所有 `print`/日志都被吞** ——
  不能用"日志没输出"判断"函数没被调用"（曾据此误判 `_finish()` 未执行）
- cdb 用法：`cdb -o -c "sxi 80000003; sxe av; g; g; g; r; kb 20; q" <exe> <args>`
  （`sxi 80000003` 必须先忽略启动断点；前两个 AV 常是 node 线程的正常等待，要 `g` 跳过）
- 符号化我们 dll 的地址：`llvm-symbolizer -e <dll> -f -C <VA>`，**VA = ImageBase
  `0x180000000` + RVA**（传 RVA 会静默失败，返回 `??:0:0`）

## api store 未生成时的上报口径（2026-10-23 定稿）

首次生成（新项目或刚清理过）时 store 还不存在，`--godotjs-api-generate` 本身正是来创建它的。
`src/api_tool/api_tool.cpp::report_loader_unavailable()` 是唯一的上报点：

| 情形 | 级别 | 频次 |
|---|---|---|
| store 尚未生成（`!ApiLoader::has_generated_data()`） | `Warning` | **每进程一次**（实测冷轮一次 `--import` 会调访问器 **1049** 次，每次一报等于把日志淹掉） |
| store 存在却读不出 / 未 `initialize()` | `Error` | 每次 |

- **禁止无消息返回**：访问器失败必须 `report_loader_unavailable()` 出一条分级消息。曾把
  `CHECK_LOADER(_V)` 改成 `if (get_loaded_loader() == nullptr) return;`（完全无消息），
  被用户判为「掏空实现」并要求改回——降级是「级别」的事，不是「有没有消息」的事。
- `get_loader()` 允许在 `finalize()`（`memdelete(ApiLoader::get_singleton())`）之后**重建**实例，
  并且**构造期不读 store**（`initialize()` 会立刻 `read_header()`，冷启动必然失败）。
- 验收证据：冷轮 step2 / step3-apigen 日志里该 Warning **各 1 条**、`Please call api_tool::initialize() first.` **0 条**；
  热轮两者均 0 条；step5 / step6 `ERROR: 0`。

## TS 增量缓存陷阱

`.godot/.tsbuildinfo` 只看源码哈希、不检查输出文件是否存在：产物 `.godot/godotjs_ext/tests/*.js` 被删后 `tsc` 不会补发（静默缺产物、引擎报 js file is missing）——先删 `.godot/.tsbuildinfo` 再重编。改 TS 后务必确认产物 mtime/内容已更新。

## 校验方法论（核心原则）

文件级 diff 掩盖真相：diff 的主要来源通常是**输入漂移**（引擎 API 变了、extension_api.json 不在项目根）、**布局抖动**（分片重划）或**测试项目内容变化**（新增/删除 TS 文件），不是生成器回归。任务是**把每个 diff 归因到具体原因**，无法归因的 diff 就是未验证的断言。

1. **忠实复现生成管线**：再生成必须消费与基线产出时相同输入、相同顺序、相同副作用（`extension_api.json.gen.ts` 的有无取决于项目根当时是否有该 json——脚本已固化备份/放回）
2. **双轮确定性验证**：`清理 → 再生 → diff` 跑两轮，交叉对比两轮产物；100% 一致 → 流程确定性成立（生成器零回归结论的唯一授权来源）
3. **Diff 卫生**：内容比较前先归一行尾（CRLF→LF）；空 unified diff 不等于相等证明；已知"退出即崩但产物完整"时按产物存在性判断成败
4. **分片输出时文件级 diff 会说谎**：分片边界重排后同名文件内容大半是不同实体——改用语义级比较（按实体名抽块、剥离注释、比成员签名集合）
5. **逐项归因**：对每个变更符号核对生成当轮的原始输入（extension_api.json）；输入里存在 ⇒ 输入漂移
6. **用证据报告**：精确计数、具名示例、溯源证据；主动给归因表
7. **纯重构用「同输入 A/B 差分」，别把过期基线当绝对判据**：基线快照会因**项目新增测试文件**
   （2026-10-23 实测：`tests/instance-hooks/*`、`tests/notification/*`、`icons/GodotJSScript.svg`
   在基线里没有 → 报成 `[gen] 多余`）与**引擎文档文本变化**（同一方法 doc 措辞不同，单分片 1274 行）而过期。
   判定「重构是否改了产物」的正确做法：按 [../build/index.md](../build/index.md) 的单文件粒度规程，
   把改动文件备份到 `.agent_tmp/`、`git show HEAD:<path>` 换回旧实现、重建，
   **同一输入**（同 store、同 `extension_api.json`、同已编译 TS）下跑 `--generate-types`，
   两轮产物 `diff -r -q --strip-trailing-cr` 必须零差异；再换回新实现重建、确认产物同样一致。
   2026-10-23 用此法证明 TypeDB 值语义化对产物零影响（而对照基线会有 29 处输入漂移差异）。

## TS 编译要排在 generate-types 之前（干净 project 的硬顺序）

从干净 project 出发时，第 5 步（tsc）必须早于第 6 步（generate-types）。反序会同时炸出两类报错，
且都不是 codegen 回归：

1. **场景/资源 d.ts 退化**：`--generate-types` 解析挂脚本节点时要加载该脚本的编译产物 `.js`，
   产物缺失 → `the javascript file is missing: ... please ensure that all typescript source files
   have already been compiled`（每个脚本 1～3 条）
2. **GDScript 侧连带失败**：引用 TS 全局类静态成员的 `.gd` 文件解析不到成员，报
   `SCRIPT ERROR: Parse Error: Cannot find member "X" in base "res://....ts"`

判别：这两类报错在 tsc 跑完后重跑 generate-types 即消失 → 顺序问题，不是生成器缺陷。

## 非 TS 脚本不得送进 JS codegen

`_request_codegen()`（`src/editor/codegen/jsb_codegen_scene_descriptors.cpp`）只接受 `.ts`。
场景/资源节点可以挂别的语言的脚本（测试项目里有 GDScript 的 `.gd`），把那种路径喂给
`require()` 会让模块解析器把 GDScript 源码当 JS 编译，报
`SyntaxError: Unexpected token 'extends'` + `Codegen failed for script '...gd' (error 36)`。
非 TS 脚本按构造就没有 `codegen()` 钩子，直接跳过、回落通用描述符即可。
