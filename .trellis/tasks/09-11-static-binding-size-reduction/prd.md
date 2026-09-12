# 静态绑定库体积优化：相同函数签名共用 thunks

## Goal

优化静态绑定的库体积（static 腿 editor 构建 55.84 MiB vs dynamic 腿 20.98 MiB，2.66x，`misc/bench_matrix.py` 报告）：消除按方法重复实例化，改为**每签名一个共享 thunk + 每方法数据经 v8 callback data（`info.Data()`）传入**，参考 `ReflectBuiltinMethodPointerCall`（`src/runtime/bridge/jsb_reflect_binding_util.h:194-298`）的既有模式。两种静态绑定形态经**编译参数**共存（见下）。

## Background（已核实事实，2026-09-12 字符串感知解析器复核）

### 实例化普查（平衡尖括号+字符串感知逐 token 计数 `src/static_binding/gen/`）

| 族 | 实例化引用 | 唯一签名（现状） | Def→可选标记 | Def 完全剥离 |
|---|---:|---:|---:|---:|
| class_method_thunk | 15,370 | 1,686 | 1,643 | **1,519** |
| builtin_method_thunk（含 VTC） | 767 | 455 | 441 | **434** |
| class_vararg_method_thunk | 15 | 10 | 10 | 10 |
| builtin_vararg_method_thunk | 6 | 5 | 5 | 5 |
| utility_function_thunk | 102 | 32 | 32 | 32 |
| utility_vararg_function_thunk | 12 | 3 | 3 | 3 |
| builtin_ctor_thunk | 133 | 133（无默认参数） | 133 | 133 |
| **合计** | **16,405** | 2,324 | 2,262 | **2,136** |

- 高频共享签名：class 族 `false, Ret<bool>` ×1,163、`false, RetVoid, Arg<bool>` ×933；class 族占 gen 源码 3.03/3.33 MB
- **共享化本身消除 16,405 → ~2,300（86%）；Def 剥离仅再省 188（8%，噪声级）**
- 勘误：旧正则计数 class 剥 Def 误报 1,546（实为 1,519）、builtin_vararg 误报 4（实为 5）、漏计 utility_vararg（12 引用 / 3 唯一）——本表为复核后数字

### 两种静态绑定形态与编译参数（2026-09-12 裁决）

- **形态 A（现状，极致静态）**：每方法一实例，`Arg<T, FixedString Def>` 默认值烘模板。零运行时间接寻址；体积大。**保持原样不动**——Def 剥离在形态 A 下仅省 188 实例，不值得
- **形态 B（签名共享，新）**：共享 thunk 按 `(签名类型参数)` 特化，身份（Hash/Class/Name）与默认值全部下沉每方法 callback data。Def 留模板参数会令默认值不同的同签名方法无法共享——**Def 移出模板是形态 B 的强制推论，非独立优化项**
- **编译参数命名（2026-09-12 二次裁决）**：`static_binding` 语义是"是否启用静态绑定"，形态是另一维度——统一参数须更名。**`binding_mode = static | shared | dynamic`**（字符串枚举，SCons `EnumVariable`），默认 `static`（= 形态 A，默认行为不变）：
  - `static` = 形态 A ｜ `shared` = 形态 B ｜ `dynamic` = 现 `static_binding=no`
  - 与 bench 腿词汇天然对齐：`bench_matrix --leg static|dynamic` 直接扩 `shared`（`build_and_deploy` 的 flag 即 `binding_mode=<leg>`）
  - 迁移面（grep 全量核实，干净切换不留别名）：`SConstruct:36,820-836`（声明+接线）、`.github/workflows/ci.yml:91,101`、`misc/bench_matrix.py:142`、spec `build/scons-build.md:25`、`cpp/generated-files.md:17`
  - 宏：保留 `JSB_WITH_STATIC_BINDINGS`（静态层公共门控）+ 新增 `JSB_WITH_SHARED_THUNKS`（形态 B 门控），按 `binding_mode` 注入
  - 运行时暴露：bridge 对象的 `STATIC_BINDING_ENABLED`（28b65a4，`jsb_bridge_module_loader.cpp`）改为暴露三态（如 `BINDING_MODE` 字符串），`benchmark.ts`/BENCH_JSON `staticBinding` 字段同步三态

### 默认值表示（2026-09-12 四次修订：静态 EncodeT 槽 + 类型分类发射，引用类 per-occurrence）

