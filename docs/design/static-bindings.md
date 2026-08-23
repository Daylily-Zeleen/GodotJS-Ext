# 静态绑定（Static Bindings）设计方案

> 分支：`feature/static-bindings` · 状态：**设计稿 v5（未实施）**
> 目标：将 JS→Godot 的函数查找从「每次调用的名称解析」降为「编译期已知哈希的一次性指针获取 + 参数编组」
>
> v2 修订：转换层命名改为 `js_to_gd/gd_to_js` 且推迟；新增固定/可变参数两类编组规则；
> 对象类索引属性静态化；内置成员属性与数组下标/键访问划清界限；删除析构处理与
> 「require-missing-module SIGSEGV」（均经代码核实）。
>
> v3 修订：运算符恢复为独立小节——`(Op, LeftVT, RightVT)` 三个引擎枚举直接作模板实参；
> 取消独立 convert 目录（必要性论证见 §3.1：V1 与 ptrcall 均非必需，若需要也是扩展现有
> TypeConvert）；新增 §13 测试与验证设计（完整性 + 行为等价性双重证明）。
>
> v4 修订：§7 新增「回退可见性」——静态绑定 miss 回退动态之前必须输出日志指明
> 未找到哪个静态绑定。
>
> v5 修订：§7.1 级别定稿——扩展类接口 miss = **ERROR**（已评审确认）；非扩展类
> 接口 miss = WARNING，其定位明确为「日后排查静态化覆盖是否完整的排查线索」；
> 澄清日志去重说明。
>
> v6 修订：§7.1 日志宏改用 godot-cpp 的 `WARN_PRINT_ONCE` / `ERR_PRINT_ONCE`
> （与 api_tool_types.h 中指针加载失败的既有处理惯例一致）；删除「调用期分派
> 需去重」的防御性条款——该场景不成立：绑定决策仅在注册期发生一次，之后每次
> 调用直接进入已挂回调，不存在重复查找。
>
> v7 修订：新增 §4.6 默认值处理——核实 extension_api.json 的 default_value 为
> 字符串字面量（"0"/"true"/"Color(0, 0, 0, 1)"…），api_tool 现状经
> `UtilityFunctions::str_to_var` 一行解析（api_tool_parser.cpp:239-247），
> 无自制字面量解析器。故静态绑定不做构建期翻译：codegen 仅搬运原始串，
> 运行时惰性解析 + magic static 缓存，解析函数与动态路径同源（单一事实源）；
> §12.4 翻译器风险随之消解。

---

## 0. 背景与问题

现状调用链（动态绑定）：

```
JS 调用 → FunctionCallback → 从 api_tool 文档缓存按名称检索方法/签名
        → 组装参数(Variant) → gdextension_interface 调用 → 返回值转 JS
```

每次调用都要付出：StringName/哈希解析、文档缓存查找、参数表拼装的代价。
对游戏循环内的高频调用（`Node.get_child`、`Vector2` 运算等）这是纯开销。

**核心洞察**：`extension_api.json` 中每条 builtin method / class method /
utility function 都携带官方 **hash**。GDExtension 接口本身支持按 hash 直接取
原生函数指针。静态绑定 = 构建期把 hash 烧进 C++ 模板实参，运行时首次调用惰性
取指针并缓存，之后每次调用只剩参数编组。

## 1. 总体架构

```
┌──────────── 构建期 ────────────────────┐
│ project/extension_api.json             │
│   └─ tools/static_binding_codegen.py   │
│        └─ src/static_binding/gen/      │
│           *.gen.h | *.gen.cpp          │──┐
└────────────────────────────────────────┘  │
                                            ▼
┌──────────── 运行期 ───────────────────────────────────────┐
│ JS 调用 → thunk<…编译期键…>(info)                          │
│   ├─ 首次：gdextension_interface 按官方键取原生函数指针     │
│   │        （函数局部 static 缓存，线程安全）               │
│   ├─ 参数：按 §4.0 编组（固定=逐参展开；vararg=仅尾部循环） │
│   │        V1 复用 TypeConvert                             │
│   └─ 返回：gd_var_to_js                                   │
│                                                           │
│ 静态注册表 miss → 回退现行动态绑定（功能无损兜底，见 §7）    │
└───────────────────────────────────────────────────────────┘
```

