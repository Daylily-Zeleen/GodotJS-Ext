# 实施清单（D：数值槽对齐引擎语义）

> 共享技术方案见父任务 `../09-06-lowprio-uint64-bigint-codegen/design.md` §3.4。
> 本文件只列执行顺序、验证命令与回滚点。

## 前置

- 需要 A 提供的 `Helper::to_int64`（`jsb_primitive_conv.h`）。
- ⚠ **与 A 在 `src/static_binding/thunks/thunks_common.h` 上重叠**（A 改 `Ret<T>`，
  本任务改 `probe_vt`）。两者对该文件的修改必须**串行**，或由同一人一次改完。
  并行时先通过 `hub` 约定谁先落该文件。

## 顺序

| 步 | 文件 | 内容 |
|---|---|---|
| 1 | `src/static_binding/thunks/thunks_common.h` | `probe_vt` 加 `IsBigInt() → Variant::INT`（与 `IsInt32` 同组，`JSB_WITH_BIGINT` 门控） |
| 2 | `src/runtime/bridge/jsb_type_convert_direct.h` | `JSToGD<float>` / `<double>` 加 BigInt 分支（走 `NumberValue`）；`JSToGD<bool>` 加 number+bigint+null/undefined |
| 3 | `src/runtime/bridge/jsb_type_convert.cpp` | `can_convert_strict<BOOL>`（`:565-567`）与 typed `js_to_gd_var` 的 `BOOL` 分支（`:200-206`）同步 |
| 4 | `src/runtime/bridge/jsb_static_binding_util.h` | 补 `StaticBindingUtil<bool>`；`<float>`/`<double>` 加 BigInt 分支 |
| 5 | `src/static_binding/thunks/builtin_operators.h` | `R == int64_t` / `R == double` / `R == bool` **三个**分支都改走转换原语（`to_int64`/`to_double`/`to_bool`） |
| 6 | `src/static_binding/thunks/type_compatible.h` | 审计表 `BOOL`/`INT`/`FLOAT` 三行补 `NIL` 与 `BIGINT` |
| 7 | `src/runtime/tests/test_jsb_int64_conv.h`（新建或追加） | C++ 用例：`probe_vt(BigInt)==INT`、`JSToGD<float/double/bool>` 接受面 |
| 8 | `src/runtime/tests/jsb_test_main.cpp` | 登记新头 |
| 9 | `project/tests/int64/`（新建或追加） | TS 场景：三槽的 BigInt / number / null / undefined |
| 10 | `project/tests/start.ts` | 登记新场景 |

步 1-6 一次编；步 7-8 一次（`tests=yes`）；步 9-10 只需 `tsc`。

### 注意

- **必须同批**：只加 `probe_vt` / 只放行 float / 只放行 bool 而不修 `builtin_operators.h`
  会引入新缺陷（新类型进入槽后被裸 `As<>` 错误解释）。这是禁止项。
- **`to_double` / `to_bool` 可能需要新建**：`jsb_primitive_conv.h`（A 创建）目前规划的是
  `to_int64` / `to_uint64` / `new_integer` / `new_unsigned_integer`。本任务若需要
  `to_double` / `to_bool`，需与 A 约定后由 A 追加（**不要在本任务里另起一份**）。
  可先复用各引擎 shim 的 `Value::NumberValue` / `BooleanValue`。
- **bool 真值映射**：`0`/`0n`/`null`/`undefined` → false，其余 number/bigint → true。
  字符串仍拒（引擎的 `BOOL` 接受面里 `STRING` 被注释掉）。
- **bool 测试目标必须选无 `default_value` 的位置**（否则 `undefined` 被默认值替换，
  掩盖真实转换行为）：
  - class 方法（**推荐**）：`Object.set_block_signals(enable)` + `is_blocking_signals()` 读回
    —— 无 `default_value`，`Object` 必然已绑定（在 `build_profile.json` 里）。
  - 内置静态（**可用**）：`Projection.create_depth_correction(flip_y)` —— 无 `default_value`。
    内置类型不受 `build_profile.json` 裁剪（实测 `create_depth_correction(true)` OK）。
  - **反例（运行时不可达）**：普通 class 若不在 `build_profile.json` 的 `enabled_classes`
    里就拿不到，例如 `AStarGrid2D.set_jumping_enabled`（实测连 `true` 都 THREW）。
  - 对照组的默认值必须是 `true` 才能判别：用 `String.strip_edges(s, left=true, right=true)`
    —— `strip_edges("  x", undefined)` 得 `"x"`（默认值替换）；若走真值转换会得 `"  x"`。
    **实测已确认得 `"x"`**。
  - **不能用 `Rect2.intersects(b, include_borders=false)`**：默认值是 `false`，
    与 `undefined` 的真值相同，两条假设不可区分（无判别力）。
