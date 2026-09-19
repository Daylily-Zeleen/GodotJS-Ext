# 设计：静态腿内置类型构造函数分发修复

## 1. Scope / Trigger

**触发**：静态腿 bench `Constructors` 组 `invalid=2`（`new Color(String)` / `new Color(String,float)`），动态腿同组 `invalid=0`；静态腿另有一条**可达的 SIGSEGV 路径**（§3.3）。

**跨层**：决策横跨 `JS 值 → 重载选择谓词 → ptrcall 编组`，涉及生成器（`misc/build/static_binding_codegen.py`）、运行时 thunks（`src/static_binding/thunks/`）、转换层（`src/runtime/bridge/jsb_type_convert_direct.h`）。按 code-spec 深度输出。

**范围**：本文件是**修复方案**（诊断 + 设计）。实施另起轮次。

## 2. 根因

### 2.1 两腿各用一套「可接受性」模型，且互相不一致（**主因**）

| | 重载筛选所用的判定 | 判据 |
|---|---|---|
| **动态腿** `jsb_primitive_bindings.cpp:210` | `TypeConvert::can_convert_strict(isolate, context, info[i], argument_type)` | **JS 值的实际形态**（`IsString` / `IsNumber` / `IsObject && is_variant` …） |
| **静态腿** 生成的 `find_ctor_<T>` | `can_be_converted_from<TargetVariantType>(probed_type)`（`type_compatible.h`） | **Variant 类型对**（镜像引擎 `Variant::can_convert_strict`） |

两者对同一个输入可以给出**相反**结论。决定性实例——`new Color("abc")`：

| | 判定 | 结果 |
|---|---|---|
| 动态腿 | `can_convert_strict(jsString, COLOR)` → 走 `FALLBACK_TO_VARIANT`，`!p_val->IsObject()` → **false** | 跳过 `Color(Color)`，选中 `Color(String)`，**成功** |
| 静态腿 | `probe_vt(jsString) = STRING`；`can_be_converted_from<COLOR>(STRING)` → `type_compatible.h:111` 放行 **true** | 选中 `Color(Color)` → `marshal_one<godot::Color>` → `extract_variant_backed` 要求 Variant 包装对象 → **失败** |

动态腿 `new Color("abc")` 实测成功（`.agent_tmp/bench-ctors-dyn.log`：`sample=obj:Color`），故契约是「应可用」——静态腿与之背离。

**为何会漂移**：`marshal_one` 链**没有非抛出的"能否转换"探针**（`produce_value` 失败即 `jsb_throw`，`thunks_common.h:471`）。生成器无法回问转换器，只能自造一张平行的**预测表**；预测表与被预测的转换器各写各的，必然漂移。

**结构根因一句话**：动态腿只有**一个**函数同时承担"筛选"与"转换"，天然一致；静态腿把这个决策**拆成两个实现**（生成期的谓词表 + 运行期的 marshaller），一致性无人保证。

### 2.2 谓词放行但 marshaller 必失败的**全集**（程序化审计）

以生成器实跑的 32 个 `find_ctor_*` 为输入，逐条比对「谓词接受面」与「`JSToGD<CppT>` 接受面」：

| 谓词目标 | C++ 形参 | 谓词额外放行的源 | marshaller 是否接受 | 受影响 ctor | 状态 |
|---|---|---|---|---|---|
| `COLOR` | `godot::Color` | `STRING` | ✗（`extract_variant_backed`） | `Color` | **已观测**（bench invalid=2） |
| `COLOR` | `godot::Color` | `INT` | ✗ | `Color` | 同类，未观测 |
| `RID` | `godot::RID` | `OBJECT` | ✗ | `RID` | 同类，潜伏 |

共 3 条。其余 34 个谓词目标与各自 marshaller 一致（含 `ARRAY ↔ PACKED_*` 双向、数值互转、`TRANSFORM3D` 家族——这些目标的 marshaller 是 `extract_variant_backed`，接受任意包装对象，而谓词放行的源本身都是可包装类型，故成立）。

> 注：`STRING_NAME`/`NODE_PATH` 的谓词接受 `STRING`，其 marshaller（手写 `JSToGD<StringName>`/`<NodePath>`）确实接受 `IsString()`——一致，**不在失配集**。这是"手写而宽松"与"宏生成而严格"两类 marshaller 的差别所在。

### 2.3 无匹配时没有兜底抛出（**SIGSEGV 来源**）

`throw_no_suitable_ctor` 已定义（`builtin_constructors.h:42`，`thunks::internal` 内，注释写明 "When no overload matches"）但**从未被发射**——生成的 `find_ctor_*` 在 `if (argc == N)` 链之后直接结束函数体（`dispatch_builtin.gen.cpp` 内引用数 = 0）。

后果：谓词全不匹配时函数**掉出末尾**，`new` 回调返回由 `ClassBuilder::New<IF_VariantFieldCount>`（`jsb_primitive_bindings.cpp:575`）建立的对象——`internal_field_count = 1` 但 `IF_Pointer` 字段**为 null**。

