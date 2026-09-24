# 补 uint64/bigint 静态绑定 codegen

> 来源：`.本地文档/低优先级.md` bigint/uint64 条目（P3，类型映射评估）。
> **2026-09-24 修订**：调研完成（含实机实测），本任务从「评估或记录限制」升级为「实施修复」，
> 拆出 4 个子任务。共享技术方案见本目录 `design.md`。

## Goal

让 64 位整数（int64 / uint64）在 JS 与 Godot 之间**不再静默丢信息**，**参数传入永不抛异常**，
并且三个数值槽（`int` / `float` / `bool`）的转换器接受面**对齐引擎 `Variant::can_convert_strict`
的语义**，使 BigInt 与 number（以及 bool 槽的 null/undefined）都能用于内置类型的构造器与运算符；
在全部 JS 引擎（v8 / node / quickjs-ng / jsc / web）与全部绑定模式（static / shared / dynamic）
下行为一致。

用户可见的目标效果：`object.get_instance_id()` → `instance_from_id(id)` 这条最常规的
对象句柄往返**能用**（今天对 RefCounted 对象必然失败）。

## Background（2026-09-24 实机实测）

测试构建：`bin/windows/godotjs-ext.windows.editor.x86_64.dll`
md5 `a78c33e6e8ae2f3ca5aad3f5665a5d00`，`binding_mode=shared`
（依据：`.build/runtime/dispatch_class.gen.windows.editor.x86_64.obj` 含
`find_shared_class_method_binding`×11 / `SharedClassMethodData`×858，`find_class_method_thunk`=0）。
探针脚本见 `research/`（用法 `research/README.md`），日志 `.agent_tmp/probe_*.log`。

### 缺陷 1：64 位返回值超过 2^53 时丢低位（读方向）

`src/runtime/impl/{v8,quickjs,jsc,web}/jsb_*_helper.h` 各有一份**内容完全相同**的
`new_integer`（四份复制粘贴），阈值判断是**单边**的：

```cpp
if (const int32_t downscale = (int32_t)p_val; (int64_t)downscale == p_val) return v8::Int32::New(...);
#if JSB_WITH_BIGINT
if (p_val > JSB_MAX_SAFE_INTEGER) return v8::BigInt::New(isolate, p_val);   // ← 只判正向
#endif
return v8::Number::New(isolate, (double)p_val);                             // ← 负值全部落这里
```

实测：

| 引擎真值 | JS 拿到 | 位模式 | 是否丢信息 |
|---|---|---|---|
| `get_u64()` = 2^53 | `9007199254740992`（bigint） | `0x0020000000000000` | 完好 |
| `get_u64()` = 2^63 | `-9223372036854776000`（number） | `0x8000000000000000` | 位对、值语义错 |
| `get_u64()` = 2^63+1 | `-9223372036854776000`（number） | `0x8000000000000000` | **丢位** |
| `get_u64()` = 2^64-1 | `-1`（number） | `0xffffffffffffffff` | 恰好完好 |
| `get_64()` = INT64_MIN+1 | `-9223372036854776000`（number） | `0x8000000000000000` | **丢位（int64 同害）** |

**不是 uint64 专属**：所有「负且幅值 > 2^53」的值都被舍入。

用户可见后果（实测）：`Resource.get_instance_id()` 真值 `0x80000006890005ec`，JS 拿到
`0x8000000689000400`，`instance_from_id()` 返回 null、`is_instance_id_valid()` 为 false。
根因见 `design.md` §2.4（ObjectID bit63 承载 `is_ref_counted`，RefCounted 的 id 必然为负）。

现有规避：`project/tests/cross-environment/test-cross-environment.ts:434-441` 已写明
「RefCounted ObjectIDs set bit 63 ... never use that number as an ObjectDB key」并改用 `weakref()`。

### 缺陷 2：uint64 参数在静态绑定被拒绝（写方向）

`src/runtime/bridge/jsb_type_convert_direct.h:111-140` 的 `js_to_fixed_width_int<uint64_t>`：

