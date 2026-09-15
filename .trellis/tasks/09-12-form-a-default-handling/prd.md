# 静态绑定形态A默认值处理改进：class剥离Def入元数模板参数，builtin预编码默认槽

## Goal

按用户 2026-09-12 指令改进**形态 A**（`binding_mode=static` 现状；本任务不涉及形态 B 实施本身）的默认值处理：

1. **class 族**：`Def` 移出 `Arg` 模板参数，thunk 模板加入 M（最少参数个数，`int` 模板参数）供元数检查
2. **builtin 族**：默认值预编码为静态 `EncodeT` 槽（懒初始化），缺参位 `arg_ptrs[i]` 直取槽指针，消除逐调用 `default_as` + `PtrToArg::encode`
3. **String 默认值双路径解析 bug 修复**（用户裁决 OQ1：双端一起修）——静态 `default_as` 与 dynamic `api_tool_parser.cpp` 同源对称统一 `str_to_var`

**本指令推翻 09-11 族"形态 A 零改动"条款**（R5 修订已于规划期完成）；本任务先于 09-12 三子任务实施（改变子任务 1 的迁移源与 "static diff 为空" 基线，双方工件已互相注记）。

## Background（2026-09-12 全量实测，锚点已核实）

### 数据面普查（json = extension_api-4-7.json；gen = src/static_binding/gen/ 实发）

| 族 | json 带默认值方法 | json Def 位点 | gen 实发位点 | 唯一 (T,Lit) 对 |
|---|---:|---:|---:|---:|
| class | 1,040（全带 hash） | 1,715 | 1,715 | 125 |
| builtin | 126 | 150 | **95** | 16 |
| builtin ctor | 156 | 0 | 0 | — |
| utility | 114 | 0 | 0 | — |

- builtin 150→95 差额 = String/StringName 接收者方法（55 位点）——codegen **按设计排除**（`static_binding_codegen.py:845-847` "JS string aliases"，走 JS 字符串原型路径），非静态发射面（方法级 41 归属不可靠，勿引用）
- class 1,715 精确核对通过（含 63 处指针型 `Arg<godot::Object*, "null">`）；**全 API 1,865 Def 位点非尾部连续 = 0**（尾部连续前提成立，codegen 仍加生成期断言作未来 API 安全网）
- class Def 类型面：bool 409、int 429、float 178、String 84、StringName 61、Color 57、Object* 63、Variant 34、Array 31、Dictionary 11、Vector2 27 等；builtin 16 对中非平凡默认仅 String `""` ×1 + Variant `"null"` ×3（引用类 Array/Dictionary 默认 = 0）
- vararg 前缀实测 0 默认（class_vararg 15 引 / builtin_vararg 6 引），M=F 恒成立

### class 族：默认值运行时无用（引擎 MethodBind 补全）

- `class_methods.h:59-64`：注释明示缺参不填、引擎权威（该注释的"json lossy for String"认知需随 String 修复更正——非 lossy，是我方解析 bug）；`:82-84` 元数检查 `provided < M || provided > N`；`:96-108` fold 只编组 `I < provided`；`:118-119` 将 `provided` 原样传给 `object_method_bind_call`
- 引擎链（09-11 PRD 核实）：`MethodBind::call` 全变体走 `call_with_variant_args*_dv(..., get_default_arguments())`；`binder_common.h:188-194` 缺参防御**仅 DEBUG_ENABLED**——release 缺参 OOB，**M 预检查是真实安全职责**
- 形态 A 现状：class 路径实例化了 113 个 class 独有 (T,Lit) 对的 `default_as` 死码（`produce_value` 默认分支对 class fold 守卫不可达，thunks_common.h:457-460）；1,715 处 Def 字面量唯一被消费的信息是 `has_default`→M 折叠（class_methods.h:68）

### builtin 族：必须自填（ptrcall 契约）

- `builtin_methods.h:163-178`：`std::tuple<PtrToArg<T>::EncodeT...> slots` + 逐参 `marshal_one` + `fn(base_ptr, arg_ptrs, &ret_val, N)`——**fn 收 N 全槽位，所有指针必须有效**，缺参位必须物化默认值（自填根因）
- `thunks_common.h:483-491` `marshal_one`：缺参分支 `out = default_as<...>()`（magic static 单例）→ `PtrToArg::encode(value, &slot)` **逐调用转换**——本任务要消除的对象
- `PtrToArg` 契约（`method_ptrcall.hpp:44-114` 三宏族；`:116` `MAKE_PTRARGCONV(bool, uint8_t)`、`:130` float→double）：`encode(T, void*)` 是"把 T 值写成 EncodeT 布局"的单一转换源，槽类型 = `EncodeT`
- **懒初始化约束（沿 09-11 裁决）**：`str_to_var` 走引擎 UtilityFunction hook，DLL 静态初始化期不可用 → 槽经 magic static 懒初始化，首次使用在挂载/首调期（与既有 `default_as` 时机一致）

### codegen 改造锚点

