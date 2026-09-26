# 批 1（机械清理）执行记录

## 完成项

| 步 | 文件 | 结果 |
|---|---|---|
| R1.1 | `.github/workflows/ci.yml` | 去掉 `default` 选项；input 默认 `shared`；守卫简化为 `inputs.binding_mode != '' && (windows\|linux\|macos)` |
| R1.2 | `jsb_primitive_conv.h` | 去 `ValueT` 模板（6 处），读取器改定值 `const v8::Local<v8::Value>`；去掉随之失效的 `.template As<>`（8 处） |
| R1.3 | `jsb_type_convert_direct.h` | 15 处 `(void)x` → `jsb_unused(x)` |
| R1.4 | 新增文件 | 去掉 `Contributors of GodotJS` 两行（`jsb_primitive_conv.h` / `test_jsb_int64_conv.h`）；规则写入 `.trellis/spec/godotjs-ext/cpp/index.md` 质量检查单 |
| R1.5 | `jsb_static_binding_util.h` | 24 个 `get`/`set` 加 `_FORCE_INLINE_` |
| R1.6 | `jsb_primitive_conv.h` | 补回 `JSB_LOG(VeryVerbose, "represented as bigint %d", (int)p_val)` |

**附带**（R1.2 的硬前提）：`jsb_primitive_conv.h` 的 `include` 从四个 shim 的 pch **之前**移到**之后**
（v8 31→33、quickjs/jsc/web 29→34）。原因：改用 `_FORCE_INLINE_` 后，pch 之前该宏未定义。

## 验证证据

| 项 | 结果 |
|---|---|
| 构建（v8 static，含 include 顺序变更 → 全量重编） | rc=0、230s、0 error |
| C++ 套件 | `813 / 813` + `12 / 12`，SUCCESS |
| TS 集成 | tsc rc=0、`C=1 F=0`、`INT64-DIAG checks=55 expected=55 bigint=true`、`NUMERIC-DIAG checks=21` |
| CI 守卫模拟 | `static/shared/dynamic` 三值均只作用于 windows/linux/macos，web/ios/android 无 flag |
| CI YAML | `yaml.safe_load` 通过；input 为 `{default: shared, options:[static,shared,dynamic]}` |

## 发现：R1.6 的日志是**永不输出**的死代码（需决策）

`src/runtime/internal/jsb_log_severity.def.h` 的枚举顺序：

```
VeryVerbose = 0
Verbose     = 1
Debug       = 2
...
```

`JSB_LOG_IMPL` 的条件是 `Severity >= JSB_MIN_LOG_LEVEL`，而 debug 下
`JSB_MIN_LOG_LEVEL = Verbose`(1)。故 `VeryVerbose`(0) `>= 1` **恒假** —— 编译期整条被丢弃。
`.def.h` 注释自己也写着：`VeryVerbose // very trivial (omitted by default even if JSB_DEBUG is on)`。

实测佐证：三个构建产物 DLL 里 `'represented as bigint'` 字符串计数**均为 0**
（若编进去会有该字符串）。另一路实测：用 `--verbose` 跑 C++ 套件（内含
`new_integer(JSB_MAX_SAFE_INTEGER + 1)→IsBigInt()` 必然命中该分支），也**没有任何输出**。

**含义**：原始四份副本里那行同样从未输出过。所以"补回"在行为上是**保真**的
（与既有代码一致），但**不可观测**，因此 AC6 的"实测打出该行"无法满足。

三条路待选：
1. 保持 `VeryVerbose`（保真、无害、仍不可观测）—— AC6 降级为代码审查。
2. 提到 `Verbose`：仍需运行时 `--verbose`（`Logger::verbose` 里 `is_stdout_verbose()` 门控），
   但至少可观测，AC6 可实测。
3. 删除该行。

## 下一步

批 2（`jsb::impl::internal` 命名空间）与批 1 的 include 重排**已重叠完成**（重排是本批前提）。
批 2 剩余：命名空间迁移 + 转发指向。批 3（出口契约：三态宏 / `GDToJS` / `translate_return` 收敛 / typings）。

---

# 批 3（出口契约）执行记录

## 已完成

| 步 | 文件 | 结果 |
|---|---|---|
| R2.1 | `jsb_primitive_conv.h` + 四 shim | 命名空间迁到 `jsb::impl::internal`，改 `_FORCE_INLINE_`，四 shim 各 6 个转发；`#include` 移到 pch 之后 |
| R2.2 | 新增 `src/runtime/bridge/jsb_gd_to_js.h` | `GDToJS<T>`：主模板回退 `gd_var_to_js`，标量特化直取；64 位宽度走 `*_ret` |
| R2.3 | `thunks/thunks_common.h` | `Ret<T>::translate_return` 收敛为 `GDToJS<type>::convert(...)`；删 `internal::translate_return` / `translate_uint64_return` / `is_same_v<uint64_t>` 特判 |
| R2.4 | `jsb.config.h` + `jsb_primitive_conv.h` | 新增 `JSB_64BIT_RETURN_FIXED_BIGINT`（默认 0）+ `#error` 约束；新增 `new_integer_ret` / `new_unsigned_integer_ret` |
| R2.5 | `jsb_codegen_defs.cpp` + `jsb_codegen_type_db.cpp` | 别名随宏生成；固定模式加 `int64_ret`/`uint64_ret` 并把返回值位置换成 `*_ret` |
| R3.1 | `jsb_primitive_conv.h` | 负 `Number`→uint64 加 `#if JSB_DEBUG` 告警（`%d`+(int)） |
| R3.2 | 父任务 `design.md` + 本任务 `design.md` | 「回绕」措辞清零 + 定位更正节 |

## 关键决策：A 方案（固定 bigint 只作用于 declared-64-bit 出口）

原实现按「固定模式下所有 `Variant::INT` 出口一律 bigint」实现，导致固定模式下 C++ 套件
7 例失败（`bridge_table` / `any_runtime` / `shadow_realm` 等无 meta 的 `Variant` 直传路径被牵连）。
改为按 `p_meta` 分流：只有 `INT_IS_INT64` / `INT_IS_UINT64` 两个声明的 64 位 meta 走 `*_ret`；
无 meta 的出口（eval 结果、Variant 直传、容器取整数值）保持值相关。

依据：属性自身不声明 meta，语义来自 getter 的 `return_metadata`，而 `_godot_object_get2` 确实传
`getter_func->get_return_metadata()` —— 所以按 meta 分流已覆盖 class 方法返回值 + 属性 getter，
不需要平行函数族。

## 验证证据（v8 static）

| 场景 | 结果 |
|---|---|
| 固定模式（FIXED=1）构建 | rc=0、183s、0 error |
| 固定模式 C++ | `58/58`、`813/813`，0 failing assertions |
| 固定模式 TS | `tsc` rc=0、`GODOTJS_TEST_PROJECT_COMPLETED`、`F=0`、`INT64-DIAG checks=56`、`NUMERIC checks=21` |
| 默认模式（FIXED=0）复位后构建 | rc=0、380s、0 error |
| 默认模式 C++ | `58/58`、`813/813`，SUCCESS |
| 默认模式 TS | `tsc` rc=0、`COMPLETED`、`F=0`、`INT64-DIAG checks=55`、`NUMERIC checks=21` |

## 固定模式暴露的测试模式感知（非实现缺陷）

固定模式让每个 declared-64-bit 返回值无条件变成 `bigint`，因此集成测试里假设它是
`number` 的位置需要归一化。新增 `FIXED_BIGINT_FOR_64BIT_RETURN` 导出（`jsb_bridge_module_loader.cpp`
+ `scripts/typings/godot.minimal.d.ts`），测试按该开关分支：

- `cross-environment/test-cross-environment.ts`：`Time.get_ticks_msec()/usec()` 算术经 `asNumber()` 归一；
  `objectId` / `childId` 声明加宽为 `number | bigint`
- `cross-environment/messaging.ts`：`objectId?` / `controlId?` 加宽
- `benchmark/benchmark.ts`：`nowMs()` 归一
- `int64/test-int64.ts`：`bytes.size()` 归一；`FIXED_MODE` 分支下 typeof 期望与
  `EXPECTED_CHECKS`（56）模式感知；`Node id typeof` 期望按开关取值
- `default-args/test-default-args.ts`：`check()` 比较前 `asNumber()` 归一，
  诊断格式化器改成 bigint 安全（`JSON.stringify` 对 BigInt 抛错）

---

# 验收执行记录

## AC13 本地 5 腿（全部实测）