```cpp
if (wide < 0) return false;                                                            // ← 早退
if (static_cast<uint64_t>(wide) > std::numeric_limits<uint64_t>::max()) return false;   // ← 恒假，无意义
```

实测（同一构建，**普通 number 输入，不含 BigInt**）：

| 你写 | 静态绑定 | 动态绑定 |
|---|---|---|
| `put_u64(2^53)` | 成功 `0x0020000000000000` | 成功 |
| `put_u64(2^63)` | **抛 `bad argument 0: got number`** | 成功 `0x8000000000000000` |
| `put_u64(1e19)` | **抛异常** | 成功 `0x8ac7230489e80000` |
| `put_u64(-1)` | **抛异常** | 成功 `0xffffffffffffffff` |
| `rng.state = 2^63`（属性赋值） | 成功 | 成功 |

**静态腿拒绝的不是「BigInt」，是「任何 ≥ 2^63 的 uint64 参数」**；且同一逻辑操作，
方法调用抛异常、属性赋值正常。

受影响接口（api json 实测，uint64 参数共 75 个 / 30 个类）：`RenderingServer.instance_attach_object_instance_id`、
`PhysicsServer2D/3D.area_attach_object_instance_id` / `body_attach_object_instance_id`、
`NavigationServer2D/3D.region_set_owner_id` / `link_set_owner_id`（传的就是 ObjectID，RefCounted 位为 1）、
`FileAccess.seek` / `store_64`、`StreamPeer.put_u64`、`XMLParser.seek`、`RenderingDevice.*`、一批 `OpenXR*`。

### 缺陷 3：开关不存在（配置面）

`src/jsb.config.h:155` 的 `JSB_WITH_BIGINT` 目前是 `1`，但它**不是可用的开关**：
关掉之后出口退化成「一律 number」（继续丢位），入口不认 BigInt，而**缺陷 2 与它无关，关不关都存在**。

用户诉求：开关只决定「出来的值怎么表示」；「进去的值」不管哪种模式都不许抛异常。

### 缺陷 4：数值槽的转换器接受面窄于引擎语义（三个槽都受影响）

> **范围修正记录（2026-09-24）**：初稿把范围划成「api json meta 里出现 uint64 的那部分」，
> 于是只覆盖 `int` 槽，把 `float` 槽写成「不承诺支持」、`bool` 槽写成「保持现状」。
> 这是**划错范围**：`int`/`float`/`bool` 都是**无 meta 的数值槽**，同样是「构造器/运算符参数」。
> 正确口径是「**逐档核对转换器接受面**」。
>
> 另一处错误：初稿曾用 `obj.call("set_block_signals", 1)` 成功来论证「动态腿宽松」。
> 实测证明那走的是 **vararg 无类型通道**（api json：`Object.call` 的 `is_vararg=True`），
> 没有声明类型可检查 —— 不是「宽松」，是**绕过类型检查**。该论证无效，已废弃。

**真实分歧是「本项目 vs 引擎」，不是「静态 vs 动态」。**

引擎 `Variant::can_convert_strict`（`core/variant/variant.cpp:575-604`）：
`BOOL` 接受 `INT`/`FLOAT`/`NIL`；`INT` 接受 `BOOL`/`FLOAT`/`NIL`；`FLOAT` 接受 `BOOL`/`INT`/`NIL`。
（`STRING` 三处都被注释掉。）

实测（**无默认值**的 `Projection.create_depth_correction(flip_y:bool)` 隔离真实转换，
避免默认值替换掩盖结果）：

| 传入 | 本项目静态腿 | 引擎语义 |
|---|---|---|
| `true` / `false` | OK | OK |
| `undefined` / `null` | **THREW** | 应接受（NIL） |
| `1` / `0` | **THREW** | 应接受（INT） |
| `1n` / `0n` | **THREW** | 应接受（INT） |
| `""` | THREW | 拒绝 ✓ 一致 |

三个槽的规模（api json 实测）：

