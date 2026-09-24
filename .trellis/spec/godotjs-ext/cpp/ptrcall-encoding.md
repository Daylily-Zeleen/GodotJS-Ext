# ptrcall 参数编码与缓冲区规范（api_tool / 静态绑定）

> 语义以 godot-cpp `include/godot_cpp/core/method_ptrcall.hpp`、`classes/ref.hpp` 为准，适用于 api_tool 全部 ptrcall 调用路径（builtin 方法 / utility 函数 / 运算符求值 / 成员存取）。

## ptrcall 调用约定

引擎为去 Variant 装箱开销提供 ptrcall（指针式调用）。godot-cpp 侧入口（`godot.hpp` 声明的 gdextension 接口）：

```
object_method_bind_ptrcall(method_bind, instance, args, ret)
variant_*_ptrcall(...)          # builtin 类型成员/构造/求值
utility_function_ptrcall(...)   # utility 函数
```

调用方须在栈上准备：

- **参数指针数组**（godot-cpp 生成代码变量名 `argptrs`；api_tool 里是 `ptr_args`）：`GDExtensionTypePtr` 数组，每元素指向一块参数内存
- **每块参数内存**：布局由该参数类型的 `PtrToArg<T>::EncodeT` 决定
- **返回内存**（`ret_ptr`）：布局 = 返回类型的 `EncodeT`
- builtin 成员/运算符另有 self/操作数内存（api_tool 的 `base_ptr`）

## PtrToArg<T> 与编码类型 EncodeT

`PtrToArg<T>` 是 ptrcall 的参数编码/解码适配器：

- `convert(const void *p_ptr) -> T`：从参数内存读出 T
- `encode(T p_val, void *p_ptr)`：把 T 写入参数内存
- `typedef ... EncodeT`：**编码类型**——参数内存的实际布局类型

三类宏的真实语义（理解溢出 bug 的关键）：

| 宏 | EncodeT | encode 写入方式 |
|---|---|---|
| `MAKE_PTRARG(T)` | `T` | `*(T*)p_ptr = p_val` 按值整体写入 |
| `MAKE_PTRARGCONV(T, C)` | `C` | 转 C 后写入（bool→uint8_t、小整数→int64_t、float→double） |
| `MAKE_PTRARG_BY_REFERENCE(T)` | `T` | `*(T*)p_ptr = p_val` **仍是按值整体写入完整结构体** |

⚠ `MAKE_PTRARG_BY_REFERENCE` 的 "BY REFERENCE" 只指 encode 的入参形式（`const T &p_val`，避免调用方临时拷贝），**不是参数内存存引用**。Basis=36B、Transform3D=48B、Projection=64B 就是 `sizeof(T)` 本身——参数内存里内联整个结构体。

Object 派生类型走 `PtrToArg<T *>` 特化：EncodeT = `Object *`，参数内存只写一个 `GDExtensionObjectPtr`（8 字节）。

## 特殊编码：Ref<T>（ref.hpp）

`PtrToArg<Ref<T>>` 的参数内存**不是 C++ `Ref<T>` 对象布局**，而是引擎侧的 `GDExtensionRefPtr`：

- `encode` 调用 `ref_set_object(ref, p_val->_owner)`；注释明确要求 p_ptr 指向**引擎侧一个未设置的 Ref 变量**，只有 valid 时才 set
- `convert` 调用 `ref_get_object(ref)` + `get_object_instance_binding`
- EncodeT 名义上 `Ref<T>`，内存语义是引擎管理的 Ref 句柄，扩展侧不能当普通 C++ 对象读写

api_tool 当前不涉及 RefCounted 参数/返回（UtilityFunctions 与内建类方法均无），非内建类调用全部转 Variant 走 `object_method_bind_call`，未使用此特化。

## 各类型 EncodeT 编码大小表

| Variant 类型 | EncodeT | 字节 |
|---|---|---|
| NIL（Variant 本体） | Variant（BY_REFERENCE） | 24 |
| BOOL | uint8_t（CONV） | 1 |
| INT | int64_t | 8 |
| FLOAT | double（CONV） | 8 |
| VECTOR2 / VECTOR2I | 值 | 8 |
| RECT2 / RECT2I | 值 | 16 / 8 |
| VECTOR3 / VECTOR3I | BY_REFERENCE | 12 |
| TRANSFORM2D | 值 | 16 |
| VECTOR4 / VECTOR4I | BY_REFERENCE | 16 |
| PLANE | BY_REFERENCE | 16 |
| QUATERNION | 值 | 16 |
| AABB | BY_REFERENCE | 24 |
| **BASIS** | BY_REFERENCE | **36** ⚠ |
| **TRANSFORM3D** | BY_REFERENCE | **48** ⚠ |
| **PROJECTION** | BY_REFERENCE | **64** ⚠ |
| COLOR | BY_REFERENCE | 16 |
| STRING / STRING_NAME / NODE_PATH / RID / CALLABLE / SIGNAL / DICTIONARY / ARRAY / PACKED_* | 值（对象包装） | 8 |
| OBJECT | `Object *` | 8 |

