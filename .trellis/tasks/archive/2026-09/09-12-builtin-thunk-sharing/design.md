# 静态绑定 builtin 族 thunk 共用化（形态 shared）— design.md

> 依据：本任务 PRD + 父 09-11 裁决 + 09-12-form-a-default-handling（形态 A 槽化锚点）。
> 形态 shared 仅在 `JSB_WITH_STATIC_BINDINGS + JSB_WITH_SHARED_THUNKS` 下编译；形态 A 路径零改动（codegen static 基线逐字节 IDENTICAL 闸门）。

## 1. 架构总览

### 1.1 调用数据流（shared 形态）

```
JS obj.method(a)
  → v8 shared_builtin_method_thunk<VTC, IsStaticC, RetT, ArgsT纯类型>（按签名去重的共享实例）
  → info.Data().As<External>()->Value() → const SharedBuiltinMethodData &md
  → fn = md.fn.load(relaxed)                  // 挂载期 eager 解析的 ptrbuiltin
  → provided=M..N：元数判定经 md.defaults[provided]==nullptr（见 §2）
  → 缺参位 i∈[provided,N)：arg_ptrs[i] = md.defaults[i]()   // 调用槽 accessor，懒初始化
  → fn(base_ptr, arg_ptrs, &ret_val, N)
  → translate_return<RetT>
```

### 1.2 唯一签名（实测复核 `dispatch_builtin.gen.cpp`）

- 767 实例化 → **434** 唯一 `(VTC, IsStaticC, RetT, Args纯类型)`（字符串感知解析器计数）→ AC ≤440 ✓
- vararg 6 个不动（PRD Out of Scope，仍走 `builtin_vararg_method_thunk`）

## 2. 数据契约

### 2.1 每方法描述符（DLL .data，codegen 发射）

```cpp
struct SharedBuiltinMethodData {
    mutable std::atomic<GDExtensionPtrBuiltInMethod> fn; // 挂载期 eager 解析（CAS 幂等）
    const char *type_name;   // 报错文案
    const char *method_name; // 报错文案
    void *(*const *defaults)(void); // 每参数位一个槽 accessor 指针；必填位 = nullptr
};
```

- `fn`：挂载点 `jsb_primitive_bindings.cpp:808` 以 TYPE/name/hash eager 解析，`compare_exchange_strong` 幂等缓存（跨 worker Environment 共享同一份 .data，并发首次写同槽需良定义，relaxed atomic = 裸指针等宽零开销，见 class 任务审查点 3）
- **无 min_argc 字段**：元数由 defaults 列表自带（尾部连续前提，见 §2.4）

### 2.2 槽 accessor（懒初始化，复用形态 A 机制）

**槽定义为函数局部 magic static**，复用 `thunks_common.h` 既有的 `DefV` + `default_arg_slot<DefT, IdT>()`：

```cpp
_FORCE_INLINE_ void *default_arg_slot();  // 已存在：返回 ReboundDefT::get_encoded_ptr()
```

- **去重槽**（平凡可拷贝 14 对 + CowData String）：`(T, Lit)` 相同 → 同一 `DefT`（同缺陷 ctor）+ `IdT = void` → 所有共享此槽的方法共用同一 static EncodeT 槽
- **per-occurrence 槽**（Variant 3 处；Array/Dict 今天 0 处但机制预留）：`IdT` 用唯一递增整数（`std::integral_constant<int, k>`）→ 每 (方法 × 参数位) 独立 static 槽，消除跨函数泄漏

**为什么 accessor 而非数组元素直接存槽地址**：懒初始化约束禁止 DLL 静态初始化期物化槽（PW 若未来默认值走 str_to_var 需引擎 hook）。defaults 数组若存"已物化的槽指针"必须在静态初始化期求值 accessor → 违反。故 **defaults 数组存 accessor 函数指针**（编译期常量，无静态副作用），thunk 缺参时调用之——与形态 A `default_arg_slot()` 逐调用调用完全同构，首次触发 magic static，后续单次内联 load。

### 2.3 defaults 列表布局

```cpp
// codegen 每方法发射一个静态 accessor 指针数组（编译期常量，无静态初始化副作用）
static void *(*const k_defs_METHOD[])(void) = {
    nullptr,                          // 必填位（provided >= i 不受影响）
    &default_arg_slot<DefVs<int64_t, make<int64_t,0>>>,   // 可选位 → accessor
    ...
};
// data 行的 defaults 字段指向它
```

选 **accessor 函数指针数组**（非"nullptr 描述符捷径"）：与懒约束一致、类型安全、单一事实源（同 def_ctor_expr 生成 DefT）。

**无默认值方法（680/767）布局 —— 长空表定论（2026-09-20 用户裁决）**：
- 一张共享 `k_shared_no_defaults[]` 全 nullptr 长表（长度 = 全局最大参数个数 max_n），所有无默认值方法的 `defaults` 字段指向同一张表
- 只有真实携带 defaults 的方法（87/767）发独立 `k_defs_*` 表（codegen `has_default` 判定，杜绝 `if acc` 对任何有参方法误发独立表）
- 元数探测 `defaults[provided]`：无默认值方法因每位置均 nullptr，`provided<N` 即 missing——语义与形态 A `M..N` 严格等价
- 目的**非体积**（实测长空表省 ~6.4KB，占 dll <0.05%），而是**结构正确性**：把"全必填"从 680 份重复描述收敛为单一事实