| 腿 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0 | `58/58`、`813/813` | `COMPLETED`、`INT64-DIAG checks=55`、`NUMERIC=21` |
| v8 shared | rc=0、119s | `58/58`、`813/813` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| v8 dynamic | rc=0、71s | `57/57`、`801/801` + `12/12` | `COMPLETED`、`checks=55` |
| quickjs-ng static | rc=0、495s | `59/59`、`818/818` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| quickjs-ng dynamic | rc=0、88s | `58/58`、`806/806` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |

各腿 0 failing assertions、0 `GODOTJS_TEST_PROJECT_FAILED`、`tsc` rc=0。

## AC9 三态实测

| 组合 | 证据 |
|---|---|
| `FOR_64BIT=1, FIXED=0`（默认） | `INT64-DIAG checks=55`、`bigint=true`（值相关） |
| `FOR_64BIT=1, FIXED=1` | `checks=56`（小值也 bigint）、`Node id typeof` 得 `bigint` |
| `FOR_64BIT=0` | `checks=50`、`bigint=false`（恒 number） |
| 非法 `FIXED=1 && FOR=0` | scons rc=2，`JSB_64BIT_RETURN_FIXED_BIGINT=1 requires JSB_BIGINT_FOR_64BIT=1` 命中 |

## AC10 typings

- 默认模式：`type int64 = number | bigint` / `uint64 = number | bigint`，**无** `*_ret`
- 固定模式：`+ type int64_ret = bigint` / `uint64_ret = bigint`，返回值位置用 `*_ret`
  （13 处 `int64_ret`、12 处 `uint64_ret`；残留 `get xxx(): int64` 是属性 getter，无 64 位声明，保持宽别名正确）
- `FOR_64BIT=0`：无 `*_ret`（与上面默认模式一致）

## AC11 负 Number→uint64 告警

- 静态：`grep -c` 字节计数 —— `template_release` = **0**，`template_debug` = 1，`editor` = 1
- 动态实测（editor 构建，临时探针场景，已全部清理）：
  `put_u64(-4294967296)` → `WARNING: [jsb][Warning] negative number -2147483648 fed to a uint64 slot: its bits are reinterpreted, not its value`
  注：`-1` 是 int32，走快路径不告警，必须用堆 `Number`（如 `-2^32`）触发。

## AC14 bench matrix

`python misc/bench_matrix.py --report .agent_tmp/matrix` → rc=0、24 runs、`invalid` 合计 **0**；
另用当前代码新采集 `--leg static --rounds 1 --build`（dll md5 `14dd4901`）→ 2 runs、`invalid=0`、234 cases、rc=0。

## AC1~AC5、AC7、AC8、AC12（代码级 grep 证据）

| AC | 证据 |
|---|---|
| AC2 | `jsb_primitive_conv.h` 中 `ValueT` 命中 0 |
| AC3 | `jsb_type_convert_direct.h` 中 `(void)p_isolate`/`(void)p_context` 命中 0 |
| AC4 | 三个新文件 `Contributors of GodotJS` 命中 0；spec 规则条目命中 1 |
| AC5 | `jsb_static_binding_util.h` 中 `_FORCE_INLINE_ ... get/set` 命中 24 |
| AC7 | `is_same_v<type, uint64_t>` / `translate_uint64_return` 命中 0；`thunks_common.h` 无 `Variant(` |
| AC12 | `design.md` 中「回绕」命中 0；「扩大表示范围」唯一命中在两处「**不是**」否定句中 |
| AC1 | CI YAML `yaml.safe_load` 通过，input `{default: shared, options: [static, shared, dynamic]}`；守卫单条件。**真实触发待定** |

## 遗留

- **AC1 真实触发未做**：本机 `gh` 不存在，且改动未提交 → 需用户当轮授权提交并指定 feature 分支/远端。
  另注意 `ci.yml` 的 `concurrency.group` 含 `github.ref_name` 且 `cancel-in-progress: true`，
  三模式必须**串行**触发。
- 未提交任何改动（HEAD 仍为 `6745e93`）。

---

# 复审整改批次（2026-09-26，用户逐条复审后）

用户复审提出 10 条，逐条处置。**其中 8 条确认为我的错并已修**。

| # | 问题 | 处置 |
|---|---|---|
| 1 | `GDToJS` 另开 `jsb_gd_to_js.h`，与 `JSToGD` 不对称 | 已删该文件，`GDToJS` 并入 `jsb_type_convert_direct.h`，同 `jsb` 命名空间，与 `JSToGD` 并列 |
| 2 | 显式传 `CppWidth` 宏参数 | 已删。宽度判据改 `sizeof(T) == 8`；13 特化 + 2 宏收成 1 个主模板 + `if constexpr` |
| 3 | 只做整数/浮点/bool/Variant，未对齐 `JSToGD` | 已补 `Object*` / `String` / `StringName` 快路径 |
| 4 | `StaticBindingUtil` 加占位参数防实例化 | 8 个显式特化改**偏特化**（`template <typename PlaceholderT>`）；主模板默认参数，调用点不变 |
| 5 | `js_bool_as_number` 的必要性 | 见下"bool→number 槽"，实测 GDScript 不报错，**保持现状**（用户裁定） |
| 6 | `verify_narrow_int_slot` 的 `(void)` | 已改 `jsb_unused()`（`jsb_macro.h` 与 `JSB_LOG` 同头，该 TU 已用 `JSB_LOG`） |
| 7 | `*_ret` 走 `Helper` | 已改直调 `impl::internal`，删除 4 shim × 2 转发 |
| 8 | `jsb.config.h` 空行 | 根因：**全文件被我写成 `\r\r\n`**（313 行），且 LF→CRLF。已从 HEAD 字节重建（纯 LF）并重插新宏块；另把 21 个我整文件重写过的文件行尾规范回 LF |
| 9 | 加了 `GDToJS` 却没替换静态绑定里的 `gd_var_to_js` | 已切 `member_getter_thunk`、`shared_member_getter_thunk`、`operator_binary_thunk`、`operator_unary_thunk`、`StaticBindingUtil<T>::set`（主模板 + `Object*`/float/double/bool）。**索引属性 getter（433 处）未切，理由见下** |
| 10 | `translate_return` 里重写缓冲形状分支 | 已按用户方案：`translate_return` 单点归一 `Variant` / `EncodeT` 两种形状，`GDToJS<T>` 只剩一个 `convert(Isolate*, Context&, const T&, Local<Value>&) -> bool` |

## 第 9 条：索引属性 getter 不切（有实质风险，需决策）

`misc/build/static_binding_codegen.py` 的 `prop_vt_value()` 对**非内建类型名一律返回 `Object`**：
`enum::` / `bitfield::` → `int`，内建类型名 → 自身，其余（具体类名、逗号列表如
`"Texture2D,-AnimatedTexture,..."`）→ `Object`。而索引属性里这类具体类型是多数
（AudioStream 64、Curve 22、Texture2D 19、...，float 142 / bool 84 / int 28）。
若把该 `Object` 当作声明返回类型交给 `GDToJS`，对一次返回 `Vector2` 的 getter 就会走
`GDToJS<godot::Object*>` 分支，把值按对象指针重解释 —— 比不优化糟糕得多。

要切必须先让 codegen 传真实的 `prop_type`，并确认该 `Variant` 结果与声明类型一致（含逗号列表的情况）。
这是"codegen + 运行期语义"耦合变更，风险高于收益，**停在这里等你决定**。

## bool→number 槽（用户追问）

实测 GDScript（独立项目，`.agent_tmp/gdprobe2/`）：

```
PROBE vector2_bools -> OK (1.0, 0.0)     PROBE vector2i_bool -> OK (1, 1)
PROBE color_bool    -> OK (1.0, 0.0, 1.0, 1.0)   PROBE float_bool -> OK 1.0
```

引擎 `core/variant/variant.cpp` 的 `can_convert_strict`：`INT = {BOOL, FLOAT, NIL}`、
`FLOAT = {BOOL, INT, NIL}`、`BOOL = {INT, FLOAT, NIL}` —— **BOOL→INT/FLOAT 是引擎自己放行的**。
`can_be_converted_from<INT/FLOAT>` 与之对齐，故 `JSToGD<int64_t>`/`<float>`/`<double>` 必须接受 boolean，
否则"谓词选中数值重载 → 编组器拒绝"（`type_compatible.h` 契约）。调用点不止构造函数：内建方法
（实测 `new Vector2(3,0).limit_length(true)` → OK）、运算符（`builtin_operators.h`）、属性 setter。
**裁定：保持现状。**

## 附带修复（复审中发现的既有缺陷）

原 `internal::translate_return` 在 `gd_var_to_js` 失败时 `jsb_throw(...)`；我上一版改成返回 void 的
`GDToJS` 后**把该 throw 静默吞掉了**。现 `translate_return` 恢复该分支。

## 改后验证（五腿全绿）

