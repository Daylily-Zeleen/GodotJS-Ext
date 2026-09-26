# 第一轮调研的实测证据（历史存档）

> ⚠ **本文件是 2026-09-24 第一轮调研的快照，已不是最新方案。**
> 权威版本是父任务目录的 `../design.md`（含子任务 D 与测试覆盖要求）。
> 本文件保留的价值是：第一轮探针的原始输出与当时的判断链条。
> 已被后续修订推翻/更新的点：
> 1. `src/runtime/js_type_extension/string_ext.cpp` 不再是参照（用户将移除该目录）。
> 2. BigInt 构造器/运算符已从 Out of Scope 变为子任务 D。
> 3. **§3.4 的范围划错了**：初稿只覆盖 `int` 槽，把 `float` 槽写成「不承诺支持」、
>    `bool` 槽写成「保持现状」。正确口径是「逐档核对转换器接受面」——`int`/`float`/`bool`
>    都是无 meta 的数值槽。见 `../design.md` §3.4 的 D-0 ~ D-5。
> 4. **§3.4 里「动态腿宽松」的论证无效**：`obj.call("set_block_signals", 1)` 走的是
>    **vararg 无类型通道**（api json：`Object.call` 的 `is_vararg=True`），没有声明类型
>    可检查 —— 不是「宽松」，是绕过类型检查。真实分歧是「本项目 vs 引擎」。
> 5. **bool 槽实测补全**（本节下方 §3.5）：`true`/`false` OK；`undefined`/`null`/`1`/`0`/
>    `1n`/`0n`/`""` 全 THREW。引擎语义（`variant.cpp:575-604`）应接受 `INT`/`FLOAT`/`NIL`。
> 关联任务：`.trellis/tasks/09-06-lowprio-uint64-bigint-codegen/`（planning，P3，area=static-binding）。
> 实测环境：分支 `feature/int64`，dll `bin/windows/godotjs-ext.windows.editor.x86_64.dll`
> md5 `a78c33e6e8ae2f3ca5aad3f5665a5d00`（2026-09-24 08:07），binding_mode=**shared**
> （证据：`.build/runtime/dispatch_class.gen.windows.editor.x86_64.obj` 含
> `find_shared_class_method_binding`×11 / `SharedClassMethodData`×858 / `min_argc`×1，
> 而 `find_class_method_thunk` 为 0；gen 源 `src/static_binding/gen/dispatch_class.gen.cpp` 同形）。
> 探针脚本留存于 `.agent_tmp/probe/`，日志 `.agent_tmp/probe_*.log`。

---

## 1. 结论摘要

1. **当前缺陷不是「uint64 专属」，而是 `new_integer` 的阈值判断是单边的**：只对
   `p_val > JSB_MAX_SAFE_INTEGER`（2^53-1）走 BigInt，**所有负数且幅值 > 2^53 的值
   静默经 `(double)` 舍入**。这同时打坏 int64（132 个 int64 返回 + 366 个 int64 参数）
   与 uint64。
2. **直接后果（实测，非推断）**：`RefCounted` 的 `get_instance_id()` → `instance_from_id()`
   往返在当前构建上**必然失败**（bit63=1 ⇒ 有符号为负 ⇒ 被舍入）。TS 测试因此被迫绕道
   `weakref()`（`project/tests/cross-environment/test-cross-environment.ts:434-441`）。
3. **静态腿与动态腿在「写入 uint64 高位」上不一致（实测）**：`2^63`/`2^63+1`/`2^64-1`
   经静态 thunk 被**拒绝**（`bad argument 0: got <unknown>`），经动态 `Object.call` 被
   **按位精确写入**。根因是 `js_to_fixed_width_int<uint64_t>` 的 `if (wide < 0) return false;`。
4. **静态腿的 meta 信息已经全部可用，不需要改 codegen**：`Ret<uint64_t>` / `Args<uint64_t>`
   已经把 unsigned 语义编译进模板实参（`Object.get_instance_id` = `k_shared_thunks[852]` =
   `shared_class_method_thunk<false, Ret<uint64_t>, Args<>>`）。handoff 里「需要给 `Ret` 加
   `is_unsigned_meta` 模板参数」的假设**不成立**——`Ret<uint64_t>` 与 `Ret<int64_t>` 本身就是
   两个不同实例，`std::is_same_v<type, uint64_t>` 即可判据。
