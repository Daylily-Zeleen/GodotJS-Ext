# uint64 参数不抛异常、静态与动态行为一致

> 父任务：`09-06-lowprio-uint64-bigint-codegen`。共享技术方案见父任务 `design.md`。
> 本任务 = 父任务 R2，覆盖写方向（JS → Godot）。
> **依赖**：需要父任务 A（`09-24-uint64-bigint-return`）提供的 `Helper::to_uint64`。
> 文件集与 A 不重叠，可并行开工；但**最终形态必须用 `to_uint64`**，不能用
> `to_int64` + 位重解释凑合（否则 > 2^64 的 BigInt 在两腿间语义仍不一致）。

## Goal

uint64 参数在静态绑定下**不再抛异常**，并且静态与动态两条路径对同一输入写入**完全相同的字节**。

今天的行为：同一个逻辑操作，用方法调用抛异常、用属性赋值正常。

## Background（2026-09-24 实机实测）

`src/runtime/bridge/jsb_type_convert_direct.h` 的 `js_to_fixed_width_int<uint64_t>`
（实际文件 `src/runtime/bridge/jsb_type_convert_direct.h:111-140`）：

```cpp
if constexpr (std::is_same_v<CppT, uint64_t>) {
    if (wide < 0) return false;                                                          // ← 早退
    if (static_cast<uint64_t>(wide) > std::numeric_limits<uint64_t>::max()) return false; // ← 恒假
}
```

`wide` 来自 `JSToGD<int64_t>::convert` → `Helper::to_int64`，BigInt 走 `Int64Value()`
（mod 2^64 的有符号视角）。所以 `2^63` 读成 `-2^63` → `wide < 0` → 直接拒绝。

实测（同一构建，普通 number 输入，不含 BigInt）：

| 你写 | 静态绑定 | 动态绑定 |
|---|---|---|
| `put_u64(2^53)` | 成功 | 成功 |
| `put_u64(2^63)` | **抛 `bad argument 0: got number`** | 成功，写入 `0x8000000000000000` |
| `put_u64(2^63+1)` | **抛异常** | 成功，写入 `0x8000000000000000`（double 精度所限） |
| `put_u64(1e19)` | **抛异常** | 成功，写入 `0x8ac7230489e80000` |
| `put_u64(-1)` | **抛异常** | 成功，写入 `0xffffffffffffffff` |
| `rng.state = 2^63`（属性赋值） | 成功 | 成功 |

BigInt 输入同理：静态对 `2^63` / `2^63+1` / `2^64-1` 全部拒绝，动态全部按位精确写入。

**受影响接口**（api json 实测，uint64 参数共 75 个 / 30 个类，其中高频）：
`RenderingServer.instance_attach_object_instance_id`、
`PhysicsServer2D/3D.area_attach_object_instance_id` / `body_attach_object_instance_id`、
`NavigationServer2D/3D.region_set_owner_id` / `link_set_owner_id`（传的就是 ObjectID，
RefCounted 位为 1）、`FileAccess.seek` / `store_64`、`StreamPeer.put_u64`、`XMLParser.seek`、
`RenderingDevice.*`、一批 `OpenXR*`。

`node` 复用 v8 实现；写方向是**引擎无关的 C++ 逻辑**，五引擎共用同一份改动。

## Requirements

- **R2.1** 新增 `template <> struct JSToGD<uint64_t>`，走 `Helper::to_uint64`
  （**不再经 `int64_t`**）。从 `JSB_DIRECT_FIXED_INT` 列表移除 `uint64_t`
  （`jsb_type_convert_direct.h:157`）。
- **R2.2** 删除 `js_to_fixed_width_int` 的 `uint64_t` 分支
  （`:125-131`）与那句恒假的 `static_cast<uint64_t>(wide) > numeric_limits<uint64_t>::max()`。
- **R2.3** 窄无符号（uint8/16/32）与窄有符号（int8/16/32、char32）的 `wide < 0 → false`
  / 范围检查**保留**——它们是真的窄槽，静默截断才是缺陷。
- **R2.4** 语义口径：uint64 槽按 **mod 2^64 回绕**写入，不做范围拒绝。负值、超 64 位的 BigInt
  一律回绕（与引擎 `PtrToArg<uint64_t>` 及三个非 v8 引擎的 C API 一致）。