| 腿 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0、398s | `58/58`、`813/813` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| v8 shared | rc=0、113s | `58/58`、`813/813` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| v8 dynamic | rc=0、84s | `58/58`、`806/806` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| quickjs-ng static | rc=0、404s | `58/58`、`818/818` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| quickjs-ng dynamic | rc=0、93s | `58/58`、`806/806` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |

全部 0 failing assertions、0 `GODOTJS_TEST_PROJECT_FAILED`、`tsc` rc=0。

## 未切但已论证「无可优化」的类型

`Vector2/3/4(+i)`、`Rect2(+i)`、`Transform2D/3D`、`Plane`、`Quaternion`、`Basis`、`Projection`、`Color`、
`RID`、`NodePath`、`Callable`、`Signal`、`Array`、`Dictionary`、`Packed*` —— 出口侧
`gd_var_to_js` 必须 `expose_godot_primitive_class` → `clazz.NewInstance` → `bind_valuetype`，
那个 JS wrapper 是语义必需；特化只能写成转发。故经主模板回退，与 `JSToGD` 的"wrapper 读取式特化"
在**出口方向本就不对称**（入口能读原生 payload，出口必须造 wrapper）。

---

# 复审整改 第 2 轮（2026-09-26）

用户复审再提 4 条。

| # | 问题 | 处置 |
|---|---|---|
| 1 | `kPredefinedLines` 里 64 位夹在中间 | 重排为**定宽梯形**：有符号按宽度 ↑（byte/int8/16/32/64）→ float32/64 → 无符号按宽度 ↑（uint8/16/32/64）；`int64_ret`/`uint64_ret` 的 `#if` 移到 `uint64` 旁边，条件块内**只剩** `_ret`（`int64`/`uint64` 入参别名两种模式完全一致，移出条件块） |
| 2 | 只特判 `char32_t` 够吗 | 实测生成物里 `char16_t`/`char8_t`/`wchar_t` 均为 **0**（`Ret<>` 0、`Args<>` 0；只有 `char32_t` 2+8），且 `godot::Variant` **没有** `char16_t` 转换运算符（只有 `const char16_t*` 构造）。`char32_t` 特判保留（`FontFile.get_char_from_glyph_index`、`InputEventKey.get_unicode` 真的返回它，且无转换运算符），但补 `static_assert(std::is_convertible_v<const godot::Variant&, T>)` 使其**穷尽**：将来多出返回类型会在编译期炸，而不是静默走错运算符 |
| 3 | `GDToJS` 用 constexpr，`JSToGD` 要不要对齐 | 选了**把 `JSToGD` 的标量梯子也改成"主模板 + `if constexpr`"**，两边形式对齐（同 `jsb` 命名空间、同单模板参数、同 `convert(Isolate*, Context&, …) -> bool`）。入口侧仍需值类型特化（wrapper 读取 / StringName 缓存 / 代理解包各自不同），出口侧那些类型统一转发 `gd_var_to_js` —— 这是机制决定的不对称，已在两处注释写明 |
| 4 | 索引属性 getter 什么情况 | 见下 |

## 第 4 条：索引属性 getter 的展开

生成链：`misc/build/static_binding_codegen.py`（scons 每次构建都跑，`SConstruct:834`）→
`thunks::indexed_property_getter_thunk<Hash, ClassLit, MethodLit, IndexC>` → 生成物里 **433 处**。

现状 thunk 体（`class_indexed_properties.h:43`）：

```cpp
godot::Variant ret;
object_method_bind_call(method_bind, owner, [index], 1, &ret, &call_error);   // 运行期 Variant
v8::Local<v8::Value> jrval;
if (TypeConvert::gd_var_to_js(isolate, context, ret, jrval)) { ... }          // 运行期分派
```

即**返回类型在编译期完全没传进来**。要切 `GDToJS<T>` 必须由 codegen 多传一个模板参数，而
codegen 目前算不出可信类型：

```python
def prop_vt_value(t):
    if t in VARIANT_TYPE_VALUES: return VARIANT_TYPE_VALUES[t]
    if t.startswith(("enum::","bitfield::")): return VARIANT_TYPE_VALUES["int"]
    return VARIANT_TYPE_VALUES["Object"]     # 具体类名 / 逗号列表 -> Object（推断，非声明）
```

索引属性的实际类型分布（`extension_api-4-7.json`）：`float` 142、`bool` 84、`AudioStream` 64、`int` 28、
`Curve` 22、`String` 20、`Texture2D` 19、`Vector2` 17、…，还有**逗号列表**如
`"Texture2D,-AnimatedTexture,-AtlasTexture,…"`。这些落到 `return VARIANT_TYPE_VALUES["Object"]`，
被当成 `Object`。若把它直接当声明返回类型：

- 一次返回 `Vector2` 的 getter 会落进 `GDToJS<godot::Object*>` 分支 → 把值按对象指针重解释；
- 即使只把 `Object` 用于选"是不是 64 位"也是错的（`NodePath` 不是对象，`int` 会被浮点属性误伤）。

所以要切必须：① codegen 传真实的 `prop_type`（含逗号列表的解析）；② 确认该 `Variant` 结果与声明类型一致。
属 codegen + 运行期语义的耦合变更，**未做，等你定**。

## 本轮发现并修掉的一个真实回归（我上一轮 #9 引入）

上一轮把 `member_getter_thunk` / `shared_member_getter_thunk` / `operator_*_thunk` 切到 `GDToJS<T>` 后，
**固定模式下 `Vector2i(2n,3).x` 变成 BigInt**（`test-numeric.ts:67` 报 `got 2`）。

根因：这三处的模板参数是 Variant **存储类型**，不是 API 声明宽度 ——
`VariantNativeType_t<Variant::INT>` 是 `int64_t`，所以 `GDToJS<int64_t>` 命中了 declared-64-bit 的
`*_ret` 写入器；而 `Vector2i` 的成员声明是 `int32`（生成物里 `member_getter_thunk<…, INT, "x">` 13 处全如此）。
`*_ret` 是**声明宽度**的概念，存储类型不具备该信息。

处置：这三处**恢复** `TypeConvert::gd_var_to_js` 并写明理由、给出切过去的条件（需 `member_getter_thunk`
携带完整 `GDExtensionClassMethodArgumentMetadata`；32 位存储恒为 INT32/UINT32/CHAR32，64 位恒为 INT64/UINT64）。
`translate_return` 传的是**真实声明类型**（`Ret<T>`），不受此影响 —— 固定模式 56 检查仍全绿。

这同时说明上一轮 report 里"5 处切 `GDToJS`"实际只有 2 处成立（`translate_return` + `StaticBindingUtil::set`）。

## 第 2 轮验证（五腿全绿，默认态）

| 腿 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0、224s | `58/58`、`813/813` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=21` |
| v8 shared | rc=0、88s | `58/58`、`813/813` + `12/12` | 同上 |
| v8 dynamic | rc=0、64s | `58/58`、`806/806` + `12/12` | 同上 |
| qjs static | rc=0、289s | `58/58`、`818/818` + `12/12` | 同上 |
| qjs dynamic | rc=0、67s | `58/58`、`806/806` + `12/12` | 同上 |

固定模式（`FIXED=1`，v8 static）另验：构建 rc=0、C++ `58/58 813/813`、TS `COMPLETED checks=56`、
typings 梯形顺序正确（`int64` → float → `uint64` → `int64_ret`/`uint64_ret` → char16/32）。
`jsb.config.h` 已复位 `FIXED_BIGINT=0`。

---

# 复审整改 第 3 轮：去掉 FIXED 开关，并把两个 BigInt 宏合并为一个（2026-09-26）

## 用户决策

1. **删掉 `JSB_64BIT_RETURN_FIXED_BIGINT`**：返回值一律按值大小选 `number` / `bigint`，
   不再给项目加复杂度；需要确定类型的调用者自己收窄。
2. **删掉 `JSB_BIGINT_FOR_64BIT`，直接复用 `JSB_WITH_BIGINT`**：后者原注释就是
   *"use bigint if a value can not represented as Integer(Number)"*，语义重复。
3. 指出我的遗漏：`JSB_WITH_BIGINT` 仍然存在，所以 `int64`/`uint64` 的别名**必须**条件编译，
   还存在纯 `number` 的情况。

三条都成立，已执行。

## 执行

| 项 | 处置 |
|---|---|
| `JSB_64BIT_RETURN_FIXED_BIGINT` | 删除（宏 + `#error` + JS 导出 `FIXED_BIGINT_FOR_64BIT_RETURN` + typings 声明 + 测试分支） |
| `new_integer_ret` / `new_unsigned_integer_ret` | 删除；4 个 shim 的转发与 `impl::internal` 直调点一并清掉 |
| `jsb_type_convert.cpp` 的 `INT` 出口 | 去掉 `p_meta` 三态；只按**符号性**分流（`UINT64` meta → unsigned 写入器 + 一个 uint64 让位；其余 → signed 写入器） |
| `GDToJS` 数值臂 | 只按 `is_signed_v` 选写入器（`sizeof(T)==8` 的判据不再需要），值大小决定 Number/BigInt |
| `JSB_BIGINT_FOR_64BIT` | 删除，全部使用点改为 `JSB_WITH_BIGINT`（`jsb_primitive_conv.h`、`test_jsb_int64_conv.h`、`jsb_bridge_module_loader.cpp`、typings、`test-int64.ts`）。JS 导出名仍为 `BIGINT_FOR_64BIT` |
| `kPredefinedLines` | `int64`/`uint64` 改回条件编译：`#if JSB_WITH_BIGINT` → `number \| bigint`，`#else` → `number`。梯形顺序（signed→float→unsigned→char）保持 |
| `type_db::make_return` | 去掉 `*_ret` 改名逻辑 |