## 2. 构建系统

```python
# SConstruct
opts.Add(BoolVariable("static_binding",
    "Enable compile-time static bindings (requires project/extension_api.json)", False))
...
if env["static_binding"]:
    # 前置校验：extension_api.json 必须存在（CI dump 步骤或本地 dump 产生），
    # 缺失则 print_error 并提示两条引导命令后退出
    # 代码生成：python tools/static_binding_codegen.py \
    #             --input project/extension_api.json --out src/static_binding/gen
    # 产物源集加入构建；CPPDEFINES += JSB_WITH_STATIC_BINDINGS
```

要点：
- `static_binding=no` 时生成物与相关代码**完全排除**在构建外（宏 + 源集双重隔离），现有产物零影响
- 生成产物**提交入库**：可 review diff、避免鸡生蛋；提供 `scons static_binding_gen`
  单独再生成；CI 增加 `codegen --check` 新鲜度校验（防止改了 json 忘了重生成）
- CI 矩阵后续增加一条 `static_binding=yes` 的 leg（矩阵中多出一行构建组合，
  用静态绑定产物编译并跑现有测试 + §13 生成的对照测试）做守护（P5 落地）

## 3. 类型转换层

### 3.1 结论：不新建层，扩展点留在现有 TypeConvert

**V1（Variant 路径）直接复用现有 `TypeConvert::js_to_gd_var(isolate, ctx, val,
type, out)` / `gd_var_to_js(...)`**（`src/runtime/bridge/jsb_type_convert.*`），
签名行为均不变。

关于「为什么不另起一层」的论证：

1. **功能上不需要**：thunk 的正确性只依赖「JS 值 ↔ Variant」，这正是
   TypeConvert 已做的事；静态化的收益来自指针获取与编组方式，与转换实现无关。
2. **ptrcall 也未必需要新层**：ptrcall 参数缓冲区完全可以由
   「现有 js_to_gd_var 得到 Variant → `var.operator T()` / `(T)var` 提取原始值」
   两步填充，全程复用现有转换。新转换层唯一能省的是中间那次 Variant 暂存。
3. **若 P4 基准证明该暂存开销显著**：届时在**现有 `jsb_type_convert.h/.cpp`
   内**新增按 C++ 类型分派的模板重载（命名沿用 `js_to_gd<T>` / `gd_to_js<T>`，
   与 `*_gd_var_*` 家族词汇对齐），保持单一转换事实源——**不建
   `src/static_binding/convert` 目录**，避免两处维护同一份类型知识。

### 3.2 Traits 与 Concepts（C++20，仅在上述第 3 点成立时引入）

```cpp
template <typename T> struct GdTypeTraits;   // 每类型特化：
//   static constexpr GDExtensionVariantType VARIANT_TYPE;
//   static constexpr const char* NAME;      // "float" / "Vector2" / ...
//   static constexpr bool IsBuiltin = ...;

template <typename T>
concept GdFundamental = std::same_as<T, double> || std::same_as<T, float>
                     || std::same_as<T, int64_t> || std::same_as<T, bool>;

template <typename T>
concept GdBuiltin = requires { typename GdTypeTraits<T>; }
                 && GdTypeTraits<T>::IsBuiltin;
```

覆盖清单同 `Variant::Type` 全集；Object 类走既有实例包装器往返。

## 4. 函数 Thunk 模板族（核心）

统一形制——**编译期键作为模板实参，运行时惰性取原生指针**。

### 4.0 参数编组规则（固定参数 / 可变参数两族）

生成器依据 `METHOD_FLAG_VARARG` 与 `default_arguments.size()` 将每个方法归入
以下两类之一，**模板也按此分两族**，禁止混用统一的「循环所有实参」写法：

**A. 固定参数方法（非 vararg）——逐参数展开，无循环**

设声明参数 N 个、默认值 D 个、必填 M = N − D：