### 2.4 空包装在下游被无条件解引用（**放大为崩溃**）

| 位置 | 代码 |
|---|---|
| `thunks_common.h:347` / `:364`（`probe_vt`） | `((const Variant *)obj->GetAlignedPointerFromInternalField(IF_Pointer))->get_type()` |
| `builtin_operators.h:51` / `:115` | 同上形态 |
| `jsb_type_convert_direct.h:254`（`extract_variant_backed`） | `r_out = *(godot::Variant *)pointer;` |

**反证**：同文件的 `TypeConvert::can_convert_strict`（`jsb_type_convert.cpp:641-646`）**有**该守卫（`if (!target) { return Variant::can_convert_strict(Variant::NIL, p_type); }`）——守卫模式是本仓库既有惯例，上述三处漏了。

## 3. 三个候选方向的裁决

### ✗ 3.1 改 `builtin_ctor_thunk` 的参数处理 —— 错层

thunk 的契约是「**已选定**的重载，转换为 `EncodeT` 写入 ptrcall 槽」。转换失败即抛是**正确**行为：放宽它会把「不匹配」变成「静默构造垃圾值」，破坏类型安全。问题不在它。

### △ 3.2 调整重载匹配优先级 —— 治症不治本

把 `Color(String)` 排到 `Color(Color)` 前，`new Color("abc")` 会绿。但：

- `new Color(1)`（`probe_vt` = INT，同样被 `COLOR` 谓词放行）仍失败；
- 完全不触及 §2.3/§2.4：无匹配时依旧返回空包装、依旧 SIGSEGV；
- 把 JSON 里的重载顺序变成**隐式契约**——顺序本不具语义。

**结论：仅作应急止血，不是修复。**

### ✓ 3.3 修 `marshal_one` 整条链路 —— **正解**

链路的不完备**不在转换本身**，而在于它只提供"转换或抛"一种模式，缺少非抛出的可接受性探针，从而**逼迫生成器维护一张会漂移的平行预测表**（§2.1）。同时其读取侧无空指针守卫（§2.4）。

## 4. 修复方案

### P1 止血（小改，消除已观测失效与崩溃类）

**P1.1 谓词对齐 marshaller**——`type_compatible.h`：
- `COLOR` 去掉 `STRING` 与 `INT`（§2.2 已证：marshaller 为 `extract_variant_backed`，只吃包装对象）。
- `RID` 去掉 `OBJECT`（同类失配）。
- 文件顶部写明**契约**：*`can_be_converted_from<T>` 必须 ⊆ `JSToGD<T>` 的接受面；违反即"选中后 marshal 失败"*，并附 §2.2 的审计表作为引擎升级时的核对清单。
- **不改变**已一致的项（`ARRAY ↔ PACKED_*`、数值互转、`TRANSFORM3D` 家族、`STRING_NAME`/`NODE_PATH`）。

**P1.2 补兜底抛出**——`static_binding_codegen.py:emit_ctor_dispatch`：在每个 `find_ctor_*` 的 arity 链之后发射 `thunks::internal::throw_no_suitable_ctor(<VT>, info);`。把「静默空包装」变成「ctor 处的清晰 JS 异常」，与动态腿的 `jsb_throw("no suitable constructor")`（`jsb_primitive_bindings.cpp:250`）同形。
- 附带：`throw_no_suitable_ctor` 由 `static` 改 `inline`（头文件内定义，便于任何 TU 引用且无未使用告警）；它当前只在 `dispatch_builtin.gen.cpp` 一处被 include，故此项是卫生性而非功能性。

**P1.3 空指针守卫 —— 经用户指出后撤销（2026-09-19）**。原方案是给 §2.4 三处加 `if (!pointer) return <失败>`；撤销理由：`IF_VariantFieldCount` 包装的 `IF_Pointer` 由 `bind_valuetype` 写入，**不存在"是 variant 包装但指针为空"的合法状态**，故消费端判空是把分发缺陷的症状当输入契约来兜。正解是 P1.2 让分发永不出产空包装。`probe_vt` 现用 `TypeConvert::is_variant`/`is_object`（与原 `InternalFieldCount()` 比较语义等价，并覆盖 NODE 下 Promise 特例），无判空。

**P1.4 落地后收益**：`new Color(String)` / `(String,float)` 转为走 `Color(String)` / `Color(String,real_t)` 重载**成功**；`new Color(1)` 与动态腿一致地抛 `no suitable constructor`。

### P2 根治（选择与转换共用同一接受模型）—— **已决定不做（用户 2026-09-19）**

> **决策：只做 P1，P2 取消。** 理由：`type_compatible.h` 本就是为静态 ctor 参数筛选**专设**的表（非"意外的第二数据源"）；P1 让谓词健全后已观测缺陷全部闭合，且零性能损失。谓词漂移风险改由 P1.1 的契约注释 + 审计表承担（新增目标类型时逐行核对）。以下 P2 内容仅存档考虑过的方案，不再实施。