| 槽 | 内置构造器参数 | 运算符右操作数 | class 方法参数 |
|---|---|---|---|
| `int` | 19 | 69 | 441（有 meta）+ 大量无 meta |
| `float` | 39 | 60 | 大量 |
| `bool` | 0（注册类内） | 20 | **1908** |

### 缺陷 4 的子项：BigInt 不能用于内置类型构造器与运算符

`probe_vt`（`src/static_binding/thunks/thunks_common.h:342-371`）**没有 BigInt 分支** →
返回 `VARIANT_MAX` → `can_be_converted_from<INT>(VARIANT_MAX)` 为 false → 构造器重载筛选失败。

实测：

| 你写 | 结果 |
|---|---|
| `new Vector2i(2n, 3)` | **拒绝**：`no suitable constructor for Vector2i (received 2 arg(s): (unknown), int)` |
| `new Vector2(2n, 3n)` | **拒绝**（同上） |
| `new Vector2i(2, 3)` | 成功 |
| `new Vector2(1,2).OP_MULTIPLY(2n)` | 成功（走动态回退，值正确 x=2 y=4） |
| `new Vector2(1,2).OP_MULTIPLY(2)` | 成功 |

受影响面（api json 实测）：**19 个内置构造器参数**是 `int` 类型；
**69 个二元运算符的右操作数是 `int`**（`int` 自身全部算术/位运算、`float` 比较、
`String %`、`Vector2/2i/3/3i/4/4i` 的 `*` `/`、`Transform2D *` `/`、`Quaternion`、`Basis`、
`Transform3D` 等）。

## Requirements

- **R1**（子任务 A）：64 位返回值不再丢信息。能被 JS `Number` 精确表示（`|v| <= 2^53-1`）时仍出
  `Number`；超出时按 meta 出口 BigInt —— uint64 走**无符号**生成（`get_instance_id()` 得
  正数 BigInt 而非负数），int64 与无 meta 走有符号生成。
- **R2**（子任务 B）：uint64 参数不再抛异常；静态与动态两腿对同一输入产生**相同字节**。
  判定口径为「按 64 位二进制原样写入（mod 2^64）」，不做范围拒绝。
- **R3**（子任务 C）：新增**独立**宏控制 64 位整数的 BigInt 表示（与 `JSB_WITH_BIGINT` 解耦）。
  开关只作用于出口表示；入口在两种模式下都不抛异常。默认值需与现状兼容。
- **R4**（子任务 D）：三个数值槽（`int` / `float` / `bool`）的转换器接受面对齐引擎语义，
  使 BigInt 与 number 都能用于内置类型构造器与运算符：
  - `probe_vt` 增加 BigInt → INT 分支
  - `JSToGD<float>` / `<double>` 接受 BigInt（走 `Number()` 语义）
  - `JSToGD<bool>` / `can_convert_strict<BOOL>` / typed `js_to_gd_var` 接受
    number + bigint + null/undefined（对齐引擎的 `INT`/`FLOAT`/`NIL`）
  - `StaticBindingUtil<bool>` 补特化
  - **并同步修** `builtin_operators.h` 的裸 `As<>` 与 `type_compatible.h` 的审计表
  - ⚠ 全部同批改，只做一部分是禁止项
- **R5**：五个引擎（v8 / node / quickjs-ng / jsc / web）行为一致。node 复用 v8 实现，
  实际需单独补的是 quickjs-ng / jsc / web。
- **R6**：三种绑定模式（static / shared / dynamic）行为一致。
- **R7**：窄整数（int8/16/32、uint8/16/32、char32）**保持现有范围检查**——它们是真的窄槽，
  静默截断才是缺陷。本任务不动。
- **R8**：新增 **C++ doctest 测试**与 **TS 集成测试**覆盖以上全部行为（见下「测试覆盖要求」）。

## 测试覆盖要求（R8 细化）

### C++ 侧（doctest，`--jsb-run-tests`）