```
argc 校验：M ≤ info.Length() ≤ N，越界立即抛异常（报方法名与期望个数区间）
第 i 参（0 ≤ i < N）按声明类型独立展开（codegen 顺序发射 N 条语句）：
    i < info.Length() → 严格类型检查（can_convert_strict 语义）后转换；
                        失败即抛 JS 异常，信息含参数序号、JS 实际类型、期望 Godot 类型
    否则              → 填入默认值（机制见 §4.6；缺失且无默认值的情况已被 M 下界拦住）
```

**B. 可变参数方法（vararg）——仅尾部允许循环**

设固定参数 F 个（含其默认值，处理规则与 A 完全相同，逐个展开）：

```
argc 校验：info.Length() ≥ F − F内默认值数
前 F 参：逐参数展开，各自带声明类型检查
尾参 [F, info.Length())：唯一的循环段，按无类型 Variant 收集；
     以实际 argc 传给引擎（builtin/utility vararg 的原生约定）
```

约束：vararg 方法**永远不走 ptrcall**（引擎限制），锁定 Variant 数组路径；
§4.3 的 ptrcall 仅适用于「非 vararg && 无默认值」的方法。

### 4.1 内置类型方法（有 hash）

```cpp
template <GDExtensionVariantType VT, uint64_t MethodHash>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    // 注意：取指针需要 (type, 方法名 StringName, hash) 三元输入，
    // 方法名来自启动期惰性建立的 SN 缓存表（见 §4.3）
    static const auto fn = [&] {
        return gdextension_interface::variant_get_ptr_builtin_method(
            VT, sn_table.builtin_method_name<MethodHash>(), (GDExtensionInt)MethodHash);
    }();
    // magic static：并发首调安全；指针进程级有效
    // 按 §4.0 A/B 规则编组参数 → fn(&base, args, &ret, argc)
    // 返回：gd_var_to_js
}
```

### 4.2 全局 utility 函数（有 hash）

与 4.1 同构：`variant_get_ptr_utility_function(name_sn, hash)`（同样需要函数名
StringName），无 base 参数；编组规则同 §4.0。

### 4.3 类方法（有 hash）

需要两个 StringName + hash：启动期（CORE init 之后）建立**类名/方法名 SN 缓存
表**，由生成器发射引用（`string_names.gen.h`），运行时惰性构造。

```cpp
template <uint64_t ClassId, uint64_t MethodHash>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    static GDExtensionMethodBind *mb = classdb_get_method_bind(
        SN_TABLE.get<ClassId>(), SN_TABLE.get<MethodHash>(), (GDExtensionInt)MethodHash);
    // V1：Variant 路径（§4.0 编组；object_method_bind_call）
    // 可选优化：「非 vararg && 无默认值」→ object_method_bind_ptrcall
    //          （参数缓冲由 Variant 提取填充，见 §3.1 第 2 点）
}
```

**对象类索引属性**（`ApiPropertyInfo::index ≥ 0`，见 §4.4-B）复用本节的
MethodBind 获取方式，另加 index 模板实参。

### 4.4 属性访问器（三类，边界必须分清）

**A. 内置类型成员属性（如 `Vector2.x/y`）——无 hash、无 index**

对应 `ApiBuiltinClass.members`（`ApiMemberInfo`），引擎侧入口是按**成员名**
StringName 取的 setter/getter 函数指针：

```cpp
template <GDExtensionVariantType VT, uint64_t MemberId>
void builtin_member_get_thunk(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &) {
    static const auto fn = gdextension_interface::variant_get_ptr_getter(
        VT, sn_table.member_name<MemberId>());
    // base 由 this（值类型包装）取出；getter(base, &ret)；gd_var_to_js(ret)
}
template <GDExtensionVariantType VT, uint64_t MemberId>
void builtin_member_set_thunk(...);   // variant_get_ptr_setter(VT, name_sn)
```

注册侧：类包装构建时登记为 **v8 accessor 模板**——JS 侧 `v.x` 直接触发，
不再是 Function 调用。

**B. 对象类索引属性（`ApiPropertyInfo::index ≥ 0`）——getter/setter 方法 + 前置 index**

