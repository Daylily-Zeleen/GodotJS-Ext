# 实施报告：修复静态腿内置类型构造函数分发

## 一、目标

静态腿 `new X(...)` 分发的两类**既有**缺陷：

1. **错选重载后编组失败**：`new Color("abc")` / `new Color("abc", 1.5)` 选中 `Color(Color)` 重载，`marshal_one` 拒绝 JS string → `bad argument 0`（bench `Constructors` 组 `invalid=2`，动态腿同组 `invalid=0`）。
2. **无匹配时返回空包装**：生成的 `find_ctor_*` 在 arity 链后掉出函数体，`throw_no_suitable_ctor` 定义了但从未发射；返回的包装 `IF_Pointer` 为 null，下游解引用即 SIGSEGV。

## 二、动作与证据

### 2.1 代码改动（P1 四项）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/static_binding/thunks/type_compatible.h` | 文件头加「契约 + 审计表」；`COLOR`/`RID` 谓词改为 `return false;`（删去 `COLOR` 放行 `STRING`/`INT`、`RID` 放行 `OBJECT`） |
| 2 | `misc/build/static_binding_codegen.py` | `emit_ctor_dispatch` 在每个 `find_ctor_*` 的 arity 链后发射兜底抛出（含注释） |
| 3 | `src/static_binding/thunks/thunks_common.h` | `probe_vt` 用 `TypeConvert::is_variant`/`is_object` 取代 `InternalFieldCount()` 直接比较（语义等价 + NODE 下 Promise 特例） |
| 4 | `src/static_binding/thunks/builtin_constructors.h` | `throw_no_suitable_ctor` 由 `static` 改 `inline`（头文件内定义，多 TU 包含） |

### 2.2 已撤销的改动（用户指正）

- **消费端空指针守卫（R3 原方案）撤销**：`extract_variant_backed`（`jsb_type_convert_direct.h`）的 `if (!pointer) return false;` 已回退，该文件与 HEAD 零 diff。理由（用户指出，已核实）：`IF_VariantFieldCount` 包装的 `IF_Pointer` 由 `bind_valuetype` 写入，不存在"已判定为 variant 但指针为空"的合法状态；空包装是分发缺陷的**症状**，正解是让分发永不出产它（改动 #2），不在消费端打补丁。唯一显式置空点是 `Environment::dispose_binding_object`，其 `[Symbol.dispose]` 入口尚未暴露（`jsb_object_bindings.cpp` TODO）。
- 该决策已写入 `prd.md` R3 / `design.md` P1.3 / `architecture-constraints.md`「其他陷阱」首条。

### 2.3 验证证据（静态腿，quickjs-ng）

| 判据 | 结果 | 证据 |
|---|---|---|
| bench `Constructors` 组 | **`invalid: 0`**（原 2）；`new Color(String)`/`(String,float)` 均 `sample=obj:Color` | `.agent_tmp/final-bench-static.log` |
| 无匹配 ctor 抛错不崩 | `array-literal threw=true`、`color-int threw=true`（`no suitable constructor`）、`valid-empty threw=false got=0`、崩溃标记 0 | `.agent_tmp/probe-ctor.log` |
| C++ 双套件 | 51/51 passed（586 断言）+ 3/3 passed（12 断言），exit=0 | `.agent_tmp/final-cpp-static.log` |
| 静态腿全量 TS 套件 | exit=0、`OPERATORS-DIAG methods=236 unary=50 binary=186 calls=344`、`COMPLETED`、0 Orphan | `.agent_tmp/final-ts-static.log` |
| 生成器 diff 范围 | 仅 `emit_ctor_dispatch` 兜底 4 行 + 注释（另含上一任务的既有一行 `JS_NATIVE_LEFT`）；32 个 `find_ctor_*` 各自恰 1 行兜底；去掉该行后 32/32 分支体与既有产物逐字节相同；产物与磁盘 `.gen.cpp` `cmp` 一致 | `git diff misc/build/static_binding_codegen.py`；`.agent_tmp/genp1` |

## 三、附带修正（用户 2026-09-19 指出的三项）

### 3.1 TS 测试必须真过类型检查（`--noCheck` 是掩盖）

- **`project/tests/operators/test-operators.ts`**：原先靠 `Record<string, (...args) => unknown>` 索引签名会掩盖类型（普通 `tsc` 下报 32 处 TS2322 + 2 处 TS7053）；`vecEq(v: Record<string, number>)` 报 5 处 TS2345。改用 `Reflect.get`/`Reflect.apply` 的 `callOp`/`vecEq`（标准库路径，**无 `as any`/`as unknown`**）。
- **`project/tests/benchmark/benchmark.ts`**：`Engine.get_version_info() as unknown as {...}["full_name"]` 移除，改 `Engine.get_version_info().get("string")`——顺带修正了该字段的**实际取值**：原 cast（及我最初的 `.get("full_name")`）分别产出 `"[object Object]"` 与 `"null"`，现在输出真实版本串 `"4.7.2-stable (official)"`。
- **`project/tests/benchmark/cases.object.ts`**：`(n as any)._bench_child` 自造属性移除，`move_child` 改用 `t.get_child(0)`。
- **`project/tests/default-args/test-default-args.ts`**：arity 边界用例的 `(arr as any).bsearch()` 等 4 处改为 `callWithArity(target, name, args)`（`Reflect.apply`，专为"签名不接受的实参个数"而设）。
- **`project/scripts/gen-godotjs-types.mts`**：去掉生成前那趟 `tsc --noCheck`。改为显式三段：①bootstrap 只 emit（typings 不存在时无法类型检查，失败可忽略）→ ②`--generate-types`（容忍引擎关机段已知崩溃，与 `verify_codegen.py` 同口径）→ ③**强制** `tsc`，非零即失败。
- 文档同步去掉 `--noCheck` 指引：`build/scons-build.md`、`test/codegen-baseline.md`、`test/index.md`（3 处）、`misc/verify_codegen.py` 注释、`README.md`。
- **CI**（`.github/workflows/ci.yml`）：原"CI 不做 gen:types，故所有 tsc 都 `--noCheck`"的做法改为——api 数据建好后新增 **`Generate project typings (type-check enforced)`** 步骤跑 `pnpm gen:types`（其内即强制 `tsc`）；其余各步（诊断步骤、C++ 测试、TS 集成）的 `tsc` 一律去掉 `--noCheck`。benchmark job 因不建 api store，其 tsc 仍是 bootstrap emit（注释已写明）。

