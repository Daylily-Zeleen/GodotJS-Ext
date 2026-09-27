# 实施清单（A：读方向）

> 共享技术方案见父任务 `../09-06-lowprio-uint64-bigint-codegen/design.md` §3.1。
> 本文件只列执行顺序、验证命令与回滚点。

## 前置

无。可与 `09-24-uint64-bigint-arg` 并行；本任务**先创建** `jsb_primitive_conv.h` 并固定其签名
（B 会消费 `Helper::to_uint64`）。

## 顺序

| 步 | 文件 | 内容 |
|---|---|---|
| 1 | `src/runtime/impl/jsb_primitive_conv.h`（新建） | `to_int64` / `to_uint64` / `new_integer` / `new_unsigned_integer`；**D 需要 `to_double` / `to_bool` 时由本步追加**（见「交点」） |
| 2 | `src/runtime/impl/{v8,quickjs,jsc,web}/jsb_*_helper.h` | 四份 `to_int64` / `new_integer` 改为委托，删除重复体 |
| 3 | `src/runtime/impl/quickjs/jsb_quickjs_primitive.{h,cpp}` | `BigInt::Uint64Value` → `JS_ToBigUint64`（`quickjs.h:873`） |
| 4 | `src/runtime/impl/jsc/jsb_jsc_primitive.{h,cpp}` | `BigInt::Uint64Value` → `JSValueToUInt64(ctx, val, nullptr)`（`JSValueRef.h:506`） |
| 5 | `src/runtime/impl/web/jsb_web_primitive.{h,cpp}` | `BigInt::Uint64Value` → 已有 `jsbi_Uint64Value`；`jsb_check(res)` 改为返回 false |
| 6 | `src/static_binding/thunks/thunks_common.h` | `Ret<T>` 的 uint64 分支 + `internal::translate_uint64_return` |
| 7 | `src/runtime/bridge/jsb_type_convert.{h,cpp}` | 带 `GDExtensionClassMethodArgumentMetadata` 的重载 |
| 8 | `src/runtime/bridge/jsb_object_bindings.cpp` | `:483` / `:511` / `:569` / `:612` 传入 meta |
| 9 | `src/runtime/tests/test_jsb_int64_conv.h`（新建） | C++ 用例：`to_int64` / `new_integer` / `new_unsigned_integer` 的阈值与位模式 |
| 10 | `src/runtime/tests/jsb_test_main.cpp` | 登记新头 |
| 11 | `project/tests/int64/`（新建） | TS 场景：ObjectID 往返、uint64 回读位精确、阈值下界不变 |
| 12 | `project/tests/start.ts` | 登记新场景到 `scenes` 列表 |

步 1-5 一次编；步 6 一次；步 7-8 一次；步 9-10 一次（`tests=yes`）；步 11-12 只需 `tsc`。

### C++ 测试要点

- 位置：`src/runtime/tests/test_jsb_int64_conv.h`，被测逻辑在 runtime 层
  （见 `.trellis/spec/godotjs-ext/test/doctest.md`）。
- 形态：`TEST_CASE("[runtime] [jsb.int64] ...")`，用 `jsb_test_helpers.h` 的
  `GodotJSScriptLanguageIniter` + `JSB_TESTS_EXECUTION_SCOPE` 拿 isolate/context。
- **必须在 `src/runtime/tests/jsb_test_main.cpp` 的 include 列表登记**，否则用例不注册。
- 与 B / D 共用该文件时需串行：B 加 `JSToGD<uint64_t>` 用例、D 加 `probe_vt` 与
  `JSToGD<float/double/bool>` 用例。**登记只需一次**（先到者建头 + 登记，
  后到者追加用例）。

### 注意

- v8 / node **不需要改**：v8 直接用 `third/v8/include/v8-primitive.h:749` 的
  `Uint64Value`；node 走 `impl/node/jsb_node_helper.h` → `../v8/jsb_v8_helper.h`。
- web 的 `Uint64Value` 实现（`impl/web/bridge/src/monolith.ts:659`、`impl/web/js/jsbb.impl.js:478`）
  裸 `BigInt(val)` 写入**就是 mod 2^64**，保留，只补注释说明按位契约。
  改了 `monolith.ts` 需要重编 web bridge（`src/runtime/impl/web/bridge` 的 `tsc`）。