5. **动态腿缺的是「把 meta 传进转换函数」**：`get_argument_metadata(i)` / `get_return_metadata()`
   在 `api_tool_types.h:185-188` 可读且已在 `api_tool_internal.h` 内部使用，但
   `jsb_object_bindings.cpp` 的 `_godot_object_method` / `_godot_object_get2` / `_godot_object_set2`
   只传 `get_argument_type(i)`，没传 meta。
6. **每个引擎只缺两个原语**：`uint64_t BigInt::Uint64Value(bool*)` 与「无符号出口」。
   web 的 `jsbi_Uint64Value` **已存在**（`jsb_web_interop.h:167`），只是 C++ 侧没接。
   node 复用 v8 的 helper（`impl/node/jsb_node_helper.h` → `../v8/jsb_v8_helper.h`），自动覆盖。
7. **四份复制粘贴的 `to_int64`/`new_integer` 是本类缺陷反复漂移的结构性原因**，方案要把它
   收敛成一份引擎无关实现。

---

## 2. 实测数据（api json 全量扫描）

数据源：`third/godot-cpp/gdextension/extension_api-4-7.json`（4.7.2 stable，precision=single；
与 `project/extension_api.json` 的 meta 直方图一致）。

### 2.1 int64/uint64 meta 分布

| 位置 | int64 | uint64 |
|---|---|---|
| class 方法返回 | 132 | **115** |
| class 方法参数 | 366 | **75** |
| builtin 方法（返回/参数） | 0 | 0 |
| utility 函数（返回/参数） | 0 | 0 |
| builtin 构造器参数 | 0 | 0 |
| signal 参数 | 0 | 0 |
| class 属性（含 index>=0） | 0 | 0 |
| 常量 / 全局枚举 | 0 | 0 |

- **uint64 参数带 `default_value` 的数量 = 0** → 静态腿的默认值替换逻辑
  （`shared_class_method_thunk` 的 `md.defaults`）无需为 uint64 做特殊处理。
- **索引属性（`properties` 中 `index>=0`，433 个）中 64 位 meta = 0** →
  `class_indexed_properties.h` 无需改 meta 处理。
- 非索引属性（`index=-1`）中 64 位 meta 有 16 个 getter/setter 对，其中 uint64 三对：
  `EncodedObjectAsID.object_id`、`PhysicsPointQueryParameters2D.canvas_instance_id`、
  `RandomNumberGenerator.seed` / `.state`。这些走 `_godot_object_get/set`（**仅动态路径**）。

### 2.2 关键 uint64 返回样本

`Object.get_instance_id`（**benchmark 覆盖：`project/tests/benchmark/cases.object.ts:43`**）、
`RandomNumberGenerator.get_state/get_seed`、`FileAccess.get_position/get_length/get_64/get_modified_time`、
`StreamPeer.get_u64`、`Engine.get_physics_frames`、`DisplayServer.window_get_attached_instance_id`、
`RenderingDevice.limit_get/buffer_get_device_address/texture_get_native_handle/get_driver_resource`、
`RenderingServer.get_rendering_info/texture_get_native_handle`、大量 `OpenXR*`。

### 2.3 `instance_from_id` 是 int64 语义（无 meta）

```
{"name": "instance_from_id", "arguments": [{"name": "instance_id", "type": "int"}]}   # 无 meta
{"name": "is_instance_id_valid", "arguments": [{"name": "id", "type": "int"}]}        # 无 meta
```
引擎侧：`Object *VariantUtilityFunctions::instance_from_id(int64_t p_id)`
（`D:/Dev/godot/godot/core/variant/variant_utility.cpp:1124`）内部 `ObjectID((uint64_t)p_id)`。
→ **只能靠位模式（补码）正确，没有无符号入口。**

### 2.4 ObjectID 位布局（决定「必须按位」）

`D:/Dev/godot/godot/core/object/object.h:882-886`：
```
OBJECTDB_VALIDATOR_BITS = 39, OBJECTDB_SLOT_MAX_COUNT_BITS = 24
OBJECTDB_REFERENCE_BIT  = 1 << (24 + 39) = 1 << 63
```
`object.cpp:2628` 拼 id 时 `is_ref_counted ? OBJECTDB_REFERENCE_BIT : 0`；
`object_id.h:45` `is_ref_counted() = (id & (1ull<<63)) != 0`。
→ **refcounted 对象的 ObjectID 必然 bit63=1，即 int64 视角必然为负**，这正是被舍入命中的分支。

### 2.5 引擎侧 BigInt API 现状