把生成器发射的谓词从「手工类型对表」换成**非抛出的编组探针**：

```cpp
// 生成器发射（示意）：CppT 直接取自 arg_template_expr
if (thunks::can_marshal<godot::Color>(isolate, context, info[0])) { ... }
```

`can_marshal<CppT>` 定义在 `thunks_common.h`（紧邻 `marshal_one`）：用 `try_js_to_gd` 试转到临时值并丢弃、返回 bool——**与 `marshal_one` 同一接受面**，结构上不可能漂移。

- **代价**：选中前多一次转换（ctor 非热点路径，可接受）。
- **替代（更大改）**：照搬动态腿的**尝试链**（`ReflectConstructorCall` 的 `do{...}while(false)` 序列 + 末尾 `jsb_throw("no suitable constructor")`，`jsb_reflect_binding_util.h` 内 16 处）——每次候选直接试编组、失败继续，零重复转换，但需槽位可重建与部分失败回滚。
- 生成器需要 `isolate`/`context`：`find_ctor_*` 目前只有 `info`，加两行局部变量即可。
- **P2 落地后 `type_compatible.h` 的表整体废弃**（P1.1 的收紧随之作废），仅保留 P1.2/P1.3 作纵深防御。

### 建议

**P1 全做**（有界、消除已观测缺陷与崩溃类、动态腿零影响）；**P2 单列后续项**，并在 P1.1 的审计表里注明"谓词表是临时方案，P2 后删除"——否则下次引擎加类型仍会漂移。

## 5. Validation & Error Matrix

| 输入（静态腿） | 现状 | P1 后 | 与动态腿一致？ |
|---|---|---|---|
| `new Color("abc")` | 选 `Color(Color)` → `bad argument 0` | 选 `Color(String)` → 成功 | ✓ |
| `new Color("abc", 1.5)` | 同上 | 选 `Color(String, real_t)` → 成功 | ✓ |
| `new Color(1)` | 选 `Color(Color)` → `bad argument 0` | 无匹配 → `no suitable constructor` | ✓（动态腿亦抛） |
| `new Color(new Color())` | 成功 | 成功（不变） | ✓ |
| `new RID(<Object 包装>)` | 选 `RID(RID)` → `bad argument 0` | 无匹配 → `no suitable constructor` | ✓ |
| `new PackedVector2Array([...])` | 无匹配 → 空包装 → 下游 SIGSEGV | `no suitable constructor` 异常 | ✓ |
| 空包装进入运算符接收者位 | SIGSEGV（`probe_vt`） | `VARIANT_MAX` → 动态兜底/抛错 | — |

## 6. Tests Required

- **断言点 1（回归）**：静态腿 bench `Constructors` 组 `invalid == 0`；`new Color(String)` / `new Color(String,float)` 两条 `sample != "invalid"`。
- **断言点 2（崩溃类）**：`new PackedVector2Array([new Vector2()])` 以 `no suitable constructor` 异常返回，进程不崩。**须能区分**「异常」与「SIGSEGV」——crash 会让整轮无 `COMPLETED`，异常则有 `FAILED:` 哨兵。
- **断言点 3（空包装不可达）**：R2 兜底抛出后，无匹配 ctor 抛异常而非产出空包装（探针实测 `array-literal threw=true`）。~~去掉 `probe_vt` 的 null 守卫 → 复现 SIGSEGV~~ —— 该负向验证随 P1.3 一并作废（消费端守卫不属本修复）。
- **断言点 4（两腿一致）**：`static_binding=yes/no` 对同一 ctor 用例集合判定一致（现有 `Constructors` 组即覆盖）。
- **断言点 5（生成器越界）**：`emit_ctor_dispatch` 之外零 diff；`find_ctor_*` 的 arity 分支体逐字节不变（只多末尾兜底行）。
- 全部在 **quickjs-ng** 下跑；C++ 双套件（`tests=yes dev_build=yes`）各一次。

## 7. Wrong vs Correct

#### Wrong —— 靠重载顺序掩盖
```python
# 生成器里把 String 重载排到同类型重载之前
overloads.sort(key=lambda c: c["args"][0]["type"] == "String")
```
`new Color("abc")` 绿了，`new Color(1)` 仍失败，空包装仍会 SIGSEGV，且顺序成为隐式契约。

#### Correct —— 让选择与转换同源
```cpp
// P1.1：谓词收紧到 marshaller 接受面（COLOR 不再收 STRING/INT）
case godot::Variant::COLOR:
    return false;            // extract_variant_backed 只接受包装对象；标量源交给 Color(String) 重载或报错

// P1.2：无匹配时抛出，而非掉出函数体
thunks::internal::throw_no_suitable_ctor(godot::Variant::COLOR, info);

// P1.3：守卫后返回"未识别"，交给上层兜底
if (!pointer) return godot::Variant::VARIANT_MAX;

// P2：谓词不再手工维护，直接用与 marshal_one 同源的探针
if (thunks::can_marshal<godot::Color>(isolate, context, info[0])) { ... }
```
