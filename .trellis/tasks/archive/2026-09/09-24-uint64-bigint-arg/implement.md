# 实施清单（B：写方向）

> 共享技术方案见父任务 `../09-06-lowprio-uint64-bigint-codegen/design.md` §3.2。
> 本文件只列执行顺序、验证命令与回滚点。

## 前置

- 需要 A 提供的 `jsb::impl::Helper::to_uint64`（A 的步 1）。
- A 未落地时可先开工，但必须按「`to_int64` + 位重解释」过渡；
  **A 落地后切到 `to_uint64`**。最终形态不得残留过渡写法（否则 > 2^64 的 BigInt
  在两腿间语义仍不一致）。

## 顺序

| 步 | 文件 | 内容 |
|---|---|---|
| 1 | `src/runtime/bridge/jsb_type_convert_direct.h` | 新增 `JSToGD<uint64_t>`（走 `to_uint64`）；从 `JSB_DIRECT_FIXED_INT` 移除 `uint64_t`（`:157`） |
| 2 | 同上 | 删除 `js_to_fixed_width_int` 的 `uint64_t` 分支（`:125-131`）与恒假的 max 比较 |
| 3 | `src/runtime/bridge/jsb_static_binding_util.h` | （顺带，非必须）补 `StaticBindingUtil<uint64_t>`（`:115-131` 旁） |
| 4 | `src/runtime/tests/test_jsb_int64_conv.h` | C++ 用例：`JSToGD<uint64_t>` 按位写入；窄类型仍拒绝越界 |
| 5 | `src/runtime/tests/jsb_test_main.cpp` | 登记新头（若 A 已登记则跳过） |
| 6 | `project/tests/int64/` | TS 场景：uint64 写读字节、纯 number 输入、窄整数仍拒绝 |
| 7 | `project/tests/start.ts` | 登记新场景（若 A 已登记则跳过） |

步 1-3 一次编；步 4-5 一次（`tests=yes`）；步 6-7 只需 `tsc`。

### C++ 测试要点

- 与 A 共用 `src/runtime/tests/test_jsb_int64_conv.h` —— **需串行**，
  或由同一人一次改完两个任务的用例。
- 窄类型拒绝越界的用例要有**负向验证**：人为放行一次确认 FAILED，再还原。

### 注意

- 窄类型（int8/16/32、uint8/16/32、char32）的范围检查**保留**
  （`js_to_fixed_width_int` 的非 uint64 分支不动）。
- 改动是**引擎无关的 C++ 逻辑**，五引擎共用，不需要动 `impl/*`。
- 静态腿 uint64 参数走 `Args<uint64_t>`（生成物 26 处）→ `marshal_one<uint64_t>` →
  `try_js_to_gd` → `JSToGD<uint64_t>`。修 `JSToGD<uint64_t>` 即覆盖。
- 动态腿「恰好正确」的原因：无类型 `js_to_gd_var` 的 BigInt 分支
  （`jsb_type_convert.cpp:553-556`）只调 `Int64Value()`，而它是 mod 2^64，
  于是 `PtrToArg<uint64_t>::encode` 拿到按位正确的值。**本任务不需要改动态腿**。

## 交点（与其他子任务）

- `src/runtime/impl/jsb_primitive_conv.h`：**由 A 创建**，本任务只调用 `Helper::to_uint64`。
- `src/runtime/bridge/jsb_type_convert_direct.h` / `jsb_static_binding_util.h`：
  本任务与 D 都改这两个文件 → **必须串行**（本任务先，D 后；或反之）。
- `src/runtime/tests/test_jsb_int64_conv.h`：A 建头 + 登记，本任务追加用例 → **串行**。
- 结论：**A → (B → D)** 或 **A → (D → B)**，本任务与 D 不可并行。

## 验证

```bash
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
godot --headless --path ./project --jsb-run-tests     # C++ 套件
godot --audio-driver Dummy --headless --path project  # TS 集成
```

探针：`probe-num.ts`（纯 number 输入对照）、`probe-uint64.ts`（BigInt 输入字节级）、
`probe-prop.ts`（属性 vs 方法分歧）。

**期望值（三腿一致）**：

| 输入 | 改动前（静态） | 改动后（静态 = 动态） |
|---|---|---|
| `put_u64(2^53)`（number） | 成功，`0x0020000000000000` | 不变 |
| `put_u64(2^63)`（number） | **抛 `bad argument 0: got number`** | 成功，`0x8000000000000000` |
| `put_u64(1e19)`（number） | **抛异常** | 成功，`0x8ac7230489e80000` |
| `put_u64(-1)`（number） | **抛异常** | 成功，`0xffffffffffffffff` |
| `put_u64(2^63+1)`（BigInt） | **抛异常** | 成功，`0x8000000000000001` |
| `put_u64(2^64-1)`（BigInt） | **抛异常** | 成功，`0xffffffffffffffff` |

**必须同时验证窄整数仍拒绝越界**（AC2.4）：
`StreamPeer.put_8(300)`、`put_u16(-1)`、`FileAccess.store_8(300)` 各抽一个，
保持现有拒绝行为。

## 回滚点

单文件改动（`jsb_type_convert_direct.h`），整体可回滚；`jsb_static_binding_util.h` 独立可回滚。

## 完成前检查

- [ ] 三腿都编过、探针都跑过，期望值表逐行核对
- [ ] 窄整数越界仍被拒绝（负向验证：人为放行一次确认测试 FAILED，再还原）
- [ ] `git status` 干净（探针已移出 `project/`）
- [ ] C++ 用例与 TS 场景都已登记并跑过
- [ ] 新守卫做了负向验证（人为放行一次确认 FAILED，再还原）
- [ ] AC2.1 - AC2.8 逐条勾选
