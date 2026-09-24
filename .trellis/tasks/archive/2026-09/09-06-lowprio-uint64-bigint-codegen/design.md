# int64/uint64 ↔ BigInt 共享技术方案

> 父任务：`09-06-lowprio-uint64-bigint-codegen`。三个子任务共用本方案。
> 全部结论来自 2026-09-24 实机实测 + api json 全量扫描，非推断。
> 测试构建 md5 `a78c33e6e8ae2f3ca5aad3f5665a5d00`，`binding_mode=shared`。
> 探针 `.agent_tmp/probe/*.ts`，日志 `.agent_tmp/probe_*.log`。

---

## 1. 为什么这么改（先讲清楚道理）

### 1.1 根本问题

Godot 的整数内部就是 64 位，JS 的 `Number` 只能精确表示到 2^53-1。今天项目在
「超出 2^53 怎么办」上做了一半：正向（正数）转 BigInt，负向直接 `(double)` 舍入。

### 1.2 为什么不能靠「拒绝超范围的值」

看起来「超范围就报错」很安全，实际上会打断**正常用法**：

- `RefCounted` 对象的 ObjectID 高位（bit63）承载 `is_ref_counted` 标志
  （`D:/Dev/godot/godot/core/object/object.h:882-886`：`OBJECTDB_REFERENCE_BIT = 1 << 63`），
  所以**每个 RefCounted 的 id 在有符号视角下必然是负数**。
- 引擎侧 `instance_from_id(int64_t)` / `is_instance_id_valid(int64_t)`
  （`core/variant/variant_utility.cpp:1124`）没有无符号入口，内部 `ObjectID((uint64_t)p_id)`。
- `Variant::operator uint64_t()` 本身就是
  `static_cast<uint64_t>(operator int64_t())`（`third/godot-cpp/src/variant/variant.cpp:289-291`），
  即**引擎自己就是按二进制位读**。

结论：报错会禁掉 `get_instance_id()` → `instance_from_id()`；按位是唯一无损通道。

### 1.3 为什么按位在五个引擎上天然一致

| 引擎 | 无符号读 BigInt | 越界时行为 |
|---|---|---|
| v8 / node | `v8::BigInt::Uint64Value(bool* lossless)` | 有 `lossless` 反馈（可检测） |
| quickjs-ng | `JS_ToBigUint64` | 实现是 `JS_ToBigInt64Free`，注释「return the value mod 2^64」，**不检查** |
| jsc | `JSValueToUInt64` | 文档：BigInt 被 truncate 到 uint64_t |
| web | `jsbi_Uint64Value`（**已存在**） | 裸 `BigInt(val)` 写入，不检查 |

只有 v8 能报 lossless。若选「报错」，五个引擎语义分叉；选「按位回绕」，五个引擎**原生就一致**。
因此：**`lossless == false` 在 v8 上只记 `VeryVerbose` 日志，不抛异常**。

### 1.4 目标语义

**读（Godot → JS）**
- `|v| <= 2^53-1` → 出 `Number`（保持性能与兼容）。
- 否则按 meta：uint64 → `BigInt::NewFromUnsigned(v)`；int64 / 无 meta → `BigInt::New(isolate, v)`。
- 判据必须**双边**：`v > JSB_MAX_SAFE_INTEGER || v < -JSB_MAX_SAFE_INTEGER`。
- 无符号出口没有现成先例可参照（`src/runtime/js_type_extension/` 是临时目录，用户将移除，
  不作为参照）；按 §1.3 的引擎 API 直接实现。

**写（JS → Godot）**
- int64/uint64 槽同时接受 `Number` 与 `BigInt`。
- BigInt 按 meta 读法：uint64 → `Uint64Value`，int64 → `Int64Value`，随后**按位**写入槽。
- 超 64 位 / 负值给 uint64 槽：**mod 2^64 回绕，不报错**（与引擎 `PtrToArg<uint64_t>` 及
  三个非 v8 引擎的 C API 一致）。
- 窄整数（int8/16/32、uint8/16/32、char32）**保持现有范围检查**——它们是真的窄槽。

**`JSB_MAX_SAFE_INTEGER` 的值不动**（`src/jsb.config.h:160` 标注 DO NOT CHANGE），只改判据。

---

## 2. 实测数据

### 2.1 api json 中 int64/uint64 meta 分布

数据源 `third/godot-cpp/gdextension/extension_api-4-7.json`（4.7.2 stable）。

