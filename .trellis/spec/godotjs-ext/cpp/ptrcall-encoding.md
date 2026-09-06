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
