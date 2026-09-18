# design.md — 形态A默认值处理改进

## 0. 改动面总览

| 文件 | 改动 | 风险 |
|---|---|---|
| `src/static_binding/thunks/thunks_common.h` | `default_as` 删 String 特判 + 注释更正（:62-77）；`produce_value` 缺参默认分支删（:457-460）；`marshal_one` 缺参分支删（:483-491 内） | 双路径共用，错则 dynamic 也挂 |
| `src/static_binding/thunks/class_methods.h` | 两 thunk 模板加 `int M`，删 D/M 折叠（:67-69/:134-135）；注释 :59-64 更正 | 全 class gen 依赖，错=编译期全面失败 |
| `src/static_binding/thunks/builtin_methods.h` | 固定元数 thunk 编组段改槽直取（:162-178） | ptrcall ABI 敏感 |
| `misc/build/static_binding_codegen.py` | `arg_template_expr` 加 `emit_default` 开关；class 发射插 M；两条生成期断言 | gen 唯一源 |
| `src/api_tool/api_tool_parser.cpp` | 删 STRING 特判（:243-252 一带），统一 `str_to_var` | dynamic 全路径共用；不碰 `api_tool_types.h`（用户 WIP） |

gen 文件全部经 scons 构建重生成（SConstruct 调 codegen），禁手编。

## 1. R1 class 族：Def 剥离 + M 模板参数

新签名（两模板同头改法）：

```cpp
template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, int M, class RetT, class... ArgsT>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info);

template <uint32_t HashC, FixedString ClassLit, FixedString NameLit, bool IsStaticC, int M, class RetT, class... ArgsT>
void class_vararg_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info);
```

- 删 `:67-69`/`:134-135` 的 `constexpr int D/M` 折叠（Def 剥离后 `has_default` 恒 false）；`M` 直接来自模板参数
- 元数检查**逐字保留**：固定元数 `provided < M || provided > N`（:82-84）、vararg `provided < M`（:149）——M 是真实安全职责（引擎 `binder_common.h:188-194` 缺参防御仅 DEBUG_ENABLED，release 缺参 OOB）
- codegen 侧：固定元数 `M = len(args) - default_count(e)`（复用 `default_count` :363-364）；vararg `M = F`（前缀参数数，断言前缀 0 默认）
- Arg 发射：class 固定元数/vararg 均发 `Arg<ct>`（Def 全消 1,715 处；113 个 class 独有 (T,Lit) `default_as` 实例死码随之消失）
- 注释 :59-64 更正：json 非 lossy，是解析 bug（见 §3）；缺参不填、引擎补全的机制说明保留

## 2. R2 builtin 族：预编码 EncodeT 槽（直取）

模板签名**不变**（`Arg<T, Def>` 保留——Def 即槽键、`has_default` 继续支撑 thunk 内 M 折叠）；改动在 `builtin_methods.h:162-178` 编组段：

```cpp
// slots 元组仅为 provided 位服务（EncodeT 布局）；缺参位直取持久槽
std::tuple<typename godot::PtrToArg<typename ArgsT::gd_type>::EncodeT...> slots;
void *arg_ptrs[N > 0 ? N : 1];
bool ok = true;

// per-occurrence 默认槽：lambda 定义在 thunk 体内 → 每个 thunk 实例化 × I
// 各得一个 magic static（per 方法 × 参数位）
auto default_slot = []<std::size_t I, class ArgT>() -> const void * {
	using T = typename ArgT::gd_type;
	using EncodeT = typename godot::PtrToArg<T>::EncodeT;
	static_assert(ArgT::has_default, "missing argument without default");
	static const EncodeT slot = [] {
		EncodeT s{};
		godot::PtrToArg<T>::encode(default_as<T, ArgT::def>(), &s);
		return s;
	}();
	return &slot;
};

[&]<std::size_t... I>(std::index_sequence<I...>) {
	((void)((int)I < provided
				 ? (void)(ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I, std::get<I>(slots), provided),
							(void)(arg_ptrs[I] = (void *)&std::get<I>(slots))
				 : (void)(arg_ptrs[I] = const_cast<void *>(default_slot.template operator()<I, ArgsT>()))),
			...);
}(std::make_index_sequence<N>{});
```