| 位置 | int64 | uint64 |
|---|---|---|
| class 方法返回 | 132 | **115** |
| class 方法参数 | 366 | **75** |
| builtin 方法（返回/参数） | 0 | 0 |
| utility 函数（返回/参数） | 0 | 0 |
| builtin 构造器参数 | 0 | 0 |
| signal 参数 | 0 | 0 |
| class 属性（含 `index>=0` 的 433 个索引属性） | 0 | 0 |
| 常量 / 全局枚举 | 0 | 0 |

推论（决定了改动面）：
- **uint64 参数带 `default_value` 的数量 = 0** → 静态腿的默认值替换逻辑
  （`shared_class_method_thunk` 的 `md.defaults`）无需为 uint64 做特殊处理。
- **索引属性中 64 位 meta = 0** → `class_indexed_properties.h` 无需改。
- 非索引属性（`index=-1`）中 uint64 有 3 对：`EncodedObjectAsID.object_id`、
  `PhysicsPointQueryParameters2D.canvas_instance_id`、`RandomNumberGenerator.seed` / `.state`。
  它们走 `_godot_object_method`（`jsb_object_bindings.cpp:126/128` 注册），即方法路径。

### 2.2 uint64 分布集中度（决定实际风险面）

- 115 个 uint64 返回中：`RenderingDevice` 18、`OpenXRExtensionWrapper` 15、
  `OpenXRAPIExtension` 12、`FileAccess` 5、`OS` 4、`PhysicsServer2D` 4 …
- 75 个 uint64 参数中：`OpenXRSpatialEntityExtension` 11、`OpenXRAPIExtension` 10、
  `RenderingDevice` 7、`PhysicsServer2DExtension` 5 …

**benchmark 唯一覆盖到的 uint64 返回是 `Object.get_instance_id`**
（`project/tests/benchmark/cases.object.ts:43`）。

### 2.3 `instance_from_id` 是 int64 语义（无 meta）

```
{"name": "instance_from_id",      "arguments": [{"name": "instance_id", "type": "int"}]}   # 无 meta
{"name": "is_instance_id_valid",  "arguments": [{"name": "id", "type": "int"}]}            # 无 meta
```
→ 只能靠位模式（补码）正确，没有无符号入口。

### 2.4 ObjectID 位布局

`D:/Dev/godot/godot/core/object/object.h:882-886`：
```
OBJECTDB_VALIDATOR_BITS = 39,  OBJECTDB_SLOT_MAX_COUNT_BITS = 24
OBJECTDB_REFERENCE_BIT  = 1 << (24 + 39) = 1 << 63
```
`object.cpp:2628` 拼 id 时 `is_ref_counted ? OBJECTDB_REFERENCE_BIT : 0`；
`object_id.h:45` `is_ref_counted() = (id & (1ull<<63)) != 0`。

---

## 3. 逐文件改动点

### 3.1 子任务 A：读方向

**A-1. 新增引擎无关转换实现** `src/runtime/impl/jsb_primitive_conv.h`

只依赖四个引擎**同名同签名**的 shim（`v8::Local` / `v8::Value` / `v8::Int32` / `v8::Number` /
`v8::BigInt`）：
```cpp
namespace jsb::impl {
// 读：Number / Int32 / BigInt -> int64（mod 2^64 位模式，不报错）
template <typename ValueT> bool to_int64(const ValueT &p_val, int64_t &r_val);
// 读：同上 -> uint64（mod 2^64 位模式）
template <typename ValueT> bool to_uint64(const ValueT &p_val, uint64_t &r_val);
// 读：Number / BigInt -> double（BigInt 走各引擎 NumberValue，即 Number() 语义）  [D 消费]
template <typename ValueT> bool to_double(const ValueT &p_val, double &r_val);
// 读：Number / BigInt / Null / Undefined -> bool（按 JS 真值；字符串拒）          [D 消费]
template <typename ValueT> bool to_bool(const ValueT &p_val, bool &r_val);
// 写：int64，双边阈值，超 2^53 走 BigInt::New
v8::Local<v8::Value> new_integer(v8::Isolate *isolate, int64_t p_val);
// 写：uint64，超 2^53 走 BigInt::NewFromUnsigned
v8::Local<v8::Value> new_unsigned_integer(v8::Isolate *isolate, uint64_t p_val);
}
```