- `JSB_MAX_SAFE_INTEGER` 的值**不动**（`src/jsb.config.h:160` DO NOT CHANGE），只改判据为双边。
- `Ret<uint64_t>` 与 `Ret<int64_t>` 已是不同模板实例（生成物
  `src/static_binding/gen/dispatch_class.gen.cpp:893-902`、`:1552`），**不需要改 codegen**。
- `Object.get_instance_id` = `k_shared_thunks[852]` =
  `shared_class_method_thunk<false, Ret<uint64_t>, Args<>>`。

## 交点（与其他子任务）

| 交点 | 内容 |
|---|---|
| `src/runtime/impl/jsb_primitive_conv.h` | 本任务**创建**并固定签名；B / D 只调用。D 需要 `to_double` / `to_bool` 时**由本任务追加**（D 不另起一份）。规划：`to_int64` / `to_uint64` / `to_double` / `to_bool` / `new_integer` / `new_unsigned_integer` |
| `src/static_binding/thunks/thunks_common.h` | 本任务改 `Ret<T>`；D 改 `probe_vt`。**必须串行**（本任务先，D 后） |
| `src/runtime/tests/test_jsb_int64_conv.h` | 本任务建头 + 登记；B / D 追加用例。**串行** |
| `src/runtime/bridge/jsb_type_convert_direct.h` | B 改 `JSToGD<uint64_t>`；D 改 `JSToGD<float/double/bool>`。**串行** |
| `src/runtime/bridge/jsb_static_binding_util.h` | B 补 `StaticBindingUtil<uint64_t>`；D 补 `StaticBindingUtil<bool>`。**串行** |

> 顺序约束：**A → (B → D)** 或 **A → (D → B)**，B 与 D 不可并行（共享两个文件）。

## 验证

```bash
# 三腿各一次
scons platform=windows target=editor binding_mode=static  compiledb=no debug_symbols=no dev_build=no verbose=no
scons platform=windows target=editor binding_mode=shared  compiledb=no debug_symbols=no dev_build=no verbose=no
scons platform=windows target=editor binding_mode=dynamic compiledb=no debug_symbols=no dev_build=no verbose=no
```

```bash
# 三腿各编一次（带 C++ 测试）
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
godot --headless --path ./project --jsb-run-tests     # C++ 套件
godot --audio-driver Dummy --headless --path project  # TS 集成
```

探针（用法见 `../09-06-lowprio-uint64-bigint-codegen/research/README.md`）：
`probe-id.ts` / `probe-idnum.ts` / `probe-readback.ts` / `probe-uint64.ts`。

**期望值（三腿一致）**：

| 断言 | 改动前 | 改动后 |
|---|---|---|
| `Resource.get_instance_id()` 的 `typeof` | `number`，`-9223372008786492000` | `bigint`，正数（如 `9223372064923059180n`） |
| `instance_from_id(get_instance_id()) === obj` | `false` | `true` |
| `is_instance_id_valid(get_instance_id())` | `false` | `true` |
| `Node.get_instance_id()` 往返 | `true` | `true`（不得回退） |
| `get_u64()` 回读 `2^63+1` 位模式 | 丢位 | 精确 |
| `get_64()` 回读 `INT64_MIN+1` 位模式 | 丢位 | 精确 |
| `2^53-1` 及以下的 `typeof` | `number` | `number`（不得变成 bigint） |

## 回滚点

| 步 | 回滚方式 |
|---|---|
| 1-5 | 整体回滚（只影响出口表示，不动调用点签名） |
| 6 | 单独回滚（`Ret<T>` 分支） |
| 7-8 | 单独回滚（meta 重载；无 meta 的旧重载保留） |

## 完成前检查

- [ ] 三腿都编过、探针都跑过，期望值表逐行核对
- [ ] 五个引擎至少编译通过（本机 windows+v8；qjs-ng/jsc 由 CI 覆盖）
- [ ] `git status` 干净（探针已移出 `project/`）
- [ ] C++ 用例已登记进 `jsb_test_main.cpp`，`--jsb-run-tests` exit 0 且无泄漏
- [ ] TS 场景已登记进 `start.ts`，全量集成测试 exit 0 + COMPLETED
- [ ] 新守卫做了负向验证（人为削减一次确认 FAILED，再还原）
- [ ] AC1.1 - AC1.8 逐条勾选
