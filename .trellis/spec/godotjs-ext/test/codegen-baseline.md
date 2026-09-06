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

前置：扩展 DLL 已构建并安装到 addon（先 scons）；测试项目 TS 已编译（`cd project && node_modules/.bin/tsc --noCheck`）；确认当前 codegen 逻辑**尚未**包含待验证的修改。

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
4. <godot> --headless --editor --path ./project --generate-types                 # 消费 json → gen/ + typings/
```

- **不要改回"移动式"暂存**——api-generate 找不到输入会静默产出空 store，下游报 `godot class not found 'Node'`，看似代码回归实为管线事故。正确做法：dump 后**复制备份** → api-generate 消费原件 → generate-types 前还原
- **GodotJSEditorHelper 必须 `GDREGISTER_CLASS`（exposed）注册**：`--generate-types` 的场景/资源生成在 JS 运行时经 api store 解析该类；internal 注册的类不进 extension_api.json（8-22+ 引擎起严格跳过 unexposed 类），缺失即抛 `godot class not found 'GodotJSEditorHelper'`。exposed 不会污染产物——生成器由 `NamingUtil::get_omitted_original_classes()` 硬编码过滤
- 前置条件：测试项目 TS 已编译，否则场景/资源 `.gen.ts` 退化成裸 Node 类型且日志报 `xxx.js is missing`
- 长跑必须后台（generate-types 全量可超 5 分钟，前台超时会杀进程留下半成品）；脚本每步超时 900s
- 脚本容忍引擎退出崩溃码（0xC0000005），但配合产物存在性检查——产物没落盘仍判失败

## 退出码不可信（已知引擎问题）

headless 任务完成后编辑器在退出阶段崩溃（0xC0000005 / bash 报 139）是已知问题。判成败用**产物存在性**：`project/extension_api.json` >1MB、`project/.godot/.api_dumping/header.capi`、`project/gen/**/*.ts` 非空。

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
