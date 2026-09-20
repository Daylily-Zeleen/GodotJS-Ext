# 09-12-builtin-thunk-sharing 交付报告

> 状态：**实现 + 验证完成，未 commit**（待用户授权提交，同 class 任务先例）。
> 构建统一 quickjs-ng + `binding_mode=shared`（本任务与 JS 运行时无关）。
> 父任务：09-11-static-binding-size-reduction 子任务 2（P2）。形态 A 基线 = 09-12-form-a-default-handling 槽化后锚点。

## 一、目标回顾

`binding_mode=shared` 下消除 builtin_method_thunk 按方法重复实例化：**767 → 434** 唯一签名（含 VTC，移出 Hash/Name/Defs）。核心决策：**默认参数迁"静态 EncodeT 槽 + 类型分类发射"**（平凡/CowData 按 (T, Lit) 去重共享、Array/Dictionary/Variant per-occurrence），每方法数据经 v8 callback data 传入。

## 二、实现（改动清单）

### thunks（`src/static_binding/thunks/builtin_methods.h`）
- 补 `#include <atomic>`
- `#if JSB_WITH_SHARED_THUNKS` 门控共享段：
  - `SharedBuiltinMethodData{ std::atomic<GDExtensionPtrBuiltInMethod> fn; godot::Variant::Type vt; const char *method_name; DefaultAccessor const *defaults; }`——`vt` 用 `Variant::Type`（实测对比 `const char*`/`uint8_t` 三态 `sizeof` 均 32，无差异取语义清晰者），报错经 `get_type_name(vt)`；defaults 为每参数位 accessor 指针数组（必填位 nullptr，可选位 `&default_arg_slot<DefT, IdT>`），无默认值方法指向共享长空表
  - `ensure_builtin_method(vt, name, hash, md)`：挂载期 eager 解析 `variant_get_ptr_builtin_method`，`compare_exchange_strong` 幂等缓存（跨 worker 共享 .data 并发写良定义，relaxed atomic）
  - `shared_builtin_method_thunk<VTC, IsStaticC, RetT, AllArgsT>`：纯类型特化；fn 热路径单次 relaxed load；元数经 `defaults[provided]` 探测（`provided>N`→too-many；`provided<N && defaults[provided]==nullptr`→missing，与 `M..N` 严格等价，尾连续前提由 codegen 生成期断言）；缺参位置 `arg_ptrs[i] = md.defaults[i]()`

### codegen（`misc/build/static_binding_codegen.py`）
- `shared_builtin_entry_expr`：签名只留 (VTC, IsStaticC, RetT, Args纯类型)
- `_emit_shared_builtin_dispatch(m)`：签名去重 → `k_shared_thunks[]` → 共享全 nullptr 长空表 `k_shared_no_defaults`（无默认值方法共用，长度 = 全局最大参数个数；**长空表定论 2026-09-20**）→ 仅真实带默认值方法发独立 `k_defs_<VT>_<name><hash>[]` accessor 表（codegen `has_default` 判定，杜绝 `if acc` 对任何有参方法误发）→ 每类型 `k_data_<VT>[]` 行 → `find_builtin_<VT>` 纯查找 → 顶层 `find_shared_builtin_binding(p_vt, name, hash, r_data)` 命中后唯一一次 `ensure_builtin_method`（失败返回 nullptr → 挂载点 dynamic 回退）；`k_defs_` 元素一元素一行统一 `\t` 前缀（格式化定论）
- `emit_builtin_dispatch_cpp(m, op_tables, binding_mode)`：加 shared 分支；member-accessor 段提取为 `_emit_builtin_member_accessors` 两模式共用
- main 调用点传 `ns.binding_mode`
- 默认值槽分类：`_PER_OCCURRENCE_DEFAULT = {Array, Dictionary, Variant}` 用递增 `std::integral_constant<int, k>` 隔离；平凡/CowData 用 `void` IdT 按 (T, Lit) 共享

### dispatch.h
- `find_shared_builtin_binding(Variant::Type, name, hash, const void **r_method_data)`（JSB_WITH_SHARED_THUNKS 门控，注释同 class 手法）

### 挂载端（`src/runtime/bridge/jsb_primitive_bindings.cpp`）
- version `.788-807`：`sb_data` 提升到 shared/static 分叉外；shared 走 `find_shared_builtin_binding` + `Method(name, thunk, (void*)sb_data)` 透传；static 走原 `find_builtin_thunk`（无 data）

## 三、验证（全部实测，quickjs-ng）