> **本头由 A 独占创建并固定完整签名**，包括 `to_double` / `to_bool`。
> B 消费 `to_uint64`；D 消费 `to_int64` / `to_double` / `to_bool`。
> **D 不得另起一份**（否则就是「每引擎 shim 漂移」的翻版）。
> `to_double` / `to_bool` 的**用途说明**：`to_double` 给 `JSToGD<float>` / `<double>` 的
> BigInt 分支与 `builtin_operators.h` 的 `R == double` 分支；`to_bool` 给 `JSToGD<bool>`、
> `can_convert_strict<BOOL>` 与 `builtin_operators.h` 的 `R == bool` 分支。
> 二者在 A 落地时**只建骨架 + 单测**，D 落地时才接进槽位（避免 A 的任务膨胀）。

四个 `impl/*/jsb_*_helper.h` 的 `to_int64` / `new_integer` 改成**委托**（保留原函数名，
不动调用点），删掉四份重复体。`JSB_*_LOG` 差异不进这个头（`VeryVerbose` 日志丢弃）。

**A-2. 每引擎补 `Uint64Value`**

| 文件 | 改动 |
|---|---|
| `impl/v8/jsb_v8_helper.h` | 无（直接用 `v8::BigInt::Uint64Value`，`third/v8/include/v8-primitive.h:749`） |
| `impl/node/jsb_node_helper.h` | 无（`#include "../v8/jsb_v8_helper.h"`） |
| `impl/quickjs/jsb_quickjs_primitive.{h,cpp}` | 加 `uint64_t BigInt::Uint64Value(bool*)` → `JS_ToBigUint64`（`quickjs.h:873`） |
| `impl/jsc/jsb_jsc_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → `JSValueToUInt64(ctx, val, nullptr)`（`JSValueRef.h:506`，macos15/ios18+） |
| `impl/web/jsb_web_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → 已有 `jsbi_Uint64Value`（`jsb_web_interop.h:167`）；把 `jsb_check(res)` 改为返回 false 而非中止 |

web 的 `Uint64Value` 实现（`monolith.ts:659` / `jsbb.impl.js:478`）裸 `BigInt(val)` 写入
**就是 mod 2^64**，保留并补注释说明按位契约。

**A-3. 静态腿返回值按 meta 出口 —— 不需要改 codegen**

`src/static_binding/thunks/thunks_common.h` 的 `Ret<T>`，`type` 已携带 unsigned 语义：
```cpp
if constexpr (std::is_same_v<type, uint64_t>) {
    const uint64_t v = std::is_same_v<ReturnBufT, godot::Variant>
        ? (uint64_t)p_ret_val                                    // Variant::operator uint64_t() 即按位
        : godot::PtrToArg<uint64_t>::convert(&p_ret_val);
    internal::translate_uint64_return(p_isolate, v, p_info);      // -> new_unsigned_integer
}
```
新增 `internal::translate_uint64_return`（与现有 `translate_return` 同形，
`thunks_common.h:121-133`）。覆盖全部 11 个 `Ret<uint64_t>` 签名（含 `get_instance_id`）。
`Ret<int64_t>` 与 `Ret<godot::Variant>` 走既有路径（受 A-1 双边阈值修复）。

> **为什么不需要改 codegen**：`Ret<uint64_t>` 与 `Ret<int64_t>` 本身就是两个不同的模板实例，
> 生成物已把它们分开（`src/static_binding/gen/dispatch_class.gen.cpp:893-902`、
> `:1552`）。unsigned 语义在编译期就已可用。

**A-4. 动态腿把 meta 接进 TypeConvert**

- 新增重载：
  `TypeConvert::js_to_gd_var(isolate, context, jval, Variant::Type, GDExtensionClassMethodArgumentMetadata, Variant&)`
  与 `gd_var_to_js(..., Variant::Type, GDExtensionClassMethodArgumentMetadata, ...)`。
  入口签名现状见 `src/runtime/bridge/jsb_type_convert.h:56-57`。
- 调用点补 meta：
  - `jsb_object_bindings.cpp:483`（`_godot_object_method` 参数）→ `get_argument_metadata(index)`
  - `jsb_object_bindings.cpp:511`（`_godot_object_method` 返回）→ `get_return_metadata()`
    —— **这一处同时覆盖 `index=-1` 的属性 getter/setter**（它们在 `:126/128` 注册为
    `_godot_object_method`，数据是 getter/setter 的 `method_info`），因此
    `RandomNumberGenerator.seed/state` 走的就是这条路径。
  - `jsb_object_bindings.cpp:569/612`（`_godot_object_get2` / `_godot_object_set2`，索引属性）
    → getter 的 `get_return_metadata()` / setter 的 `get_argument_metadata(0)`。
    按 api json 这些属性无 64 位 meta，功能上无变化；补上只为两条属性路径语义一致。
  - `jsb_object_bindings.cpp:387`（vararg 尾参）保持无 meta（`Variant::NIL` 路径）。