## 第 4 轮：`JSB_WITH_BIGINT=0` 是合法配置，原先上游就是坏的

实测该配置（这是用户特意指出的情况）：

- **HEAD 也坏**：HEAD 的 `test_jsb_int64_conv.h` 无条件断言 BigInt 读取行为，而
  `to_int64` / `to_uint64` / `to_double` / `to_bool` 的 BigInt 分支从项目诞生起就被
  `#if JSB_WITH_BIGINT` 门控 → 该配置下 42 个 C++ 断言必然失败。**此前从未跑过这个配置。**
- 我引入的部分：把 `int64`/`uint64` 别名放宽成 `number | bigint` 后，TS 侧
  `is_instance_id_valid(int64)` 等调用点全部失败。

已修：

| 层 | 处置 |
|---|---|
| `test_jsb_int64_conv.h` | 新增 `bigint_inputs_supported` 编译期判据；22 处 BigInt 输入断言按它 `if constexpr` 门控（`=0` 时断言 BigInt 被拒，而不是断言能读） |
| `test-int64.ts` | BigInt 往返段（`u64/i64 readback`、`u64 write`、`ObjectID round-trip`）与窄槽的 BigInt 用例按 `BIGINT_MODE` 跳过；`EXPECTED_CHECKS = 55 / 5` |
| `test-numeric.ts` | BigInt 参数用例（构造/运算符/bool 槽）按 `BIGINT_MODE` 跳过；`EXPECTED_CHECKS = 23 / 15`。另补 2 个无条件控制（`Vector2i(2,3)` 的 x/y） |
| `test-status.ts` | 新增 `export type Numeric64 = int64 \| uint64` —— 复用**按构建生成**的别名，而不是硬编码 `number \| bigint`（后者在 `=0` 时比 `int64` 还宽，反而报错） |
| messaging / cross-environment / benchmark | 注解改用 `Numeric64`；`asNumber` 形参放宽以接 `uint64` |

## 验证

**默认态（`JSB_WITH_BIGINT=1`）五腿全绿**

| 腿 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0、529s | `58/58`、`813/813` + `12/12` | `COMPLETED`、`checks=55`、`NUMERIC=23` |
| v8 shared | rc=0、271s | `58/58`、`813/813` | 同上 |
| v8 dynamic | rc=0、199s | `58/58`、`806/806` | 同上 |
| qjs static | rc=0、671s | `58/58`、`818/818` | 同上 |
| qjs dynamic | rc=0、213s | `58/58`、`806/806` | 同上 |

**`JSB_WITH_BIGINT=0`（v8 static）**：构建 rc=0、C++ `58/58`、`765/765`、0 failing；
typings `int64 = number` / `uint64 = number`；`tsc` rc=0；TS `COMPLETED`、
`NUMERIC checks=15`、`INT64 checks=5`、`bigint=false`。**该配置此前从未通过，现在通过。**

三种状态（`=1` 默认、`=0`）均已实测。

## 备注

- 之前 report 里提到的"固定 bigint 只作用于 declared-64-bit 出口（A 方案）"已随 FIXED 宏一并作废。
- `jsb_primitive_conv.h` 的注释已改为"按值大小决定类型，调用者自收窄"。
- `GDToJS<Object*>` 快路径与 `StaticBindingUtil` 的偏特化不受本轮影响，保留。

---

# 复审整改 第 4 轮：静态绑定里剩下的 `gd_var_to_js` 换成 `GDToJS` / `translate_return`（2026-09-26）

用户逐条指出三处，全部执行。

## 1. `builtin_operators.h` 的三处 `gd_var_to_js`

前两处（`operator_thunk` ~143、`operator_unary_thunk` ~181）改完。第三处
`evaluate_dynamic_binary`（~210）**不适用，保留**：它是 `Variant::evaluate` 产出
的运行期 `Variant ret`，没有编译期类型，`GDToJS<T>` 无从下手。

```cpp
// 改前：Ret 是 operator 的 Variant STORAGE 类型
Variant ret_val = godot::PtrToArg<Ret>::convert(&ret_slot);
if (!TypeConvert::gd_var_to_js(isolate, context, ret_val, rval)) { ... }
// 改后：Ret 已知，结果直接走 GDToJS<Ret>
const Ret value = godot::PtrToArg<Ret>::convert(&ret_slot);
if (!GDToJS<Ret>::convert(isolate, context, value, rval)) { ... }
```

**等价性论证（实测）**：从 `src/static_binding/gen/*.gen.*` 统计 operator thunk 的
`Ret` 分布，只有 `bool`(164) 与 wrapper 类型（`Vector3` 16、`Vector2` 12、
`Vector4` 12、`Vector2i` 8、`Vector3i` 8、`Vector4i` 8、`Color` 8、`Quaternion` 7、
`Transform2D` 5、`Basis` 5、`Transform3D` 5、`PackedVector2Array` 3、
`PackedVector3Array` 3、`Rect2` 2、`Plane` 2、`AABB` 2、`Projection` 1、`Array` 1、
`PackedByteArray` 1），**没有标量**。`GDToJS<T>` 对 wrapper 类型走的就是
`TypeConvert::gd_var_to_js(Variant(p_src), r_out)` 那一条，与原文逐字同路；
`bool` 两侧都是 `v8::Boolean::New`。

## 2. `builtin_members.h`：`MemberVT` 换成具体 C++ 类型

`member_getter_thunk`（Form A）与 `shared_member_getter_thunk`（Form B）的第二个
模板参数从 `godot::Variant::Type MemberVT` 改为 **`typename T`**（具体 C++ 类型），
出口由 `TypeConvert::gd_var_to_js(result)` 改为 `GDToJS<T>::convert(value)`。
setter 两个 thunk **不动**（严格转换需要 Variant 类型）。

**等价性论证（实测）**：`project/extension_api.json` 里 62 个 builtin member 的声明
类型分布为 `float` 27 / `int` 13 / `Vector2` 6 / `Vector3` 8 / `Vector4` 4 /
`Vector2i` 3 / `Basis` 1 —— **0 个 bool、0 个 char**。而槽宽由引擎
`variant_setget.h` 的 `SETGET_NUMBER_STRUCT(Vector2, double, x)` /
`(Vector2i, int64_t, x)` 决定，恰好是 `VariantEncodeType<real_t>` /
`<int64_t>`，即原 `VariantNativeType_t<FLOAT>` / `<INT>`。故 ptrcall 槽位与出口
语义逐位等价（`Vector2i.x` → `int64_t`，`Color.r8` → `int64_t`，`Vector2.x` →
`real_t`，实测生成物一致）。

codegen：`MemberData` 增 `type_str`（原始声明类型串）；新增模块级
`member_cpp_type(t, meta)`（复用 `arg_template_expr`，逗号列表取首项）；Form A 的
getter 分支用具体类型、setter 分支仍用枚举；Form B 拆成 `g_sigs`/`s_sigs` 两张表与
两个 id 映射（`g_sig_id` / `s_sig_id`），各成员行的索引相应拆开。

## 3. `indexed_property_getter_thunk`：新增返回值类型参数

模板加 `class RetT`；thunk 体改为

```cpp
// 后备方法经 object_method_bind_call 返回完整 Variant —— 正是
// Ret<T>::translate_return 归一化的缓冲形状
if constexpr (RetT::has_return) {
    RetT::translate_return(isolate, context, ret, info);
}
```