⚠ 标记类型 sizeof 超过 `sizeof(Variant)=24`，是溢出 bug 的来源。

## api_tool 参数内存管理

api_tool（`src/api_tool/`）为每次 ptrcall 在栈上准备：

```
var_args   = 参数内存数组（每参数一块，布局 = MaxSizeEncodeArgType）
ptr_args   = 参数指针数组（GDExtensionTypePtr / void*，指向 var_args 各块）
ret_ptr    = 返回内存（布局 = MaxSizeEncodeArgType）
base_ptr   = builtin self / 运算符操作数内存
```

- `var_to_arg_ptr(Variant, void *arg_ptr, ...)`：按参数类型 `PtrToArg<T>::encode` 写入参数内存——**写入字节数 = sizeof(EncodeT)，不是 sizeof(Variant)**
- `arg_ptr_to_var(...)`：反向，`convert` 读出转回 Variant
- 编码支持全部 builtin Variant 类型（NIL 到 PACKED_VECTOR4_ARRAY）

## 参数内存分配（强制规则）

**反例**：按 `stack_alloc(Variant, 1)` = `alloca(24)` 分配，而 BASIS/TRANSFORM3D/PROJECTION 按 EncodeT 写 36/48/64 字节——每次调用越界写 12~40 字节踩坏相邻栈内存。

**强制规则**：

1. 参数内存/返回内存**必须**统一按 `sizeof(internal::MaxSizeEncodeArgType)` 分配（编译期从全部 EncodeT 候选取最大，当前 = Projection 64 字节）
2. **新增 Variant 类型的 ptrcall 支持时，必须同步 `api_tool_internal.h` 的 `all_encode_types` tuple**（文件顶部注释有说明）
3. main 分支的 17 处参数存储分配点同源——改 main 相关代码时同样适用此规则

所有 ptrcall 调用路径（builtin 方法 / utility / 运算符 / 成员存取，static 与 dynamic 两条路径）均适用本规则。

## produce_value / produce_variant / marshal 分层（thunks_common.h）

- `produce_value<ArgT>`：typed 转换到 `ArgT::gd_type`（编译期已知目标类型 → JSToGD<T> 精确转换）
- `produce_variant<ArgT>`：typed 转换到局部 gd_type 后赋值进 Variant
- `marshal_one<ArgT>`：ptrcall 风格，按 `PtrToArg<T>::encode` 写入参数内存
- 失败路径：所有 produce 转换失败时目标内存保持调用前状态（不部分写入），调用方按"未构造"/"已构造"对应处理

## int64 / uint64 按位契约与 BigInt 阈值（jsb_primitive_conv.h）

> 引擎无关的数值转换实现在 `src/runtime/impl/jsb_primitive_conv.h`，四个引擎 shim
> （`impl/{v8,node,quickjs,jsc,web}/jsb_*_helper.h`）只做转发。**新增数值转换必须改这个头，
> 不要在 shim 里再写一份**——四份复制粘贴正是本类缺陷反复漂移的结构性原因。

### 读方向（Godot → JS）

- **64 位整数按位读，不做范围检查**。越界值按 mod 2^64 回绕，与引擎自身一致
  （`Variant::operator uint64_t()` 就是 `static_cast<uint64_t>(operator int64_t())`；
  quickjs-ng / jsc / web 的 C API 原生也是 mod 2^64）。
- 出口判据**必须双边**：`v > JSB_MAX_SAFE_INTEGER || v < -JSB_MAX_SAFE_INTEGER` → `BigInt`；
  否则 `Number`（能塞进 int32 时先出 `Int32`）。
  **单边判据是历史缺陷**：负且幅值 > 2^53 的值会落到 `Number::New((double)v)` 被舍入。
- uint64 槽必须走**无符号**出口（`new_unsigned_integer` / `BigInt::NewFromUnsigned`）。
  `ObjectID` 的 bit63 承载 `is_ref_counted`（`core/object/object.h` `OBJECTDB_REFERENCE_BIT`），
  有符号出口会让每个 RefCounted 的 id 变成负数，`instance_from_id()` 随即收到不同的 id。
- 无符号语义来自 **C++ 类型本身**（`Ret<uint64_t>` 与 `Args<uint64_t>` 是不同模板实例），
  静态路径不需要 meta。