- meta 读取 API 现成：`src/api_tool/api_tool_types.h:185-188`。
- `sanitize_return_type`（`jsb_object_bindings.cpp:41`）保持不变（只做类型合法性）。

### 3.2 子任务 B：写方向

**B-1. `src/runtime/bridge/jsb_type_convert_direct.h`**

- 新增 `template <> struct JSToGD<uint64_t>`，走 `Helper::to_uint64`（**不再经 `int64_t`**）。
- 从 `JSB_DIRECT_FIXED_INT` 列表移除 `uint64_t`（`:157`）；窄类型保持 `js_to_fixed_width_int`。
- 删除 `js_to_fixed_width_int` 的 `uint64_t` 分支（`:125-131`）与那句恒假的
  `static_cast<uint64_t>(wide) > numeric_limits<uint64_t>::max()`。
- 窄无符号（uint8/16/32）的 `wide < 0 → false` **保留**（正确）。

**B-2. 与 A 的接口**

B 依赖 A 提供的 `Helper::to_uint64`。若 B 先做，需在 A 落地前先用
`Helper::to_int64` + 位重解释过渡（`r_out = (uint64_t)wide`），但**最终形态必须是 `to_uint64`**，
否则 > 2^64 的 BigInt 语义在两腿间仍不一致。

**B-3. `src/runtime/bridge/jsb_static_binding_util.h`（顺带一致性，非必须）**

`:115-131` 的 `StaticBindingUtil<int64_t>` 旁补 `<uint64_t>` 特化（`get` → `to_uint64`，
`set` → `new_unsigned_integer`）。全仓库无实例化，无实际影响。

### 3.3 子任务 C：开关

- 在 `src/jsb.config.h` 新增独立宏（建议名 `JSB_BIGINT_FOR_64BIT`，最终名由 C 任务定），
  与 `JSB_WITH_BIGINT`（`:155`）解耦。
- 语义：**只作用于出口表示**。开 = 超阈值出口 BigInt；关 = 超阈值仍出 `Number`
  （与今天行为一致，接受丢位）。入口在两种模式下都不抛异常（由 B 保证）。
- 默认值需与现状兼容（当前行为 = 开），并在 `jsb.config.h` 注释里写明与
  `JSB_WITH_BIGINT` 的区别与交互。
- 需要梳理 `JSB_WITH_BIGINT` 的既有引用点，避免两个宏语义重叠导致四种组合里有未定义行为。
  至少明确：`JSB_BIGINT_FOR_64BIT=1` 依赖 `JSB_WITH_BIGINT=1`。

---

### 3.4 子任务 D：BigInt / 数值接入内置类型构造器与运算符

> **范围修正记录（2026-09-24）**：本方案初稿把范围划成「api json meta 里出现 uint64 的那部分」，
> 因此只覆盖 `int` 槽，并把 `float` 槽写成「不承诺支持」、把 `bool` 槽写成「保持现状」。
> 这是**划错了范围**：`int`/`float`/`bool` 都是**无 meta 的数值槽**，同样是「构造器/运算符参数」，
> 属于同一类问题。正确口径是「**逐档核对转换器接受面**」，而不是「meta 里有什么」。
>
> 另一处错误：初稿曾用 `obj.call("set_block_signals", 1)` 成功来论证「动态腿宽松」。
> 实测证明那走的是 **vararg 无类型通道**（api json：`Object.call` 的 `is_vararg=True`，
> `jsb_object_bindings.cpp:387-388` 用无类型 `js_to_gd_var`），根本没有声明类型可检查 ——
> 不是「宽松」，是**绕过类型检查**。该论证无效，已废弃。

**D-0. 真实分歧是「本项目 vs 引擎」，不是「静态 vs 动态」**

引擎 `Variant::can_convert_strict`（`D:/Dev/godot/godot/core/variant/variant.cpp:575-604`）：