这类属性本质是「一族属性共用一对 getter/setter **方法**」，调用时把 index
作为**第一个前置参数**传入（现状见 `jsb_object_bindings.cpp` 中
`prop_index >= 0` 分支与 `_godot_object_get2/_godot_object_set2`：
`Variant args[] = { prop.index }` 后调 `validated_call`）。

静态化：index 从运行时数据变为模板实参，调用点同步改造——

```cpp
template <uint64_t ClassId, uint64_t GetterHash, int32_t PropIndex>
void object_prop_get_thunk(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &) {
    static GDExtensionMethodBind *mb = /* 同 §4.3，按 GetterHash */;
    Variant idx{ PropIndex };              // 编译期常量直接构造，无重映射查找
    // object_method_bind_call(mb, instance, {&idx}, 1, &ret, &err)
}
template <uint64_t ClassId, uint64_t SetterHash, int32_t PropIndex>
void object_prop_set_thunk(...);           // args = { idx, converted_value }
```

注册侧同为 v8 accessor；动态路径的 `FPropertyInfo2` 重映射表仅保留给回退分支。

**C. 数组下标 / 键访问——明确排除在范围外**

`ApiBuiltinClass` 的 `indexing_type / has_indexing_return_type / is_keyed` 及
`variant_get_ptr_indexed_*`（Array/PackedArray 元素访问）、`variant_get_ptr_keyed_*`
（Dictionary 键访问）**不进入静态绑定**：JS 侧目前没有实现下标运算符（需
Proxy）、GDictionary 亦未支持 `["key"]` 调用，无调用点可言。待未来实现 JS
下标语法时另行立项。

### 4.5 运算符（三个引擎枚举直接作模板实参）

键就是 `(Variant::Operator op, Variant::Type left, Variant::Type right)`
**三个枚举本身**——无 hash、无 StringName，天然是模板非类型实参。
api json 每个 builtin 类的 `operators` 数组已枚举全部组合（api_tool 侧
`ApiOperatorInfo` 即按此四元组建模）；`jsb_primitive_operators.def.h` 头部的
TODO（"通过 extension_api.json 进行代码生成"）正是本节要还的债。

现状（`jsb_primitive_bindings_reflect.cpp`）：JS 侧运算符以**方法调用**形式
触发，`BinaryOperator/UnaryOperator::invoke` 从 `info.Data()` 取 op 后调
`Variant::evaluate`——godot-cpp 封装内部**每次调用都重新解析** evaluator。

静态化：

```cpp
template <Variant::Operator Op, Variant::Type LeftVT, Variant::Type RightVT>
void binary_operator_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    static const auto eval = gdextension_interface::variant_get_ptr_operator_evaluator(
        Op, LeftVT, RightVT);          // 键=三枚举，一次取定，进程级有效
    // 左右操作数经现有 TypeConvert 转 Variant（与现行为一致的宽松语义）
    // eval(&left, &right, &ret)；失败路径与现状相同抛 get_variant_operator_name 信息
}

template <Variant::Operator Op, Variant::Type VT>
void unary_operator_thunk(...);        // 右操作数固定 NIL Variant（与现状一致）
```

注册点不变（类包装构建时按运算符名挂 FunctionCallback），仅回调从泛型
invoke 换成具体实例；生成器从 json 全量发射所有 `(LeftVT, Op, RightVT)`
组合的实例与注册项。

### 4.6 默认值处理

**数据形态（已核实）**：extension_api.json 中 `default_value` 是字符串形式的
Godot 字面量——`"0"` / `"true"` / `"Color(0, 0, 0, 1)"` /
`"PackedByteArray()"` 等；STRING 类型参数的默认值就是字面文本本身。
api_tool 现状解析仅两行（api_tool_parser.cpp:239-247）：STRING →
`Variant(原串)`；其余 → `UtilityFunctions::str_to_var(原串)`——**引擎自带
解析器，项目无自制字面量解析代码**。

因此静态绑定**不做任何构建期翻译**（不在 Python 里重写 GDScript 字面量解析
器），生成物只搬运原始数据，解析发生在运行时首次用到时：

