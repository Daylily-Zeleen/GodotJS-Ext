# 函数与信号签名解析

> 父任务：`09-24-script-static-members`。**硬约束（用户当轮原话）**：「绝对不要写注解去反射」
> —— 不允许要求作者再写一遍参数/返回值类型。
> 目标：让方法参数名/类型/可选性/默认值/剩余参数、返回值类型、信号参数类型可从 TS 源提取，
> 由运行时消费，使 inspector / 远程调试器 / GDScript 分析器看到真实签名。

## Background

### 决定性事实：类型只存在于 `.ts`

| 事实 | 证据 |
|---|---|
| 编译产物 `.js` 里类型全被擦除，参数名/默认值/剩余参数保留 | `research/signature-approach.md` §1.1 |
| 导出只打包 `.js`，`.ts` 被显式跳过 | `jsb_export_plugin.cpp:271-277` |
| 运行时可见文本只有 `Function.prototype.toString()` 与 `Function.length` | §1.2 |
| `Function.length` 是**下界**不是个数（`(a, b = 2, ...rest)` ⇒ 1） | §1.2 实测 |
| 信号参数类型只写在 `Signal<...>` 类型实参里，运行时不可见 | §1.6 |
| TS Compiler API 给出精确签名且 `optional` / `default` **分列** | §1.5 |
| `typescript@6.0.3` 已是硬 devDependency | `project/package.json` |

⇒ 运行时解析源码（tree-sitter / 正则）在导出形态下**拿不到类型**（输入不存在），
方案必须是**编辑器侧提取 + sidecar 清单**。

### 已否决的路线

- **正则**：具名脆弱点 7 条（嵌套箭头函数/泛型逗号/解构/注释逗号/字符串逗号…），
  写对即等于自研 mini-parser（§2）。
- **tree-sitter**：解析的是运行期可见文本，导出形态无 `.ts` ⇒ 同样拿不到类型；
  唯一真实收益（替换 4 处类名提取正则）与本目标无关（§3）。
- **读 `--declaration` 产物**：`.d.ts` 把「有默认值」合并成「可选」（`b = "z"` → `b?`），丢信息；
  且需多跑一次 tsc、多一棵产物树（§4.3）。

## Requirements

### R1 签名清单 sidecar（方案）

- R1.1 编辑器侧用 TS Compiler API 提取类/方法/信号签名，落清单文件。
- R1.2 清单位置与生命周期**与 `.paths_mapping` 同型**：编辑器生成 → 与编译产物同目录
  （`res://.godot/godotjs_ext/`）→ 运行时加载 → 导出打包 → 缺失时静默降级。
- R1.3 触发时机沿用 `.paths_mapping` 节奏（窗口获焦 / 编辑器就绪 / 安装完成），
  每处由 MD5 空操作门控（**用户决策**，`research/signature-approach.md` §5.3）。
- R1.4 清单缺失（纯 JS 项目 / 未跑提取器）时降级：类型留空、参数名与个数仍由函数源文本提供。
- R1.5 类型字符串原样保留 TS 写法；TS→Variant 映射由消费端负责，且**必须查已知别名表**
  （`int32`/`uint64`/`float64`/`byte`/`StringName` 经 Compiler API 得到的是**别名名本身**，
  `aliasSymbol.declarations` 为空 —— §5.1）。
- R1.6 重载：清单**显式记录全部声明**（TS emit 会擦除重载，不可从产物反推）；
  运行时行为对齐 C# 先例 —— `get_script_method_list` 列全部条目、
  `get_method_info` 遇同名第二条退化为空 `MethodInfo()`、参数个数取首个匹配（§5.2）。

### R2 参数个数（Q4，**本轮已实现**）

- R2.1 语义对齐 GDScript：计**全部已声明参数**，**剩余参数不计**
  （`GDScriptFunction::_argument_count` + `_vararg_index`；`Callable::get_argument_count()`
  文档为 *including optional arguments*）。
- R2.2 **不得使用 `Function.length`**（下界）；也**不得**依赖 `v8::Function::Length()`
  （jsc/quickjs/web 三腿的 shim 只在 `FunctionCallbackInfo` 上有它）。
  输入取 `impl::Helper::to_string_without_side_effect`（`Function.prototype.toString` 语义、无副作用），
  解析期扫描形参表。
- R2.3 消费端两层，对齐 GDScript：`Script` 层**只报自有**方法；
  实例层**自己走 `base` 链**（`Object::get_method_argument_count` 先问实例且不走链）。
- R2.4 未知 / 不可判定**必须报 invalid**，不得报 0：
  `_get_script_method_argument_count` 返回**空 `Variant`**（否则
  `ScriptExtension` 的 INT 分支会认下，`get_method_info` 回退路径永不生效）；
  实例层 miss 时置 `*r_is_valid = false`（否则遮蔽 ClassDB 第二腿）。