**类型来源更正**：不用 `prop_type`。实测 415 个已解析的索引属性里，`prop_type`
有 23 处与其 getter 方法的真实返回类型不符（`Texture2D,-AnimatedTexture,…` 列表
→ 方法返回 `Texture2D`；`CurveTexture`/`CurveXYZTexture` → `Texture2D`；
`int` → `enum::Viewport.PositionalShadowAtlasQuadrantSubdiv`）。改为取
**getter 方法自身的 `return_value`**，新增 `method_ret_expr(meth)` 复用
`ret_template_expr`，收集期存入 `_gret`。生成 413 个实例，实际用到的
`Ret<>` 为 `Ret<float>` 140 / `Ret<godot::Object*>` 128 / `Ret<bool>` 81 /
`Ret<int32_t>` 24 / `Ret<godot::Vector2>` 17 / `Ret<godot::String>` 10 /
`Ret<godot::PackedByteArray>` 5 / `Ret<godot::NodePath>` 4 / `Ret<int64_t>` 4。

## 验证

**五腿（`JSB_WITH_BIGINT=1` 默认）全绿**

| 腿 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0 245s | `58/58` `813/813` + `12/12` | `COMPLETED` `checks=55` `NUMERIC=23` |
| v8 shared | rc=0 84s | `58/58` `813/813` | 同上 |
| v8 dynamic | rc=0 66s | `57/57` `806/806` | 同上 |
| qjs static | rc=0 293s | `59/59` `818/818` | 同上 |
| qjs dynamic | rc=0 65s | `58/58` `806/806` | 同上 |

**`JSB_WITH_BIGINT=0`（v8 static）**：构建 rc=0 210s、C++ `58/58` + `3/3`、
`tsc` rc=0、TS `COMPLETED`、`NUMERIC checks=15`、`INT64 checks=5`、
`bigint=false`。复验后已复位 `JSB_WITH_BIGINT 1` 并重编（rc=0 214s），
静态腿再确认一次 `58/58` + `checks=55/23`。

**新路径端到端冒烟（一次性场景，已删除）**

五腿套件没有专门触发索引属性 getter（413 个实例此前零覆盖），故建
`project/tests/_smoke_indexed/` 做差分验证：同一个后备 getter 既作为索引属性
（改后的 `indexed_property_getter_thunk`）又可作为普通 class method
（`class_method_thunk`，本来就走 `Ret<T>::translate_return`），两者一致即证明等价。
状态变更**只走方法 API**（`set_param_max` / `set_offset`），因为索引 setter 模板本次
一行未动、且其中一个撞上无关的既有引擎崩溃。结果 **26/26 通过**，覆盖：

- 索引 getter：`Ret<bool>`（particle_flag）、`Ret<float>`（param_max/param_min/
  offset，含负值）、`Ret<int64_t>`（Viewport quad 0..3，enum 型 getter）、
  `Ret<godot::Object*>`（param_curve / albedo_texture，null 与非 null）、
  `Ret<godot::NodePath>`
- builtin member：`int64_t`（`Vector2i.x` = -2147483648 / `.y` = 2147483647，确认
  仍是 Number 而非 BigInt）、`real_t`（`Vector2.x/.y`、`Color.r`、`Vector3.z`）、
  `int64_t`（`Color.r8` / `.a8`）、`Vector2`（`Transform2D.origin`）、
  `Vector3`（`Vector3.z`、`Basis.z`）、`Basis`（`Transform3D.basis`）

## 附带发现（**既有缺陷，不是本轮回退，未改**）

> **更正（第 5 轮）**：本节初版把根因写成"`prop_vt_value()` 送回 `Variant::BOOL`/`FLOAT`，
> ptrcall 按错的类型解释 `set_flag(int, bool)` / `set_offset(Side, float)` 的首参"。
> **那是错的**，用户质询后已用实验推翻，见下面「根因更正」。

---

# 复审整改 第 5 轮（2026-09-26）

## 1. `RetT` 移到 `IndexC` 之前（已执行）

```cpp
// 改后
template <uint32_t HashC, FixedString ClassLit, FixedString MethodLit, class RetT, int IndexC>
void indexed_property_getter_thunk(...)
```

codegen 同步：`indexed_property_getter_thunk<%du, "C", "m", Ret<...>, <index>>`。
现在 getter 与 setter 的参数顺序对齐（setter 是 `..., int IndexC, godot::Variant::Type ArgVT`）：
两者都是"常量索引在前、类型在后"的镜像。

## 2. 为什么是 `Ret<>` 而不是裸返回类型（说明，未改）

因为**这一层需要的不是类型本身，而是"该类型对应哪个 ptrcall 缓冲形状"**，而
`Ret<T>::translate_return` 存在的唯一理由就是归一这件事。三个具体原因：

**(a) 这里的返回值来自 `object_method_bind_call`，得到的是完整 `godot::Variant`，
不是 `PtrToArg<T>::EncodeT` 缓冲。** `translate_return` 的全部代码就是这一个分支：

```cpp
const type value = [&] {
    if constexpr (std::is_same_v<ReturnBufT, godot::Variant>) {
        return variant_as<type>(p_ret_val);          // 从 boxed Variant 取标量
    } else {
        return godot::PtrToArg<type>::convert(&p_ret_val); // 从 EncodeT 缓冲取
    }
}();
GDToJS<type>::convert(p_isolate, p_context, value, jrval);
```

裸类型参数**表达不了** `ReturnBufT` 是 `godot::Variant` 这件事，调用点就得自己写
`variant_as<T>(ret)` 再 `GDToJS<T>`。（`builtin_operators.h` / `builtin_members.h`
之所以能直接用 `GDToJS<Ret>`，正因为那两处拿到的是 `PtrToArg<Ret>::EncodeT` 缓冲，
走的是 `translate_return` 的 else 分支。）

**(b) `variant_as<T>` 带 `is_character_code_unit_v` 特判 + 穷尽 `static_assert`。**
`Variant` 没有 `char16_t`/`char32_t` 转换运算符，必须 `(T)(int64_t)p_v`；这个修正与
"哪个类型不能直接读"的编译期守卫，挂在 `Ret<T>` 里是共享的，散到每个 thunk 调用点
就是重复且容易漏。

**(c) `void` 与"无静态类型"这两态无法用裸类型表达。** `Ret<void>` 带
`has_return == false`；而 `Variant` / `typedarray::*` 的返回要用
`Ret<godot::Variant>`（即回到 `gd_var_to_js`）。若走裸类型，调用点会退化成
`GDToJS<T>`，那 `T` 就没法表示"无返回值"和"类型未知"。

结论：`Ret<>` 是这一层的 buffer-shape 描述符，不是多余包装；它让
`class_method_thunk` / `class_vararg_method_thunk` / `shared_class_method_thunk` /
`indexed_property_getter_thunk` 四个调用点共用同一条归一逻辑。

## 3. 根因更正：setter 崩溃与 `prop_vt_value()` / ptrcall **无关**

**用户质询是对的，我上一轮的归因错了。** 实测推翻：

- 崩溃帧 `[2]` 落在 `gdextension_interface.cpp:1343`，那一行是
  `memnew_placement(r_return, Variant(mb->call(o, args, p_arg_count, error)));`
  —— 这是 **Variant 路径**（`mb->call`），不是 ptrcall；`args` 就是从 thunk 传进去的
  那个 `Variant` 数组，首元素确实是 `Variant((int64_t)IndexC)`，语义正确。
- 真正的问题是 **`r_return` 传了 `nullptr`**，而该函数**不判空**，直接
  `memnew_placement` 到空指针 → 崩。

判决性实验（临时场景，已删除；实验性改动已回退）：把 setter 的调用点从
`nullptr` 改成 `&ret`（本仓库其它 4 处 `object_method_bind_call` 调用点全部传 `&ret`），
其余一行不动 —— `Control.set_offset(0,-7.5)` 的方法调用 + `ctl.offset_left = -1.25`
的属性写入 **全部通过，不再崩溃**：

```
SMOKE-SETTER step1 ok, offset_left=-7.5
SMOKE-SETTER step2 ok, offset_left=-1.25
```

所以：
- 与"`prop_vt_value()` 送错 Variant 类型"无关（该值只用于 setter 的
  `js_to_gd_var(..., arg_vt, value)` 严格转换，且 `args[0]` 的枚举语义是对的）；
- `MethodBindT<Control,enum Side,float>::call` 只是崩溃时正在执行的被调方法，
  是**受害者**而非原因；
- 属**既有缺陷**（与 `GDExtension` API 的 `r_return` 非空约定不一致），本次未改；
  一行修复即 `object_method_bind_call(..., 2, &ret, &call_error)`（返回值丢弃）。

## 验证（第 5 轮）

`RetT` 顺序调整后重跑全矩阵：

