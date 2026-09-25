# 父任务 09-06-lowprio-uint64-bigint-codegen — 执行记录

## 目标

64 位整数（int64/uint64）在 JS ↔ Godot 之间不再丢信息、参数传入不再抛异常，
三个数值槽（`int`/`float`/`bool`）的转换器接受面与引擎 `Variant::can_convert_strict`
对齐，BigInt 可用于内置类型的构造器与运算符；五引擎 × 三绑定模式行为一致。

用户可见验收：`instance_from_id(get_instance_id()) === obj`（RefCounted 的 ObjectID bit63
为真，值必然为负，此前必失败）。

## 四个子任务（全部已实施并提交）

| 子任务 | commit | 内容 |
|---|---|---|
| A `09-24-uint64-bigint-return` | `e165c8e` | 读方向：新建 `jsb_primitive_conv.h`（`to_int64`/`to_uint64`/`new_integer`/`new_unsigned_integer`），四引擎 helper 委托；uint64 无符号出口 |
| B `09-24-uint64-bigint-arg` | `2878498` | 写方向：`JSToGD<uint64_t>` + `StaticBindingUtil<uint64_t>`；**修 `to_uint64` Number 分支 UB**（`put_u64(1e19)` 曾写 `0x8000...`） |
| D `09-24-uint64-bigint-ctor-operators` | `28503cb` | `probe_vt` 加 `IsBigInt()→INT`；`JSToGD<bool>/<float>/<double>` 走原语；`builtin_operators.h` 改走原语 |
| C `09-24-uint64-bigint-switch` | `be6528b` | 新增 `JSB_BIGINT_FOR_64BIT`（默认 1，只管出口）+ 非法组合 `#error`；`BIGINT_FOR_64BIT` 导出到 JS |

独立修复 `66f5efc`：还原 `BINDING_MODE` 导出（`56306d5` 删除后残留未使用变量，bench 门禁依赖）。

## 本轮（窄槽对齐 + CI）— commit `919bcc5` / `b860933` / `7bd4d08`

### 窄槽对齐引擎：拒绝 → 截断

用户规则「引擎有检查就以动态腿为准，没有就以静态腿为准」。纯 GDScript 探针实测
（`.agent_tmp/gdprobe/probe*.gd`）确认引擎**从不校验宽度**：

```
put_8(300) -> [44]        put_8(-129) -> [127]      put_8(2^40) -> [0]
put_u16(70000) -> [112,17]                          put_8(1e300) -> [0]
Vector2i(3000000000, -3000000000) = (-1294967296, 1294967296)
put_8("abc") / put_8(null) -> 报错（类型类别不匹配）
```

根源 `core/variant/binder_common.h:58-72`：DEBUG 只走 `can_convert_strict`（Variant
类型类别级，拿不到宽度），release 直接 `static_cast`。故静态腿改为**截断**，与动态腿
（class 方法路径就是引擎自身转换）一致。越界只在 `JSB_DEBUG` 下告警；release 零开销
（实测 `narrow slot` 字符串在 template_release dll 中出现 **0** 次，editor dll 2 次）。

`StaticBindingUtil` 补齐 6 个窄整型特化（`int8/16`、`uint8/16/32`、`char32_t`）。

### CI

- `workflow_dispatch` 新增 `binding_mode` choice（default/static/shared/dynamic），
  仅覆盖桌面腿（windows/linux/macos）。
- 修 `benchmark-build` 上传名与 `benchmark` job 消费名不一致的既有 bug：
  产物统一为 `benchmark-static` / `benchmark-dynamic`。

### 任务目录移回

`15718e9` 提前归档了父任务与四个子任务，已移回 `.trellis/tasks/`（`7bd4d08`）。
**任务保持打开**，等远端 CI 覆盖补齐后再议归档。

## 本地验证证据（全部本轮实测）

| 腿 | C++ 套件 | TS 集成 | 窄槽告警 | 备注 |
|---|---|---|---|---|
| v8 static | 58/58、813 断言 SUCCESS | `C=1 F=0`、`INT64-DIAG checks=55` | 5 次 | |
| v8 shared | 813 断言 SUCCESS | `C=1 F=0`、`checks=55` | 5 次 | |
| v8 dynamic | 801 断言 SUCCESS | `C=1 F=0`、`checks=55` | 5 次 | 告警来自统一入口 `js_to_gd_var` |
| quickjs-ng static | 59/59、818 断言 SUCCESS | `C=1 F=0`、`checks=55`、`NUMERIC-DIAG checks=21` | 5 次 | |
| quickjs-ng dynamic | 806 断言 SUCCESS | `C=1 F=0`、`checks=55` | 5 次 | |
| release static | — | — | 0（零开销） | `template_release` 构建 0 error |