- 动态（reflect）路径靠 `GDExtensionClassMethodArgumentMetadata`，**两个方向都需要**：
  - 返回方向：`gd_var_to_js` 的 `INT_IS_UINT64` 分支走 `new_unsigned_integer`。
  - 参数方向：`js_to_gd_var` 的 `INT_IS_UINT64` 分支走 `to_uint64` 再写回有符号槽。
    两个方向的 Variant INT 槽存的确实是同样的 64 位，**但取位方式不同**：从
    `[2^63, 2^64)` 的 double 取位时，`(int64_t)` 是 UB（x86 返回 INT64_MIN 哨兵），
    必须走无符号转换。实测 `put_u64(1e19)` 在 dynamic 腿因此写成了
    `0x8000000000000000` 而不是 `0x8ac7230489e80000`。
  调用点：`jsb_object_bindings.cpp` 的 `_godot_object_method`（参数 + 返回）、
  `_godot_object_set2`（setter 参数）、`_godot_object_get2`（getter 返回）。
  vararg 尾参没有声明类型，保持无 meta。

### 每引擎原语表

| 引擎 | `BigInt::Uint64Value` 实现 | 越界行为 |
|---|---|---|
| v8 / node | 官方 `v8::BigInt::Uint64Value(bool*)` | 有 `lossless` 反馈 |
| quickjs-ng | `JS_ToBigUint64`（内部 `JS_ToBigInt64Free`，注释「return the value mod 2^64」） | 不检查 |
| jsc | `JSValueToUInt64`（文档：BigInt 被 truncate 到 uint64_t） | 不检查 |
| web | `jsbi_Uint64Value`（JS 侧裸 `BigInt(val)` 写入） | 不检查 |

**`lossless` 不得参与分支**：只有 v8 能报，分支会让五引擎语义分叉。选「按位回绕」时五引擎
原生就一致。

### 数值槽的接受面（与引擎 `can_convert_strict` 对齐）

引擎 `Variant::can_convert_strict`（`core/variant/variant.cpp`）：

| 目标 | 引擎接受 | 对应 JS |
|---|---|---|
| `BOOL` | `INT` / `FLOAT` / `NIL` | number / bigint / null / undefined |
| `INT` | `BOOL` / `FLOAT` / `NIL` | boolean / number / bigint / null / undefined |
| `FLOAT` | `BOOL` / `INT` / `NIL` | boolean / number / bigint / null / undefined |

三处的 `STRING` 在引擎里都是**被注释掉**的 → 字符串仍拒。

- **`to_double` 不走引擎 `NumberValue`**：v8 的 `NumberValue` 是 `ToNumber()` 语义，
  对 BigInt 抛 TypeError 并留下 pending exception（只有 `Number()` 函数特判 BigInt）。
  改读 64 位有符号值，全引擎一致且精确。
- **`to_bool`** 放行 number / bigint / null / undefined，字符串拒。
- ⚠ **默认值优先级不能破坏**：`undefined` 在**有默认值**的位置仍走默认值替换，
  只有**无默认值**的位置才走转换。测这条必须用**默认值为 `true`** 的 bool 参数
  （默认值 `false` 与 `undefined` 的真值相同，两条假设不可区分）——
  实测判据：`String.strip_edges("  x", undefined)` 得 `"x"`（走了默认值 `true`）。

### 出口表示开关（`JSB_BIGINT_FOR_64BIT`，`src/jsb.config.h`）

两个宏不要混用：

| 宏 | 管什么 |
|---|---|
| `JSB_WITH_BIGINT` | 引擎构建**有没有** BigInt，以及 JS → Godot 方向是否接受 BigInt（入口） |
| `JSB_BIGINT_FOR_64BIT` | 64 位值**离开** Godot 时的表示：`1` = 超 2^53-1 出 `BigInt`（默认，与历史行为兼容）；`0` = 仍出 `Number`，超 2^53-1 静默丢位 |

- **只作用于出口**。关掉它**不会**让任何参数开始抛异常 —— 入口接受面由 `JSB_WITH_BIGINT` 单独决定。
- `JSB_BIGINT_FOR_64BIT=1` 依赖 `JSB_WITH_BIGINT=1`，非法组合在 `jsb.config.h` 里有 `#error` 拒绝。
- 运行期可见：`BIGINT_FOR_64BIT`（`godot-jsb` 模块），与 `BINDING_MODE` 同一用途 ——
  让单个集成场景能在两种配置下都通过，而不必为每种模式分别构建场景。
- 关掉后的可观察差异：RefCounted 的 ObjectID 往返不再无损（`get_instance_id()` 出丢位 `Number`）；
  `put_u64` 等**入口**行为与字节写入**完全不变**。

### 窄整型（int8/16/32、uint8/16/32、char32）保持范围检查

它们是真窄槽，静默截断才是缺陷。只有 64 位槽改按位。

### 测试落点

- C++：`src/runtime/tests/test_jsb_int64_conv.h`（须在 `jsb_test_main.cpp` 登记）。
- TS：`project/tests/int64/`（须在 `project/tests/start.ts` 的 `scenes` 登记）。
  **场景内不要调 `get_tree().quit()`** —— 那会抢在 `start.ts` 打印
  `GODOTJS_TEST_PROJECT_COMPLETED` 之前退出引擎，表现为「测试跑了但没有哨兵」。