```cpp
// default_values.gen.h —— codegen 发射（每方法一份特化）
template <> struct MethodDefaults<0x<MethodHash>> {
    static constexpr int N = <声明参数数>, D = <默认值个数>;
    // (声明类型, 原始串)：类型用于构造目标 Variant，串交给 str_to_var
    static const std::array<Variant, D> &get();
};
// 实现骨架（thunks/ 手写一次，codegen 只填表）：
static const std::array<Variant, D> &MethodDefaults<H>::get() {
    static const auto v = internal::parse_defaults(
        std::array{ DefaultEntry{VT_STRING, "res://"},
                    DefaultEntry{VT_VECTOR2, "Vector2(0, 0)"}, ... });
    return v;
}
```

`internal::parse_defaults` 内部逐条执行与 api_tool_parser.cpp 完全相同的
规则（STRING 直取；其余 `str_to_var`），**解析逻辑单一事实源**——两条绑定
路径的默认值语义不可能分叉。

要点：
- **缓存形制**：函数局部 static（magic static），每方法仅首次缺参调用时解析
  一遍，此后 O(1) 取缓存；与指针缓存的线程安全模型一致
- **下标是编译期常量**：§4.0-A 展开后第 i 参缺失即 `defaults[i − M]`，i 与
  M 都是编译期值，省掉动态路径每次调用的
  `index − (argc − default_size)` 运行期算术（api_tool_types.h:180 同款公式）
- **跳过类型检查**：默认值源自官方 json、按声明类型构造，必然匹配，不走
  can_convert_strict
- **与 ptrcall 正交**：补齐发生在编组阶段，补齐后参数缓冲完整；V1 保守策略
  不变（含默认值方法留 Variant 路径），P4 放宽 ptrcall 覆盖面时可直接纳入
- **失败行为与动态路径一致**：str_to_var 解析失败得 NIL，两条路径同源同
  结果，由 §13.C 双路径对照天然覆盖

## 5. 无哈希实体的静态化策略（汇总）

| 实体 | 引擎侧取指针的键 | 方案 |
|---|---|---|
| 内置方法 | hash（+ 类型与方法名 StringName） | §4.1 |
| 类方法 | hash（+ 类名/方法名 StringName） | §4.3 |
| utility 函数 | hash（+ 函数名 StringName） | §4.2 |
| 构造函数 | **(类型, 重载 index)**——构造函数没有 hash，`variant_get_ptr_constructor(VT, ctor_index)` 第二参是 json 重载序号 | `constructor_thunk<VT, CtorIndex>`，写入未初始化 Variant；重载选择沿用现有「按参数个数+严格类型匹配」逻辑，候选表由生成器静态发射 |
| ~~析构~~ | —— | **不处理**（已核实：全库无 `variant_get_ptr_destructor` 调用点；`has_destructor` 仅存档从未消费。内置值实例即堆上 `godot::Variant*`，GC finalizer 经 `Environment::dealloc_variant` → `variant_allocator_.free` 析构，无需插桩） |
| 内置成员属性 | (类型, 成员名 StringName)，无 hash | §4.4-A |
| 对象类索引属性 | getter/setter 方法 hash + index | §4.4-B |
| 运算符 | `(op, left_type, right_type)` 三个引擎枚举 | §4.5：三枚举直接作模板实参 |
| 全局常量/枚举 | 纯数据 | 保持 api_tool 动态读取（需求允许）；后续可选生成 constexpr 映射表 |

## 6. 代码生成器

- 位置：`tools/static_binding_codegen.py`（纯标准库：json + 文本模板，零依赖）
- 输入：`project/extension_api.json`；可选 `--extension-only`（见 §8）
- 输出到 `src/static_binding/gen/`：

```
gen/
├── string_names.gen.h        # 类名/方法名/成员名 StringName 引用表（集中惰性初始化）
├── builtin_methods.gen.h     # 按 builtin 类型分组：方法 thunk（A/B 两族实例化）
├── class_methods.gen.h       # 按 class 分组（MethodBind 惰性获取）
├── utility_functions.gen.h
├── constructors.gen.h        # (VT, ctor_index) 键
├── properties.gen.h          # §4.4-A 内置成员 + §4.4-B 对象索引属性 accessor
├── operators.gen.h           # §4.5 运算符 thunks（三枚举组合全量发射）
├── default_values.gen.h      # 各方法默认值表：(声明类型, 原始串)，运行时惰性解析（§4.6）
└── registry.gen.h/.cpp       # 类级分派表 + hash→thunk 入口
```