- `fn(base_ptr, arg_ptrs, &ret_val, N)` 传 N 不变（fn 收 N 全槽位，所有指针必须有效——缺参位现在指向持久槽，一次初始化后只读）
- 元数检查 `provided < M || provided > N` 不变（M 仍由 `ArgsT::has_default` 折叠算出，builtin 的 Def 保留）
- **槽语义（OQ3 映射）**：引用类（Array/Dictionary/Variant）per-occurrence 隔离要求完整满足——lambda 闭包类型随 thunk 实例化唯一，其 static 为每方法 × 每参数位独立（细于现状 `default_as` (T,Lit) 跨函数共享，消除跨函数改写泄漏）；标量/CowData 共享与否无语义差，统一 per-occurrence 单一机制（形态 A 无独立数据表，(T,Lit) 去重无收益——那是形态 B 子任务 2 的发射面）。现存引用类默认仅 Variant `"null"` ×3（NIL 不可变），零可观察差异
- **懒初始化约束保持**：static 局部首调初始化（挂载/首用期，引擎 hook 已就绪），禁 DLL 静态初始化期调 `str_to_var`——与现状 `default_as` 时机一致；magic static 线程安全（C++11），槽初始化后只读，并发缺参安全
- `encode` 契约：`PtrToArg<T>::encode(T, void*)` 把 T 值写成 EncodeT 布局（int→int64、float→double、bool→uint8_t，`method_ptrcall.hpp:44-130`），单一转换源
- vararg builtin thunk（:184-263）不动：gen 前缀 0 默认，无缺参填充路径

## 3. String 默认值双路径修复

根因链（已核实）：引擎 dump `Variant::get_construct_string()`（`extension_api_dump.cpp:823/1148` → `variant.cpp:3535` VariantWriter）→ json String 默认带引号；`str_to_var` 是全类型统一正确解码。

- **静态**：`thunks_common.h:66-77` 删 `if constexpr (std::is_same_v<T, godot::String>)` verbatim 分支，全类型走 `str_to_var`；注释 `:62-65` 一并更正（"same rule the dynamic path follows" 随双端修复失效）
- **dynamic**：`api_tool_parser.cpp:243-252` 删 STRING case（注释 `:245-246` "the value IS the literal string" 与 dump 格式矛盾），全类型 `str_to_var`
- 受益面：dynamic 97 处 String 默认位点（84 class + 13 builtin）+ `.d.ts` 渲染（`jsb_codegen_type_db.cpp:821-824` `make_literal_value` 同源数据，零代码改动）+ 静态 1 处（`get_string_from_multibyte_char`）
- StringName（`&"..."`）两路径本就 `str_to_var`，不受影响；`has_default = Def.length > 0` 无表示性问题
- dynamic 腿验证前提：`.godot/.api_dumping/` 二进制缓存重新生成（gitignored；TS 流程的 api 数据生成步骤）

## 4. 死码清理（R1/R2 后无调用方）

- `produce_value` 缺参默认分支（`thunks_common.h:457-460`）：class fold 守卫 `I < provided` 不可达、builtin 改槽直取、utility 无默认、vararg 前缀 0 默认——全族无调用方，删
- `marshal_one` 缺参分支（`:483-491` 内的 `default_as` 路径）：R2 后仅被 provided 位调用，删
- `default_as<T, Lit>` 本体**保留**（槽初始化的解析源 + §3 修复后是 String 正确解码点）

## 5. codegen 发射改动（`misc/build/static_binding_codegen.py`）

1. `arg_template_expr(a, emit_default=True)`（:750-779）：class 固定元数与 vararg 发射传 `False`（`Arg<ct>`）；builtin/utility 发射不变（`Arg<ct, cxx_str(default)>`）
2. class thunk 引用发射（`:982-999` `class_entry_expr` 及 vararg 同点）：模板串在 `cxx_bool(is_static)` 后插 `M`（固定元数 `M = N - default_count`、vararg `M = F`）
3. 两条生成期断言（`SystemExit` FATAL 风格，对照 `_assert_byte_sorted` :353-360，置于 `collect()` 后发射前）：
   - 固定元数方法（class+builtin）：默认值尾部连续（非尾部默认 → 拒绝发射）
   - vararg（class+builtin）：固定前缀 0 默认
4. manifest `uniq_defaults`（:1547-1553）数字随 class Def 消失自然变化（125 → 16），仅统计面，不需改

## 6. 兼容性与迁移

- 元数检查语义逐字保留（M 真实安全职责）；builtin ptrcall ABI 不变（fn 收 N 全槽位）
- 09-11 族 R5 修订已完成（规划期）：形态 B 全部既有裁决不变；子任务 1 迁移源 = 本任务完成后的 `class_methods.h` 新签名，其 "static 输出 diff 为空" 基线在本任务完成后重采
- 用户 WIP `src/api_tool/api_tool_types.h` 不触碰；parser 修复在 `api_tool_parser.cpp`，无冲突面

## 7. 回滚

每步一提交；步骤 1（String 双端修复）与步骤 2（class R1）、步骤 3（builtin R2）互相独立可单独回滚；gen 由 codegen 唯一源重生成，回滚 = 还原 codegen/thunk 头 + 重建。