- **R2.5** 新增 **C++ doctest** 与 **TS 集成测试**覆盖以上行为（见 AC2.7 / AC2.8）。
- **R2.6** 顺带一致性（非必须）：`src/runtime/bridge/jsb_static_binding_util.h:115-131`
  的 `StaticBindingUtil<int64_t>` 旁补 `<uint64_t>` 特化（`get` → `to_uint64`，
  `set` → `new_unsigned_integer`）。全仓库无实例化，无实际影响。

## Acceptance Criteria

- [ ] **AC2.1** `StreamPeerBuffer.put_u64(v)` → 读回 8 字节，对
      `v ∈ {2^53, 2^63, 2^63+1, 2^64-1}`，以 **BigInt 与普通 number 两种形式**传入，
      在 `binding_mode ∈ {static, shared, dynamic}` 三腿下：
      **都不抛异常**，且写入字节等于 `v mod 2^64`，静态与动态**完全相同**。
- [ ] **AC2.2** `put_u64(-1)` 与 `put_u64(1e19)` 在静态腿不再抛异常，写入
      `0xffffffffffffffff` / `0x8ac7230489e80000`（与动态腿一致）。
- [ ] **AC2.3** `FileAccess.store_64` / `StreamPeer.put_u64` 的实际使用路径无回归
      （`2^53` 及以下的现有行为不变）。
- [ ] **AC2.4** 窄整数槽仍拒绝越界值：例如 `StreamPeer.put_8(300)` 与
      `put_u16(-1)` 保持现有拒绝行为（`int8/16/32`、`uint8/16/32`、`char32` 各抽一个验证）。
- [ ] **AC2.5** 五个引擎编译通过（改动是引擎无关的 C++ 逻辑）。
- [ ] **AC2.6** 父任务 AC8（全量 TS 集成测试）、AC9（C++ 测试）、AC10（bench 门禁）通过。
- [ ] **AC2.7** 新增 C++ 用例覆盖 `JSToGD<uint64_t>` 与 `js_to_fixed_width_int`
      （含窄类型仍拒绝越界），放在 `src/runtime/tests/test_jsb_int64_conv.h`
      （与 A 共用该文件时需串行，见 implement.md），在 `jsb_test_main.cpp` 登记。
- [ ] **AC2.8** 新增 TS 集成测试场景（可复用 `project/tests/int64/`）覆盖 AC2.1 / AC2.2 /
      AC2.4；断言一律经 `reportTestFailure`，守卫要断言数量，并做**负向验证**。
      **不进 bench**。

## Out of Scope

- 读方向（返回值丢位）→ `09-24-uint64-bigint-return`。
- 独立开关宏 → `09-24-uint64-bigint-switch`。
- BigInt 用于内置类型构造器 / 运算符（`new Vector2i(2n, 3)` 今天被拒）→ 父任务 Out of Scope。

## 交点（与其他子任务）

- `src/runtime/impl/jsb_primitive_conv.h`：**由 A 创建**，本任务只调用 `Helper::to_uint64`，
  **不得**另起一份。
- `src/runtime/bridge/jsb_type_convert_direct.h`：本任务改 `JSToGD<uint64_t>`；
  D 改 `JSToGD<float/double/bool>` → **串行**。
- `src/runtime/bridge/jsb_static_binding_util.h`：本任务补 `StaticBindingUtil<uint64_t>`；
  D 补 `StaticBindingUtil<bool>` → **串行**。
- `src/runtime/tests/test_jsb_int64_conv.h`：A 建头 + 登记，本任务追加 `JSToGD<uint64_t>` 用例 → **串行**。
- 结论：**A → (B → D)** 或 **A → (D → B)**，本任务与 D 不可并行。

## Technical Notes

- 动态腿「恰好正确」的原因：无类型 `js_to_gd_var` 的 BigInt 分支
  （`jsb_type_convert.cpp:553-556`）只调 `Int64Value()`，而 `Int64Value` 本身是 mod 2^64，
  于是 `PtrToArg<uint64_t>::encode` 拿到的就是按位正确的值。
- 静态腿 `uint64_t` 参数走 `Args<uint64_t>`（生成物 26 处），`marshal_one<uint64_t>` →
  `try_js_to_gd` → `JSToGD<uint64_t>`。修 `JSToGD<uint64_t>` 即可覆盖。
- `put_u64` 是 `Ret<void>, Args<uint64_t>`（`k_shared_thunks[1454]`），
  与返回值改动无关，两任务互不阻塞。
- 三腿都要验证：`SConstruct:37` 默认 `binding_mode=shared`。
