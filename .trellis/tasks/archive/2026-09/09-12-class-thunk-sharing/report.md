# 09-12-class-thunk-sharing 交付报告

> 状态：**实现 + 验证完成，不 commit、不 push，留待人工审查**（用户要求）。
> 构建统一用 quickjs-ng（用户指示，本任务与 JS 运行时无关，无需 v8/jsc/web 多引擎）。
> 默认 binding_mode 已按用户改为 **shared**（PRD 原写 static，已由本任务裁决覆盖）。

## 一、目标回顾

在 `binding_mode=shared` 形态下，消除 class_method_thunk 按方法重复实例化：15,370 实例 → 唯一签名 1,519；每方法身份（class/method 名、min_argc）与 method_bind 经 v8 callback data 传入，thunk 只按签名特化。

## 二、2026-09-20 用户审查 + 裁决（已回写 PRD / 实现）

1. **`SharedClassMethodBinding::name` 冗余** → 删除。codegen 查找改用平行 `k_data_<Class>[]` 表内 `SharedClassMethodData{...}` 的 `method_name`/`hash` 同值判定，无独立 `name`。
2. **`SharedClassMethodBinding` 是 .cpp 内部细节，不该暴露 header** → 结构体整体移除。shared 走并行 const 数据表（`SharedClassMethodData` 行），查表由 `find_shared_class_method_binding(class,name,hash,const void **r_method_data)` 完成——命中写 `*r_method_data` 返回签名 thunk；无 `find_class_method_binding`。挂载端透传 opaque `void*` 不 deref，静/共享同路径。
3. **`method_bind` eager 解析在挂载点，且收敛为单源调用点** → `ensure_class_method_bind(md)` 只在顶层 `find_shared_class_method_binding` 二分命中后、返回前调用**唯一一处**（codegen 发射 `static_binding_codegen.py` 单点；`find_cls_*` 全部退化为纯查找——命中写 `*r_method_data` + 返回签名 thunk，不含解析）。解析失败返回 nullptr → 挂载点落 dynamic 回退。**共享 thunk 热路径只做一次 `relaxed load`**（与读裸指针同一条指令），不含 resolver/CAS 分支。**`std::atomic` 保留且仅在挂载期那一刻需要**：`k_data_` 表是跨 worker Environment 共享的同一份 DLL .data，多 worker 并发首次解析同一槽是真实场景——裸指针并发写是数据竞争，relaxed atomic（= 8 字节裸指针、单字对齐、零 lock 前缀零 fence）使"写同值"良定义。解析不发生在调用路径上。
   > 结构收敛记录：曾将 `ensure` 逐 case 发射进 `find_cls_*`（15,385 处源文本），经审查收敛到顶层单源调用点——源调用点唯一化后也无需 `_NO_INLINE_`，改回 `_FORCE_INLINE_`（与 `resolve_class_method` 一致）。
4. **`shared_class_methods.h` 并入 `class_methods.h`** → 采纳。共享段（`SharedClassMethodData`+`ensure`+两个共享 thunk）以 `#if JSB_WITH_SHARED_THUNKS` 门控并入 `class_methods.h`，删 `shared_class_methods.h`。

> 2026-09-20 裁决（vararg）：原把 **class vararg 划入子任务 3** 是 PRD 错误。子任务 3（utility-thunk-sweep）只处理 **utility 函数**（函数指针执行方式与 class 族不同）。**class 族的 vararg 方法也纳入本任务**：`class_vararg_method_thunk` 一并按签名共享（`shared_class_vararg_method_thunk`），共享签名数即减 class_vararg 族 15→10。已回写 PRD Out of Scope。

## 三、改动清单

### 构建接线（SConstruct + 迁移面）
- `SConstruct:37` `static_binding` BoolVariable → `binding_mode` EnumVariable（`static|shared|dynamic`，**默认 shared**）
- `:14` 顶部补 `from SCons.Variables import EnumVariable`
- `:828-841` codegen 传 `--binding-mode <mode>`；`:858-859` 宏注入：static→`JSB_WITH_STATIC_BINDINGS`，shared→`+JSB_WITH_SHARED_THUNKS`
- `.github/workflows/ci.yml` bench 矩阵 `static-binding`→`binding-mode`、flags 相应迁移
- `.github/actions/scons-build/action.yml` flags 注释例子迁移
- `misc/bench_matrix.py`：`--leg` 扩 `shared`、`build_and_deploy` flag→`binding_mode=<leg>`、`assert_leg` 改三态 `bindingMode`、legs 默认三态