**type_name 字段 —— Variant::Type（2026-09-20 用户裁决 + 实测）**：
- `SharedBuiltinMethodData` 用 `godot::Variant::Type vt`（弃 `const char* type_name`），报错经 `godot::Variant::get_type_name(vt)`
- 实测三态 `sizeof` 均 = 32（const char* / Variant::Type / uint8_t）——type_name 后紧跟指针，均被填充到 8 字节指针对齐边界；无 size 差异故取语义清晰的 `Variant::Type`
- 值域安全：VARIANT_MAX=39，远小于后续指针偏移

**k_defs_ 格式化定论（2026-09-20 用户指正）**：每元素单独成行 + 统一 `\t` 前缀，与 `k_data_`/`k_shared_thunks` 生成风格一致；禁用 `'\n\t\t\t\t'.join(acc)` 式硬编码 tab 拼接（曾致多元素表缩进错乱）。

### 2.4 元数检查与缺参填充（用户指正）

`provided` 判定：
- `provided > N` → too-many 异常
- `provided < N && md.defaults[provided] == nullptr` → missing 异常
- 与形态 A `provided < M || provided > N` 严格等价（默认值尾部连续前提，1,127 方法实测非尾部连续 = 0）

报错文案 M：冷路径扫描首个非空 defaults 项（仅缺参时执行）。codegen 生成期 `_assert_default_layout` 已断言尾部连续（违反即构建期拒绝）。

无默认值方法（680/767）：defaults 数组全 nullptr，data 行 defaults 指向一个共享全空表或 nullptr 描述符捷径——design 抉择取 **共享全空表** 简化（同 thunk 内 `md.defaults[provided]` 恒 nullptr 即 missing，语义正确）。

## 3. 模板与发射

### 3.1 shared_builtin_method_thunk

```cpp
template <godot::Variant::Type VTC, bool IsStaticC, class RetT, class AllArgsT>
void shared_builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info);
```

- 只保留 `(VTC, IsStaticC, RetT, ArgsT纯类型)`；Hash/Name/Def 移出（同 class 手法）
- 体迁移自 `builtin_methods.h:126` 形态 A thunk，仅改：
  1. `static const fn = resolve_builtin_method(...)` → `md.fn.load(relaxed)`（eager 挂载期已解析）
  2. 缺参路径 `default_arg_slot<DefT, decltype(thunk)>()` → `md.defaults[i]()`（accessor 地址来自 data）
  3. 报错文案 `get_type_name(VTC)`/NameLit → `md.type_name`/`md.method_name`
- `<atomic>` include（class_methods.h 已加，builtin_methods.h 需补）

### 3.2 codegen（`--binding-mode shared`）

`emit_builtin_dispatch_cpp` 加 shared 分支（镜像 class 的 `_emit_shared_class_dispatch`）：

- 签名去重：`shared_builtin_entry_expr` 只保留 (VTC, IsStaticC, RetT, Args纯类型) → `k_shared_thunks[]` 单点实例化
- 槽分配：扫全部 builtin 方法的 defaults → 按类型分类建槽表（(T,Lit)→去重 IdT；Array/Dict/Variant→per-occurrence IdT）→ 每方法发 `k_defaults_<M>[]` accessor 指针数组 + `SharedBuiltinMethodData k_data_<VT>[]` 行
- finder：每 VT 一个 `find_builtin_<VT>` hash switch（命中写 `*r_method_data` + 返回签名 thunk）→ 顶层 `find_shared_builtin_binding(TYPE, name, hash, r_data)` 经 `find_builtin_thunk` 同型查找，命中后 eager resolve fn（一次）
- vararg 保持形态 A `builtin_vararg_method_thunk`
- static 模式路径逐字节不动（闸门）

### 3.3 dispatch.h / 挂载

- `dispatch.h`：`find_shared_builtin_binding(Variant::Type, name, hash, const void **r_data)`（JSB_WITH_SHARED_THUNKS 门控）
- `jsb_primitive_bindings.cpp:808`：shared 分支 match class 手法——resolve fn + `Method(name, thunk, (void*)r_data)` 透传（不 deref）；失败落 dynamic 回退

## 4. 权衡记录

- **槽 accessor 函数指针数组 vs 运行时先前物化**：选前者。懒约束 + 类型安全 + 与形态 A 同构；挂载期只需 eager resolve fn（PRD），slots 首次缺参调用懒。
- **共享空表 vs nullptr 捷径**：选共享空表（语义统一，680 无默认方法共用一张）。
- **atomic fn 保留**：跨 worker 共享 .data 并发首次写需良定义（同 class 审查点 3）；非热路径（挂载期一次）。
- **去重槽共享后端 vs 形态 A per-occurrence**：PRD 明确这是"更细隔离的有意改进"——形态 A `default_as` 模板按 (T,Lit) 全二进制唯一跨函数共享；shared 去重槽同样按 (T,Lit) 共享（同键），但 per-occurrence 类（Variant/Array/Dict）独立。今天 Array/Dict 默认=0、String 仅 1 处 → 行为差异零可观察，机制就位。

## 5. 回滚

- `binding_mode=static` 即回形态 A；gen 每次构建重生成，无状态残留
- 实施分步独立提交，任一步可单独回退

## 6. 风险

- **懒约束**：defaults 数组存 accessor 指针（编译期常量）而非槽地址——若误存槽地址，静态初始化期物化槽，违反。实现时 codegen 发射注释强调。
- **错误文案 M 冷路径**：`missing argument` 需报 M——仅供错误路径，扫 defaults 找到首个非空位。
- 形态 A 基线闸门：static codegen 输出必须逐字节一致；slot 机制两形态并列，不改形态 A 发射。