| 引擎 | 无符号读 | 无符号写 | lossless 可靠 |
|---|---|---|---|
| v8 / node | `v8::BigInt::Uint64Value(bool*)`（`third/v8/include/v8-primitive.h:749`） | `NewFromUnsigned`（`:732`） | ✅ 文档明确：负数或截断时 `false` |
| quickjs-ng | `JS_ToBigUint64`（`third/quickjs-ng/quickjs.h:873`） | `JS_NewBigUint64`（`:766`） | ❌ 实现是 `JS_ToBigInt64Free`，注释「return the value mod 2^64」，**不做范围检查**（`quickjs.c:14629-14651`） |
| jsc | `JSValueToUInt64`（`JSValueRef.h:506`，macos15/ios18+） | `JSBigIntCreateWithUInt64`（`:405`） | ❌ 文档：BigInt 被 truncate 到 uint64_t，异常经 `exception` 出参 |
| web | `jsbi_Uint64Value`（`jsb_web_interop.h:167`，**已存在**） | `jsbi_NewBigUint64` | ❌ `monolith.ts:659` / `jsbb.impl.js:478` 是裸 `BigInt(val)` 写入，无校验 |

**四份 `v8::BigInt` shim 都缺 `Uint64Value`**（`impl/{v8,quickjs,jsc,web}/jsb_*_primitive.h`
只有 `Int64Value` + `New` + `NewFromUnsigned`；v8 引擎直接吃官方头，只有三个 shim 需要补）。

---

## 3. 实测行为（当前 shared 腿构建，探针输出）

### 3.1 读方向（engine → JS）

| 引擎返回值 | `typeof` | JS 值 | JS 位模式 | 精确 |
|---|---|---|---|---|
| `get_u64` = 2^53 | `bigint` | 9007199254740992 | `0x0020000000000000` | ✅ |
| `get_u64` = 2^63 | `number` | **-9223372036854776000** | `0x8000000000000000` | 位对、符号/类型错 |
| `get_u64` = 2^63+1 | `number` | -9223372036854776000 | `0x8000000000000000` | ❌ **丢位** |
| `get_u64` = 2^64-1 | `number` | **-1** | `0xffffffffffffffff` | 位对、值语义错 |
| `get_64` = INT64_MIN+1 | `number` | -9223372036854776000 | `0x8000000000000000` | ❌ **丢位（int64 同样受害）** |

根因（`impl/*/jsb_*_helper.h` 的 `new_integer`，四份内容相同）：
```cpp
if (const int32_t downscale = (int32_t)p_val; (int64_t)downscale == p_val) return v8::Int32::New(...);
#if JSB_WITH_BIGINT
if (p_val > JSB_MAX_SAFE_INTEGER) return v8::BigInt::New(isolate, p_val);   // ← 单边！
#endif
return v8::Number::New(isolate, (double)p_val);                             // ← 负值落这里
```
`JSB_MAX_SAFE_INTEGER = ((int64_t)1<<53)-1`（`src/jsb.config.h:160`，标注 DO NOT CHANGE，
本方案不改它的值，只改判据）。

### 3.2 写方向（JS → engine），uint64 参数

| 输入 | 静态 thunk（`spb.put_u64`） | 动态（`spb.call("put_u64")`） |
|---|---|---|
| 2^53 (BigInt) | 写入精确 ✅ | 写入精确 ✅ |
| 2^63 (BigInt) | **拒绝** `bad argument 0: got <unknown>` | 写入 `0x8000000000000000` ✅ |
| 2^63+1 (BigInt) | **拒绝** | 写入 `0x8000000000000001` ✅ |
| 2^64-1 (BigInt) | **拒绝** | 写入 `0xffffffffffffffff` ✅ |
| 2^63 (Number) | `rng.state = 2^63` → 位精确 ✅ | — |

静态拒绝的根因（`src/runtime/bridge/jsb_type_convert_direct.h:111-140`）：
```cpp
if constexpr (std::is_same_v<CppT, uint64_t>) {
    if (wide < 0) return false;                                             // ← BigInt 2^63 → Int64Value() = -2^63 → 拒绝
    if (static_cast<uint64_t>(wide) > std::numeric_limits<uint64_t>::max()) return false;  // ← 恒假，无意义
}
```
动态腿走的 `TypeConvert::js_to_gd_var`（无类型）→ `p_jval.As<v8::BigInt>()->Int64Value()`
（`jsb_type_convert.cpp:553-556`）→ `Int64Value` 是 mod 2^64，**恰好等于按位语义**，所以动态是对的。