- 分派策略：类内 `switch (method_hash)`（整型 switch 编译器优化良好），
  类间再按类名/类 hash 一级分派；避免全量扁平巨型 switch
- 生成物提交入库（可 review、CI 可校验新鲜度：`codegen --check` 比对无 diff）
- **对账自检**（详见 §13.A）：生成结束时输出实体计数清单并与 json 逐一核对

## 7. 注册与回退（混合模式）

```
类首次加载（module loader 构建 wrapper 时）：
    for each method/member/property/operator:
        if (auto *entry = static_binding::find(class_id, key))
            → 挂静态 thunk（FunctionCallback 或 accessor 描述符）
        else
            → 【回退可见性检查，见 §7.1】
            → 挂现行动态闭包（api_tool 查询路径，行为与 main 一致）
```

### 7.1 回退可见性（miss 必须留痕）

**回退到动态绑定之前**，先判定宿主实体的 API 来源并输出日志——
APIType 取自 `ApiClass::api_type`（`godot::ClassDB::APIType`，api_tool 已随
类数据存储）：

```
APIType ∉ {API_EXTENSION, API_EDITOR_EXTENSION}（引擎核心/编辑器内置类接口）:
    WARN_PRINT_ONCE(
        "static binding not found: %s.%s [%s], falling back to dynamic binding",
        类名, 实体名(方法/属性/运算符), api_type 名);
    —— 引擎类接口未命中静态绑定是**预期行为**（不在生成范围内）。输出警告的
       定位是**排查线索**：日后怀疑某处功能不完整或性能不符预期时，可据此日志
       快速回答「哪些调用实际走了动态路径」，不必翻生成清单人工比对

APIType ∈ {API_EXTENSION, API_EDITOR_EXTENSION}（本项目扩展类接口）却 miss:
    ERR_PRINT_ONCE(同上格式);
    —— 主战场接口缺失意味着生成物与引擎不同源或注册表损坏，属缺陷级
       （ERROR 已评审定稿）
```

要点：
- 警告内容必须**指名道姓**：哪个类、哪个实体、什么 APIType、做了什么决定
- **为什么不会刷屏（无需去重设计）**：static/fallback 分配决策只发生在
  **注册期**（类首次加载、构建 wrapper 时），每个实体一生只决策一次；此后
  每次 JS 调用直接进入已挂好的回调，不存在重复查找，日志天然单条。宏本身的
  ONCE 语义（内部 static 标记）是第二重保险，与 `api_tool_types.h` 中指针
  加载失败路径的既有处理（L141/244/288/603 `ERR_PRINT_ONCE`）完全同款
- 该日志流同时是 §13.B 注册期审计的数据源（单测断言「警告集合 == 预期回退
  集合」）

其余回退触发条件：
1. `static_binding=no`（宏隔离，注册表整体不存在，无日志问题）
2. `--extension-only` 模式未包含该类（引擎内置类走动态，逐条按 §7.1 记录）
3. 引擎版本与生成物不同源 → 按 hash/index 取指针返回空 → 回退 + 按 §7.1 记录

原则不变：**任何 miss 都必须落到动态路径**——静态绑定只做加速、不做功能裁剪；
但每一次 miss 都必须在日志里留下「丢了什么」的记录。

## 8. api_tool 扩展类过滤（`--extension-only`）

- `api_tool_editor` generate 阶段新增可选开关：仅保留
  `APIType ∈ {API_EXTENSION, API_EDITOR_EXTENSION}` 的**类文档**
- 语义限定为「类文档过滤」：builtin 类型与 utility 文档**仍保留**——
  §4.1/§4.2/§4.5 的 thunk 依赖其签名与枚举；进一步瘦身另立开关
