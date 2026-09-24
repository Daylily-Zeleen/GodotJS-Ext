# 实施清单（父任务）

> 本文件是父任务层面的执行顺序与验收门禁。**逐子任务的实施步骤写在各自子任务的
> `implement.md`**；本文件只负责串起依赖、验证命令与回滚点。
> 共享技术方案（逐文件改动点）见 `design.md`。

## 依赖图

```
A 09-24-uint64-bigint-return ──┬──> B 09-24-uint64-bigint-arg ──> C 09-24-uint64-bigint-switch
   （读方向，无依赖）           │     （写方向，需 A 的 to_uint64）  （开关收口，依赖 A+B）
                               └──> D 09-24-uint64-bigint-ctor-operators
                                     （数值槽对齐，需 A 的 to_int64/to_double/to_bool）
```

- C 只收口**出口**表示（`new_integer` / `new_unsigned_integer` 的 BigInt 分支）+ 入口不抛异常的保证，
  故依赖 **A + B**；D 是**入口**侧改动，与 C 无依赖关系（先做哪个都行）。
- B 与 D 串行（共享两个文件），所以实际排期：`A → B → (C 或 D) → 另一个`。

⚠ **B 与 D 不可并行** —— 两者共享 `jsb_type_convert_direct.h` 与 `jsb_static_binding_util.h`。
实际顺序只能是 **A → (B → D)** 或 **A → (D → B)**。

各子任务文件集：
- **A**：`src/runtime/impl/jsb_primitive_conv.h`（新建）、`src/runtime/impl/*/jsb_*_helper.h`、
  `src/runtime/impl/{quickjs,jsc,web}/jsb_*_primitive.{h,cpp}`、
  `src/static_binding/thunks/thunks_common.h`（`Ret<T>`）、
  `src/runtime/bridge/jsb_type_convert.{h,cpp}`、`src/runtime/bridge/jsb_object_bindings.cpp`
- **B**：`src/runtime/bridge/jsb_type_convert_direct.h`（`JSToGD<uint64_t>`）、
  `jsb_static_binding_util.h`（`StaticBindingUtil<uint64_t>`）
- **D**：`src/static_binding/thunks/thunks_common.h`（`probe_vt`）、`builtin_operators.h`、
  `type_compatible.h`、`src/runtime/bridge/jsb_type_convert_direct.h`
  （`JSToGD<float/double/bool>`）、`jsb_type_convert.cpp`（`can_convert_strict<BOOL>` +
  typed `BOOL` 分支）、`jsb_static_binding_util.h`（`StaticBindingUtil<bool>`）
- **C**：`src/jsb.config.h`

**交点 1：`jsb_primitive_conv.h`**（A 创建，B/D 只调用）。契约已在 `design.md` §3.1 A-1 固定，
**含 `to_double` / `to_bool`**（D 消费；D 不得另起一份）。

**交点 2：`thunks_common.h`** —— A 改 `Ret<T>`，D 改 `probe_vt`。**必须串行**。

**交点 3：C++ 测试头 `src/runtime/tests/test_jsb_int64_conv.h`** ——
A 写转换原语的用例、B 写 `JSToGD<uint64_t>` 的用例、D 写 `probe_vt` +
`JSToGD<float/double/bool>` 的用例。三者**必须串行**，
且都要在 `jsb_test_main.cpp` 登记（登记只需一次，先到者建头 + 登记）。

**交点 4：`jsb_type_convert_direct.h` 与 `jsb_static_binding_util.h`** —— B 与 D 都改。
**必须串行**（这就是 B 与 D 不可并行的原因）。

## 顺序

| 步 | 内容 | 归属 | 前置 |
|---|---|---|---|
| 1 | `jsb_primitive_conv.h` + 四份 helper 委托化 + 每引擎 `Uint64Value` | A | — |
| 2 | `Ret<uint64_t>` 无符号出口（`thunks_common.h`） | A | 步 1 |
| 3 | 动态腿 meta 传递（`jsb_type_convert.*` + `jsb_object_bindings.cpp`） | A | 步 1 |
| 4 | `JSToGD<uint64_t>`（`jsb_type_convert_direct.h`） | B | 步 1 |
| 5 | `probe_vt` + `builtin_operators.h` + `type_compatible.h`（三处同批） | D | 步 1；且与步 2 串行（同文件） |
| 6 | 独立宏 + 语义收口（`jsb.config.h`） | C | 步 1-5 |

