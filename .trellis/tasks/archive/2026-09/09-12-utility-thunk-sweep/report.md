# 09-12-utility-thunk-sweep 交付报告

> 状态：**实现 + 验证完成，未 commit**。
> 构建统一 quickjs-ng + `binding_mode=shared`。父任务：09-11-static-binding-size-reduction 子任务 3（P3，收口）。
> 依赖：子任务 1（class）/ 2（builtin）共享机制定型后推广。基线 = 09-12-builtin-thunk-sharing 落地后（15,034.0 KB）。

## 一、目标回顾

`binding_mode=shared` 下对剩余小头族套用同一签名共享模式，并**补做子任务 2 遗漏的 builtin vararg**（用户裁决：子任务 2 未覆盖即未完成）。

| 族 | 改造前 | 改造后 |
|---|---:|---:|
| utility_function_thunk | 102 实例 | **32** 唯一签名 |
| utility_vararg_function_thunk | 12 实例 | **3** 唯一签名 |
| class_vararg_method_thunk | （子任务1 已共享）| **10** |
| **builtin_vararg_method_thunk** | **6 实例（此前 OS）** | **5** 唯一签名 |

所有 AC 实例化数达标（utility≤35、class_vararg≤10、builtin_vararg≤5、utility_vararg≤3）。

## 二、实现（改动清单）

### thunks（`src/static_binding/thunks/utility_functions.h`）
- 补 `#include <atomic>`
- `#if JSB_WITH_SHARED_THUNKS` 门控共享段：
  - `SharedUtilityFunctionData { std::atomic<GDExtensionPtrUtilityFunction> fn; const char *name; }`——utility 无自引用、无默认值（API 实测 0 非空 DefVs），故 data 仅 fn+name
  - `ensure_utility_function(name, hash, data)`：挂载期 eager 解析 `variant_get_ptr_utility_function` + `compare_exchange_strong` 幂等缓存（同 class/builtin 共享 worker 语义，relaxed）
  - `shared_utility_function_thunk<RetT, ArgsT>`：固定参，**严格 N 元数**（无默认值，M==N）
  - `shared_utility_vararg_function_thunk<RetT, ArgsT>`：前缀展开 + 尾部循环，编译期最小元数 F

### thunks（`src/static_binding/thunks/builtin_methods.h`）
- 新增 `shared_builtin_vararg_method_thunk<VTC, IsStaticC, RetT, ArgsT>`：前缀（Variant 槽）+ 尾部循环，身份经 `SharedBuiltinMethodData`（fn/vt/method_name），热路径单次 relaxed load

### codegen（`misc/build/static_binding_codegen.py`）
- `shared_builtin_vararg_entry_expr`：vararg 用同签名维度（VTC, IsStaticC, RetT, ArgsT）+ vararg 模板名
- `_emit_shared_builtin_dispatch`：放开 `is_vararg` 过滤（原 `not e.get("is_vararg")`）→ fixed+vararg 均入签名去重，data 表同构（vararg 无默认值、defaults 指向 k_shared_no_defaults）
- `shared_utility_entry_expr` / `shared_utility_vararg_entry_expr`：utility 签名（RetT, ArgsT）去重
- `emit_utility_dispatch_cpp(m, binding_mode)`：加 shared 分支 → `_emit_shared_utility_dispatch`（`k_shared_utility_thunks[]` + `k_utility_data[]` + `resolve_shared_utility` 纯查找 + `find_shared_utility_binding` 顶层 eager resolve）；`<cstring>` 仅 shared 分支 include（保 static 逐字节一致）
- main 调用点传 `ns.binding_mode`

### dispatch.h
- `find_shared_utility_binding(name, hash, const void **r_method_data)`（JSB_WITH_SHARED_THUNKS 门控）

### 挂载端（`src/runtime/bridge/jsb_godot_module_loader.cpp`）
- shared 走 `find_shared_utility_binding` + `JSB_NEW_FUNCTION(..., v8::External::New(isolate, sb_data))` 透传 data；static 走原 `find_utility_thunk`

## 三、验证（全部实测，quickjs-ng）

| 判据 | 结果 |
|---|---|
| `--binding-mode static` vs HEAD | **逐字节 IDENTICAL** |
| utility 共享实例化 `shared_utility_function_thunk<` | **32**（≤35 AC）✓ |
| utility vararg 共享实例化 `shared_utility_vararg_function_thunk<` | **3**（≤3 AC）✓ |
| class vararg 共享签名 | 10（≤10 AC，子任务1 承接）✓ |
| builtin vararg 共享实例化 `shared_builtin_vararg_method_thunk<` | **5**（≤5 AC）✓（Callable call/deferred/rpc/rpc_id/bind、Signal emit）|
| `find_shared_utility_binding` / `resolve_shared_utility` 源调用点 | 顶层 finder 1 处 |
| C++ 双套件（shared）| runtime 51/51 + editor 3/3, result 0, exit 0 |
| TS 编译 | `tsc` 退出 0（gen:types + tsc）|
| TS 集成 | `GODOTJS_TEST_PROJECT_COMPLETED` |
| bench | `bindingMode: shared, invalid: 0, exit 0` |
| static-binding 回退告警 | **0**（全部 utility 静态命中，无 dynamic 回退）|

**体积（main dll，release，quickjs-ng，editor）**：
- static（形态 A）：**54,653.0 KB**（`75f250c8`）
- shared（class + builtin + **utility + 全 vararg**）：**14,862.0 KB**（`0a38b611`）
- dynamic：**4,877.5 KB**（`d9d14128`）

**逐步降幅（shared 腿，相对 static 54,653 KB）**：
- class 共享（子任务1）：15,913.5 KB（−70.9%）
- + builtin 共享（子任务2）：15,034.0 KB（−72.5%）
- **+ utility + vararg（本任务）：14,862.0 KB（−72.8%）**——本次再降 **−172 KB**

本任务相对子任务2 基线 15,034 → 14,862 KB（−172 KB，utility/vararg 共享为消除每方法实例的小头收益）。

## 四、设计要点

- **utility data 最小化**：无自引用 → 无 base_ptr；无默认值 → 固定参严格 `provided != N` 报错（M==N 编译期），仅 fn+name 两字段
- **builtin vararg 归位**：此前被错误排为 OS（子任务2 report line 83），实属未完成；本任务补 5 签名共享
- **eager 解析一致**：utility/vararg 与 class/builtin 同用顶层 finder 单点 `ensure_*` 挂载期解析，失败 → dynamic 回退

## 五、遗留

- 未 commit（待用户授权提交，同前两子任务先例）
- `resolve_shared_utility` / `k_utility_data` / `k_shared_utility_thunks` 为 `static`（internal linkage，同位 external `find_shared_utility_binding` 分开），避免命名空间符号外泄

## 六、规格沉淀

- 无新 spec 改动（复用 generated-files.md 的 `--binding-mode` 条目；scons-build.md 已含 `binding_mode` 参数表）