### 核心（thunks，2026-09-20 审查后收敛）
- `class_methods.h` 内以 `#if JSB_WITH_SHARED_THUNKS` 门控并入共享段（删 `shared_class_methods.h`）：
  - `SharedClassMethodData{ mutable std::atomic<GDExtensionMethodBindPtr> method_bind; const char *class_name; const char *method_name; uint32_t hash; int32_t min_argc; }`（DLL .data 平行表行）
  - `ensure_class_method_bind(md)`：**挂载期 eager 解析**——`find_cls_<Class>` 命中后先 `classdb_get_method_bind` 再 `compare_exchange_strong` 幂等缓存；失败返回 nullptr 落 dynamic 回退。`std::atomic` 保跨 worker 并发首次写同一槽良定义（挂载期一次，不在热路径）
  - `shared_class_method_thunk` / `shared_class_vararg_method_thunk`：体迁移自对应 per-method thunk，身份改从 `info.Data()` 读 `SharedClassMethodData`，热路径**仅 `relaxed load` method_bind**（不含解析/CAS）
- `dispatch.h`：`find_class_method_thunk(class,name,hash)` 保持原签名（static 基线 AC 不动）；JSB_WITH_SHARED_THUNKS 下新增 `find_shared_class_method_binding(class,name,hash,const void **r_method_data)`（值语义 `#if` 门控）
- `src/jsb.config.h`：`JSB_WITH_SHARED_THUNKS` 值语义默认 0

### codegen（`misc/build/static_binding_codegen.py`）
- `--binding-mode` 参数（static|shared|dynamic）
- **shared 发射**：签名去重（`shared_entry_expr`/`shared_vararg_entry_expr`）→ `k_shared_thunks[]` 单点实例化 → **每类平行 `k_data_<Class>[]` 表**（`SharedClassMethodData` 行，非结构体）→ `find_cls_<Class>` hash switch（命中先 `ensure_class_method_bind(md)` 挂载期 eager 解析，成功写 `*r_method_data` 返回签名 thunk、失败返回 nullptr）→ 顶层 `find_shared_class_method_binding` 二分查找；索引属性段不变（两模式统一）
- `SharedClassMethodBinding` 结构体整体移除（审查点 2：codegen 内部实现细节不暴露 header）；无 `find_class_method_binding`
- 解析失败（引擎版本漂移等）→ `find_cls_*` 返回 nullptr → 挂载点落既有 "falling back to dynamic binding" 回退
- static 模式路径零改动（`--binding-mode static` 输出与改造前基线**逐字节一致**，闸门锤实）
- `main()` 传 `ns.binding_mode`；shared 分支 include 指向 `class_methods.h`

### 挂载 / 运行时暴露
- `src/runtime/bridge/jsb_object_bindings.cpp`：JSB_WITH_SHARED_THUNKS 分支统一 `Method(name, thunk, r_method_data)` 透传（不 deref，静/共享同路径）；解析在 `find_shared_class_method_binding`（挂载路径，eager，name+hash 已在手）内通过 codegen 发射的 `find_cls_*` 命中后 `ensure_class_method_bind` 完成；include 复用 dispatch.h，删 thunk 头直接 include
- `src/runtime/bridge/jsb_bridge_module_loader.cpp`：`STATIC_BINDING_ENABLED`→`BINDING_MODE` 三态字符串

### TS / spec
- `project/tests/benchmark/benchmark.ts`：`BINDING_MODE` 导入 + `bindingMode` 字段
- `scripts/typings/godot.minimal.d.ts`：`BINDING_MODE: "static"|"shared"|"dynamic"`
- `.trellis/spec/godotjs-ext/cpp/generated-files.md`、`build/scons-build.md`：`static_binding=yes`→`binding_mode` 迁移

## 四、验证（全部实测，quickjs-ng）