- **builtin 族是唯一需要自填默认值的族**（ptrcall 引擎不补；class 走 MethodBind 引擎补，见下节）
  - 唯一 (T, 字面量) 对 16 个、Def 出现 95 处；**Array/Dictionary 默认 0 个**——唯二非标量默认：`String ""` ×1（`PackedByteArray.get_string_from_multibyte_char`）、`Variant "null"` ×3（`Array.reduce`/`Dictionary.get`/`Dictionary.get_or_add`）；其余 14 对全为标量/POD（int64/bool/real_t/Vector2/Vector3/Color）
  - **勘误（用户质询核实）**：bool 两对（"false"/"true"，28 处）**今天就已 T≠EncodeT**——`PtrToArg<bool>::EncodeT = uint8_t`（`third/godot-cpp/include/godot_cpp/core/method_ptrcall.hpp:116` `MAKE_PTRARGCONV(bool, uint8_t)`）。此前"16 对全部 EncodeT==T"说法有误；T≠EncodeT 是现存需求而非仅未来兼容（单精度构建下 real_t→double 同理）
  - **静态槽机制（通用，不按对特判）**：每默认值位一个 `PtrToArg<T>::EncodeT` 槽（平凡/CowData 类按 (T, Lit) 去重共享，见下分类；引用类 per-occurrence），初始化 `PtrToArg<T>::encode(default_as<T, Lit>(), &slot)`——`encode` 契约即"把 T 值写成 EncodeT 布局"（int32→int64、float→double、枚举→int64、`TypedArray<T>`→`Array`、`const Ref<T>&`→`Ref<T>` 自动正确），单一转换源；未来 API 新增对只需 codegen 多发槽，模板零改动。**槽初始化时机约束**：禁止 DLL 静态初始化期初始化——`str_to_var` 走引擎 UtilityFunction hook，entry 之前未就绪；槽经 accessor 函数局部 magic static **懒初始化**，首次使用在挂载期（与形态 A `default_as` 时机一致）
  - **默认值槽分类（2026-09-12 引擎源码定论）**——ptrcall 边界按值物化每参（引擎 `core/variant/method_ptrcall.h:42-49` `PtrToArgDirect::convert` 返回 `T` 拷贝；`binder_common.h:78-84` 逐参 convert），各类型拷贝链三类、发射策略随之：
    - **平凡可拷贝**（现存 14 对：int64/bool/real_t/Vector2/Vector3/Color）→ (T, Lit) 去重共享静态槽，`arg_ptrs[i] = defaults[i]` 零拷贝
    - **CowData 支撑 = 写时 fork 值语义**：String（`ustring.h:281` `_cowdata` 成员、`:677` 默认拷贝构造）与全部 Packed*Array（`variant.h:82-92` = `Vector<T>`、`vector.h:321` 默认拷贝）——拷贝 = refcount++ 指针共享，但一切写路径强制 `_copy_on_write`（`cowdata.h:504-512`：refcount>1 分配新缓冲；`ptrw()` `:168`、`set()` `:186` 触发）→ 静态槽永不被写 → 同样 (T, Lit) 去重共享
    - **引用共享无 fork = 引用语义**：Array（`array.cpp:950-953` 拷贝构造 → `_ref` `:56-72`：`refcount.ref()` + `_p` 后端指针共享、无数据 fork；变更仅守 `_p->read_only` 标志——`:280-281` push_back、`:122-123` clear、`:304-305` resize——refcount 不参与写隔离）与 Dictionary（同构：`dictionary.cpp:756-759` → `:287-302`、守卫 `:202-204`）；引擎从不给默认值标 read_only（`make_read_only` 调用点仅 `array.cpp:931-933` create_read_only 与 `variant_call.cpp:2628/2688` 显式绑定）→ **per-occurrence 独立槽**（用户裁决：同函数两个空 Array 默认 = 两个独立对象）——**跨函数隔离严格细于形态 A**：形态 A 的 `default_as<T, Lit>` 为函数模板，static 按 (T, Lit) **全二进制唯一**（inline 模板弱符号合并），同对默认值在形态 A 本就跨函数共享同一后端、被调方原地改写会跨函数泄漏——per-occurrence 消除之（有意改进；现状 builtin 引用类默认 = 0，今天零可观察差异）；Variant 保守同类（`method_ptrcall.h:241` ByReference 边界零拷贝，内含 Array/Dict 共享后端）
  - **统一填充，无逐调用 copy 分支**（原"值语义二分"取消）：三类均 `arg_ptrs[i] = defaults[i]`——CowData 类经 fork 机制天然安全；引用类经 per-occurrence 独立槽。同函数跨调用共享后端两形态均存在（形态 A magic static / 形态 B 静态槽），引擎 class 侧默认值同样跨调用传共享存储指针（`binder_common.h:201`）——现状数据（builtin 引用类默认 = 0）下零可观察差异
  - **元数信息由 defaults 列表自带（用户指正，2026-09-12）**：builtin 描述符**无 min_argc 字段**——`const void *defaults[N]` 即元数信息（必填位 nullptr、默认值尾部连续）：`provided > N` → too-many；`provided < N && defaults[provided] == nullptr` → missing。与 `provided < M || provided > N` 严格等价；同一探测同时服务元数判定与缺参填充（一个 load 两用）；报错文案的 M 冷路径现扫（首个非空位，≤N 次比较）。**codegen 生成期断言默认值尾部连续**——未来 API 违反前提时构建期报错拒绝发射，而非静默生成错误检查