| 配置 | 构建 | C++ | TS |
|---|---|---|---|
| v8 static | rc=0 195s | `58/58` + `3/3` | `COMPLETED` `checks=55` `NUMERIC=23` |
| v8 shared | rc=0 90s | `58/58` + `3/3` | 同上 |
| v8 dynamic | rc=0 60s | `57/57` + `3/3` | 同上 |
| qjs static | rc=0 273s | `59/59` + `3/3` | 同上 |
| qjs dynamic | rc=0 64s | `58/58` + `3/3` | 同上 |
| `JSB_WITH_BIGINT=0` | rc=0 228s | `58/58` + `3/3` | `NUMERIC=15` `INT64=5` `bigint=false` |
| 复位 `=1` 后复验 | rc=0 207s | `58/58` + `3/3` | `checks=55` `NUMERIC=23` |

`tsc` 全 rc=0。生成物已确认新参数顺序
（`indexed_property_getter_thunk<..., Ret<float>, 0>` / setter 仍
`indexed_property_setter_thunk<..., 0, godot::Variant::FLOAT>`）。

索引 getter 的 `Ret<>` 实测分布（433 实例）：
`Ret<float>` 142、`Ret<godot::Object*>` 128、`Ret<bool>` 84、`Ret<int32_t>` 24、
`Ret<godot::String>` 20、`Ret<godot::Vector2>` 17、`Ret<godot::PackedByteArray>` 10、
`Ret<godot::NodePath>` 4、`Ret<int64_t>` 4 —— 即 9 种；其中 `Object*`、`Vector2`、
`String`、`PackedByteArray`、`NodePath` 五种没有 `GDToJS` 的专用臂，走的是它内部
`TypeConvert::gd_var_to_js(Variant(p_src), ...)` 的 fallback —— 正是因此"直接用
`GDToJS<裸类型>`"与"经 `Ret<T>`"在结果上一致。`Ret<void>` / `Ret<Variant>` /
char32 **均未出现**，`translate_return` 的 `has_return` 与 `variant_as` 特判在索引
getter 上都不需要触发（但共享同一份模板，故仍正确）。

## 3b. `r_ret` 能不能传 `nullptr`：不能，即使方法无返回值

问题：`indexed_property_setter_thunk` 调的 `set_offset`/`set_flag` 无返回值，`r_ret`
传 `nullptr` 是否合法？

**不合法。** 引擎实现（`core/extension/gdextension_interface.cpp:1337-1345`）：

```cpp
static void gdextension_object_method_bind_call(..., GDExtensionUninitializedVariantPtr r_return,
                                                GDExtensionCallError *r_error) {
    ...
    memnew_placement(r_return, Variant(mb->call(o, args, p_arg_count, error)));  // 无条件
    if (r_error) { ... }                                                          // 只有 error 判空
}
```

- `memnew_placement(r_return, ...)` **无条件执行**，不看 `mb` 有没有返回值。
- `MethodBindT::call(...)`（`method_bind_common.h:79-90`）签名**返回 `Variant`**，总是
  `return Variant();`（void 方法也返回空 `Variant`）——所以"无返回值"在
  `object_method_bind_call` 这一层根本不存在，永远会写一次。
- 这与紧邻的 `object_method_bind_ptrcall` 不同，后者直接转发 `mb->ptrcall(o, args, p_ret)`，
  对 void 方法确实可以传 `nullptr`。
- godot-cpp 自己的生成代码（`binding_generator.py:2261`、`gen/src/classes/*.cpp`）
  **一律**传 `&ret`，从不传 `nullptr`。

**clean A/B 对照**（同一份源码、同一参数顺序，唯一变量 `nullptr` vs `&ret`）：

| 组 | setter 调用点 | 结果 |
|---|---|---|
| A | `..., 2, nullptr, &call_error)` | `Control.offset_left = -1.25` → **崩溃**（`gdextension_interface.cpp:1343`） |
| B | `..., 2, &ret, &call_error)` | `SMOKE-SETTER step2 ok -1.25`，通过 |

故属既有缺陷；一行修复即 `object_method_bind_call(..., &ret, &call_error)`（返回值丢弃）。
本轮**未改**（未被要求）。工作区已恢复为 `nullptr`。

---

# 第 6 轮：修复索引属性 setter 的两条路径 + 提交推送（2026-09-26）

用户授权：「把这个修了，在基准测试里新增几个相关的测试进行覆盖。然后根据实际情况
分次提交，再推送，让 CI 跑测」。

## 修复 1：static 路径 `r_return = nullptr`

`indexed_property_setter_thunk` 调 `object_method_bind_call` 时传 `nullptr` 作为
`r_return`。引擎实现在 `core/extension/gdextension_interface.cpp:1337-1345`：

```cpp
memnew_placement(r_return, Variant(mb->call(o, args, p_arg_count, error)));  // 无条件
if (r_error) { ... }
```

`memnew_placement` **不判空**，且 `MethodBindT::call` 即使对 void 方法也
`return Variant();` —— 所以"无返回值"在这一层不存在，永远会写一次。**即使方法无
返回值，`r_return` 也不能传 `nullptr`。** 对比紧邻的 `object_method_bind_ptrcall`：
它直接 `mb->ptrcall(o, args, p_ret)`，void 时确实可为 null。godot-cpp 自己的生成代码
（`binding_generator.py:2261`、`gen/src/classes/*.cpp`）一律传 `&ret`。

全仓 6 个 `object_method_bind_call` 调用点里，只有这一处传 `nullptr`。

**clean A/B 对照**（同一份源码、同一参数顺序，唯一变量就是这一个实参）：

| 组 | 调用点 | 结果 |
|---|---|---|
| A | `..., 2, nullptr, &call_error)` | `Control.offset_left = -1.25` → 崩溃（`gdextension_interface.cpp:1343`） |
| B | `..., 2, &ret, &call_error)` | 通过 |

修复：传一个真实 `godot::Variant ret`（返回值丢弃）。

## 修复 2：reflect 路径取错了参数

覆盖测试在 dynamic 腿立刻暴露第二个既有缺陷：`_godot_object_set2` 用
`get_argument_type(0)` 转换**被赋的值**。但索引属性的 setter 首参是**常量索引 /
枚举**，值在末位：

```
set_offset(Side, float)                  set_param_max(Parameter, float)
set_flag(Flags, bool)                    set_param_texture(Parameter, Texture2D)
set_particle_flag(CPUParticles2D.Particle, bool)
```

实测 `project/extension_api.json`：**415 个已解析的索引 setter 全部恰好 2 个参数**
（arg0 = index/enum，arg1 = 值），无一例外。所以按 arg0 转换必然错：
- `initial_velocity_min = 11.5` → 按 INT 转换 → 读回 `11`（截断）
- `particle_flag_align_y = true` → `Failed to convert JS variable to Variant.
  Expected: int. Found: boolean`

修复：读末位参数 `get_argument_count() - 1`。

## 覆盖

- **新增 `project/tests/indexed-props/test-indexed-props.ts`**（+`.tscn`，注册进
  `start.ts` 场景列表）。这是必要的，不只是"顺手加"：**benchmark job 的 `if` 是
  `github.event_name == 'workflow_dispatch'`，push 不会跑它**，而 `test` job 跑
  `project/`。不放集成场景的话，推送后 CI 根本不会验证这些修复。
  覆盖 float / bool / object / NodePath / enum-首参 五种形状，**get 与 set 都测**，
  且每个值再通过方法 API 回读交叉验证；四个 offset 索引互不相同的断言抓"常量索引
  被共享"；失败走 `reportTestFailure` 哨兵，CI 会失败。`EXPECTED_CHECKS = 32` 硬编码。
- **benchmark 新增 14 个用例**：`IndexedProp`（CPUParticles2D：float idx6/idx0、
  bool idx0、object idx6 —— 最后这个就是修复前必崩的）与 `IndexedPropEnum`
  （Control：get/set_offset、get_anchor、get/set_focus_neighbor）。

## 验证

| 腿 | 构建 | C++ | TS | bench |
|---|---|---|---|---|
| v8 static | rc=0 267s | `58/58` + `3/3` | `COMPLETED` `INDEXED-PROPS=32` `checks=55` `NUMERIC=23` | invalid=0，新用例 14/14 无错 |
| v8 shared | rc=0 80s | `58/58` + `3/3` | `COMPLETED` `INDEXED-PROPS=32` | — |
| v8 dynamic | rc=0 59s | `57/57` + `3/3` | `COMPLETED` `INDEXED-PROPS=32`（修前 25/32 失败） | invalid=0，新用例 14/14 无错 |
| qjs static | rc=0 | `59/59` + `3/3` | `COMPLETED` `INDEXED-PROPS=32` | — |
| qjs dynamic | rc=0 | `58/58` + `3/3` | `COMPLETED` `INDEXED-PROPS=32` | — |

## 提交与推送

已推送 `6745e93..62044ef` → `origin/feature/int64`，8 个分批提交：

