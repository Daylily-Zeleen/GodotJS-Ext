# Design — 运算符绑定与代码生成改造（静态 → 成员）

## 1. 边界与不变量

**不变**（改造前后必须逐项保持）：
- 运算符 JS 名：`JSB_OPERATOR_NAME(op_code)` = `"OP_" #op_code`（`src/internal/jsb_macros.h:38`）原样保留，宏不动、名字不重命名。
- 重载选择：probe 右侧实参 `Variant::Type` → 查 (left, op) 表 → 命中走 `operator_thunk`，miss/探测失败走 `Variant::evaluate`。
- `==`/`!=` 的 null/undefined 短路（`operator_thunk` 内 constexpr 分支）。
- `right=NIL`（api json 的 `right_type: "Variant"`）三分类：`==`/`!=` 不发表行、`and`/`or`/`xor` 不发表行、`%` 发 `Variant::NIL` case。**已核实（生成器实跑）**：`%` 的 `right=Variant` 行**只存在于 String 与 StringName**，两者均不在挂载集内——即该分支对已挂载类型目前**无活代码**；R7 去掉 String 生成后，静态腿连这 3 张表也不再产出（Vector2i/Vector3i/Vector4i 的 MODULE 表本就无 NIL case）。保留该分支是与引擎语义一致的**防御性**逻辑，不是当前可达路径。
- 一元运算符不进表，注册期直挂；表函数签名 `ThunkFn f(godot::Variant::Type)`、其声明头/定义 TU 划分、`##` 拼接的表名。
- 挂载跳过的左类型集合（`Nil`/`bool`/`int`/`float`/`StringName`/`String`）——R7 起由生成器显式排除 `String`。
- **两腿的运算符宏实现**不变（形态由宏实现决定）——但见 §5/R6：两个生成器**本次有改动**（排除 String），生成文件内容随之变化。除该范围外生成器零改动。

**改变**（唯一目标）：左操作数从首参改为接收者 `this`；挂载面 static → instance；d.ts 成员签名。

## 2. 取操作数（R2.1 / R2.2）

### 动态腿（`jsb_primitive_bindings.cpp`）

| 函数 | 现在 | 改为 |
|---|---|---|
| `BinaryOperator::invoke` (`:114`) | `left = info[0]`, `right = info[1]`, `Length()!=2` 报错 | `left = info.This()`, `right = info[0]`, `Length()!=1` 报错 |
| `UnaryOperator::invoke` (`:147`) | `left = info[0]`, `Length()!=1` | `left = info.This()`, `Length()!=0` |
| 注册宏 (`:45-74`) | `class_builder.Static().Method(...)` | `class_builder.Instance().Method(...)` |

`TypeConvert::js_to_gd_var(isolate, context, info.This(), left)` 走既有的 Variant 包装分支，与同文件 `_getter`/`_setter`（已用 `info.This()` + `IF_Pointer`）同路径；类型不符时 `js_to_gd_var` 失败 → 既有 "bad translation" 抛出。方法名宏不变。动态腿的 `JSB_LOG` 行不变。

### 静态腿（`src/static_binding/thunks/builtin_operators.h`）

| 函数 | 现在 | 改为 |
|---|---|---|
| `operator_thunk` (`:65`) | `left_backing_of<L>(info[0])`；右参 `info[1]`；`Length() < 2` 短路判定 | `left_backing_of<L>(info.This())`；右参 `info[0]`；`Length() < 1` |
| `operator_unary_thunk` (`:134`) | `left_backing_of<L>(info[0])` | `left_backing_of<L>(info.This())` |
| `operator_dispatch_binary` (`:197`) | probe `info[0]` / `info[1]` | probe `info.This()` / `info[0]` |
| `evaluate_dynamic_binary` (`:169`) | 读 `info[0]`、`info[1]` | 读 `info.This()`、`info[0]` |

细节：`left_backing_of<L>` 形参是 `const v8::Local<v8::Value>&`，`info.This()`（`Local<Object>`）隐式转换，模板不动。`left_vt == GetTypeInfo<LeftT>::VARIANT_TYPE` 守卫保留（语义不变，只是探测对象从首参换成接收者）。`right_slot` 提取分支（int64/double/bool/String/包装）整体 `info[1]` → `info[0]`；`R = godot::Variant` 分支维持 `(void)info` 不读实参。`operator_unary_thunk` 现有 `v8::HandleScope handle_scope(isolate)` 保留。

风险点：`IF_ObjectFieldCount`（Object 包装）不属本任务范围——`left_backing_of` 只认 `IF_VariantFieldCount`，与现状一致。