### 3.3 ObjectID 往返（用户可见故障）

```
Node.get_instance_id:     typeof=number bits=0x00000006820005eb  roundTrip=true   valid=true
Resource.get_instance_id: typeof=number bits=0x8000000689000400  roundTrip=false  valid=false
Resource exact (to_string): -9223372008786491924 → 0x80000006890005ec
instance_from_id(exact BigInt) same=true      ← 目标语义已可用
instance_from_id(lossy number) same=false     ← 当前返回路径的必然结果
```
即：**修复读方向后，`get_instance_id()` → `instance_from_id()` 的 BigInt 往返立刻成立**
（写方向对 int64 参数已经能接受 BigInt，实测 `same=true`）。

### 3.4 BigInt 在 builtin 构造器 / 运算符上不可用

```
new Vector2i(BigInt 2, 3)  → no suitable constructor for Vector2i (received 2 arg(s): (unknown), int)
new Vector2i(BigInt 2, BigInt 3) → 同上（float 槽同样被拒）
new Vector2i(number 2, 3)  → ok
new Vector2(1,2).OP_MULTIPLY(2n) → 成功，x=2 y=4（走动态回退）
```

根因（**两个槽，不是只有一个**）：
1. `probe_vt`（`src/static_binding/thunks/thunks_common.h:342-371`）**没有 BigInt 分支** →
   返回 `VARIANT_MAX` → `can_be_converted_from<INT>(VARIANT_MAX)` 为 false → **重载筛选**失败。
2. `JSToGD<float>` / `<double>`（`jsb_type_convert_direct.h:93-98`）只认 `IsNumber()`
   → 即使筛选通过（`probe_vt` 把 BigInt 归为 `INT`，而 `FLOAT` 槽接受 `INT` 源），
   **marshal 仍会拒绝** float 槽。这是 `type_compatible.h` 契约要求的「谓词接受面 ⊆
   编组器接受面」被违反的实例。

> ⚠ **不能只改 `probe_vt`**：那会让 float 槽从「筛选失败」变成「筛选通过但 marshal 拒绝」，
> 仍是缺陷，只是换了报错位置。三个槽（`int` / `float` / `bool`）必须与
> `builtin_operators.h` 的裸 `As<>`（`:99-106`，三个分支）同批改（详见 §5.6 与 `../design.md` §3.4）。

**被推翻的论证**：初稿曾用 `obj.call("set_block_signals", 1)` 成功来论证「动态腿宽松」。
无效 —— `Object.call` 在 api json 里 `is_vararg=True`，走的是无类型 `js_to_gd_var`
（`jsb_object_bindings.cpp:387-388`），**没有声明类型可检查**。不是「宽松」，是绕过类型检查。

### 3.5 bool 槽实测（补录，2026-09-24 第二轮）

用**无 `default_value`** 的位置隔离真实转换行为（有默认值时 `undefined` 被替换成默认值，
会掩盖结果）。推荐目标：`Object.set_block_signals(enable)`（class 方法，无默认值，
`is_blocking_signals()` 可读回）。

| 传入 | `set_block_signals`（无默认值） | `String.strip_edges(s, left=true)`（有默认值） |
|---|---|---|
| `true` / `false` | OK（读回 true / false） | — |
| `undefined` | **THREW** | OK → `"x"`（= 默认值 `true`，**不是**真值转换） |
| `null` | **THREW** | THREW |
| `1` / `0` | **THREW** | THREW |
| `1n` / `0n` | **THREW** | THREW |
| `""` | THREW | THREW |

结论：
1. 本项目静态腿的 bool 槽**只接受 `boolean`**（`can_convert_strict<BOOL>` 只认 `IsBoolean()`，
   `jsb_type_convert.cpp:565-567`；typed `BOOL` 分支 `:200-206` 同）。
2. 引擎语义（`core/variant/variant.cpp:575-604`）应接受 `INT`/`FLOAT`/`NIL`
   （对应 JS 的 number / bigint / null / undefined），**字符串仍拒**（`STRING` 被注释掉）。
3. **默认值优先**得到直接证据：`String.strip_edges("  x", undefined)` → `"x"`。
   该对照组的 bool 默认值是 `true`，所以能区分「默认值替换」（得 `"x"`）与
   「真值转换」（`undefined` 为假 → 得 `"  x"`）。
   默认值为 `false` 的对照组（`Rect2.intersects(b, include_borders=false)`）
   **无判别力** —— `false` 既是默认值又是 `undefined` 的真值，两条假设同结果。