```cpp
case BOOL:  { static const Type valid[] = { INT, FLOAT, /*STRING,*/ NIL }; ... }
case INT:   { static const Type valid[] = { BOOL, FLOAT, /*STRING,*/ NIL }; ... }
case FLOAT: { static const Type valid[] = { BOOL, INT, /*STRING,*/ NIL }; ... }
```

映射到 JS 的放行面：

| 引擎目标 | 引擎接受 | 对应 JS |
|---|---|---|
| `BOOL` | `INT` / `FLOAT` / `NIL` | number / bigint / null / undefined |
| `INT` | `BOOL` / `FLOAT` / `NIL` | boolean / number / bigint / null / undefined |
| `FLOAT` | `BOOL` / `INT` / `NIL` | boolean / number / bigint / null / undefined |

而本项目静态腿（实测，用**无默认值**的 `Projection.create_depth_correction(flip_y:bool)` 隔离）：

| 传入 | 本项目静态腿 | 引擎语义 |
|---|---|---|
| `true` / `false` | OK | OK |
| `undefined` | **THREW** | 应接受（NIL） |
| `null` | **THREW** | 应接受（NIL） |
| `1` / `0` | **THREW** | 应接受（INT） |
| `1n` / `0n` | **THREW** | 应接受（INT） |
| `""` | THREW | 拒绝（引擎注释掉了 STRING）✓ 一致 |

`can_convert_strict<BOOL>`（`jsb_type_convert.cpp:565-567`）与 typed `js_to_gd_var` 的
`BOOL` 分支（`:200-206`）都只认 `IsBoolean()`。

**D-1. 三个数值槽的改动**（全部受 `JSB_WITH_BIGINT` 门控的 BigInt 部分）

| 槽 | 构造器参数 | 运算符右操作数 | class 方法参数 | 改动 |
|---|---|---|---|---|
| `int` | 19 | 69 | 441（有 meta）+ 大量无 meta | `JSToGD<int64_t>` 已接受 BigInt；`probe_vt` 加分支 |
| `float` | 39 | 60 | 大量 | `JSToGD<float>` / `<double>` 加 BigInt 分支（走各引擎 `NumberValue`） |
| `bool` | 0（注册类内） | 20 | **1908** | `JSToGD<bool>` / `can_convert_strict<BOOL>` / typed `js_to_gd_var` 加 number+bigint+null/undefined |

**D-1a. `float` / `double` 槽**

- `JSToGD<float>` / `JSToGD<double>` 加 BigInt 分支，走各引擎的「转数字」原语
  （**与 `can_convert_strict` 已声明的行为一致** —— 它把 `FLOAT` 与 `INT` 放在同一 `case` 标签下
  并接受 `IsBigInt()`）：
  - v8：`v8::Value::NumberValue(context)`（`third/v8/include/v8-primitive.h` 同族；
    `v8-value.h:440`，文档「equivalent of `ToNumber()->Value()`」）
  - quickjs-ng：`Value::NumberValue`（`jsb_quickjs_primitive.cpp:79`，内部 `JS_ToFloat64`）
  - jsc：`JSValueToNumber`（`JSValueRef.h:461`，文档「equivalent to `Number(value)`」）
  - web：`jsbi_NumberValue`（`jsb_web_interop.h:162`，实现 `Number(val)`）
- `StaticBindingUtil<float>` / `<double>`（`jsb_static_binding_util.h:68-114`）同步
  （动态腿的 `ReflectConstructorCall` 走这条）。
- **口径（用户已定）**：按 `Number()` 语义转 double。`> 2^53` 的 BigInt 转 double 会丢低位，
  这是 double 槽的固有精度，不额外报错。

**D-1b. `bool` 槽**

- **口径（用户已定）**：对齐引擎 —— 放行 **number + bigint + null/undefined**
  （即引擎的 `INT` / `FLOAT` / `NIL`），字符串仍拒（引擎注释掉了 `STRING`）。
- 真值映射：`0` / `0n` / `null` / `undefined` → false；其余 number / bigint → true。
- 改动点：
  - `JSToGD<bool>`（`jsb_type_convert_direct.h:93` 的 `JSB_DIRECT_SCALAR(bool, IsBoolean(), ...)`）
  - `can_convert_strict<BOOL>`（`jsb_type_convert.cpp:565-567`）
  - typed `js_to_gd_var` 的 `BOOL` 分支（`jsb_type_convert.cpp:200-206`）
  - `StaticBindingUtil<bool>` **补特化**（今天缺 → 走主模板 → 经 `Variant` 中转，与 `JSToGD<bool>` 不同路）
  - `type_compatible.h` 的 `BOOL` 谓词（`:86-87`，已放行 `INT`/`FLOAT`）补 `NIL` 并逐行核对编组器接受面