- `static_binding_codegen.py:750-779` `arg_template_expr`：唯一 Arg 发射点（`Arg<ct, cxx_str(default)>`）；class 剥离后 class 侧不再发 Def、thunk 模板串追加 M
- thunk 引用发射：`:816-820`（builtin/utility，f-string 拼名）、`:982-995`（class）；ctor 发射（`:1434/:1499-1503`）无默认参数不涉及
- `cxx_str :204-217` 字节稳定转义；`parse_args_list :235-244` json `default_value` → `entry["default"]`；`default_count :363-364`（M 计算复用此函数）
- manifest `uniq_defaults :1547-1553`：统计面，class Def 消失后 125 → 16，仅数字变化
- 挂载点：class `jsb_object_bindings.cpp:145`、builtin `jsb_primitive_bindings.cpp:808`（回退 `:816`）——本任务不改挂载逻辑，仅 thunk 模板与 gen 发射

### String 默认值双路径 bug（系统性缺陷，用户裁决：双端一起修）

**根因**：引擎 dump 用 `Variant::get_construct_string()`（`extension_api_dump.cpp:823/1148` → `variant.cpp:3535` `VariantWriter::write_to_string`）——**String 默认值在 json 中带引号**（`"region"`、空串 = 两引号字符）；正确解码是 `str_to_var`（全类型统一表示），verbatim 读取即得含引号错误串：

- **静态形态 A**：`thunks_common.h:69-70` `default_as` String 特化 verbatim `T(Lit.value)`（注释 `:62-65` "same rule the dynamic path follows" 已随双端修复失效，一并更正）——gen 实发 1 处（`dispatch_builtin.gen.cpp:1442` `PackedByteArray.get_string_from_multibyte_char` encoding `Arg<godot::String, "\"\"">`）→ 缺参传入两引号 → Windows 编码表（`os_windows.cpp:1050` `encodings[""] = 0` 即 CP_ACP 存在）查无 → `ERR_FAIL_NULL_V_MSG`（`os_windows.cpp:1209`）报错 + 空串返回；引擎真默认 = 空 encoding → CP_ACP 解码（`variant_call.cpp:2745` `varray(String())`）
- **dynamic 路径**：`api_tool_parser.cpp:247-248` STRING 类型 verbatim `Variant(dv_str)`（注释 `:245-246` "the value IS the literal string" 与 dump 格式矛盾；其余类型 `str_to_var` 正确）→ dynamic 模式下全部 97 处 String 默认位点（84 class + 13 builtin）缺参填充错误（`TabBar.add_tab()` title = 两引号字符、`CodeEdit.set_code_region_tags()` = 带引号 `"region"`、`push_list()` bullet = 带引号 `"•"`、`FileAccess.get_csv_line(delim)` = `","` 含引号）+ `.d.ts` 默认字面量渲染（`jsb_codegen_type_db.cpp:821-824` `make_literal_value`，同一 parser 数据源）同样失真——**parser 修复后自动受益，零渲染代码改动**
- 修法：两端删 String 特判，统一 `str_to_var`（`str_to_var('""')` → 空串、`str_to_var('"region"')` → region）；佐证 `String::split` 真默认空串（`ustring.h:497`）
- StringName（json `&"..."` 格式）两路径均走 str_to_var 不受影响；`has_default = Def.length > 0`（thunks_common.h:88）无表示性问题（json 空串默认 = 2 引号字符，length>0）
- class 侧不受行为影响（默认值死码），但 `class_methods.h:59-64` 注释认知随修复更正
- **dynamic 腿观察前提**：`.godot/.api_dumping/` 二进制缓存（gitignored）须重新生成，修复行为才可验证（TS 验证流程含 api 数据生成步骤，见 spec `test/index.md`）

### 测试覆盖缺口（双套件核实）

- bench `cases.builtin.ts:57-58/97-98/212` 等全部显式传参；`project/tests` + `src/tests` 缺参默认调用（`limit_length()`、`split()`、`get_string_from_multibyte_char()` 等）**零覆盖** → R3 必须补

## Requirements

- **R1 class 族剥 Def**：Arg 发射去除 Def 字面量（1,715 处全消），`class_method_thunk` / `class_vararg_method_thunk` 模板加入 `int M`（IsStaticC 之后）；元数检查语义严格等价 `provided<M||provided>N`（M 是真实安全职责：release 下引擎无缺参防御）
- **R2 builtin 族槽化**：默认值预编码为 thunk 内静态 `EncodeT` 槽（magic static 懒初始化，per-occurrence），缺参位 `arg_ptrs[i]` 直取槽指针；`marshal_one` 缺参分支不再逐调用 `default_as`+`encode`；`Arg<T, Def>` 模板保留（Def 供槽键控）
- **R3 缺参回归测试**：补齐当前零覆盖的缺参默认调用，**双腿**（static/dynamic）覆盖 class 代表类型 + builtin 槽路径 + 元数边界（M-1/M/N/N+1 抽查）
- **R4 String 双路径修复（无条件）**：静态 `thunks_common.h` `default_as` 删 String 特判 + dynamic `api_tool_parser.cpp` 删 STRING 特判，统一 `str_to_var`；`.d.ts` 渲染随 parser 自动受益；dynamic 腿验证前重新生成 api 二进制数据
- **R5 工件同步（已完成于规划期）**：09-11 族"形态 A 零改动"条款全部修订（父 PRD `:28/:85/:95/:103/:112`、class 子 PRD `:3/:7/:27/:36/:45`、builtin 子 PRD `:3/:7/:24/:28/:33/:50`、utility 子 PRD `:7/:31`、class implement.md `:12/:20` 基线闸门移至本任务后）；死码清理：`produce_value` 缺参默认分支（`thunks_common.h:457-460`）与 `marshal_one` 缺参分支在 R1/R2 后无调用方，一并删除