### 3.6 探针可达性（补录）

- **普通 class 受 `build_profile.json` 裁剪**（`enabled_classes` 65 个）：
  `AStarGrid2D` 不在其中 → 运行时拿不到，**连 `set_jumping_enabled(true)` 都 THREW**
  （实测），会被误读成「转换被拒」。
- **内置类型不受该裁剪**：`Projection.create_depth_correction(true)` OK、
  `String.strip_edges("  x")` OK、`Object.set_block_signals(true)` OK。
- 选探针目标前先确认：普通 class → 查 `enabled_classes`；内置类型 → 直接用。

---

## 4. 目标语义（决策）

### 4.1 总口径：64 位槽一律「按位」，不做范围拒绝

理由（每条都有实测或源码依据）：
- 引擎自身 `Variant::INT` 就是 int64，`Variant::operator uint64_t() =
  static_cast<uint64_t>(operator int64_t())`（`third/godot-cpp/src/variant/variant.cpp:289-291`；
  主仓 `variant.h:342-358` `_to_int<T>` 是 `T(_data._int)`）——**引擎自己就是按位**。
- `instance_from_id(int64_t)` / `is_instance_id_valid(int64_t)` 无无符号入口，报错会打断
  `get_instance_id()` → `instance_from_id()` 的正常用法（TS 测试与 benchmark 都依赖它）。
- `ObjectID` 的 bit63 承载 `is_ref_counted`，按位是唯一无损通道。
- quickjs-ng / jsc / web 的 C API **原生就是 mod 2^64**，只有 v8 能报 lossless；
  选「报错」会让五个引擎语义分叉，选「按位」则天然一致。

### 4.2 读方向（gd → js）

- 值能被 JS `Number` 精确表示（`|v| <= 2^53-1`）→ 出 `Number`（保持现有性能与兼容）。
- 否则按 meta 出口：
  - **uint64 meta** → `BigInt::NewFromUnsigned(v)`（例如 `get_instance_id` 得
    `9223372064923059180n`，不是 `-9223372008786491924`）。
  - **int64 / 无 meta** → `BigInt::New(isolate, v)`。
- 判据必须**双边**：`p_val > JSB_MAX_SAFE_INTEGER || p_val < -JSB_MAX_SAFE_INTEGER`。
- 无符号出口没有现成先例可参照（`src/runtime/js_type_extension/` 是临时目录，用户将移除）。

### 4.3 写方向（js → gd）

- int64/uint64 槽同时接受 `Number` 与 `BigInt`（现状已如此，只是 uint64 静态腿被 bug 挡住）。
- `BigInt` 读法按 meta：uint64 → `Uint64Value`，int64 → `Int64Value`，随后**按位**写入槽。
- 超 64 位 / 负值给 uint64 槽：**定义为 mod 2^64 回绕，不报错**（与引擎 `PtrToArg<uint64_t>`
  及三个非 v8 引擎的 C API 一致，保证五引擎同语义）。
- v8/node 上 `Uint64Value(&lossless)` 的 `lossless==false` **只记 `VeryVerbose` 日志**，
  不抛异常（避免 v8 与非 v8 行为分叉）。
- 窄类型（int8/16/32、uint8/16/32、char32）**保持现有范围检查**（`js_to_fixed_width_int`
  的非 uint64 分支不变）——它们是真正的窄槽，静默截断才是 bug。

---

## 5. 实施方案

### 5.1 收敛为一份引擎无关的数值转换实现（结构性修复）

新增 `src/runtime/impl/jsb_primitive_conv.h`（引擎无关，只依赖四个引擎**同名同签名**的
shim：`v8::Local` / `v8::Value` / `v8::Int32` / `v8::Number` / `v8::BigInt`）：

```cpp
namespace jsb::impl {
// 读：Number / Int32 / Uint32 / BigInt -> int64（mod 2^64 位模式，不报错）
template <typename ValueT> bool to_int64(const ValueT &p_val, int64_t &r_val);
// 读：同上 -> uint64（mod 2^64 位模式）
template <typename ValueT> bool to_uint64(const ValueT &p_val, uint64_t &r_val);
// 写：int64，双边阈值，超 2^53 走 BigInt::New
v8::Local<v8::Value> new_integer(v8::Isolate *isolate, int64_t p_val);
// 写：uint64，超 2^53 走 BigInt::NewFromUnsigned
v8::Local<v8::Value> new_unsigned_integer(v8::Isolate *isolate, uint64_t p_val);
}
```
四个 `impl/*/jsb_*_helper.h` 的 `to_int64` / `new_integer` 改成**委托**（保留原函数名以
不动调用点），删掉四份重复体。`JSB_*_LOG` 差异不进这个头（`VeryVerbose` 日志丢弃，
避免为日志引入每引擎钩子）。