- R2.5 已知盲点（best effort，注释已具名）：正则字面量中的不平衡括号、
  `${}` 内嵌反引号的模板字面量、V8 `NoSideEffectsToString` 在 >128 字符时截断到前 111 字符
  ⇒ 形参表无法闭合时返回 `k_ArgumentCountUnknown`（-1）。

### R3 不做（明确边界）

- R3.1 **不填 `_get_method_info()` 的 `args`**：那会让
  `GDScriptAnalyzer::validate_call_arg` 把每个参数当**必填**（`PropertyInfo` 一律
  `usage = PROPERTY_USAGE_DEFAULT`、无默认值扣除）⇒ 对外来脚本产生**新的 GDScript 解析错误**。
  `_get_script_method_argument_count` 是唯一通道。
- R3.2 **不引入 tree-sitter / 自研解析器**（§3）。
- R3.3 **不写注解**（用户约束）。

## Acceptance Criteria

- [x] **A1** `get_method_argument_count` 对真实多参方法返回**声明参数个数**（剩余参数不计），
      而非 `Function.length` 的下界 —— 夹具 `add(a: number, b = 2, ...rest: number[])`：
      `length` 为 1、声明数为 2，断言期望 **2**。
- [x] **A2** 实例层走 `base` 链：`D.new().get_method_argument_count("add")` 在 `add` 仅由基类声明时
      仍得 2（`static-members-gdcheck.gd`）。
- [x] **A3** `Script` 层只报自有：派生脚本的 `_get_script_method_argument_count("add")`
      **不是** INT（C++ 用例）。
- [x] **A4** 未知名字**必须报 invalid**：`Script` 层返回空 `Variant`（C++ 用例断言
      `get_type() != Variant::INT`）；实例层 miss 后落到 ClassDB 第二腿
      （GDScript 侧断言 `has_method` 得 1）。
- [x] **A5** 负向控制（两条，均实测）：
      ① 扫描器恒返 0 ⇒ C++ 用例 `defaults_and_rest == 2` FAIL（`TESTS_RC=1`、`54 passed | 1 failed`）；
      ② 实例层链走断 ⇒ GDScript 侧 `inherited method argument count: expected 2, got 0`、
      `GODOTJS_TEST_PROJECT_FAILED=1`。两条均还原后复验绿。
- [x] **A6** 验收判据（`.trellis/spec/godotjs-ext/test/index.md`）：
      Orphan StringName = 0、`GODOTJS_TEST_PROJECT_COMPLETED` = 1、`GODOTJS_TEST_PROJECT_FAILED` = 0。
- [x] **A7** `misc/verify_codegen.py` 全量校验通过（RC=0，产物与基线一致）。
- [x] **A8** 签名清单端到端：提取器 + 清单位置 + 运行时读取 + 导出打包 + 降级路径
      —— **已实现并实测**：提取器（TS Compiler API，产出 `<outDir>/<rel>.sig`）→ 编辑器 spawn
      接线（4 个触发点 + 门控摘要）→ 运行时懒加载读取器（`jsb_signature.{h,cpp}`）→
      `MethodInfo` 填充 → 导出打包（`jsb_export_plugin.cpp` 的 `export_raw_file` 分支）。
      降级路径经**负向控制**验证：移走全部 18 份 sidecar 后全量验收仍绿（回退到函数源文本扫描）。
      **边界**：导出打包**只做了推导规则反查**，未端到端实测（本机无导出模板，
      `%APPDATA%/Godot/export_templates` 为空）。
- [x] **A9** 参数名/类型、返回值类型、信号参数类型进入 inspector / 调试器 / GDScript 分析器
      —— **已实测**（第五轮 GDScript 打印，见 `report.md`）：
      `get_script_method_list()` 打出 `add(a: FLOAT, b: FLOAT) -> FLOAT  default_args=1  flags=NORMAL|VARARG`；
      `get_script_signal_list()` 打出 `test_signal(value: FLOAT)`。
      这三个 API 正是 inspector / 远程调试器 / 分析器所读的同一批 `Script` 反射钩子。
      分析期 arity 效果已由第六轮判决性证据确认（`add()` 报 `Expected at least 1`
      = arguments 2 − default_count 1，即分析器确实读了 `default_arguments`）。

## Notes

- 本任务已落地 **R2（参数个数）+ 签名清单（提取器 / 读取器 / 运行时消费 / 导出打包）**。
  R2 的裁决与早期取证见 `research/signature-approach.md`；签名部分全部取证见 `report.md`
  （第一轮提取器、第二轮接线、第三轮消费者、第四轮生成收紧、第五轮 GDScript 打印、第六轮 arity 判决）。
- **遗留**：导出打包未端到端实测；跨文件继承链不作 Godot 性判定（判据刻意宽松 ⇒ 不丢签名）；
  jsc / quickjs / web 三腿未实测。
- 关联：`09-06-lowprio-tree-sitter-ast`（已评估，结论不引入；本任务不依赖它）。
- 进度与实施证据见 `report.md`。