### Codegen 闸门
| 判据 | 结果 |
|---|---|
| `--binding-mode static` vs 改造前基线 diff | **空（逐字节一致）** |
| 共享签名固定元数 `shared_class_method_thunk<` | **1,519** |
| 共享签名 vararg `shared_class_vararg_method_thunk<` | **10** |
| 合计（≤1,530） | **1,529** ✓ |
| static 模式 Form A `class_method_thunk<` | 15,370（不变） |

### 构建 / 测试
| 项 | shared | static |
|---|---|---|
| C++ 双套件（quickjs-ng tests=yes） | runtime **51 passed / 586 asserts** + editor **3 passed / 12 asserts**, `result 0`, exit 0 | 同左 |
| TS 集成 `GODOTJS_TEST_PROJECT_COMPLETED` | ✓ + DefaultArgs `fail=false` | — |

### bench shared 腿（relase 构建）
- `cases: **234**, invalid: **0**, bindingMode: **shared**, exit **0**`（PRD 预写 231，实际 234——含 vararg case 面，invalid=0 达成）

## 五、三态产物体积对比（release，quickjs-ng，main dll）

> shared 列 = 解析收敛最终版（顶层单源调用 `ensure_class_method_bind`，`find_cls_*` 纯查找）后实测。static/dynamic 重构不触及共享发射与 thunk（static gen 逐字节一致），体积不变。

| binding_mode | main dll 大小 | md5 | vs static |
|---|---|---|---|
| **static**（形态 A，基线） | **54,653.0 KB** | `972e7c79…` | — |
| **shared**（本任务，最终） | **15,913.5 KB** | `6a464bed…` | **−70.9%** |
| **dynamic** | 4,877.5 KB | `ccb6bfba…` | −91.1% |

editor dll 三态均 1,278.5 KB（静态绑定表在 main dll，editor 不含）。shared 相对 static 削减 **70.9%**。

**体积决策记录（2026-09-20 实测四版）**——膨胀源是内联，不是 atomic：

| version | ensure 调用点 | volume |
|---|---|---|
| 懒解析（ensure 进 1,529 thunk，每调用执行） | 每 thunk 一份 | 16,958.0 KB |
| eager 进 `find_cls_*`，`_FORCE_INLINE_` | 每 case 一份（15,385） | 17,768.0 KB（回退）|
| eager 顶层，`_NO_INLINE_` | 单源调用点 | 16,055.5 KB |
| **eager 顶层，`_FORCE_INLINE_`（最终）** | **单源调用点，唯一处内联** | **15,913.5 KB** |

收敛要点：`ensure_class_method_bind` 只在顶层 `find_shared_class_method_binding` 二分命中后、返回前调用**一处**；`find_cls_*` 全部退化为纯查找（写 `*r_method_data` + 返回签名 thunk）。源调用点唯一化后改回 `_FORCE_INLINE_`，既避免逐 case 复制（17,768 回退）又免 `_NO_INLINE_` 调用开销（16,055.5 → 15,913.5）。atomic 与体积无关（`GDExtensionMethodBindPtr = void*`，等宽、relaxed 零指令）。

> 语境注：dynamic 因完全不发射静态绑定表而最小；shared 保留全部静态绑定（argc 预检 + 直接 marshal），在保留静态路径的前提下把 15,370 份 per-method 模板实例压缩到 1,529 份签名，是体积与静态性能的平衡点。

## 六、遗留

- **未 commit / 未 push**（用户要求人工审查）。改动在工作树，如需回退各文件独立。
- `GENERATED_NOTE`（codegen 头部注释）仍含 "static_binding=yes" 字样——属生成文件元数据注释，改它会破坏 static 基线 diff 判据，故保持不动（纯展示面，无功能语义）。
- bench PRD 写 231 case，实测 234：case 面来自 hand-maintained `cases.builtin.ts`（212）`cases.object.ts`（25）实装，非 PRD 数字倒退；关键 invalid=0 达成。

## 七、规格沉淀

- `generated-files.md`：codegen 条目补 `--binding-mode`、默认 shared、dynamic 不跑
- `scons-build.md`：`binding_mode` 参数表