## Key Decisions（已收敛，无未决问题）

1. **OQ1（用户裁决）**：String 默认值 bug 双端一起修——`default_as` + `api_tool_parser.cpp` 两处小改同源对称；`.d.ts` 随 parser 修复自动受益；R3 双腿回归覆盖
2. **OQ2：M 作显式 `int` 模板参数**（非 D）——元数检查直接消费、与形态 B callback-data 的 min_argc 语义一致；形态 A 按方法实例化，M 入模板**不增实例数**（父 PRD `:112` 放弃的是形态 B 场景——共享签名随 M 增 131，与本场景不冲突）
3. **OQ3：引用类默认槽 per-occurrence 隔离**（复用形态 B 分类的语义要求）——Array/Dictionary/Variant 默认槽按方法×参数位独立（细于现状 `default_as` 的 (T,Lit) 跨函数共享，消除被调方原地改写跨函数泄漏）；实现统一 thunk 内 magic static（标量/CowData 的 (T,Lit) 共享在形态 A 无独立数据表可省、语义无差，单一机制为分类的严格细化；现存引用类默认仅 Variant `"null"` ×3 = NIL 不可变，零可观察差异）
4. **OQ4：vararg 族一并剥 Def**——gen 实测前缀 0 默认（M=F 恒成立），`class_vararg_method_thunk` 模板同样加 M=F；codegen 生成期断言**默认值尾部连续**（固定元数 class+builtin）+ **vararg 前缀 0 默认**，未来 API 违反前提时构建期拒绝发射

## Acceptance Criteria

- [ ] class gen 1,715 处 Def 字面量全消；元数检查边界逐例等价（M-1 抛错/M/N/N+1 抛错三档抽查 + 全量方法冒烟）
- [ ] builtin 缺参调用零逐调用转换（默认位指针直取槽）；`get_string_from_multibyte_char()` 缺参返回 "hi"（`[0x68,0x69]` 输入，Windows CP_ACP 解码）且无 ERR 报错（修复前返回 "" + 控制台报错）
- [ ] dynamic 腿 String 默认修复可观察：`TabBar.add_tab()` 后 `get_tab_title(0) === ""`（修复前为两引号字符）、`set_code_region_tags()` 缺参行为正确；`.d.ts` String 默认字面量渲染无引号失真（`--generate-types` 基线纪律，spec `test/codegen-baseline.md`）
- [ ] 缺参默认调用回归测试双腿落地（class：String/StringName/bool/int/float/Color/Variant/Array/Dictionary/Object* 代表；builtin：标量/String/Variant 槽路径 + `Array.bsearch(x)`/`Vector2.limit_length()`/`Dictionary.get(k)`；元数边界抽查）
- [ ] 行为基线：C++ 双套件全绿（exit 0、无 Orphan）、TS `GODOTJS_TEST_PROJECT_COMPLETED`、bench invalid=0；体积对比记录（class gen 源码收缩 + 形态 A dll 前后数据）
- [ ] R5 工件修订完成（规划期已落地，验收时复核）

## Out of Scope

- 形态 B 实施本身（09-12 三子任务保持 planning，时序后置于本任务）
- api_tool 元数据表（codegen `:600-744` 数据面，`k_*_methods` 消费的 `default_count` 喂表逻辑不动）
- `src/api_tool/api_tool_types.h`（用户 WIP，勿动）；运算符/构造器/成员族（无默认参数）
- builtin 槽的 (T,Lit) 跨方法去重（形态 A 无收益，属形态 B 子任务 2 的发射面）

## Notes

- 测试：官方稳定版 headless `godot --headless --path ./project --jsb-run-tests` 双套件 + TS `GODOTJS_TEST_PROJECT_COMPLETED`（spec `test/index.md`、`build/scons-build.md`）
- 任务挂靠：无父顶层 P1（09-08 低优先级父与 09-11 形态 B 目标均不匹配）；与 09-12 三子任务时序依赖已写入双方工件（R5）
- 实施顺序：String 双端修复（独立可回滚）→ class R1 → builtin R2+死码清理 → 测试补齐 → 全量验收（详见 implement.md）
