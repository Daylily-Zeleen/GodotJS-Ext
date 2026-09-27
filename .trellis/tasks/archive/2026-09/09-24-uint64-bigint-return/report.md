# 任务 A：int64/uint64 读方向（BigInt 出口）— 执行记录

## 目标

64 位整数从 Godot 返回 JS 时不再丢信息；超 2^53 按 meta 出 BigInt（uint64 无符号）。
验收核心：`instance_from_id(get_instance_id()) === obj` 三腿成立。

## 已完成的改动

| 文件 | 改动 |
|---|---|
| `src/runtime/impl/jsb_primitive_conv.h`（新建） | 引擎无关的 `to_int64` / `to_uint64` / `to_double` / `to_bool` / `new_integer` / `new_unsigned_integer`。双边阈值；uint64 无符号出口 |
| `src/runtime/impl/{v8,quickjs,jsc,web}/jsb_*_helper.h` | 四份重复体删除，改为委托到上面这个头；各加 `to_uint64` / `to_double` / `to_bool` / `new_unsigned_integer` 转发 |
| `src/runtime/impl/quickjs/jsb_quickjs_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → `JS_ToBigUint64` |
| `src/runtime/impl/jsc/jsb_jsc_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → `JSValueToUInt64` |
| `src/runtime/impl/web/jsb_web_primitive.{h,cpp}` | 加 `BigInt::Uint64Value` → `jsbi_Uint64Value`（bridge 侧已存在） |
| `src/static_binding/thunks/thunks_common.h` | 新增 `internal::translate_uint64_return`；`Ret<T>` 在 `type == uint64_t` 时走它 |
| `src/runtime/bridge/jsb_type_convert.{h,cpp}` | `gd_var_to_js` 新增带 `GDExtensionClassMethodArgumentMetadata` 的重载（meta = `INT_IS_UINT64` 时走 `new_unsigned_integer`）；旧 5 参重载保留为转发 |
| `src/runtime/bridge/jsb_object_bindings.cpp` | `:511` 方法返回、`:579` 属性 getter 传入 `get_return_metadata()` |
| `src/runtime/tests/test_jsb_int64_conv.h`（新建） | 5 个用例：双边阈值、负值位保持、无符号出口、`to_int64`/`to_uint64` 位模式、`to_double`/`to_bool` 接受面 |
| `src/runtime/tests/jsb_test_main.cpp` | 登记新测试头 |
| `project/tests/int64/{test-int64.ts,Int64.tscn}`（新建） | TS 场景：u64/i64 回读位精确、ObjectID 往返、无符号出口 |
| `project/tests/start.ts` | 登记 `res://tests/int64/Int64.tscn` |

## 设计决策（与初稿的偏离，均已实测支撑）

1. **`to_double` 不走引擎 `NumberValue`**：v8 的 `NumberValue` 是 `ToNumber()` 语义，
   对 BigInt 抛 TypeError 并留下 pending exception。改读 64 位有符号值（全引擎一致、精确）。
2. **`lossless` 不参与分支**：只有 v8 能报，分支会让五引擎分叉。
3. **R1.5 只给「返回方向」加 meta**：参数方向（`js_to_gd_var`）的 Variant INT 槽
   存的就是同样的 64 位，不需要 meta。加一个用不到的参数是死重。
4. **`jsb_object_bindings.cpp:612`（setter）未加 meta**：同上，写方向不需要。

## 验证证据

| 腿 | 构建 | C++ 套件 | TS 套件 |
|---|---|---|---|
| shared | OK（91.3s 首次 / 16.6s 增量） | 55 用例 / 687 断言 / SUCCESS | `COMPLETED=1 FAILED=0`，`INT64-DIAG checks=26 expected=26` |
| static | OK | 55 用例 / 687 断言 / SUCCESS | `COMPLETED=1 FAILED=0`，`INT64-DIAG checks=26 expected=26` |
| dynamic | OK | 55 用例 / 687 断言 / SUCCESS | `COMPLETED=1 FAILED=0`，`INT64-DIAG checks=26 expected=26` |

三腿一致 → AC1.1 / AC1.2 / AC1.3 / AC1.4 / AC1.7 / AC1.8 达成。

### 负向验证（已做，双向）

- **C++**：把 `new_integer(isolate, -JSB_MAX_SAFE_INTEGER - 1)` 改成 `-JSB_MAX_SAFE_INTEGER`
  → doctest 报 `assertions: 687 | 686 passed | 1 failed | Status: FAILURE!`，失败行号正是
  `test_jsb_int64_conv.h(80)`。还原后恢复 687/687 SUCCESS。
- **TS**：把 `EXPECTED_CHECKS` 从 26 改成 27 → `COMPLETED=0 FAILED=1`，
  日志 `GODOTJS_TEST_PROJECT_FAILED: int64 coverage` + `checks=26 expected=27`。
  还原后恢复 `COMPLETED=1 FAILED=0`。

## 过程中发现并修掉的自身缺陷

1. `to_double` 初版调 `NumberValue` → 对 BigInt 会抛异常（见上「设计决策 1」）。
2. `to_double` 第二版用 `lossless` 拒绝 > 64 位的 BigInt → 只有 v8 有该信息，会导致引擎分叉。
3. TS 场景 `finally` 里调了 `get_tree().quit()` → 抢在 `start.ts` 打 COMPLETED 哨兵之前退出引擎，
   表现为「测试跑了但无哨兵」。其他场景都不 quit，由 `start.ts` 统一收尾。
4. TS 里 `new Node()` 后调 `free()`：typings 无 `free`，且会留泄漏。改用 `this`（已在树上的节点）。
5. `Resource id exact bits` 断言：`to_string()` 走 `itos` 打印**有符号**视图，而 `resId` 已是
   **无符号** BigInt → 比较前需两边都归一化到位模式。
6. 测试里 `1 << 40` 是 int 移位（UB，MSVC 报 C4293）→ 改 `(int64_t)1 << 40`。

## spec 沉淀（父任务 AC12）

已写入 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`，新增一节
「int64 / uint64 按位契约与 BigInt 阈值（jsb_primitive_conv.h）」，含：
读方向双边阈值与按位契约、每引擎原语表、`lossless` 不得参与分支的理由、
数值槽接受面与 `can_convert_strict` 的对应表、`to_double` 为何不走 `NumberValue`、
窄整型保持范围检查、测试落点（含「场景内不要 quit()」这条坑）。

## 遗留 / 待办

- AC1.5：五个引擎编译 —— 本机只有 windows+v8；quickjs-ng / jsc / node 由 CI 覆盖。
- AC1.6 / AC10：`python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix` 门禁（进行中）。
- AC1.7 要求「无泄漏」：三腿 C++ 套件均 exit 0，日志未见 Orphan StringName 报告。
  注：`.trellis/spec/godotjs-ext/test/index.md` 记载 3 个已知遗留 orphan
  （start.ts 的 call_deferred 方法名字面量），属既有、非本轮引入。