### 5.2 每引擎补齐两个原语

| 文件 | 改动 |
|---|---|
| `impl/v8/jsb_v8_helper.h` | 无（直接吃 `v8::BigInt::Uint64Value`） |
| `impl/node/jsb_node_helper.h` | 无（`#include "../v8/jsb_v8_helper.h"`） |
| `impl/quickjs/jsb_quickjs_primitive.{h,cpp}` | 加 `uint64_t BigInt::Uint64Value(bool*)` → `JS_ToBigUint64`（`:280-303` 旁） |
| `impl/jsc/jsb_jsc_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → `JSValueToUInt64(ctx, val, nullptr)`（`JSValueRef.h:506`） |
| `impl/web/jsb_web_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → 已有 `jsbi_Uint64Value`（`jsb_web_interop.h:167`） |
| `impl/web/bridge/src/monolith.ts` + `impl/web/js/jsbb.impl.js` | `Uint64Value` 的裸 `BigInt(val)` 保留（就是 mod 2^64），补注释说明按位契约；`jsb_web_primitive.cpp` 的 `jsb_check(res)` 改为**不 check 失败**（返回 false 而非中止） |

### 5.3 静态腿：修 `uint64_t` 的直连转换

`src/runtime/bridge/jsb_type_convert_direct.h`：
- 新增 `template <> struct JSToGD<uint64_t>`，走 `Helper::to_uint64`（**不再经 `int64_t`**）。
- `JSB_DIRECT_FIXED_INT(uint64_t)` 移除；窄类型保持 `js_to_fixed_width_int`。
- `js_to_fixed_width_int<uint64_t>` 分支与那句恒假的 `static_cast<uint64_t>(wide) > max` 一并删除。
- 窄无符号类型（uint8/16/32）的 `wide < 0 → false` 保留（正确）。

### 5.4 静态腿：返回值按 meta 出口（**不需要改 codegen**）

`src/static_binding/thunks/thunks_common.h` 的 `Ret<T>`：`type` 已携带 unsigned 语义，
在 `translate_return` 里按 `std::is_same_v<type, uint64_t>` 分支：

```cpp
if constexpr (std::is_same_v<type, uint64_t>) {
    const uint64_t v = std::is_same_v<ReturnBufT, godot::Variant>
        ? (uint64_t)p_ret_val                                   // Variant::operator uint64_t() 即按位
        : godot::PtrToArg<uint64_t>::convert(&p_ret_val);
    internal::translate_uint64_return(p_isolate, v, p_info);     // -> new_unsigned_integer
}
```
新增 `internal::translate_uint64_return`（与现有 `translate_return` 同形，只换成
`new_unsigned_integer`）。这样 `Ret<uint64_t>`（11 个签名，含 `get_instance_id`）全部覆盖；
`Ret<int64_t>` 与 `Ret<godot::Variant>` 走既有路径（受 §5.1 双边阈值修复）。

### 5.5 动态腿：把 meta 接进 TypeConvert

- 新增重载：`TypeConvert::js_to_gd_var(isolate, context, jval, Variant::Type, GDExtensionClassMethodArgumentMetadata, Variant&)`
  与 `TypeConvert::gd_var_to_js(..., Variant::Type, GDExtensionClassMethodArgumentMetadata, ...)`。
- 调用点补 meta：
  - `jsb_object_bindings.cpp:483`（`_godot_object_method` 参数）→ 传 `get_argument_metadata(index)`
  - `jsb_object_bindings.cpp:511`（`_godot_object_method` 返回）→ 传 `get_return_metadata()`
    —— **这一处同时覆盖 `index=-1` 的属性 getter/setter**：它们在
    `jsb_object_bindings.cpp:126/128` 注册为 `_godot_object_method`（数据是 getter/setter 的
    `method_info`），因此 `RandomNumberGenerator.seed/state`、`EncodedObjectAsID.object_id`、
    `PhysicsPointQueryParameters2D.canvas_instance_id` 走的就是这条路径。
  - `jsb_object_bindings.cpp:569/612`（`_godot_object_get2` / `_godot_object_set2`，`index>=0`
    的索引属性）→ 传 `property_info.getter_func->get_return_metadata()` /
    `setter_func->get_argument_metadata(0)`。按 api json 实测这些属性**没有 64 位 meta**，
    所以功能上无变化；补上只为两条属性路径语义一致。
  - `jsb_object_bindings.cpp:387`（vararg 尾参）保持无 meta（`Variant::NIL` 路径）
