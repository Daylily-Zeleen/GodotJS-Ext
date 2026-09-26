# 设计说明

## 1. `jsb_primitive_conv.h` 的命名空间与内联

### 问题

四个 shim（v8 / quickjs / jsc / web）各自 include 该头的位置在**自己的 pch 之前**：

| shim | 该头 | pch |
|---|---|---|
| `v8/jsb_v8_helper.h` | `31` | `33`（`jsb_v8_pch.h`） |
| `quickjs/jsb_quickjs_helper.h` | `29` | `34` |
| `jsc/jsb_jsc_helper.h` | `29` | `34` |
| `web/jsb_web_helper.h` | `29` | `34` |

`_FORCE_INLINE_` 由 `godot_cpp/core/defs.hpp` 定义（经 `jsb_bridge_pch.h → memory.hpp → defs.hpp`），
在 pch 之前**未定义**。实测（`g++ -fsyntax-only` 探针）：只 include 该头 → 宏不存在；
先 include `jsb_settings.h`（v8 现状）→ 宏存在。

### 方案

1. 把该头的 `#include` 移到四个 shim 的 pch **之后**（统一到 `to_string` / `new_string` 所在区域）。
2. 该头内部把 `inline` 改为 `_FORCE_INLINE_`。
3. 命名空间：`jsb::impl::internal`（与 `thunks_common.h` 的 `jsb::...::internal::translate_return` 命名习惯一致）。
4. 四个 shim 的 `Helper` 保留同名转发（`to_int64` / `to_uint64` / `to_double` / `to_bool` /
   `new_integer` / `new_unsigned_integer`），与 `to_string` / `new_string` 并列，风格统一。

**风险**：移动 include 会改变符号可见性顺序，可能暴露隐藏的 include 依赖（例如某 shim 依赖
该头间接引入的东西）。缓解：四个 shim 一次改完，全量重编一次验证；若报未定义符号，
说明存在反向依赖，需在 pch 里补 include 而不是保留旧顺序。

## 2. `GDToJS<T>` 的形态

### 必须处理的约束

`Ret<T>::translate_return` 的缓冲有两种形态，由**调用点**决定（不是模板可选的）：

| thunk | 缓冲声明 | `ReturnBufT` |
|---|---|---|
| `class_methods.h` | `godot::Variant ret;` | `godot::Variant` |
| `builtin_methods.h` | `typename RetT::encoded_type ret_val{};` | `PtrToArg<T>::EncodeT` |
| `utility_functions.h` | 同上 | 同上 |

因此 `GDToJS<T>` 的签名不能固定 `T`：

```cpp
namespace jsb::internal {
template <typename T>
struct GDToJS {
    // 主模板：回退（String / 向量 / 对象 / 容器 / Variant 自身 …）
    template <class SrcT>
    static _FORCE_INLINE_ void convert(v8::Isolate *p_isolate,
            const v8::Local<v8::Context> &p_context,
            const SrcT &p_src, v8::Local<v8::Value> &r_out) {
        // SrcT 为 Variant 时直接用；否则解出 C++ 值再包 —— 但**只有**回退路径才允许
        // 出现 Variant。标量走特化，不经过这里。
        ...
    }

    // 标量特化：直接对编码槽取值，零 Variant
    template <> struct GDToJS<uint64_t> { static ... convert(..., const uint64_t &v, ...) {
        r_out = impl::Helper::new_unsigned_integer(p_isolate, v);
    }};
    // 同法：int64_t / int32_t / uint32_t / int16_t / uint16_t / int8_t / uint8_t / char32_t
    //       bool / float / double
};
}
```

### 覆盖范围（与 `JSToGD<T>` 对称）

- **特化（编码槽直取，零 Variant）**：`bool`、全部定宽整数（含 `uint64_t`/`int64_t`/`char32_t`）、`float`、`double`。
- **主模板回退**：`Variant`、`String`、`StringName`、`NodePath`、`RID`、`Object*`（及派生）、
  向量/矩形/变换族、`Array`/`Dictionary`/Packed 系、枚举。

回退路径的两种输入要保持正确：
- `SrcT == Variant`：直接 `gd_var_to_js`。
- 其它 `SrcT`（编码槽）：解出 `T` 后仍需交给引擎的 `Variant` 出口 —— 这是**固有**的，
  因为这些类型的 JS 表示依赖 `Variant` 机制。Variant 只在这一层出现，标量已绕开。

### `translate_return` 收敛后的形态