`JSB_BIGINT_FOR_64BIT=0` 模式：`checks=50 bigint=false`、`C=1 F=0`。

bench 门禁：`python misc/bench_matrix.py --report .agent_tmp/matrix` → `BENCH_RC=0`，
24 runs（static/shared/dynamic × gc/nogc × 4 轮），`invalid=0`，三腿 DLL md5 各异。

### quickjs-ng 走查发现的真实问题

`u64 2^63 bits` 在 quickjs-ng 腿失败（`0x-8000000000000000 != 0x-8000000000000000`
—— 两侧字符串相同却不等）。探针实测定位：**quickjs-ng 的 `BigInt.asUintN` 是坏的**
—— 快路径在 `bits >= JS_SHORT_BIG_INT_BITS` 时直接返回原值，而它
`JS_SHORT_BIG_INT_BITS = JS_LIMB_BITS = 32`，故 `BigInt.asUintN(64, -1n)` 得 `-1n`
而非 `18446744073709551615n`（v8 正确）。

**本项目转换层没有问题**：同一探针里 `get_u64()` 对 2^63 返回
`typeof=bigint str=9223372036854775808 neg=false`。是**测试辅助函数**依赖了引擎的坏
builtin。已把 `asUint64`/`lossy64` 改成取模掩码
`((x % 2n**64n) + 2n**64n) % 2n**64n`（五引擎等价），修后 quickjs-ng 腿转绿。

## 已踩坑（写入 spec 备查）

1. **Godot `String::sprintf` 不支持 `%lld`** —— 用了整行日志变空。64 位值走 `%d`+`(int)`。
2. `BigInt.asUintN` 在 quickjs-ng 上不可用（见上），测试辅助不得依赖。
3. 改 TS 后必须重跑 `tsc`，否则引擎加载旧产物、假失败。
4. 测试场景内不要调 `get_tree().quit()`（会抢在哨兵前退出）。
5. 不要并发跑两个 scons（争用 `.build/`，残留错配 obj）。

## 远端 CI 覆盖（本机编不了的引擎/平台）

本机只能编 v8 与 quickjs-ng（node 缺 `third/libnode/`，jsc/web 需 macOS/emscripten）。
已推送 `feature/int64` 并手动触发三模式 run（`gh workflow run ci.yml -f binding_mode=<mode>`）。

| run | 模式 | 结果 |
|---|---|---|
| `36086875962` | push（shared 默认） | **success**，30/30 job |
| `36087468554` | static | 22 个平台构建全 success；`Test (host-v8/host-qjs/host-node)` 全 success |
| `36092461841` | shared | 同上，三个 Test 腿 success |
| `36093625127` | dynamic | 同上，三个 Test 腿 success |

覆盖到的引擎/平台：v8、qjs-ng、**node**、**jsc**；windows / linux(x86_64+arm64) /
macos(v8+qjs-ng+jsc) / ios / Android / web(带线程与不带线程)。**node 与 jsc 腿首次跑通**
（本机不可编）。

`binding_mode` 输入确实生效且只作用于桌面腿（读 job 日志验证）：
`Build (windows, x86_64, editor, v8)` 的 scons 命令行分别含
`binding_mode=static` / `=shared` / `=dynamic`；web 腿无该参数（按设计保持默认）。

### 遗留：`benchmark` job 在 CI 上挂住（**既有问题，非本次改动**）

`benchmark-build` 两个模式（含修复后的 `benchmark-static`/`benchmark-dynamic` 命名）都
success，证明产物名修复正确；但 `benchmark` job 的 `Run benchmark (both legs)` 步骤在 CI
上不再推进（>1h），run 无法自然收官，为让下一个模式跑起来只能取消该 run。workflow 内
已有注释记载这是引擎 teardown 的既有 SEGV（"known engine-side issue"）。

**本地对照**：用当前 `template_release` static 构建跑
`godot --audio-driver Dummy --headless --path project -- --bench` →
`rc=0`、63s、`BENCH_JSON ... invalid:0 bindingMode:"static"`。
另 `python misc/bench_matrix.py --report` 对本地 24 runs 汇总 `BENCH_RC=0`、`invalid=0`、
三腿 md5 各异。两次本地证据都说明**逐例结果与转换层无回归**，CI 挂住是运行环境侧问题。

**未做**：本地无法复现 CI 的网络/环境依赖，也未定位挂住的最后一行（job 未完成时
`gh run view --log` 返回空）。归属并行任务 `09-06-ci-benchmark-both-legs`。

## 遗留

- `benchmark` job 的 CI 挂住（见上，既有问题）—— 需要时另开诊断。
- 父任务归档：**等用户明确同意**。
