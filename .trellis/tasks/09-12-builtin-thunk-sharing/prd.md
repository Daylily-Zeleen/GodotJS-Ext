# 静态绑定 builtin 族 thunk 共用化（形态 shared：签名共享）

> 父任务：09-11-static-binding-size-reduction（子任务 2，P2）。依赖关系：在子任务 1（09-12-class-thunk-sharing）定型 `binding_mode` 接线与 callback-data 管道后开工。

## Goal

在 `binding_mode=shared` 形态下消除 builtin_method_thunk 重复实例化：767 个实例化 → **434** 个含 VTC 唯一签名（字符串感知解析器复核计数 `src/static_binding/gen/dispatch_builtin.gen.cpp`）。核心：**默认参数迁"静态 EncodeT 槽 + 类型分类发射"（引用类 per-occurrence）**。**形态 A 零改动**。

## Background（已核实事实）

- `builtin_method_thunk<VTC, HashC, NameLit, IsStaticC, RetT, ArgsT...>`（`src/static_binding/thunks/builtin_methods.h:126`）：VTC 参与 self 的 opaque 取指针（`get_opaque_typed<VTC>`）与 fn 类型——**属签名一部分，形态 shared 保留在模板**；Hash/Name 与 Arg 的 Def 移出
- Def 剥离依据（父任务裁决）：builtin gen 带默认值 Arg 95 处（Def 留模板则默认值差异阻断共享）。去重：455 → 434
- **默认值数据面（完整清单）**：唯一 (T, Lit) 对 16 个；**Array/Dictionary 默认 0 个**——唯二非标量默认：`String ""` ×1（`PackedByteArray.get_string_from_multibyte_char`）、`Variant "null"` ×3（`Array.reduce`/`Dictionary.get`/`Dictionary.get_or_add`）；其余 14 对全为标量/POD：`int64_t "0"` ×19（find 族）、`bool "false"` ×15、`int64_t "-1"` ×15（rfind 族）、`bool "true"` ×13（bsearch 族）、`int64_t "2147483647"` ×12（slice 族）、`real_t "1.0"` ×4、`int64_t "2"`/"1"` ×3+3、`Vector3 "Vector3(0, 1, 0)"` ×2、`Vector2 "Vector2(0, 0)"`、`real_t "1e-05"`、`Color` ×2、`int64_t "255"` ×1
- **勘误（用户质询核实）**：bool 两对（28 处）**今天就已 T≠EncodeT**——`PtrToArg<bool>::EncodeT = uint8_t`（`third/godot-cpp/include/godot_cpp/core/method_ptrcall.hpp:116`）。T≠EncodeT 是现存需求而非仅未来兼容
- **静态 EncodeT 槽机制（父任务裁决，通用不按对特判）**：
  - 每默认值位一个 `PtrToArg<T>::EncodeT` 槽，初始化 `PtrToArg<T>::encode(default_as<T, Lit>(), &slot)`——encode 契约即"把 T 值写成 EncodeT 布局"（int32→int64、float→double、枚举→int64 自动正确），单一转换源；未来 API 新增对只需 codegen 多发槽，模板零改动
  - **槽初始化时机约束**：禁止 DLL 静态初始化期初始化——`str_to_var` 走引擎 UtilityFunction hook，静态初始化期（entry 之前）未就绪；槽经 accessor 函数局部 magic static **懒初始化**，首次使用在挂载期（引擎已就绪，与形态 A `default_as` 时机一致）
  - **类型分类发射（2026-09-12 引擎源码定论，用户裁决）**——ptrcall 边界按值物化每参（引擎 `core/variant/method_ptrcall.h:42-49` `PtrToArgDirect::convert` 返回 `T` 拷贝；`core/variant/binder_common.h:78-84` 逐参 convert），各类型拷贝链分三类：
    - **平凡可拷贝**（现存 14 对：int64/bool/real_t/Vector2/Vector3/Color）→ (T, Lit) 去重共享槽，`arg_ptrs[i] = defaults[i]` 零拷贝
    - **CowData 写时 fork**：String（`ustring.h:281` `_cowdata` 成员、`:677` 默认拷贝构造）与全部 Packed*Array（`variant.h:82-92` = `Vector<T>`、`vector.h:321` 默认拷贝）——拷贝 = refcount++ 指针共享，但一切写路径强制 `_copy_on_write`（`cowdata.h:504-512`：refcount>1 分配新缓冲；`ptrw()` `:168`、`set()` `:186` 触发）→ 静态槽永不被写 → 同样 (T, Lit) 去重共享
    - **引用共享无 fork**：Array（`array.cpp:950-953` 拷贝构造 → `_ref` `:56-72`：`refcount.ref()` + `_p` 后端指针共享、无数据 fork；变更仅守 `_p->read_only` 标志——`:280-281` push_back、`:122-123` clear、`:304-305` resize——refcount 不参与写隔离）与 Dictionary（同构：`dictionary.cpp:756-759` → `:287-302`、守卫 `:202-204`）；引擎从不给默认值标 read_only（`make_read_only` 调用点仅 `array.cpp:931-933` create_read_only 与 `variant_call.cpp:2628/2688` 显式绑定）→ **per-occurrence 独立槽**（方法 × 参数位；用户裁决：同函数两个空 Array 默认 = 两个独立对象）；Variant 保守同类（`method_ptrcall.h:241` ByReference 边界零拷贝，内含 Array/Dict 共享后端）
  - **与形态 A 的语义对比（精确）**：同函数跨调用共享后端**两形态均存在**（A：magic static 跨调用共享；B：槽后端跨调用共享）；差异在形态 A 的 `default_as` 函数模板 static 按 (T, Lit) **全二进制唯一**（inline 模板弱符号合并，`thunks_common.h:66-77`）——同对默认值**跨函数共享同一后端**，被调方原地改写会跨函数泄漏；per-occurrence 消除之（更细隔离，用户裁决的有意改进）。现状 Array/Dictionary 默认 = 0 → **今天零可观察差异**
  - **统一填充，无逐调用 copy 分支**（原"值语义二分"取消）：三类均 `arg_ptrs[i] = defaults[i]`
- **默认参数消费点（改写锚点）**：`produce_value` 缺参分支 `if constexpr (ArgT::has_default) { out = default_as<...>(); }`（`thunks_common.h:449-463`）——形态 shared 下 builtin 路径改为 `arg_ptrs[i] = defaults[i]` 直填（跳过 produce_value）；必填位 defaults[i]==nullptr → "missing argument" 异常语义不变
- builtin ptrcall **引擎不补默认值**（不同于 class 的 MethodBind）：thunk 必须在 `provided < N` 时自己填（`builtin_methods.h:129-130` D/M/N 机制）——形态 shared 下 **M 不设任何存储字段**，元数由 defaults 列表自带（见下条）
- **元数检查（用户指正，2026-09-12）**：`provided > N` → too-many；`provided < N && defaults[provided] == nullptr` → missing——与 `provided < M || provided > N` 严格等价（默认值尾部连续前提，1,127 带默认值方法实测非尾部连续 = 0）；同一探测两用（元数判定 + 缺参填充）；报错文案的 M 冷路径现扫（首个非空位）；**codegen 生成期断言默认值尾部连续**（未来 API 违反前提 → 构建期报错拒绝发射）；无默认值方法（680/767）的 defaults 布局（全 nullptr 共享表 vs nullptr 描述符捷径）留 design.md 定
- **eager 解析（父任务裁决）**：绑定时（挂载点 `jsb_primitive_bindings.cpp:808`，TYPE/name/hash 在手）解析 `GDExtensionPtrBuiltInMethod` 存 callback data；失败走 "falling back to dynamic binding"（`:816` 既有模式）
- `default_as<T, FixedString Lit>`（`thunks_common.h:66-77`）形态 shared 退役（形态 A 保留；其 (T, Lit) 跨函数共享行为即上面对比中形态 A 的行为）

## Requirements

- 共享 thunk 仅按 `(VTC, IsStaticC, RetT, ArgsT...纯类型)` 特化
- codegen 在 shared 形态发射：默认值槽按类型分类（平凡/CowData 按 (T, Lit) 去重命名共享；Array/Dictionary/Variant per-occurrence）+ 每方法 `const void *defaults[N]` 指针列表数据（必填位 nullptr）；槽经 accessor magic static 懒初始化（**禁止静态初始化期调 `str_to_var`**）；**生成期断言默认值尾部连续**（违反即构建期报错）；字面量解析保持 `str_to_var` 单一事实源，String 原样特化沿用，`""` 双引号修复逻辑留在 codegen 侧
- 每方法 callback data：{eager 解析的 `GDExtensionPtrBuiltInMethod`, 类型/方法名（报错）, `const void *defaults[N]` 指针列表}——**无 min_argc 字段**，元数由列表自带
- 缺参路径改写：统一 `arg_ptrs[i] = defaults[i]`（无 copy 分支）；`default_as` 形态 shared 退役
- 行为保持：`provided < M || provided > N` 报错语义（经 defaults 探测实现，判定不变）、`get_opaque_typed` self 处理、`translate_return`

## Acceptance Criteria

- [ ] shared 形态 builtin 实例化声明数 ≤ 440（434 + 容差）
- [ ] **默认参数行为回归**：带默认值的 builtin 方法缺参调用结果与形态 A 一致（TS 集成测试覆盖 + C++ 断言；覆盖标量 int64/bool、CowData String、Variant 三类槽）
- [ ] 元数检查语义回归：抽样带默认值方法，`provided` ∈ {0..N+1} 逐档断言与形态 A 判定一致（边界 M-1、M、N、N+1 必测）
- [ ] 并发缺参冒烟验证：多线程并发缺参调用共享同一静态默认槽，结果与串行一致、无写竞争（统一纯指针填充 + 引用类槽跨函数隔离）
- [ ] C++ 双套件全绿、TS 集成 `GODOTJS_TEST_PROJECT_COMPLETED`、bench 231 case invalid=0
- [ ] dll 体积进一步下降可测量

## Out of Scope

- class / utility / vararg 族；ctor（133 无冗余）
- operator / member getter-setter 族；形态 A 改动

## Notes

- `design.md` 聚焦：槽的 codegen 命名（(T, Lit) 去重键 + per-occurrence 序号）、accessor 懒初始化形态（函数局部 magic static vs 挂载期一次性 init 函数）、defaults 列表布局（全 nullptr 共享表 vs nullptr 描述符捷径）、类型分类清单的实现位置（thunks_common.h）