步 1 与步 4 可并行开工（B 先按 `to_int64` + 位重解释过渡，A 落地后切到 `to_uint64`）。
**最终形态必须用 `to_uint64`。**
步 2 与步 5 都改 `thunks_common.h`，**必须串行**（谁先谁后都行，但不可同时编辑）。
步 4 与步 5 都改 `jsb_type_convert_direct.h` / `jsb_static_binding_util.h`，
**必须串行**；步 5 依赖步 4 落地后的文件状态。
步 6 依赖步 1-5 全部落地（开关只收口已有行为，不实现转换逻辑）。

## 构建命令

```
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      compiledb=no debug_symbols=no dev_build=no verbose=no
```

- `SConstruct:37` 默认 `binding_mode=shared`；`:858-860` 由该值决定
  `JSB_WITH_STATIC_BINDINGS` / `JSB_WITH_SHARED_THUNKS`。
- 改 `impl/*/jsb_*_helper.h` 或 `jsb.config.h` 会触发全量重编（单次 100~190s）。
  **一次列全变体，每步只编一次**；不为措辞做验证性编译。
- 禁止 `scons --clean`。

## 验证命令

```bash
# 0) 构建（三腿各一次；带 C++ 测试需要 tests=yes）
scons platform=windows target=editor binding_mode=<static|shared|dynamic> \
      tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no

# 1) C++ 单元测试
godot --headless --path ./project --jsb-run-tests
#    判据：exit 0 且无泄漏（无未释放 Resource、无 Orphan StringName）

# 2) 全量 TS 集成测试
godot --audio-driver Dummy --headless --path project
#    判据：exit 0 + GODOTJS_TEST_PROJECT_COMPLETED + 无 GODOTJS_TEST_PROJECT_FAILED:

# 3) 探针（见 research/README.md）
#    三腿各跑一次，对照 research/measured-evidence.md 的期望值

# 4) bench 门禁
python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix
#    判据：assert_leg() 通过 + invalid == 0
```

`tests=yes` 的接线：`SConstruct:900-903` 追加 `JSB_TESTS_ENABLED` 并 glob
`runtime/tests/*.cpp` 与 `editor/tests/*.cpp`。
CI 在 desktop 腿上自动带 `dev_build=yes tests=yes`（`.github/actions/scons-build/action.yml:121`）。

⚠ **当前工作区的 dll 不是 tests 构建**（`bin/windows/godotjs-ext.windows.editor.x86_64.dll`
里搜不到 `--jsb-run-tests` / `RuntimeTest` 字符串）→ 第一次跑 C++ 套件前必须先带 `tests=yes` 重编。

⚠ editor 套件静默缺席的坑：`--jsb-run-tests` 只触发已加载扩展的套件，
`project/.godot/extension_list.cfg` 必须**两行**（runtime + editor）。
当前该文件已含两行，但 `rm -rf project/.godot` 后会丢，届时 editor 套件整段不跑且无报错。
判别：日志无 `viaEditorTest`、doctest 汇总只有一段。

⚠ **不要往 bench 加 BigInt 联合类型算子 case** —— `invalid != 0` 会直接 FATAL
（`misc/bench_matrix.py:96-97`）。

## 回归测试落点（R8 / AC11）

### C++ 侧（doctest）

新建 `src/runtime/tests/test_jsb_int64_conv.h`，**必须在 `src/runtime/tests/jsb_test_main.cpp`
的 include 列表登记**，否则用例不注册。用例：

| 覆盖 | 归属 |
|---|---|
| `to_int64` / `new_integer` / `new_unsigned_integer` 的阈值与位模式（含双边阈值） | A |
| `JSToGD<uint64_t>` 按位写入；窄类型仍拒绝越界 | B |
| `probe_vt(BigInt) == Variant::INT`（受 `JSB_WITH_STATIC_BINDINGS` 门控） | D |
| `JSToGD<float>` / `<double>` 接受 BigInt；`JSToGD<bool>` 接受 number/bigint/null/undefined，拒字符串 | D |

形态见 `.trellis/spec/godotjs-ext/test/doctest.md`；参考
`src/runtime/tests/test_jsb_any_runtime.h`（`GodotJSScriptLanguageIniter` +
`JSB_TESTS_EXECUTION_SCOPE`）。
⚠ 三个任务都往这个文件加用例，**必须串行**。