- 用途：静态绑定模式下减小 store 体积、加快加载；主战场是扩展类（引擎类）
- CI/构建联动：`static_binding=yes` 时若仓库无 extension_api.json → 报错
  并给出两条引导命令（dump → generate --extension-only）

## 9. 与既有系统的交互

| 交互点 | 策略 |
|---|---|
| ApiLoader 文档缓存 | 静态命中后短路动态解析；两套状态不混用 |
| 对象包装器（InstanceHandle）/ 值类型包装 | 不变；thunk 取实例的方式与现有一致；析构链路（GC finalizer → Variant 析构）零改动 |
| 调试器/反射（列出成员） | 仍走 api_tool 文档——静态绑定只改「怎么调」，不改「有哪些」 |
| 热重载 / 引擎版本变化 | 生成物随 extension_api.json 重生成；miss 回退兜底 + §7.1 日志 + 警告 |
| 多线程 | 函数指针缓存用函数局部 static（首调线程安全）；SN 表在 CORE init 后惰性建立 |

## 10. 目录结构

```
src/static_binding/
├── gen/                      # 生成物（提交入库）
└── thunks/                   # 手写脚手架：thunk 公共骨架（argc 校验、§4.0 编组规则、
                              #   isolate scope、异常→JS 异常转译）
tools/static_binding_codegen.py
```

（不设独立 convert 目录——转换能力统一收敛在现有 `jsb_type_convert.*`，见 §3.1）

## 11. 实施计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0** | 分支/宏/SConstruct 接线 + codegen 骨架（仅生成表格，不接线） | `static_binding=no` 产物与 main 一致；`=yes` 可编译；§13.A 对账清单零缺口 |
| **P1** | builtin 方法 + utility thunks 上线（§4.0 固定/vararg 两族编组 + 默认值常量 + 回退接线含 §7.1 日志） | §13.C 双路径对照全过；§13.D 错误路径用例全过 |
| **P2** | 类方法 thunks + 首载注册钩子 + `--extension-only` | 同上 |
| **P3** | 属性访问器：§4.4-A 内置成员 + §4.4-B 对象索引属性（含调用点改造） | §13.C/D 属性部分全过（含 index≥0 属性族往返） |
| **P4** | 运算符/构造 thunks；（条件触发）TypeConvert 内直连模板 + ptrcall | §13.C 运算符全组合对照全过；基准报告 |
| **P5** | 文档完善、CI 加 `static_binding=yes` leg（跑现有测试 + §13 生成测试） | CI 绿 |

## 12. 风险与开放问题

1. **hash 随引擎版本整体变化**：生成物必须与引擎同源重生成；跨版本 miss
   靠回退兜底 + §7.1 日志（不 crash）
2. **二进制体积**：全量实例化约数千个 thunk；P4 基准测量后决定是否按项目实际
   用到的子集裁剪（配合 `--extension-only` 与白名单）
3. **StringName 初始化顺序**：SN 表必须在 GDExtension CORE init 之后建立；
   生成器只发射引用，运行时惰性构造（builtin 方法/utility 取指针同样依赖
   方法名 StringName，故 SN 表是全局前置件）
4. ~~默认值翻译正确性~~（已消解）：核实 json 默认值为字符串字面量且
   api_tool 本就经 `str_to_var` 解析（§4.6）；静态路径复用同一函数，无第二套
   翻译器可出错。残余风险仅为「解析结果与动态路径不一致」，由 §13.C 对照
   测试守护
5. **导出模板**：`EDITOR_EXTENSION` 过滤与导出兼容性需在 P2 验证
6. **对照测试的豁免清单膨胀**：需引擎上下文/有随机性的实体不可避免要豁免，
   清单是入库数据文件，PR 评审把关，防止「测不了」悄悄变成「不测」

## 13. 测试与验证设计

目标：回答两个问题——**生成的静态绑定是否完整**（该有的一个不少）、
**每个调用是否正常**（每个实体都有行为等价证据）。

### A. 生成期完整性（codegen 自检，硬门禁）

生成器结束时对账并输出报告，任何一项不平即**退出码非零**：