- meta 为 `INT_IS_UINT64` 时：返回走 `new_unsigned_integer`，参数走 `to_uint64`。
- `jsb_object_bindings.cpp:41` 的 `sanitize_return_type` 保持不变（只做类型合法性）。

### 5.6 数值槽（`int` / `float` / `bool`）对齐引擎语义（**必须同批改**）

> ⚠ 本节已被 `../design.md` §3.4 的 D-0 ~ D-5 取代（范围从「`int` 槽」扩到「三个数值槽」）。
> 保留于此仅为记录论证过程。

**改动清单（缺一不可）**：

1. `thunks_common.h` 的 `probe_vt` 加 `if (val->IsBigInt()) return Variant::INT;`
   （放在 `IsNumber()` 之后、`IsNullOrUndefined()` 之前，与 `IsInt32` 同组）。
2. `JSToGD<float>` / `<double>` 加 BigInt 分支（走各引擎 `NumberValue`，即 `Number()` 语义），
   `StaticBindingUtil<float>` / `<double>` 同步。
3. `JSToGD<bool>` / `can_convert_strict<BOOL>` / typed `js_to_gd_var` 的 `BOOL` 分支
   放行 number + bigint + null/undefined（对齐引擎的 `INT`/`FLOAT`/`NIL`）；
   `StaticBindingUtil<bool>` 补特化。字符串仍拒。
4. `builtin_operators.h:99-106` 的**三个**裸 `As<>` 分支
   （`R == int64_t` / `R == double` / `R == bool`）改为走转换原语
   （`Helper::to_int64` / `to_double` / `to_bool`，失败即 `jsb_throw`）。
   `As<S>()` 是纯类型重解释（v8 `Local<S>::Cast`），**不做运行时检查** ——
   上面三步放行后这三个槽都会收到新类型，不改就是错误解释（读越界/垃圾值）。
5. 同步更新 `src/static_binding/thunks/type_compatible.h:38-66` 的审计表三行
   （`BOOL`/`INT`/`FLOAT`），补 `NIL` 与 `BIGINT` 源，逐行核对编组器接受面。

> **禁止项**：只做其中一部分（例如只加 `probe_vt` 分支）会让缺陷换位置而不是消失。
> 若本轮不想全做，替代方案是**一步都不做**（保持现状），而不是做一半。

### 5.7 明确不在本轮范围（记录，不假装覆盖）

- `src/runtime/bridge/jsb_static_binding_util.h:115-131` 无 `<uint64_t>` 特化：
  全仓库无 `StaticBindingUtil<uint64_t>` 实例化（只有 `int64_t`/`int32_t`/`real_t`/`Vector*`），
  当前无实际影响；补特化属于「顺带一致性」，可与 §5.1 一起做。
- 无类型 `js_to_gd_var` 对 > 64 位 BigInt 静默取低位（`Int64Value` mod 2^64）：
  与 §4.3 的按位契约一致，**按定义不是 bug**，只在 spec 记录。

---

## 6. 落地顺序（每步一次编译，不为措辞重复编译）

1. **§5.1 + §5.2**（共享转换头 + 每引擎 `Uint64Value`）——单次编译，五引擎全覆盖。
2. **§5.3**（`JSToGD<uint64_t>`）——静态腿 uint64 参数可写。
3. **§5.4**（`Ret<uint64_t>` 无符号出口）——`get_instance_id` 往返成立。
4. **§5.5**（动态腿 meta 传递）——动态腿对齐。
5. **§5.6**（`probe_vt` + `JSToGD<float/double/bool>` + `can_convert_strict` +
   `builtin_operators.h` 三分支 + `type_compatible.h` 三行）——数值槽对齐引擎语义。
6. 验证（§7），然后沉淀 spec。

第 1~4 步构成一个**可独立验收的最小闭环**（读/写方向都正确、static 与 dynamic 一致）；
第 5 步已由用户决策**纳入范围**（子任务 D），不是可选增量。

---