**证据**：`tsc`（无 `--noCheck`）exit=0 / 0 error（`.agent_tmp/tsc-real7.log`）；反向证明类型检查是真的——插入一句 `const x: number = "s"` → exit=2 报 TS2322（`.agent_tmp/tsc-probe.log`）；移走 `project/typings/` 全量 → exit=2，177 处 `module 'godot' cannot be found`（`.agent_tmp/no-typings-proper.log`）；`gen:types` 在**无 typings 的干净树**上 exit=0 并重生产物（`.agent_tmp/gentypes-clean.log`）。

### 3.2 `probe_vt` 不需要空指针守卫

见 2.2。`probe_vt` 现为用户增强后的 `TypeConvert::is_variant`/`is_object` 判定；本报告亦把 spec 里那句"加空指针守卫"的错误结论改正为"不要加"。

### 3.3 `OP_EQUAL(right: Variant | null)` 应为 `GAny`

`godot::Variant` 在 typings 里**只是命名空间**（`Variant.Type`/`Variant.Operator`），不是类型——`right: Variant | null` 引用了一个不存在的类型。TS 侧与 `godot::Variant` 对等的是 `GAny`（且已含 `null`/`undefined`）。

- 改 `jsb_codegen_writer.cpp`：`right_type == Variant::NIL` 时发射 `kGodotAnyType`（`"GAny"`）。
- 重新 `--generate-types` 验证：`OP_EQUAL(right: GAny)`；全文 `Variant | null` **64 → 0**；`GAny` 右参 64 处；`tsc` exit=0。

## 四、最终状态

- P1 四项改动全部落地并验证；两处已撤销的消费端守卫与 HEAD 零 diff。
- 静态腿：bench `invalid=0`、C++ 双套件 51/51+3/3、全量 TS 套件 `COMPLETED` 且 0 Orphan。
- TS 测试项目现由**真类型检查**把关（本地 `tsc` 与 CI `gen:types` 内的强制 `tsc`）。
- 生成产物 typings 中 `Variant | null` 已清零。
- 一次性探针 `project/tests/agent-probe/` 及其编译产物（`.godot/godotjs_ext/`、`gen/godot/tests/`）已删除。
- **未 commit / push**（按约定）。

## 五、遗留

- **「残余语义分叉」已实测否定（原条目作废）**：原报告称「收紧 `COLOR` 后 `new Color(<String 包装对象>)` 静态腿抛错、动态腿接受」，该说法**仅从谓词表推演，未验证可达性**。探针实测（`.agent_tmp/probe2-dyn.log`）：`set_meta`/`get_meta` 往返、`Engine.get_version_info().get("string")`、`GDictionary.get(k, default)`、`get_name()` 四种 STRING 来源**全部返回 `typeof=string`**，`Reflect.construct(Color, [value])` 成功——**不存在 String 包装对象这一输入**。结构性佐证：`gd_var_to_js` 的 `case Variant::STRING`（`jsb_type_convert.cpp:352`）**早返回** `new_string`，不进包装分支；包装分支 case 列表（`:369-398`）不含 STRING；`String` 经 `reflect_bind_utilities` 以 `ClassBuilder::New<0>`（0 内部字段）注册（`jsb_primitive_bindings.cpp:878`）→ `is_variant` 恒 false。故该分叉无可达输入，**不构成遗留**。
- **`project/icon.svg.import`、`third/quickjs-ng`（子模块工作区）**：引擎 headless 运行的副作用（导入参数重写、未跟踪 `.obj`），非本次编辑，未还原。
- CI 的 `benchmark` job 不建 api store，其 TS 步骤仍是 bootstrap emit（非强制检查）；该 job 不跑 TS 测试套件，故未纳入强制面。

## 六、复现命令

```
# 静态腿
taskkill //F //IM godot* ; scons platform=windows target=editor static_binding=yes compiledb=no debug_symbols=no dev_build=no verbose=no use_quickjs_ng=yes -j6
cd project && rm -f .godot/.tsbuildinfo && node node_modules/typescript/bin/tsc   # 不加 --noCheck
"D:/Dev/godot/godot/bin/Godot_v4.7.2-stable_win64_console.exe" --audio-driver Dummy --headless --path ./project --verbose
# 判据：exit 0 + OPERATORS-DIAG ... calls=344 + GODOTJS_TEST_PROJECT_COMPLETED + 无 Orphan

# 类型检查（真过）
cd project && rm -f .godot/.tsbuildinfo && node node_modules/typescript/bin/tsc ; echo "exit=$?"

# typings 再生（含强制类型检查）
cd project && GODOT=<引擎路径> pnpm gen:types
```