- ⚠ **默认值优先级不能破坏**：`undefined` 在**有默认值**的位置必须仍走默认值替换
  （实测 `Rect2.intersects(b, include_borders=false)` 传 `undefined` 返回 `false`，即默认值）。
  只有在**无默认值**的位置，`undefined` 才走「转布尔」。实现要保持这个优先级。

**D-2. `probe_vt` 增加 BigInt 分支**（`src/static_binding/thunks/thunks_common.h:342-371`）

```cpp
if (val->IsInt32()) return Variant::INT;
if (val->IsNumber()) return Variant::FLOAT;
#if JSB_WITH_BIGINT
if (val->IsBigInt()) return Variant::INT;   // ← 新增，与 IsInt32 同组
#endif
if (val->IsBoolean()) return Variant::BOOL;
```

放在 `IsNumber()` 之后、`IsNullOrUndefined()` 之前，与 `IsInt32` 同组；
两个 `probe_prefer_*` 模式都要经过这个位置。

**D-3. 必须同步修 `builtin_operators.h` 的裸 `As<>`**（`:99-106`）

`operator_thunk` 的 `R == int64_t` 分支当前是：
```cpp
right_slot = (int64_t)info[0].As<v8::Int32>()->Value();
```
`As<S>()` 在各引擎 shim 里是 `Local<S>(data_)` **纯类型重解释**（v8 是 `Local<S>::Cast`），
**不做运行时类型检查**。BigInt 进入 `INT` 槽后（D-2 生效），这行会把 BigInt 的 handle
按 `Int32` 读 → **错误解释（读越界/垃圾值）**。

改为走转换原语（A 提供）：
```cpp
if (int64_t v; impl::Helper::to_int64(info[0], v)) { right_slot = v; }
else { jsb_throw(isolate, "operator: bad right operand"); return; }
```
同批核对 `R == double` / `R == bool` 分支：D-2 把 BigInt 归为 `INT`，所以
`FLOAT`/`BOOL` 槽不会因 D-2 收到 BigInt；但 **D-1a / D-1b 放行后它们会** ——
这两个分支也必须改为走转换原语（`to_double` / `to_bool`），不能再裸 `As<>`。

**D-4. 同步更新 `type_compatible.h` 的审计表**（`:38-66`）

该文件契约：「**谓词的接受面必须 ⊆ 对应 C++ 形参编组器的接受面**」。
改完 D-1 后三行都要重写并逐行核对：
- `BOOL` 行：现有 `INT`/`FLOAT` → 补 `NIL`，确认编组器（新 `JSToGD<bool>`）接受面一致
- `INT` 行：现有 `BOOL`/`FLOAT` → 补 `NIL` 与 `BIGINT`，确认 `JSToGD<int64_t>` 一致
- `FLOAT` 行：现有 `BOOL`/`INT` → 补 `NIL` 与 `BIGINT`，确认 `JSToGD<float>` 一致

历史事故：`COLOR` 曾放行 `STRING`/`INT`，导致 `new Color("abc")` 错选 `Color(Color)` 后
marshal 失败 —— 改这张表必须逐行核对，不能只加不减。

**D-5. 顺带核对 `StaticBindingUtil` 的缺特化**

`StaticBindingUtil` 只有 5 个特化（`Object *` / `float` / `double` / `int64_t` / `int32_t`），
缺 `bool` / `uint64_t` / `uint8_t` 等窄整型 / `char32_t`。缺的走主模板
（`jsb_static_binding_util.h:38-49`）→ `js_to_gd_var` 带类型 → 接受面可能与
`JSToGD<T>` 不一致。**这是静态 thunk 路径与 reflect 路径的接受面分歧**，
本任务只补 D-1 涉及的 `bool`（`float`/`double`/`int64_t` 已有）；窄整型与 `char32_t`
记录到 spec，不在本轮改。

---

## 4. 落地顺序