### TS 侧（`project/tests/int64/`，**不进 bench**）

新建场景目录并登记进 `project/tests/start.ts` 的 `scenes` 列表：

- ObjectID 往返：RefCounted 与非 RefCounted 各一（AC1）
- uint64 写读字节精确：`{2^53, 2^63, 2^63+1, 2^64-1}` × {BigInt, number} × 三腿（AC2）
- int64 负值回读位精确（AC3）
- 阈值下界不变：`2^53-1` 仍出 `Number`（AC4）
- 窄整数仍拒绝越界（AC2）
- `new Vector2i(2n, 3)`（int 槽）/ `new Vector2(2n, 3n)`（float 槽）/ `OP_MULTIPLY(2n)`（AC6 / AC7）
- bool 槽：`set_block_signals(1)/(0)/(1n)/(0n)/(null)/(undefined)` 不抛异常，
  用 `is_blocking_signals()` 读回验证真值映射；`""` 仍拒（AC7b）
- **有默认值**的 bool 位置传 `undefined` 仍走默认值替换（`Rect2.intersects(b, undefined)`
  等于 `intersects(b, false)`）；**无默认值**位置传 `undefined` 走转布尔
  （`Projection.create_depth_correction(undefined)` 不抛）（AC7c）
- 宏两种模式的差异行为（AC5）

断言一律经 `reportTestFailure`（裸 throw 不会传播到 `start.ts`，会假报 COMPLETED）。

⚠ **不要往 bench 加 BigInt 联合类型算子 case** —— `invalid != 0` 会直接 FATAL
（`misc/bench_matrix.py:96-97`）。

按 `.trellis/spec/godotjs-ext/test/index.md` 的要求做**负向验证**：
断言写完立刻人为削减一次确认 FAILED，再还原确认绿。没失败过的守卫不授权「覆盖完整」的结论。

## 风险与回滚点

| 风险 | 回滚点 |
|---|---|
| 读方向改动波及 `|v| <= 2^53-1` 的既有行为 | 步 1-3 可整体回滚（只影响出口表示） |
| 现有脚本依赖「uint64 高位出负 Number」 | 受影响面 = RefCounted ObjectID；TS 测试已用 `weakref` 绕开，不依赖旧行为 |
| A/B 并行对 `jsb_primitive_conv.h` 契约分歧 | 契约预先固定；若分歧，以 `design.md` 为准，B/D 让步 |
| A 与 D 同时编辑 `thunks_common.h` | 强制串行（见「顺序」）；并行时先经 `hub` 约定谁先落 |
| 三个任务同时编辑 C++ 测试头 | 强制串行；登记 `jsb_test_main.cpp` 只需一次 |
| `probe_vt` / `JSToGD<float/double/bool>` 改了但 `builtin_operators.h` 没改 | 禁止项；D 任务同批改三处（`R == int64_t` / `double` / `bool`），检查时逐项核对 |
| 放行 `undefined` 破坏「有默认值位置走默认值」 | D 任务必须同时测「有默认值」与「无默认值」两种位置（AC7c） |
| B 与 D 同时编辑 `jsb_type_convert_direct.h` / `jsb_static_binding_util.h` | 强制串行（见「顺序」步 4/5） |
| 五引擎中某个 shim 编译不过 | 步 1 单独可回滚（shim 只加一个方法） |

## 完成前的检查

- [ ] 三腿（static / shared / dynamic）都编过、都跑过
- [ ] 五个引擎至少编译通过（本机只能实测 windows+v8；其余在 CI 覆盖，
      `.github/workflows/ci.yml` 已有 `v8` / `qjs-ng` / `node` / `jsc` 腿）
- [ ] C++ 套件（`--jsb-run-tests`）exit 0 且无泄漏
- [ ] 新增 C++ 用例与 TS 场景都已登记并跑过；新守卫做过负向验证
- [ ] `git status` 干净（探针已移出 `project/`）
- [ ] AC7b / AC7c（bool 槽放行 + 默认值优先级）已实测验证
- [ ] 结论沉淀进 `.trellis/spec/godotjs-ext/cpp/ptrcall-encoding.md`
- [ ] 子任务 A/B/C/D 的 `prd.md` 验收项逐条勾选，未做的显式标注原因