- **class 族不需要任何默认值存储**：引擎 MethodBind 补（证据链见下节）；callback data 仅需 min_argc（元数检查——class 无 defaults 列表，M 必须显式存；对照 builtin 元数由列表自带，见上条）。class gen 1,715 处 Def 字面量仅 `has_default`→M 被消费，值运行时不可达
- utility 族无默认参数（0 对）

### 引擎侧默认值补全证据链（class 族；本地引擎源码 4.7-stable-2108-g9d6da1df74）

- `core/extension/gdextension_interface.cpp:1335-1341`：`gdextension_object_method_bind_call` → `mb->call(o, args, p_arg_count, error)`——调用方 argc 原样下传
- `core/object/method_bind_common.h:82-86/166-170/260-264/355-359/436-438/502-504`：全部 `MethodBind::call` 变体统一走 `call_with_variant_args*_dv(..., get_default_arguments())`
- `core/object/method_bind.h:43,67-88`、`method_bind.cpp:94-97`、`class_db.cpp:1624-1627`：`Vector<Variant> default_arguments` 存储与注册
- `core/variant/binder_common.h:185-203`：补默认值循环——`i >= p_argcount` 的位 `args[i] = &p_default_values[...]`，**引擎传共享存储指针，靠 Variant 拷贝语义（CoW）兜底**
- 同文件 `:188-194`：`missing > dvs` 防御**仅 DEBUG_ENABLED**——release 无检查、缺参越界读 OOB——**class thunk 的 M 预检查（`class_methods.h:82-84`）是真实安全职责，形态 B 的 min_argc 字段承接它**
- 我方 class 路径：`class_methods.h:96-108` fold 守卫只编组 `I < provided`（缺参位留 NIL 槽）、`:118-119` 传 `provided`；`produce_value` 默认值分支（`thunks_common.h:457-460`）class 调用点不可达——形态 A 还为 class 实例化了不可达的 `default_as` 死码

### 函数指针解析时机：eager（裁决）vs lazy

- **裁决：形态 B 采用 eager——绑定时解析 fn 指针存 callback data**，理由：
  - 每次调用：一次 External 解引用取 fn，无分支（lazy 需 "fn 已解析" 空检查分支 + magic-static guard）
  - 失败发现：绑定时检测，失败走既有 "falling back to dynamic binding" 回退（`jsb_primitive_bindings.cpp:816` 模式）；lazy 是首调抛 JS 异常（`builtin_methods.h:137-141`），运行中才炸
  - 解析成本：挂载点（`jsb_object_bindings.cpp:145`、`jsb_primitive_bindings.cpp:808`）已有 name+hash 在手，增量 = 每方法一次引擎哈希查找；16k 方法全触发约 1-2ms。类暴露本身按类懒（首触才 expose，`jsb_environment.cpp:1564`，`jsb_godot_module_loader.cpp:143` 驱动）——启动惰性按类粒度完整保留
  - 先例：本仓库 `ReflectBuiltinMethodPointerCall` 即 eager（绑定时解析存 External，`jsb_reflect_binding_util.h:208/227/242`）；godot-cpp 为 lazy（函数内 static）但无绑定期失败检测
- lazy 方案（per-method data 携 name+hash，首调解析回写可变字段）留作 bench 不达预期时的备选，不加独立编译开关

### 模板参数解剖

- `class_method_thunk<HashC, ClassLit, NameLit, IsStaticC, RetT, ArgsT...>`（`class_methods.h:65`）：身份参数仅用于 ①懒解析 `GDExtensionMethodBindPtr`（函数内 static，`class_methods.h:49-54`）②报错文案；marshal 逻辑全部输入是签名参数。形态 B 下身份参数移入 callback data
- `builtin_method_thunk<VTC, HashC, NameLit, IsStaticC, RetT, ArgsT...>`（`builtin_methods.h:126`）：VTC 参与 self 的 opaque 取指针（`get_opaque_typed<VTC>`）与 fn 类型——**属签名一部分，形态 B 保留**
- class 族 Def 的唯一贡献 `has_default`→M 预检查在形态 B 下由 callback data 的 min_argc 字段替代（含 release OOB 安全职责，见证据链节）