| 步 | 内容 | 依赖 | 可并行 |
|---|---|---|---|
| 1 | A：`jsb_primitive_conv.h` + 每引擎 `Uint64Value` | 无 | 与 B 并行（文件不重叠） |
| 2 | A：`Ret<uint64_t>` 无符号出口 | 步 1 | |
| 3 | A：动态腿 meta 传递 | 步 1 | |
| 4 | B：`JSToGD<uint64_t>` | 步 1（要 `to_uint64`） | |
| 5 | D：`probe_vt` + `builtin_operators.h` + `type_compatible.h` + `JSToGD<float/double/bool>` + `can_convert_strict` + `StaticBindingUtil<bool>`（同批） | 步 1 | |
| 6 | C：独立宏 + 语义收口 | A、B 完成 | |

A 与 B 的文件集不重叠：A 动 `impl/*`、`thunks_common.h`、`jsb_type_convert.{h,cpp}`、
`jsb_object_bindings.cpp`；B 动 `jsb_type_convert_direct.h`、`jsb_static_binding_util.h`。
D 动 `thunks_common.h`（`probe_vt`）+ `builtin_operators.h` + `type_compatible.h` ——
**与 A 在 `thunks_common.h` 上重叠**（A 改 `Ret<T>`，D 改 `probe_vt`），
因此 A 与 D 对 `thunks_common.h` 的修改必须串行，或由同一人一次改完。

**每步一次编译**：改 `impl/*/jsb_*_helper.h` 会触发全量重编（单次 100~190s）。
按上表一次列全变体，每步只编一次；不为措辞做验证性编译。

---

## 5. 验证方案

### 5.1 构建

沿用 `misc/bench_matrix.py:131-136` 的命令形态：
```
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      compiledb=no debug_symbols=no dev_build=no verbose=no
```
`SConstruct:37` 默认 `shared`；`:858-860` 由 `binding_mode` 决定
`JSB_WITH_STATIC_BINDINGS` / `JSB_WITH_SHARED_THUNKS`。**三种模式都要跑**（AC1/AC2）。

### 5.2 探针（`research/`，已跑通并产出本文件 §2 的证据）

| 脚本 | 覆盖 |
|---|---|
| `probe-uint64.ts` | 静态/动态 uint64 写读、字节级精确性、ObjectID 往返 |
| `probe-readback.ts` | 同一引擎值经两条返回路径的对照 |
| `probe-id.ts` | 以 `to_string()` 的 `itos` 输出为无损基准，验证 ObjectID 精确往返 |
| `probe-idnum.ts` | 只用 number（不用 BigInt）的 ObjectID 往返 |
| `probe-num.ts` | 纯 number 输入到 uint64/int64 槽的静态 vs 动态对照 |
| `probe-prop.ts` | 属性赋值 vs 方法调用的分歧 |
| `probe-gaps.ts` | int64 负值返回、BigInt 构造器、`rng.state` Number 写入 |
| `probe-ops.ts` | BigInt 用于内置构造器与运算符（`OP_MULTIPLY` / `new Vector2i`） |

运行方式见 `research/README.md`（含已知坑）。
**跑完必须把探针移出 `project/`**（`git status` 要干净）。

### 5.3 回归门禁

- `cd project && godot --audio-driver Dummy --headless --path project`：
  exit 0 + `GODOTJS_TEST_PROJECT_COMPLETED` + 无 `GODOTJS_TEST_PROJECT_FAILED:`。
- `python misc/bench_matrix.py` 的 `assert_leg()`（读 `BENCH_JSON.bindingMode`）与
  `invalid == 0` 硬门禁。
  ⚠ **不要往 bench 加 BigInt 联合类型算子 case** —— `invalid != 0` 会直接 FATAL
  （`misc/bench_matrix.py:96-97`）。
- 回归测试落点：新增 `project/tests/` 场景（**不进 bench**），断言写死期望值；
  按 `.trellis/spec/godotjs-ext/test/index.md` 的「覆盖守卫必须断言数量」要求做**负向验证**
  （人为削减一次确认 FAILED，再还原确认绿）。
- C++ 单元测试：`godot --headless --path ./project --jsb-run-tests`，exit 0 且无泄漏。
  被测逻辑所在层决定测试位置（runtime 归 `src/runtime/tests/`）；新增头必须在
  `src/runtime/tests/jsb_test_main.cpp` 的 include 列表登记。详见
  `.trellis/spec/godotjs-ext/test/doctest.md`。
- spec 沉淀：`.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md` 新增
  「int64/uint64 按位契约 + BigInt 双边阈值 + 每引擎原语表」一节。

### 5.4 测试落点（R8 / AC11）