被测逻辑的**所在层**决定测试放哪（见 `.trellis/spec/godotjs-ext/test/doctest.md`）：
- 转换原语（`jsb_primitive_conv.h` 的 `to_int64` / `to_uint64` / `new_integer` /
  `new_unsigned_integer`）→ runtime 套件
- `JSToGD<uint64_t>` / `js_to_fixed_width_int` → runtime 套件
- `probe_vt` 的 BigInt 分支 → runtime 套件（但 `probe_vt` 在 `thunks_common.h`，
  受 `JSB_WITH_STATIC_BINDINGS` 门控，用例需同门控）

形态：`TEST_CASE("[runtime] [jsb.int64] ...")`，用 `jsb_test_helpers.h` 的
`GodotJSScriptLanguageIniter` + `JSB_TESTS_EXECUTION_SCOPE` 拿 isolate/context；
新增头文件必须在 `src/runtime/tests/jsb_test_main.cpp` 的 include 列表登记。

### TS 侧（`project/tests/`，双绑定腿共享）

新增独立场景目录（**不进 bench** —— `misc/bench_matrix.py:96-97` 的 `invalid != 0` 会 FATAL）。
断言一律经 `reportTestFailure`（裸 throw 不会传播到 `start.ts`，会假报 COMPLETED）；
`start.ts` 的 `scenes` 列表需要登记新场景。

按 `.trellis/spec/godotjs-ext/test/index.md` 要求：**覆盖守卫必须断言数量**，
且新守卫要做**负向验证**（人为削减一次确认 FAILED，再还原确认绿）。

## Acceptance Criteria

- [ ] **AC1**：`Resource.get_instance_id()` 与 `Node.get_instance_id()` 在 static / shared / dynamic
      三腿下，`instance_from_id(get_instance_id())` 都返回原对象（`===` 成立），
      `is_instance_id_valid()` 为 true。
- [ ] **AC2**：`StreamPeerBuffer.put_u64(v)` → 读回 8 字节，对
      `v ∈ {2^53, 2^63, 2^63+1, 2^64-1}`（以 BigInt 与普通 number 两种形式传入）
      静态与动态两腿写入的字节**完全相同**，且等于 `v mod 2^64`。
- [ ] **AC3**：`StreamPeer.get_64()` / `get_u64()` 回读 `INT64_MIN+1`、`2^63+1` 等
      「负且幅值 > 2^53」的值时位模式精确。
- [ ] **AC4**：`|v| <= 2^53-1` 的返回值仍出 `Number`（不因本次改动变成 BigInt），
      `Int32` 快路径不变。
- [ ] **AC5**：子任务 C 的宏在两种取值下，AC2 的「不抛异常」都成立；关闭时 AC1 允许失败
      （与今天同），但**不得**出现新异常。
- [ ] **AC6**：`new Vector2i(2n, 3)`（int 槽）与 `new Vector2(2n, 3n)`（float 槽）在
      static / shared / dynamic 三腿下都成功，且结果与传 number 时相同。
- [ ] **AC7**：`OP_MULTIPLY(2n)` 这类 int 右操作数运算符在 BigInt 输入下结果正确，
      且与传 number 时相同；**不得**出现类型被错误解释（读越界/垃圾值）。
- [ ] **AC7b**：bool 槽按引擎语义放行 number + bigint + null/undefined：
      `set_block_signals(1)` / `(0)` / `(1n)` / `(null)` / `(undefined)` 都不再抛异常，
      且 `0`/`0n`/`null`/`undefined` → false、其余 number/bigint → true（用
      `is_blocking_signals()` 读回验证）。字符串仍拒。
- [ ] **AC7c**：**有默认值**的 bool 位置传 `undefined` 仍走默认值替换，不被转成布尔。
      ⚠ 对照组必须用**默认值为 `true`** 的 bool 参数才能判别（默认值 `false` 与 `undefined`
      的真值相同，两条假设同结果）。实测判据：`String.strip_edges("  x", undefined)` 得 `"x"`
      （走了默认值 `true`）；若走真值转换会得 `"  x"`。**实测已确认得 `"x"`**。