## 3. 编辑器 d.ts（R2.3）

`ClassWriter::operator_`（`jsb_codegen_writer.cpp:526`）改为：

```cpp
void ClassWriter::operator_(const OperatorDecl &p_info) {
	separator_line_ = true;
	const String return_type_name = types_->get_variant_to_name(p_info.return_type);
	if (p_info.is_unary) {
		line(p_info.op_name + "(): " + return_type_name);
		return;
	}
	const String right_type_name = p_info.right_type == Variant::NIL
			? String(kGodotAnyType)
			: types_->primitive_type_name_as_input_public(p_info.right_type);
	line(p_info.op_name + "(right: " + right_type_name + "): " + return_type_name);
}
```

- 删 `static` 前缀与 `left` 形参（接收者即 left）。
- `is_unary` 判据不能只看 `right_type == NIL`（`%` 与 `==`/`!=` 的 NIL 行是二元），必须按 op code：`NEGATE`/`POSITIVE`/`NOT`/`BIT_NEGATE`。
- `right=NIL` 的二元重载的右参类型是 `GAny`（2026-09-19 修正）：`godot::Variant` 在 typings 里只是命名空间（`Variant.Type`/`Variant.Operator`），**不是类型**，故原写法 `Variant | null` 引用了一个不存在的类型；TS 侧与 `godot::Variant` 对等的是 `GAny`（已含 `undefined | null`）。原任务曾输出 `Variant | null`（承接 `09-07-fix-editor-dts-operator-overloads`），本工作区改为 `GAny` 并重新生成 typings 验证。
- `OperatorDecl`（`jsb_codegen_type_db.h:135`）新增 `bool is_unary = false`；`op_name` 沿用（已是 `api_tool::get_variant_operator_name(op.op)` = `"OP_ADD"`，`type_db.cpp:443` 不改）。
- `p_info.left_type` 不再被 `operator_` 使用；字段保留（`ApiOperatorInfo` 有同名成员，删除会牵动 api_tool 序列化，超出本任务）。

## 4. TypeDB 单源（R3）

`load_primitive_types()`（`jsb_codegen_type_db.cpp:285-344`）重写为：

```cpp
void TypeDB::load_primitive_types() {
	// 与 runtime 侧 register_primitive_bindings 同形：单一清单源。
#pragma push_macro("DEF")
#undef DEF
#define DEF(TypeName)                                                                               \
	{                                                                                               \
		constexpr Variant::Type type_id = Variant::TypeName;                                        \
		_load_primitive_type(Variant::get_type_name(type_id), type_id, false /* utilities_mode */); \
	}
#include "jsb_primitive_types.def.h"
#pragma pop_macro("DEF")

	// String 与 runtime 侧一致：单独以 utilities 模式注册（其操作符/构造/属性不生成）。
	_load_primitive_type(Variant::get_type_name(Variant::STRING), Variant::STRING, true /* utilities_mode */);
}
```

形式对齐 `jsb_primitive_bindings.cpp:964-972`（同 `DEF` 语义：def.h 给类型名，宏内推 `Variant::Type`；用 `push_macro`/`pop_macro` 包裹避免污染 TU）。

- include 路径：`SConstruct:694-709` 把 `src/runtime/bridge` 加进 `CPPPATH`（editor 目标继承基础 CPPPATH），故裸文件名 `#include "jsb_primitive_types.def.h"` 可用，与 runtime 侧写法一致。
- 删除 `kPrimitiveTypes[]`、其循环、死宏 `JSB_CODEGEN_DEF` 与 `JSB_CODEGEN_DEF_UTIL`（统一用 `DEF`）。
- **顺序不变量**：def.h 顺序决定 `primitive_types` 插入序 → `ordered_primitives()` → d.ts 输出顺序。现 `kPrimitiveTypes[]` 顺序与 def.h 逐条一致（32 项已核对），故 d.ts 无顺序 diff——这是本项验收前提。
- 语义等价已验证：def.h 的 32 项 == 原表 32 项非 utilities 条目；原表第 33 项 `{String, utilities_mode=true}` == 新增的显式行。

## 5. 去掉 String 的运算符生成（R6）

### 事实（生成器实跑核实）