## 7. 验证方案

### 7.1 三种 binding_mode 都要跑

构建（沿用 `misc/bench_matrix.py:131-136` 的命令形态）：
```
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      compiledb=no debug_symbols=no dev_build=no verbose=no
```
（注意 `SConstruct:37` 默认 `shared`；`SConstruct:858-860` 由 `binding_mode` 决定
`JSB_WITH_STATIC_BINDINGS` / `JSB_WITH_SHARED_THUNKS`。）

### 7.2 已有探针（`.agent_tmp/probe/`，本轮已跑通并产出证据）

- `probe-uint64.ts`：静态/动态 uint64 写读、字节级精确性、ObjectID 往返
- `probe-readback.ts`：同一引擎值经两条返回路径的对照
- `probe-id.ts`：以 `to_string()` 的 `itos` 输出为无损基准，验证 ObjectID 精确往返
- `probe-gaps.ts`：int64 负值返回、BigInt 构造器、`rng.state` Number 写入

运行方式：拷到 `project/tests/<dir>/`，`cd project && node node_modules/typescript/bin/tsc`
（**不加 `--noCheck`**），再
`godot --audio-driver Dummy --headless --path project res://tests/<dir>/Probe.tscn`。

**验收判据（三腿必须一致）**：
| 断言 | 期望 |
|---|---|
| `get_instance_id()` 的 `typeof` | `bigint`（refcounted，bit63=1，> 2^53） |
| `instance_from_id(get_instance_id()) === obj` | `true`（refcounted 与非 refcounted 都成立） |
| `StreamPeerBuffer.put_u64(2^63+1)` → 字节 | `0x8000000000000001`，静态与动态一致 |
| `get_u64()` 回读 2^63+1 | 位模式精确（BigInt） |
| `get_64()` 回读 INT64_MIN+1 | 位模式精确 |
| `new Vector2i(2n, 3)` | 成功（若采纳 §5.6） |

### 7.3 回归门禁

- `cd project && godot --audio-driver Dummy --headless --path project` 全量 TS 集成测试：
  exit 0 + `GODOTJS_TEST_PROJECT_COMPLETED` + 无 `GODOTJS_TEST_PROJECT_FAILED:`。
- `python misc/bench_matrix.py` 的 `assert_leg()`（读 `BENCH_JSON.bindingMode`）与
  `invalid == 0` 硬门禁必须通过。
  ⚠ **不要往 bench 加 BigInt 联合类型算子 case**——`invalid != 0` 会直接 FATAL（`bench_matrix.py:96-97`）。
- 回归测试落点：新增 `project/tests/` 场景（不进 bench），断言写死期望值并按
  `.trellis/spec/godotjs-ext/test/index.md` 的「覆盖守卫必须断言数量」要求做**负向验证**
  （人为削减一次确认 FAILED，再还原确认绿）。
- 文档/spec：结论沉淀到 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`
  （新增「int64/uint64 按位契约 + BigInt 双边阈值 + 每引擎原语表」一节）。

---

## 8. 风险

| 风险 | 处置 |
|---|---|
| 读方向改动把 `|v| <= 2^53-1` 的行为也变了 | 明确只在超阈值时改出口；`2^53-1` 及以下仍出 `Number`，`Int32` 快路径不动 |
| 现有脚本依赖「uint64 高位出负 Number」 | 这是本任务要修的缺陷；受影响面 = refcounted ObjectID（TS 测试已用 `weakref` 绕开，不依赖旧行为） |
| quickjs/jsc/web 无法报 lossless，v8 能 | §4.3 已定：`lossless` 只记日志不抛，五引擎语义统一为按位回绕 |
| `probe_vt` / `JSToGD<float/double/bool>` 放行后运算符快路径错误解释 | §5.6 强制同批修改三个分支；只做一半是禁止项 |
| 放行 bool 槽让「传错类型」变成静默按真值处理 | 严格按引擎语义：只放 number/bigint/null/undefined，字符串仍拒 |
| 放行 `undefined` 破坏「有默认值位置走默认值」 | 实测确认现状是默认值优先（`String.strip_edges("  x", undefined)` → `"x"`）；改动只作用于无默认值位置 |
| 每引擎 shim 又漂移 | §5.1 收敛为一份实现；shim 只提供 `Uint64Value` 一个原语 |
| 改 `impl/*/jsb_*_helper.h` 触发全量重编（单次 100~190s） | 按 §6 顺序一次列全变体，每步只编一次 |