| 判据 | 结果 |
|---|---|
| `--binding-mode static` vs 基线 | **逐字节 IDENTICAL** |
| shared builtin 实例化 `shared_builtin_method_thunk<` | **434**（≤440 AC）✓ |
| `k_data_` 表 / `k_shared_no_defaults` / `k_defs_` 独立表 | 32 / 1 / 87 |
| `ensure_builtin_method` 源调用点 | 1（顶层 finder） |
| C++ 双套件（shared & static）| 各 runtime 51/51 + editor 3/3, result 0, exit 0 |
| TS 集成 | `GODOTJS_TEST_PROJECT_COMPLETED` |
| 默认参数回归 | default-args 套件覆盖 builtin 三类槽（标量 float/bool、Variant、String）——COMPLETED 即全部通过 |
| bench | `bindingMode: shared, invalid: 0, exit 0` |
| 体积（shared release）| **15,034.0 KB**（`335fe662…`，长空表/Variant::Type/格式化三改后）|

**体积对比（main dll，release，quickjs-ng）**：
- static（形态 A 基线）：54,653.0 KB
- class-shared only：15,913.5 KB
- **class + builtin shared（本任务）：15,034.0 KB**——builtin 共享再降 ~880 KB，累计相对 static **−72.5%**

## 四、设计要点（design.md，聚焦 PRD Notes）

- **defaults 布局（2026-09-20 定论）**：无默认值方法 → 共享全 nullptr 长空表 `k_shared_no_defaults`（非每方法各发一张——那是对同一"全必填"事实的重复描述）；带默认值方法 → 每方法 N 项 accessor 指针数组（必填 nullptr / 可选 `&default_arg_slot`）。选 accessor 指针数组而非已物化槽地址：保懒初始化（函数局部 magic static，禁 DLL 静态初始化期物化槽）+ 类型安全 + 与形态 A `default_arg_slot` 同构。**目的非体积**（实测长空表省 ~6.4KB，占 dll <0.05%），是结构正确性（单一事实源）
- **type_name 字段定论（2026-09-20）**：`SharedBuiltinMethodData` 用 `godot::Variant::Type vt`（报错经 `get_type_name(vt)`）。实测 `const char*`/`Variant::Type`/`uint8_t` 三态 `sizeof` 均 32 无差异 → 取语义清晰者
- **格式化定论（2026-09-20）**：`k_defs_` 元素一元素一行 + 统一 `\t` 前缀；禁用 `'\n\t\t\t\t'.join(acc)` 硬编码 tab 拼接（曾致多元素表缩进错乱）
- **槽类型分类**：(T, Lit) 平凡/CowData 去重共享；Array/Dictionary/Variant per-occurrence 隔离（用户裁决有意改进；今天 Array/Dict 默认 0 个 → 零可观察差异，机制就位）
- **元数检查单探测两用**：`defaults[provided]` 既判缺参又填默认；M 冷路径扫尾；生成期 `_assert_default_layout` 断言尾连续
- **eager 解析 + 懒槽**：fn 挂载期一次 CAS 解析；槽首次缺参调用 lazy 物化

## 五、否决记录：SharedClassMethodData 名字索引化（同会话 A/B，已回退）

**结论：索引化反而增大体积，整体回退。** 用户唯一判据是二进制大小，故否决。

同会话双腿对比（release, quickjs-ng, binding_mode=shared，main dll）：

| 腿 | 形态 | size | md5 |
|---|---|---|---|
| A | `uint16_t class_index/method_index` + 外部名字池 | **15,065.5 KB** | `5d605d76` |
| B | `const char *class_name/method_name`（指针） | **15,034.0 KB** | `d77959e4` |

指针版恰等于历史基线 (15,034.0 KB) → 对照同口径可信。**索引化净增 +31.5 KB（0.21%）**。

- struct `SharedClassMethodData`：32→24B（省 8B/行×15385≈123 KB 假想收益）
- 反噬：名字池指针数组 (841 类 + 10870 方法)×8B ≈ 94 KB + 每项 `.reloc` 重定位；净**增**
- **根因**：MSVC `/GF` 字符串池化已把指针形态的名字字面量去重（`{nullptr,"AESContext","start",hash,min}` 行内存 8B 指针、串每唯一名在 `.rdata` 只存一次）。早期"行内内联 16B 重复串"模型假设错误
- 教训：改数据结构省二进制前，先确认编译期字符串池化已消灭的重复不在此列；体积断言必须在同会话 A/B（双腿同构同构建）上实测，跨会话 DLL 大小不可比

## 六、遗留

- 未 commit（同 class 任务，待用户授权提交）
- `GENERATED_NOTE` 头部注释仍含 "static_binding=yes"（有意不改，保 static 基线 diff 判据）
- vararg builtin 6 个、ctor/operator/member-getset 均 OS（Out of Scope），未触及

## 六、规格沉淀

- 无新 spec 改动（复用 generated-files.md 的 `--binding-mode` 条目；scons-build.md 已含 `binding_mode` 参数表）