| 生成位置 | String 产物 | 为何是死代码 |
|---|---|---|
| `generate_primitive_operators.py` | `def.gen.h` 的 `JSB_TYPE_BEGIN(String)` 块：9 二元 + 1 一元（`NOT`），共 33 行 | 展开出 `OperatorRegister<String>` 特化，但**只有 `reflect_bind` 调用 `OperatorRegister<T>::generate()`**（`jsb_primitive_bindings.cpp:821`）；String 经 `reflect_bind_utilities` 注册（`:971`），从不调用 |
| `static_binding_codegen.py` | 9 张 `find_op_String_*` 表（57 `case`），连带 **55 个 String 独有 `operator_thunk` 实例化**（全量 335 中） | 唯一引用者是上面那个从不展开的宏；外部链接符号仍占体积 |
| `TypeDB`（d.ts） | 无 | **本来就正确**（String 走 `p_utilities_mode`，提前 return 跳过 operators） |

根因：两生成器各持一份排除集并互相注释对齐，但**两边都漏 String**（`JS_NATIVE_LEFT` = `{bool,int,float,StringName}`；`generate_primitive_operators.py:196` 同集合）。

### 改动

```python
# generate_primitive_operators.py  generate() 内
if class_name in ("bool", "int", "float", "StringName", "String"):
    # These types have no operator static-method surface. String is registered
    # through reflect_bind_utilities (no OperatorRegister<>::generate() call),
    # so its block would be dead code.
    continue
```

```python
# static_binding_codegen.py  emit_operator_pair_tables()
# Keep aligned with generate_primitive_operators.py.
JS_NATIVE_LEFT = {"bool", "int", "float", "StringName", "String"}
```

- **不改** `jsb_primitive_types.def.h`（String 仍参与 primitive 绑定，只是不参与运算符生成）。
- `TypeDB` 无改动（R3 单源化后确认 String 仍走 utilities 模式）。
- 不引入新的共享常量文件；两处各自硬编码但加**交叉引用注释 + 断言**（若将来新增引擎则必须同步），保持生成器无依赖。

### 规模影响

| 维度 | 去 String 前 | 去 String 后（实测） |
|---|---:|---:|
| `def.gen.h` 类型块 | 33 | **32** |
| JS 可见运算符方法 | 246（一元 51 / 二元 195） | **236（一元 50 / 二元 186）** |
| 静态腿 `find_op_*` 表 | 195 | **186** |
| 静态腿可选组合（`case` 标签） | 401 | **344** |
| `operator_thunk` 不同实例化 | 335 | **280** |

> 注：`%` 的 `right=Variant` NIL 行**只在 String/StringName**（已核实全部 76 条 `right_type=Variant` 行的归属）。两者都不挂载，故该分支对已挂载类型无活代码；`operator_thunk` 里的 `R = godot::Variant` 处理保留为防御性逻辑。

## 6. 验证环境：只用 quickjs-ng（R4）

本任务全部构建与测试在 **quickjs-ng** 下进行，不构建/不测试 v8 / node / quickjs / JavaScriptCore：

```
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes use_quickjs_ng=yes -j5
```

（`use_quickjs_ng=yes` → `JSB_WITH_QUICKJS` + `JSB_PREFER_QUICKJS_NG`，见 `SConstruct:29-30,339,400` 与 spec `third/quickjs-ng/backend/index.md`。）

- 构建产物名与 v8 腿不同，dll 部署/校验仍按 `build/scons-build.md` 双份替换 + md5 核对。
- "两腿"= `static_binding=yes` / `no`，**两腿都在 quickjs-ng 下**。
- 已知 quickjs 注意事项（spec `test/index.md`）：`bind_pointer` 与引擎 reference 回调的 `SetWeak` 幂等性只在 dev 构建的 `jsb_check` 下暴露——本次必跑 dev 构建，正好覆盖。
- 不修改 `SConstruct` 的引擎选择机制，不动其他引擎代码路径。

## 7. 运算符专项 TS 测试（R5）

### 落点与接线

- 新增 `project/tests/operators/test-operators.ts` + `Operators.tscn`（对齐 `project/tests/default-args/` 的既有形态：`_ready` 内 `section()`/`check()` + 失败走 `reportTestFailure`，绝不让异常逃出 `_ready`）。
- 挂进 `project/tests/start.ts` 的场景列表（`scenes` 数组），随主流程跑；判据仍是 `GODOTJS_TEST_PROJECT_COMPLETED`。
- 测试须**双腿通用**（同一份 TS 跑 static / dynamic），因此断言只用两腿都成立的行为。

### 覆盖矩阵

数据表由脚本从生成器产物机械抽取，不手写：

| 数据源 | 抽取内容 | 用途 |
|---|---|---|
| `.agent_tmp/ops.def.gen.h`（`generate_primitive_operators.py` 产物） | 每类 `JSB_DEFINE_OVERLOADED_BINARY_BEGIN` 的 op 名 + `JSB_DEFINE_UNARY` 的 op 名 | 全部方法的存在性/静态已移除断言 |
| `dispatch_builtin.gen.cpp`（`static_binding_codegen.py` 产物） | 每张 `find_op_<Class>_<Op>` 表的 `case` 标签（右参 Variant 类型） | 全部 (左,op,右) 调用组合 |