- [ ] **AC8**：`cd project && godot --audio-driver Dummy --headless --path project`
      全量 TS 集成测试 exit 0 且含 `GODOTJS_TEST_PROJECT_COMPLETED`、无 `GODOTJS_TEST_PROJECT_FAILED:`。
- [ ] **AC9**：`godot --headless --path ./project --jsb-run-tests` exit 0 且无泄漏
      （无未释放 Resource、无 Orphan StringName）。
- [ ] **AC10**：`python misc/bench_matrix.py` 的 `assert_leg()` 与 `invalid == 0` 门禁通过。
- [ ] **AC11**：新增 C++ 测试与 TS 集成测试覆盖 R1-R4（见「测试覆盖要求」），
      且新守卫通过负向验证。
- [ ] **AC12**：结论与「按位契约 / 双边阈值 / 每引擎原语表」沉淀进
      `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`。

## Out of Scope

- `src/runtime/js_type_extension/string_ext.cpp` —— **用户明确表示该目录是临时的、要移除**。
  本任务**不得**引用它、不得以它为参照、不得依赖它。若实施中发现需要它的行为，
  用别处（如 `impl::Helper`）的实现替代。
- `src/runtime/bridge/jsb_static_binding_util.h` 缺的**其余**特化（`uint64_t` / `uint8_t`
  等窄整型 / `char32_t`）：接受面可能与 `JSToGD<T>` 不一致。本轮只补 D 需要的 `bool`
  （`float`/`double`/`int64_t` 已有），其余记录到 spec。
- 字符串槽收数字：引擎的 `BOOL`/`INT`/`FLOAT` 接受面里 `STRING` 被注释掉，本项目一致拒绝，不改。
- 无类型 `js_to_gd_var` 对 > 64 位 BigInt 取低位：按 R2 口径**按定义正确**，只在 spec 记录。
- `src/static_binding/thunks/class_indexed_properties.h`：api json 实测索引属性中 64 位 meta = 0，无需改。

## Subtasks

| 子任务 | 覆盖 | 依赖 |
|---|---|---|
| `09-24-uint64-bigint-return` | R1、AC1、AC3、AC4 | 无（可先做） |
| `09-24-uint64-bigint-arg` | R2、AC2、AC3 | 需 A 的 `to_uint64`（文件不重叠，可并行开工） |
| `09-24-uint64-bigint-ctor-operators` | R4、AC6、AC7、AC7b、AC7c | A（`probe_vt` 的 BigInt 分支要用 `to_int64`） |
| `09-24-uint64-bigint-switch` | R3、AC5 | A、B 落地后 |

R8 / AC11 由**各子任务各自负责自己那部分的测试**；父任务的 `implement.md` 给出总体验证命令。

依赖关系写在子任务自己的 `prd.md`，不由树形位置隐含。

## Technical Notes

- 共享技术方案（含实测数据、逐文件改动点、五引擎原语表、风险）见本目录 `design.md`。
- ptrcall 编码背景见 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`。
- 测试规范见 `.trellis/spec/godotjs-ext/test/index.md`（TS/bench）与
  `.trellis/spec/godotjs-ext/test/doctest.md`（C++ 套件）。
- 硬约束见 `AGENTS.md`：不改 `*.gen.*`，改生成逻辑；临时文件放 `./.agent_tmp/`；
  文档中文、commit message 英文。
- 关键结论：`Ret<uint64_t>` / `Args<uint64_t>` **已经**把 unsigned 语义编译进模板实参
  （生成物 `src/static_binding/gen/dispatch_class.gen.cpp` 含 11 个 `Ret<uint64_t>` 签名，
  `Object.get_instance_id` = `k_shared_thunks[852]` = `shared_class_method_thunk<false, Ret<uint64_t>, Args<>>`），
  因此**不需要改 codegen**；`std::is_same_v<type, uint64_t>` 即可判据。