- **默认值优先级不能破坏**：`undefined` 在**有默认值**的位置必须仍走默认值替换
  （`shared_class_method_thunk` 的 `md.defaults` / `class_method_thunk` 的
  `substitute_default`）。改动只作用于「无默认值位置走到转换器」之后。
- `probe_vt` 的两个模式（`probe_prefer_primitive_types` / `probe_prefer_object_types`）
  都要覆盖到；BigInt 分支放在两者都经过的位置。
- **不改 codegen**：`find_ctor_*` / `find_op_*` 的筛选顺序由
  `misc/build/static_binding_codegen.py` 决定，本任务只让 `probe_vt` 结果变正确。
- 浮点槽（`new Vector2(2n, 3n)`）**本轮支持**：`JSToGD<float>` 加 BigInt 分支走
  `to_double`（`Number()` 语义）。**必须与 `probe_vt` 同批改** —— 只改 `probe_vt`
  会让筛选通过而 marshal 拒绝，违反 `type_compatible.h` 的契约。
- C++ 测试门控：`probe_vt` 在 `thunks_common.h`，受 `JSB_WITH_STATIC_BINDINGS` 门控，
  用例需要同样的门控（参考 `test_jsb_quickjs_runtime.h` 的 `#if JSB_WITH_QUICKJS` 形态）。

## 验证

```bash
# 三腿各一次
scons platform=windows target=editor binding_mode=static  tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
scons platform=windows target=editor binding_mode=shared  tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
scons platform=windows target=editor binding_mode=dynamic tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no

godot --headless --path ./project --jsb-run-tests     # C++ 套件
godot --audio-driver Dummy --headless --path project  # TS 集成
```

探针：`research/probe-ops.ts`（用法见 `research/README.md`）。

**期望值（三腿一致）**：

| 断言 | 改动前 | 改动后 |
|---|---|---|
| `new Vector2i(2n, 3)`（int 槽） | **拒绝** `no suitable constructor` | 成功，x=2 y=3 |
| `new Vector2(2n, 3n)`（float 槽） | **拒绝** | 成功，x=2 y=3 |
| `new Vector2(1,2).OP_MULTIPLY(2n)` | 成功（动态回退）x=2 y=4 | 成功 x=2 y=4（**静态路径**） |
| `set_block_signals(1)` | **THREW** | 成功，`is_blocking_signals()` 为 true |
| `set_block_signals(0)` / `(1n)` / `(0n)` | **THREW** | 成功，分别 false / true / false |
| `set_block_signals(null)` / `(undefined)` | **THREW** | 成功，均为 false |
| `set_block_signals("")` | THREW | THREW（字符串仍拒，不得变） |
| `Rect2.intersects(b, undefined)`（有默认值） | 返回 false（=默认值） | 返回 false（**不得变**） |
| `Projection.create_depth_correction(undefined)`（无默认值） | **THREW** | 成功（undefined → false） |
| `new Vector2i(2, 3)` / `OP_MULTIPLY(2)` | 成功 | 成功（不得回退） |

## 回滚点

| 步 | 回滚方式 |
|---|---|
| 1-3 | 整体回滚（三处同批，不可只回其中一处） |
| 4-5 | 单独回滚（测试头） |
| 6-7 | 单独回滚（TS 场景） |

## 完成前检查

- [ ] 三腿都编过、探针都跑过，期望值表逐行核对
- [ ] `new Vector2(2n, 3n)` 确认是**清晰拒绝**（读完整错误信息），不是垃圾值
- [ ] C++ 用例与 TS 场景都已登记（`jsb_test_main.cpp` / `start.ts`）
- [ ] 新守卫做了负向验证（人为削减一次确认 FAILED，再还原）
- [ ] `git status` 干净（探针已移出 `project/`）
- [ ] AC4.1 - AC4.9 逐条勾选