| 层 | 位置 | 覆盖 |
|---|---|---|
| C++ 转换原语 | `src/runtime/tests/test_jsb_int64_conv.h`（新建） | `to_int64` / `to_uint64` / `new_integer` / `new_unsigned_integer` 的阈值与位模式 |
| C++ 直连转换 | 同上 | `JSToGD<uint64_t>`、窄类型仍拒绝越界 |
| C++ `probe_vt` | 同上（同受 `JSB_WITH_STATIC_BINDINGS` 门控） | BigInt → `Variant::INT` |
| TS 集成 | `project/tests/int64/`（新建，登记进 `start.ts`） | ObjectID 往返、uint64 写读字节、构造器/运算符 BigInt |

TS 侧断言一律经 `reportTestFailure`；**不进 bench**；守卫要断言数量并做负向验证。

---

## 6. 风险

| 风险 | 处置 |
|---|---|
| 读方向改动波及 `|v| <= 2^53-1` 的既有行为 | 只在超阈值时改出口；`2^53-1` 及以下仍出 `Number`，`Int32` 快路径不动（AC4 守） |
| 现有脚本依赖「uint64 高位出负 Number」 | 这是要修的缺陷；受影响面 = RefCounted ObjectID，TS 测试已用 `weakref` 绕开，不依赖旧行为 |
| quickjs/jsc/web 无法报 lossless，v8 能 | §1.3：`lossless` 只记日志不抛，五引擎统一按位回绕 |
| 每引擎 shim 再次漂移 | A-1 收敛为一份实现；shim 只提供 `Uint64Value` 一个原语 |
| 改 `impl/*/jsb_*_helper.h` 触发全量重编 | §4 一次列全变体，每步只编一次 |
| A/B 并行时对 `jsb_primitive_conv.h` 的契约分歧 | 契约（函数签名与语义）在 dispatch 的 `context` 中预先固定，B 只消费不修改 |
| `probe_vt` 加 BigInt 后运算符快路径错误解释 | D 任务强制三处同批修改（`probe_vt` + `builtin_operators.h` + `type_compatible.h`）；只做一半是禁止项 |
| A 与 D 都改 `thunks_common.h` | 两者对该文件的修改必须串行，或由同一人一次改完（见 §4） |
| 放行 bool 会让「传错类型」从报错变成静默按真值处理 | 严格按引擎语义：只放 number/bigint/null/undefined，字符串仍拒；`0`→false 是引擎行为，非本项目发明 |
| 放行 `undefined` 破坏「有默认值位置走默认值」的优先级 | 实现必须保持默认值替换优先；D 任务测试要同时覆盖「有默认值」与「无默认值」两种位置 |
| `builtin_operators.h` 的 `R == double` / `R == bool` 分支在 D-1 放行后会收到 BigInt/Number | D 任务同批改为走转换原语（`to_double` / `to_bool`），不再裸 `As<>` |
| 范围划错（按 meta 划 vs 按槽划） | 本节已记录修正过程；实施时按「逐档核对转换器接受面」而非「meta 里有什么」 |

---

## 7. 明确不在本方案范围

- **`src/runtime/js_type_extension/`（`string_ext.cpp` / `string_ext.h`）**：
  用户明确表示该目录是**临时的、将被移除**。本方案**不引用、不参照、不依赖**它。
  它当前的引用点是 `src/runtime/bridge/jsb_environment.cpp:67` 与 `:382`
  （注册 String 扩展）；移除它属于另一个任务，不在本方案范围。
- **`StaticBindingUtil` 缺的其余特化**（`uint64_t` / `uint8_t` 等窄整型 / `char32_t`）：
  走主模板 → 经 `Variant` 中转，接受面可能与 `JSToGD<T>` 不一致。本轮只补 `bool`
  （D-1b 需要），其余记录到 spec，不在本轮改。
- **字符串槽收数字**（引擎的 `BOOL`/`INT`/`FLOAT` 接受面里 `STRING` 是被注释掉的）：
  本项目与引擎一致地拒绝，不需要改。
- `src/runtime/bridge/jsb_static_binding_util.h` 缺 `<uint64_t>` 特化：无实例化，无影响。
- 无类型 `js_to_gd_var` 对 > 64 位 BigInt 取低位：按 R2 口径**按定义正确**，只在 spec 记录。
- `src/static_binding/thunks/class_indexed_properties.h`：api json 实测索引属性中 64 位 meta = 0，无需改。