```cpp
template <typename T>
struct Ret {
    template <class ReturnBufT>
    static _FORCE_INLINE_ void translate_return(v8::Isolate *p_isolate,
            const v8::Local<v8::Context> &p_context, ReturnBufT &p_ret_val,
            const v8::FunctionCallbackInfo<v8::Value> &p_info) {
        if constexpr (has_return) {
            v8::Local<v8::Value> out;
            jsb::internal::GDToJS<type>::convert(p_isolate, p_context, p_ret_val, out);
            p_info.GetReturnValue().Set(out);
        }
    }
};
```

删除：`std::is_same_v<type, uint64_t>` 分支、`internal::translate_uint64_return`，
以及非 Variant 路径的 `Variant(PtrToArg<type>::convert(...))` 包装。

## 3. 出口三态与别名（决策已定）

### 3.1 宏

```cpp
// jsb.config.h
// 64 位返回值是否固定为 BigInt（仅在 JSB_BIGINT_FOR_64BIT=1 时有意义）。
// 0（默认）：值相关 —— 能放进 number 的仍出 number，只有越 2^53-1 才出 BigInt。
// 1         ：恒 BigInt —— 类型确定，调用者无需判类型、无需混用两套运算。
#define JSB_64BIT_RETURN_FIXED_BIGINT 0
#if JSB_64BIT_RETURN_FIXED_BIGINT && !JSB_BIGINT_FOR_64BIT
#   error "JSB_64BIT_RETURN_FIXED_BIGINT=1 requires JSB_BIGINT_FOR_64BIT=1"
#endif
```

### 3.2 固定模式下**去掉 Int32 快路径**（已定）

若保留快路径，`FIXED_BIGINT=1` 下小值（`get_process_frames()` 等）仍出 `number`，
类型依旧不确定 —— 该宏就失去了意义。因此固定模式下 64 位出口**一律** BigInt：

```cpp
v8::Local<v8::Value> new_integer(v8::Isolate *p_isolate, const int64_t p_val) {
#if JSB_64BIT_RETURN_FIXED_BIGINT
    return v8::BigInt::New(p_isolate, p_val);          // 无条件
#else
    if (fits_int32) return v8::Int32::New(p_isolate, downscale);
  #if JSB_BIGINT_FOR_64BIT
    if (p_val > JSB_MAX_SAFE_INTEGER || p_val < -JSB_MAX_SAFE_INTEGER)
        return v8::BigInt::New(p_isolate, p_val);
  #endif
    return v8::Number::New(p_isolate, (double)p_val);
#endif
}
```

`new_unsigned_integer` 同构（`BigInt::NewFromUnsigned`）。

**代价（实测）**：恒 BigInt 时每次返回都分配；node 22 上 200 万次 `BigInt(2^53+i)` 为 71.4ms，
而 number 基线 8.6ms。小值 `BigInt(i & 255)` 为 21.1ms（V8 对小值有内部复用）。
`get_instance_id` 现有基准 111 ns/次，恒 BigInt 会抬高其返回段。

### 3.3 typings 别名（已定）

- **入参**：`int64` / `uint64` **恒为** `number | bigint`（运行时本就两者都收，
  `JSB_BIGINT_FOR_64BIT` 只管出口，不影响接受面）。
- **返回值**：默认**复用**入参别名；仅 `JSB_64BIT_RETURN_FIXED_BIGINT=1` 时改用专用别名
  `int64_ret` / `uint64_ret`。

生成矩阵：

| `FOR_64BIT` | `FIXED_BIGINT` | 别名 | 返回值位置类型 |
|---|---|---|---|
| 0 | 无关 | `type int64 = number \| bigint` | 同上（**过宽**：实际恒 number，见下） |
| 1 | 0（默认） | `type int64 = number \| bigint` | 同上（精确） |
| 1 | 1 | `type int64 = number \| bigint` + `type int64_ret = bigint` | `int64_ret`（精确） |

`FOR_64BIT=0` 时返回值实际恒为 `number`，但别名仍声明 `number | bigint` ——
这是**安全的方向**（声明宽于实际，不会放过错误用法，只是要求用户多收窄一次）。
若将来要精确，再加 `int64_ret = number`；本轮不做，记为已知不精确。

## 4. 负 Number → uint64 的告警

保留按位模式语义（拒绝会造成 static/dynamic 分叉），仅在 `JSB_DEBUG` 告警；
定位是「位模式回传 / 引擎对齐」，不是「扩大表示范围」：

```cpp
} else if (v < 0.0 && v >= -9223372036854775808.0) {
#if JSB_DEBUG
    JSB_LOG(Warning, "negative number into a uint64 slot: %d reinterpreted as 0x%x", (int)v, ...);
#endif
    r_val = (uint64_t)(int64_t)v;
}
```

告警文案必须走 `%d` + `(int)`（`sprintf` 无 `%lld`）。