```
62044ef docs: record the copyright-header rule and align a stale comment
bc8702c docs(trellis): record the numeric contract work and its evidence
970f9ac ci: let the binding mode be selected for a manual run
5b9d775 test: make the suites build-mode aware and expand the benchmark
583a898 fix(binding): repair both indexed-property setter paths
56a395e feat(codegen): derive the fixed-width alias ladder and type the generated thunks
ddb60ab refactor(static_binding): type the getter exits instead of round-tripping a Variant
0f8c876 fix(runtime): converge fixed-width numeric conversion and the 64-bit return contract
```

不提交并保留原样：`third/quickjs-ng`（子模块内未跟踪的 `.obj` 编译产物，假修改）。

`push` 事件触发 CI 的 `build` + `test` job（三个 runtime 矩阵 host-v8 / host-qjs /
host-node 均运行 `project/`，含新集成场景）。**benchmark job 不会因 push 触发**
（`if: github.event_name == 'workflow_dispatch'`），需手动 `gh workflow run`。

> 更正：早期记录里写过"本机无 `gh`，未做手动触发"，**那是错的**。当时是用出故障的
> `bash -lc` 子 shell 去 `which gh`，那个环境 PATH 与原生 Windows 不同，于是误判。
> 实测 `gh` 在 `C:\Program Files\GitHub CLI\gh.exe`，已在 PATH 且已登录
> （token scope 含 `repo` + `workflow`）。

**注意**：`ci.yml` 的 `concurrency.group` 含 `github.ref_name` 且
`cancel-in-progress: true`，所以 `gh workflow run --ref feature/int64` 会**取消**同一
ref 上正在跑的 push CI。必须先等 push 那次跑完，再 dispatch 跑 benchmark。

---

# 第 7 轮：CI 抓到的两阶段名字查找 bug（2026-09-26）

## 事实

推送 `62044ef` 后 CI run `36229359756` 结束，**15 个 Build 腿失败**，全部同一个根因：

```
src/runtime/bridge/jsb_type_convert_direct.h:161:11: error: call to function
'js_to_fixed_width_int' that is neither visible in the template definition
nor found by argument-dependent lookup
```

MinGW g++ 报同一件事的另一种措辞：`'js_to_fixed_width_int' was not declared in
this scope, and no declarations were found by argument-dependent lookup at the
point of instantiation`。

失败腿覆盖 clang 与 MinGW 家族：macos-{v8,qjs-ng,jsc}、ios-{v8,qjs-ng,jsc}、
android-{v8,qjs-ng}×2、web-{v8,qjs-ng}×2、windows-x86_64-qjs-ng(mingw)。
通过的是 MSVC 与 Linux/GCC 的腿。

对照：改动前的 base `6745e93` 的 CI run `36116332535` 是 **success**。所以这是
本次工作引入的回归，不是既有问题。

## 根因

`jsb_type_convert_direct.h` 里 `JSToGD<T>` 主模板（第 120 行）的窄整数分支调用
`js_to_fixed_width_int<T>`，而该函数**定义在第 191 行**——在模板定义点之后。

模板里对**不依赖自身模板参数**的非限定名字的调用，按两阶段查找是在**定义点**
解析的（2.5 阶段的 ADL 也无法救：`CppT` 是 `int8_t`/`uint16_t` 这类内建类型，
关联命名空间集合为空）。所以名字必须在定义点可见。

MSVC 不做严格两阶段查找（默认延迟模板解析），GCC 在这个形状上也放过，因此**本地
MSVC 构建全绿**，而所有 clang/minGW 腿全红。本地此前只跑了 MSVC 静态/共享/动态 +
quickjs 的 Windows 构建，从未用 clang 工具链编过一次。

## 修复

在 `JSToGD` 主模板之前加前向声明（与文件里 `JSToGD` 自身已有的
`template <typename T> struct JSToGD;` 做法一致）：

```cpp
template <typename T>
struct JSToGD;

// Forward-declared because the `JSToGD` primary template below calls it, and a
// template's unqualified call to a name that does not depend on its own
// parameters is resolved at the point of DEFINITION (two-phase lookup).
template <typename CppT>
inline bool js_to_fixed_width_int(v8::Isolate *p_isolate,
        const v8::Local<v8::Context> &p_context,
        const v8::Local<v8::Value> &p_jval,
        CppT &r_out);
```

## 验证（本地复现 → 修复 → 全量）

**复现**（`.agent_tmp/probe_2phase.cpp`，强制实例化窄整数臂；已删除）：

```
clang++ -fsyntax-only -std=c++20 -fno-ms-compatibility -fno-delayed-template-parsing   -Dalloca=_alloca ... .agent_tmp/probe_2phase.cpp
```

修复前：`error: call to function 'js_to_fixed_width_int' that is neither visible in
the template definition nor found by argument-dependent lookup`（2 处）。
修复后：**rc=0，0 错误**。

（注：`-Dalloca=_alloca` 是 Windows 特有的噪声绕过——`api_tool_types.h` 的
`stack_alloc` 用 `alloca`，严格模式下的 clang 在 MSVC 头里找不到它；与目标 bug 无关。）

**全量 clang 构建**：`scons ... binding_mode=static windows_compiler=clang-cl` →
**rc=0、0 错误**，覆盖包括 `static_binding/gen/*.gen.cpp` 在内的全部 TU。

**clang-cl 产物实跑**：C++ `58/58` + `3/3`；TS `COMPLETED`、
`INDEXED-PROPS=32`、`INT64=55`、`NUMERIC=23`。即换成 clang 编译后行为与 MSVC 一致。

## 推送

`62044ef..46809bb` → `origin/feature/int64`。新 run `36229993231`。

## 第 7 轮之二：CI 抓到的第二个回归——命名空间遮蔽（web 腿）

修掉两阶段查找后，`36229993231` 只剩 **web-v8** 一条腿失败（其余全过）：

```
src/runtime/impl/web/jsb_web_global_init.cpp:190: error: no member named 'Logger'
  in namespace 'jsb::impl::internal'; did you mean '::jsb::internal::Logger'?
src/runtime/impl/web/jsb_web_global_init.cpp:211: error: no member named 'settings'
  in namespace 'jsb::impl::internal'; did you mean '::jsb::internal::settings'?
```

**也是我引入的**：我把共享标量转换函数移进新建的 `jsb::impl::internal`
（`jsb_primitive_conv.h:66`）。C++ 名字查找里，**内层命名空间会遮蔽外层的同名
命名空间**——`namespace jsb::impl { ... internal::Logger ... }` 里的非限定
`internal::` 先看 `jsb::impl::internal`，找到就停，而那里没有 `Logger`。于是项目
全局的 `jsb::internal::Logger` / `::settings` 被挡掉。

为什么只有 web 腿炸：只有 `src/runtime/impl/web/jsb_web_global_init.cpp` 恰好在
`namespace jsb::impl` 里用**非限定**的 `internal::Logger` / `internal::settings`；
v8/quickjs/jsc 的同类文件写的是非限定名但那些 TU 的 include 顺序/内容不同，
或者它们用的是其它限定形式。而这些 TU **只在各自后端构建时才编译**——本机只编
过 v8 与 quickjs-ng，所以本地全绿。

**修复**：把新命名空间改名为 `jsb::impl::detail`（28 处替换，6 个文件），与任何东西
都不冲突。

**机制最小复现**（`.agent_tmp/repro_shadow.cpp`，已删除）：

```cpp
namespace jsb {
namespace internal { struct Logger { static void set_callbacks(); }; }
namespace impl {
#ifdef REPRO_SHADOW
namespace internal { struct to_int64_placeholder {}; }   // 旧名字：在这里重新引入
#endif
void global_init() { internal::Logger::set_callbacks(); }  // 与 jsb_web_global_init.cpp 同形
}
}
```

| 组 | 结果 |
|---|---|
| 有内层 `internal`（修复前） | `error: no member named 'Logger' in namespace 'jsb::impl::internal'; did you mean '::jsb::internal::Logger'?` |
| 改名后 | rc=0 |

错误文本与 CI **逐字一致**，机制确认；同时说明它是**后端相关**的——只有该后端的
TU 会编译到，这也是本机 MSVC 构建发现不了的根本原因。

**验证**：`windows_compiler=clang-cl` 全量构建 **rc=0、0 错误**；产物实跑
C++ `58/58` + `3/3`、TS `COMPLETED`、`INDEXED-PROPS=32`、`INT64=55`、`NUMERIC=23`。

**推送**：`46809bb..84a9a26` → `origin/feature/int64`。

## 第 7 轮小结：本机验证的根本盲区

两次 CI 失败（两阶段查找、命名空间遮蔽）都是**本地 MSVC 构建永远不会发现**的类型：

