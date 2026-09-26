# 执行计划

## 批次划分（按"是否需要全量重编 + 全腿验证"分）

### 批 1：机械清理（低风险，行为不变）

| 步 | 文件 | 动作 |
|---|---|---|
| 1.1 | `.github/workflows/ci.yml` | `binding_mode` 去 `default`；input 默认 `shared`；守卫改 `inputs.binding_mode != ''` |
| 1.2 | `src/runtime/impl/jsb_primitive_conv.h` | 去 `ValueT` 模板，四个读取器改定值 `const v8::Local<v8::Value>` |
| 1.3 | `src/runtime/bridge/jsb_type_convert_direct.h` | `(void)x` → `jsb_unused(x)` |
| 1.4 | 两个新文件 | 去掉 `Contributors of GodotJS` 版权节 |
| 1.5 | `.trellis/spec/godotjs-ext/cpp/index.md`（或相邻） | 记录"新增文件不声明 GodotJS Contributors"规则 |
| 1.6 | `src/runtime/bridge/jsb_static_binding_util.h` | `get/set` 加 `_FORCE_INLINE_`（含 6 个窄整型宏生成的特化） |

**验证**：编译一次（v8 static）+ C++ 套件 + TS 套件。批 1 不动 include 顺序，不触发全量重编。

### 批 2：`jsb_primitive_conv.h` 重排（触发四引擎全量重编）

| 步 | 文件 | 动作 |
|---|---|---|
| 2.1 | 四个 shim | 该头的 `#include` 移到各自 pch 之后 |
| 2.2 | `jsb_primitive_conv.h` | `inline` → `_FORCE_INLINE_`；移入 `jsb::impl::internal` |
| 2.3 | 四个 shim | `Helper::*` 转发改指向新命名空间（与 `to_string`/`new_string` 并列） |
| 2.4 | `jsb_primitive_conv.h` | 补回 `JSB_LOG(VeryVerbose, "represented as bigint %d", (int)p_val)` |

**验证**：v8 + quickjs-ng 各编一次；C++ 套件；**实测** debug 下该日志行非空（触发一次越阈写入）。

### 批 3：出口契约（结构改造）

| 步 | 文件 | 动作 |
|---|---|---|
| 3.1 | `src/jsb.config.h` | 新增 `JSB_64BIT_RETURN_FIXED_BIGINT`（默认 0）+ `#error` |
| 3.2 | `jsb_primitive_conv.h` | `new_integer`/`new_unsigned_integer` 加固定 bigint 分支（去掉 Int32 快路径） |
| 3.3 | 新增 `src/runtime/bridge/jsb_gd_to_js.h` | `GDToJS<T>`（主模板回退 + 标量特化） |
| 3.4 | `src/static_binding/thunks/thunks_common.h` | `Ret<T>::translate_return` 收敛，删 `is_same_v` 与 `translate_uint64_return` |
| 3.5 | `src/editor/codegen/jsb_codegen_defs.cpp` | `kPredefinedLines` 按宏生成 `int64`/`uint64`/`int64_ret`/`uint64_ret` |
| 3.6 | `jsb_primitive_conv.h` | 负 Number → uint64 加 `#if JSB_DEBUG` 告警 |

**验证（最重）**：
- 三态中**每种合法组合**至少在一腿上实测（`FOR_64BIT=0` / `1+FIXED=0` / `1+FIXED=1`）。
- 本地 5 腿全绿（AC13）。
- typings grep 验证（AC10）。
- 负 Number 告警：release dll 字符串计数 0、editor > 0（AC11）。

### 批 4：文档与收尾

| 步 | 文件 | 动作 |
|---|---|---|
| 4.1 | 父任务 `design.md` | 更正 §1.4 措辞为「位模式回传 / 引擎对齐」 |
| 4.2 | `research/64bit-api-surface.md` | 已生成 ✅ |
| 4.3 | 本任务 `report.md` | 逐批证据 |

## 验证命令

```bash
# 三腿 v8
scons platform=windows target=editor binding_mode=<static|shared|dynamic>       tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
# quickjs-ng
scons platform=windows target=editor binding_mode=<static|dynamic> use_quickjs_ng=yes       tests=yes compiledb=no debug_symbols=no dev_build=no verbose=no
# C++ 套件
godot --headless --path ./project --jsb-run-tests
# TS（必须先 tsc）
cd project && node node_modules/typescript/bin/tsc
godot --audio-driver Dummy --headless --path project
# release 零开销对照
scons platform=windows target=template_release binding_mode=static compiledb=no debug_symbols=no dev_build=no verbose=no
```

## 风险

| 风险 | 缓解 |
|---|---|
| 移动 include 顺序暴露隐藏依赖 | 四个 shim 一次改完、全量重编一次；若报未定义符号则往 pch 补 include |
| `GDToJS` 主模板回退路径行为漂移 | 主模板等价于原 `translate_return` 逻辑；全腿测试即回归网 |
| 固定 bigint 影响既有 `project/tests/**`（id 作 number 用） | 固定模式默认关闭；测试断言需模式感知（沿用 `BIGINT_FOR_64BIT` 导出／`BIGINT_MODE` 先例） |
| 编译成本：批 2/3 各触发全量重编 | 每批一次编完；变体一次列全 |