### Prior art：ReflectBuiltinMethodPointerCall

- 模板仅按 `(OwnerT, 返回, 参数...)` 类型形状特化；fn 指针从 `info.Data().As<v8::External>()->Value()` 取——每方法一份 callback data，代码全签名共享。dynamic 路径长期使用，已验证可行
- 挂载侧支持已就绪：`impl::ClassBuilder::Method(name, callback, data)` 带模板 data 重载在全部四引擎 impl（v8/jsc/quickjs/web）存在

## Requirements

父任务（收口角色），实施由三个子任务承接：

- **编译参数**：`binding_mode = static | shared | dynamic`（字符串枚举，默认 `static`）；形态 A 保持现状零改动；codegen 与挂载端按形态条件发射；迁移面见 Background（干净切换不留别名）
- **形态 B 统一机制**（子任务 1 定型，2/3 复用）：共享 thunk 仅按签名类型参数特化；身份参数（Hash/Class/Name）与 Arg 的 Def 移出模板；**fn 指针 eager 解析**（绑定时，失败走 dynamic 回退）。每方法 callback data **按族分载**：class = {method_bind, class/method 名, **min_argc**}；builtin = {fn, 类型/方法名, **`const void *defaults[N]` 静态槽指针列表**}（无 min_argc，元数由列表自带）；utility = {fn, 函数名}（严格 N，编译期）；vararg = 前缀 0 默认值（gen 实测），M=F 编译期
- 子任务 1（P1）：class 族（09-12-class-thunk-sharing）——15,370 → 1,519
- 子任务 2（P2）：builtin 族（09-12-builtin-thunk-sharing）——767 → 434（含 VTC），静态 EncodeT 槽 + 类型分类发射（引用类 per-occurrence）
- 子任务 3（P3）：utility 与 vararg 族清扫（09-12-utility-thunk-sweep）——102→32、15→10、6→5、12→3

子任务间依赖已写入各自 PRD：2 复用 1 的 callback-data 管道；3 在 1/2 模式定型后收尾。

## Acceptance Criteria

- [ ] `binding_mode=static|shared|dynamic` 三态各自构建通过且 `static` 腿（形态 A）产物与改造前行为/性能基线一致（零回归）
- [ ] 缺参/元数语义与形态 A 一致：`provided<M||provided>N` 判定等价（class 经 min_argc、builtin 经 `defaults[provided]` 探测）；默认值填充统一 `arg_ptrs[i] = defaults[i]`（平凡/CowData 按 (T, Lit) 去重共享槽；Array/Dictionary/Variant per-occurrence 独立槽——跨函数隔离细于形态 A，现状引用类默认 = 0 无可观察差异）；并发缺参冒烟通过
- [ ] 三个子任务全部完成归档
- [ ] 静态绑定 dll 体积报告（三腿对比，`misc/bench_matrix.py` 纪律采数）
- [ ] 全程行为不变：C++ 双套件全绿、TS 集成 `GODOTJS_TEST_PROJECT_COMPLETED`、bench 231 case invalid=0

## Out of Scope

- 形态 A 的任何改动（含 Def 剥离——已裁决不值得）
- 运算符/构造器（133 无冗余）/成员属性族（近期已各自优化）
- api_tool dynamic 路径；eager/lazy 的独立编译开关

## Notes

- 生成文件 `*.gen.*` 一律经 `misc/build/static_binding_codegen.py` 改发射逻辑，不直接编辑
- 验收基线命令与陷阱见 `.trellis/spec/godotjs-ext/test/index.md`、`build/scons-build.md`
- `IsStaticC` 已实测裁决**保留模板参数**：折叠仅省 34 个实例（1,520→1,486，2.2%，签名去重复核），不值得每调用热路径多一分支；记录见子任务 1 design.md
- **min_argc 表示已裁决（2026-09-12，经用户二次修正）**：仅 **class 族**需要 min_argc 入 callback data（无 defaults 列表，M 显式存）；**builtin 族无 min_argc 字段**——defaults 列表本身即元数信息（`defaults[provided]==nullptr ⟺ provided<M`，尾部连续前提：1,127 带默认值方法实测非尾部连续 = 0）。备选"M 入模板参数"评估后放弃（class 1,643 vs 1,519、builtin 441 vs 434——多 131 实例换编译期 arity 检查，不值）；codegen 生成期断言默认值尾部连续作未来 API 安全网