| 盲区 | 本机为何漏掉 | CI 为何抓到 |
|---|---|---|
| 两阶段名字查找 | MSVC 默认延迟模板解析，不做严格两阶段查找 | 所有 clang 腿 + MinGW g++ |
| 命名空间遮蔽（后端特有 TU） | 本机只编过 v8 / quickjs-ng 的 Windows 构建 | web / ios / android 各自编译自己的 TU |

两次修复都用**本地可复现**的手段钉死了（严格模式 clang 复现两阶段；最小命名空间
复现遮蔽），而不是靠"改完等 CI"。

---

# 第 8 轮：benchmark CI、归档、生成产物对比（2026-09-26）

## 触发 benchmark CI

`gh workflow run ci.yml -f binding_mode=shared --ref feature/int64`
→ run [36236962521](https://github.com/Daylily-Zeleen/GodotJS-Ext/actions/runs/36236962521)。

选 `shared` 的理由：benchmark job 自己用 matrix 构建 static/dynamic 两个扩展做对比
（`benchmark-build` 腿），`binding_mode` 输入只影响 desktop 的 build/test 腿。取
`shared`（scons 默认）即测"CI 的常规配置 + static/dynamic 基准对比"。

触发前确认了没有正在跑的 run —— `ci.yml` 的 `concurrency.group` 含 `github.ref_name`
且 `cancel-in-progress: true`，此时 dispatch 会取消同 ref 上的在跑任务。

## 生成产物与基线对比（"生成文档与基线的差异"）

### 结论：不存在入库的基线，只有我 codegen 改动导致的**两行**预期变化

**基线现状**（实测）：

| 项 | 事实 |
|---|---|
| `misc/verify_codegen.py` | 在库（15600 B） |
| 基线目录 `<检出根>/.codegen-baseline/` | **不存在**；`find` 搜遍整个检出也无 |
| `project/gen/`、`project/typings/` | 生成产物，**gitignore**（`git ls-files` 计数 0；`project/.gitignore:10:typings/`） |
| `project/typings/type.extension.d.ts` | 手写，**已入库**（不在 verify_codegen 的比对范围里） |
| `scripts/typings/godot.minimal.d.ts` | 手写源，经 SConstruct `PresetDefine` 嵌入 `jsb_editor_preset.gen.cpp`（该 gen.cpp 被 `*.gen.*` ignore），产出物是安装用的 preset |

所以"基线"只存在于规范描述里，本检出从未建立过。`misc/verify_codegen.py --diff-only`
在此会直接 `die("基线目录不存在…首次建立用 --update-baseline")`。**未建立基线**——
它会把"含本次改动的产物"固化，正如规范所禁止；正确做法是在**改动前**的干净检出上建。

**我改动的 codegen 面**（`6745e93..HEAD`）：仅 `src/editor/codegen/jsb_codegen_defs.cpp`
（+21/-4）与 `scripts/typings/godot.minimal.d.ts`（2 行）。

### 唯一的行为性产物变化：两行别名

`kPredefinedLines` 里 `int64` / `uint64` 从 `number /* || bigint */`（注释掉，等于
`number`）改为按 `JSB_WITH_BIGINT` 条件编译：`number | bigint` 或 `number`。

生成产物实测印证（`project/typings/godot0.gen.d.ts`，由 `--generate-types` 产出）：

```
type int64 = number | bigint
type uint64 = number | bigint
```

与 `JSB_WITH_BIGINT=1`（默认）的预期一致；同轮已实测 `JSB_WITH_BIGINT=0` 时输出
`type int64 = number` / `type uint64 = number`，并配 `tsc` rc=0 + TS 套件通过。

**没有别的产物变化**：别名没有新增/删除行（仍在原位，只是文本变了），生成器其余部分
一个字未动。这条别名就是"输入 = `JSB_WITH_BIGINT` + 本文件"的确定性输出，无输入漂移
（引擎 4.7.1 未变、测试项目 TS 结构未变）。

### 需要你裁决的一处：`scripts/typings/godot.minimal.d.ts` 的一行

同一文件里我还改了：

```
-        function is_original_class_exposed(class_name: string): bool;
+        function is_original_class_exposed(class_name: string): boolean;
```

核实结论：
- 这是该文件里**唯一**的 `bool`；其余 15 处都是 `boolean`（`grep ': bool'` 的其余命中
  全是 `: boolean` 的前缀匹配）。**TypeScript 没有 `bool` 类型**，基线那行是笔误。
- 它**不由 codegen 生成**（手写源，经 PresetDefine 嵌入 gitignored 的 `.gen.cpp`），
  所以它既不进 `project/gen`、也不进 `project/typings`，不属于 verify_codegen 的
  比对面，**不构成"与实际生成产物的差异"**。
- 但它是**与 64 位数值契约无关的顺带修改**，在 `0f8c876` 里和主线改动混在了一个提交。

处置：**保留**（修的是真笔误，方向正确；回退等于为了"最小 diff"重新引入 typo），
但在此明确披露。需要隔离成单独提交或回退，说一声即可。

## 归档

`.trellis/scripts/task.py archive static-binding-numeric-contract`：把 `task.json`
置为 `status: completed` + `completedAt`，移入 `.trellis/tasks/archive/2026-09/`，
并自动提交（该命令自带 git 提交，precondition 要求先提交其他工作区改动）。

## benchmark CI 结果（run 36236962521，全绿）

`Benchmark build (static)` / `(dynamic)` / `Benchmark (static vs dynamic)` 三腿 success。

一致性门禁（它才是"绑定路径等价"的证据）：

```
consistency gate passed (248 cases, 1 process-dependent skipped: Node.get_instance_id(0))
```

248 个用例里 static 与 dynamic 的 probe 结果逐项相等，唯一跳过的是 ObjectID（跨进程不可比，
既有 `processDependent` 标记）。新增的 14 个索引属性用例**全部参与**且一致 —— 即两种绑定
模式的索引属性读写结果相同。

### 新增用例的实测数据（ns/call，lower is better）

| 用例 | dynamic | static | dyn/stat |
|---|---:|---:|---:|
| get float(param_max,6) | 73.1 | 94.6 | 0.77x |
| get float(param_min,0) | 72.8 | 94.6 | 0.77x |
| get bool(particle_flag,0) | 72.4 | 91.4 | 0.79x |
| get float(get_offset,0) | 75.2 | 99.0 | 0.76x |
| get float(get_offset,3) | 73.2 | 98.8 | 0.74x |
| get float(get_anchor,0) | 74.7 | 91.3 | 0.82x |
| get object(param_curve,6) | 80.2 | 109.1 | 0.74x |
| get nodepath(focus,0) | 837.3 | 826.8 | 1.01x |
| set float(param_min,0) | 144.8 | 126.1 | 1.15x |
| set float(param_max,6) | 146.6 | 123.4 | 1.19x |
| set bool(particle_flag,0) | 150.0 | 122.4 | 1.23x |
| set object(param_curve,6) | 157.5 | 131.7 | 1.20x |
| set float(set_offset,0) | 145.2 | 120.9 | 1.20x |
| set nodepath(focus,0) | 1773.2 | 1902.3 | 0.93x |

### 如实记录一个反向信号：索引 getter 在 static 下更慢

上面标量 getter 的 dyn/stat 是 **0.74~0.82x**，即 static **慢约 20~35%**（绝对值 ~20ns）。
除了 `nodepath`（1.01x，两者相当），标量索引 getter 一律如此。

初步判断（**未做实测归因，仅机制推断，`[INFERENCE]`**）：旧 static getter 是
`object_method_bind_call → Variant → TypeConvert::gd_var_to_js(ret, FLOAT)`，FLOAT 分支直接
`v8::Number::New(isolate, p_cvar)` 一次转换；改后是
`... → Ret<float>::translate_return(ret)` → `variant_as<float>`（一次 `(float)` 转换）→
`GDToJS<float>`（再一次 `(double)` 后 `v8::Number::New`）。多了一道转换与 `Ret<>` 间接层。
这是第 4 轮把 getter 出口统一到 `Ret<T>` 的**代价**；收益是四个调用点共用同一条归一逻辑，
并且是修复"索引属性 setter 崩溃"之后把两条绑定路径的行为对齐的前提。

**未处理**：这属于本轮范围外的性能优化（且 setter 侧反而是 static 更快，净收益需整体权衡）。
需要的话另开任务测：直接给 `indexed_property_getter_thunk` 传裸类型 + `GDToJS<T>`（像
`builtin_operators.h` 那样，因为这里拿到的就是完整 `Variant`，`variant_as` 那步是必需的，
可省的是 `Ret<>` 这一层 与 float 的二次转换）。

### 其它

- 二进制体积：static 67.83 MiB vs dynamic 35.33 MiB（既有结论，非本轮引入）。
- 引擎：`4.7.1-stable (official)`。
- 原始产物已下载到 `.agent_tmp/bench_artifact/`（report.md / static.json / dynamic.json）。