```
builtin_methods:   json 中每个 (type, name, hash) 都有 thunk 实例与注册项
class_methods:     每个 (class, name, hash) 同上（虚函数/纯定义除外，见豁免规则）
utility_functions: 每个 (name, hash)
constructors:      每个 (type, ctor_index)
operators:         每个 (left_type, op, right_type)
members:           每个 (type, member_name)
indexed_props:     每个 (class, prop, index≥0)
default_values:    每个 default_arguments 条目
输出 manifest.gen.json（各类计数 + 全实体键列表），提交入库供 diff 审查
```

豁免规则显式化：不可静态化实体（如无 hash 的虚函数定义）由生成器写入
`exemptions.gen.json` 并说明原因，而非静默跳过——**缺失必须可见**。

### B. 注册期完整性（C++ 单测）

- **同源交叉验证**：单测同时持有静态注册表与 api_tool store（同一 json 的两
  个独立实现），遍历 store 断言每个应静态化的实体都能在注册表命中；反向断言
  注册表中不存在 json 之外的幽灵条目
- **回退分布审计**：测试模式下记录每类包装构建时 static/fallback 的分配结果，
  断言 fallback 集合 == 豁免清单（多一个少一个都红）
- **回退日志审计**（§7.1 联动）：捕获引擎日志流（测试模式重定向/落盘解析），断言 WARNING/ERROR 集合
  与 fallback 分布一致——「说了要回退的」和「真的回退了的」一一对应

### C. 行为等价性（核心：双路径对照测试）

**原则：凡是能静态化的可调用实体，都要有一次「静态路径 vs 动态路径」的同参
对照执行，结果 Variant 级相等。**

- codegen 附带产出对照测试驱动（`tests/gen/parity_*.gen.cpp` 或 .ts，随阶段
  逐步启用）：
  - 样本值合成器：按 `Variant::Type` 给规范样本（含边界：0/1/-1/空串/典型
    结构体值），object 参数用白名单安全类的默认实例
  - 对每个实体：合成合法参数 → 分别经静态 thunk 与动态路径调用 →
    返回值/抛错行为/对象副作用状态三者比对
- 显式豁免清单（数据文件入库评审）：需要引擎上下文（场景树、渲染、物理）、
  有随机性/时间依赖、纯写副作用无法观测的实体；**豁免即登记，不允许静默**
- 属性：accessor 写后读往返 + 与动态 get/set 路径对照（重点覆盖 index≥0 族）
- 运算符：全组合对照 `Variant::evaluate` 结果（引擎参考实现）

### D. 错误路径与编组规则用例（手写，覆盖 §4.0 语义）

两族编组 × 四类异常输入矩阵：

```
缺参且有默认值 → 补齐生效（断言默认值确实被使用）
缺参且无默认值 → 抛异常（信息含方法名与期望个数区间）
实参类型不符   → 抛异常（信息含参数序号、JS 实际类型、期望 Godot 类型）
argc 越界      → 抛异常
vararg 固定段错型 → 抛；尾段任意类型 → 收集成功
```

外加：构造函数各重载 index 构造后字段抽查；含默认值方法做「缺参静态 vs
缺参动态」对照（断言两侧补入相同默认值）；`codegen --check` 新鲜度门禁
（CI 中改 json 未重生成即红）。

### E. CI 接线（P5）

- `static_binding=yes` leg：编译 + 跑**现有全套测试**（动态基线已在另一条
  leg 上保持绿色）+ §13.B/C/D 测试 → 两腿同为绿即行为一致性证据
- `manifest.gen.json` / `exemptions.gen.json` 进 `codegen --check` 校验范围

## 14. 验收标准

- `static_binding=no`：与 main 行为逐位一致（宏隔离 + 源集隔离，无新符号进产物）
- `static_binding=yes`：现有测试全过 + §13 全部门禁过（A 对账零缺口、B 注册
  审计零偏差含回退日志一致性、C 对照测试除显式豁免外全过、D 错误路径矩阵全过）
- P4 基准：热点调用（Node.get_child / Vector2 加法 / utility abs / Vector2.x
  访问）较动态路径的每次调用开销有可测量改善（指标以基准报告为准）