抽取脚本一次性运行、产出 TS 数据字面量嵌入测试文件，脚本本身留 `.agent_tmp/` 不入库（用户要求：`cases.builtin.ts` 同类手维护文件不引入生成器）。

### 断言分层

1. **形态**（全部方法覆盖，去 String 后 236）：`typeof value.OP_X === "function"`；`(Class as any).OP_X === undefined`。
2. **契约**（全部可选组合覆盖，去 String 后 344）：用该 `case` 对应的代表操作数调用，不抛；返回值的 Variant 型别与声明一致（用 `typeof`/构造器名/`instanceof` 判定，按类型分派）。
3. **精确值**（代表性子集）：`new Vector2(1,2).OP_ADD(new Vector2(3,4))` == `(4,6)`、`new Vector2(1,-2).OP_NEGATE()` == `(-1,2)` 等，含当前 bench 已覆盖的 23 条。
4. **不变量**（对其余全部类型通用）：`a.OP_EQUAL(a) === true`、`a.OP_NOT_EQUAL(a) === false`、比较反对称（`a.OP_LESS(b)` 与 `b.OP_GREATER(a)` 同真值）、结果型别正确。
5. **边界**：`OP_EQUAL(null)` → false、`OP_NOT_EQUAL(undefined)` → true。（String 的 `%`-NIL 行不测——String 不挂载运算符。）
6. **完整性守卫**：测试内声明期望集合（32 类 + 每类运算符名列表 + 总数，去 String 后实测 236 方法 / 344 组合），运行时比对实际发现的成员方法集合与覆盖计数，任一方向差异即 `reportTestFailure`（防止 api json 升级或绑定面变化后静默漏测）。

### 代表操作数构造

(左,op,右) 组合需要一个可复用的「按 Variant 类型造合法操作数」工厂：数值用字面量、`String` 用字符串、`string 型`同、`Object*` 用 `Node` 实例、容器用空 `GArray`/`GDictionary`、其余内置用无参构造。工厂放测试文件内私有函数。

## 8. 兼容面与清理（R2.4 / R7）

| 位置 | 动作 |
|---|---|
| `src/internal/jsb_macros.h:38` `JSB_OPERATOR_NAME` | **不动**（命名不变） |
| `project/tests/benchmark/cases.builtin.ts:299-342` 23 条 case | 静态调用 → 成员调用（`Vector2.OP_ADD(a,b)` → `a.OP_ADD(b)`）；组名 `Operators` 与 case 名不变 |
| `project/tests/benchmark/cases.builtin.ts:8-9` 头注释 | 去掉"静态方法"表述 |
| `misc/bench_matrix.py:114-118` docstring | 删掉 `Vector2.OP_IN !== undefined` 的失效说法（代码本就用 `STATIC_BINDING_ENABLED`），改述真实来源；逻辑不动 |
| `.trellis/spec/godotjs-ext/cpp/architecture-constraints.md` 运算符双层分发节 | 改成员形态；保留仍成立的约束（宏 `##` 只能拼宏实参、right=NIL 三分类、`%` 保留、注册期零查表、表结构） |

**无 shim / 无别名**：静态面一次性清空，`Vector2.OP_ADD` 为 `undefined`。

## 9. 风险

1. **`this` 上的方法解析**：`Instance().Method` 挂 `prototype_template_`（`jsb_v8_class_builder.h:154-162`；quickjs 同构在 `jsb_quickjs_class_builder.h`），同文件既有实例方法（`to_array`/`set_indexed`）已证可用。低。
2. **344 组合的代表操作数构造**：右侧 `Object *` 行需要真实 `Node` 实例；`Variant` NIL 行需要 `null`。工厂构造失败会导致假失败——工厂本身要有自检。
3. **bench 采数不可跨形态比较**：case 名与组名不变，但调用形态变化会改变耗时构成，历史数据不可直接比。
4. **d.ts 基线缺失**：本 worktree 无 `.codegen-baseline`，实施前必须按 `test/codegen-baseline.md` 建基线，否则 diff 无法归因。
5. **`jsb_primitive_bindings.cpp` 竞争**：与 `09-12-form-a-default-handling`（in_progress）共享该文件，开工前查 `git status` 与 mtime。
6. **quickjs-ng 单引擎**：本次不验证 v8 腿，故 v8 侧若有差异化问题不在本任务覆盖范围（R4 约束已声明）。
