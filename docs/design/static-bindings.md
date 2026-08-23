# 静态绑定（Static Bindings）设计方案

> 分支：`feature/static-bindings` · 状态：**设计稿 v2（未实施）**
> 目标：将 JS→Godot 的函数查找从「每次调用的名称解析」降为「编译期已知哈希的一次性指针获取 + 参数编组」
>
> v2 修订（评审意见落实）：转换层命名改为 `js_to_gd/gd_to_js` 且推迟到 ptrcall 阶段；
> 新增固定/可变参数两类编组规则；对象类索引属性静态化（index 进模板实参与调用点）；
> 内置成员属性与数组下标/键访问划清界限；删除析构处理与「require-missing-module SIGSEGV」（均经代码核实为多余/不存在）。

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
│   │        V1 复用 TypeConvert；ptrcall 阶段换 js_to_gd<T> │
│   └─ 返回：gd_to_js                                       │
│                                                           │
│ 静态注册表 miss → 回退现行动态绑定（功能无损兜底）          │
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
  用静态绑定产物编译并跑现有测试）做守护，防止该路径无人编译而腐烂（P5 落地）

## 3. 类型转换层（V1 复用现有；直连层随 ptrcall 引入）

### 3.1 分期结论

- **V1（Variant 路径）不新增任何转换层**：thunk 直接复用现有
  `TypeConvert::js_to_gd_var(isolate, ctx, val, type, out)` /
  `TypeConvert::gd_var_to_js(...)`（`src/runtime/bridge/jsb_type_convert.*`，
  Variant 中心，签名与行为均不变）
- **直连模板族推迟到 ptrcall 阶段（V2）**：仅当生成器把某方法标记为可
  ptrcall 时，才需要「不经 Variant」的按类型直取。届时新增：

```
js_to_gd<T>(isolate, v) -> T          // T 为 Godot 侧类型
gd_to_js<T>(isolate, val) -> v8::Local<v8::Value>
```

命名与既有 `js_to_gd_var` / `gd_var_to_js` 家族对齐（`gd` = Godot 类型），
**不用 `*_cpp_*`**——JS 包装对象本身也是 C++ 对象，"cpp" 无区分度。
Level-0 内部可选改为委托直连模板（纯实现细节，对外零影响）。

### 3.2 Traits 与 Concepts（C++20，随 V2 引入）

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

template <typename T>
concept GdArgFromJs = GdFundamental<T> || GdBuiltin<T>
                   || std::same_as<T, godot::String> || std::same_as<T, godot::StringName>
                   || std::same_as<T, godot::NodePath> || /* Callable/Signal/RID/Object */ ...;
```

### 3.3 直连转换示例（V2 时实现）

```cpp
template <GdFundamental T>
T js_to_gd(v8::Isolate *isolate, v8::Local<v8::Value> v) {
    return static_cast<T>(v->NumberValue(isolate).FromMaybe(0));
}
template <> String js_to_gd<String>(...)    // v8::String → UTF8 → String
template <> Vector2 js_to_gd<Vector2>(...)  // v8::Object → x/y 属性读取
...
// 反向：gd_to_js<T>(...)
```

覆盖清单（与 `Variant::Type` 一一对应）：bool / int / float / String /
StringName / NodePath / Vector2(2i,3,3i,4,4i) / Rect2(2i) / Transform2D /
Vector3(3i) / Transform3D / Basis / Quaternion / Plane / AABB / Color /
Projection / RID / Object / Callable / Signal / Dictionary / Array /
PackedByteArray|Int32Array|Int64Array|Float32Array|Float64Array|
StringArray|Vector2Array|Vector3Array|ColorArray

Object 类走既有实例包装器往返；Packed 数组后续优先对接 TypedArray/
ArrayBuffer 视图减少拷贝（优化点，随 V2 评估）。

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
    否则              → 填入烧进生成物的默认值常量（源自 extension_api.json）
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
§4.3 的 V2 ptrcall 仅适用于「非 vararg && 无默认值」的方法。

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
    // 返回：gd_to_js
}
```

### 4.2 全局 utility 函数（有 hash）

与 4.1 同构：`variant_get_ptr_utility_function(name_sn, hash)`（同样需要方法名
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
    // V2：「非 vararg && 无默认值」→ object_method_bind_ptrcall（typed 解包，零 Variant）
}
```

**对象类索引属性**（`ApiPropertyInfo::index ≥ 0`，见 §5）复用本节的
MethodBind 获取方式，另加 index 模板实参，见 §4.4-B。

### 4.4 属性访问器（三类，边界必须分清）

**A. 内置类型成员属性（如 `Vector2.x/y`）——无 hash、无 index**

对应 `ApiBuiltinClass.members`（`ApiMemberInfo`），引擎侧入口是按**成员名**
StringName 取的 setter/getter 函数指针：

```cpp
template <GDExtensionVariantType VT, uint64_t MemberId>
void builtin_member_get_thunk(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &) {
    static const auto fn = gdextension_interface::variant_get_ptr_getter(
        VT, sn_table.member_name<MemberId>());
    // base 由 this（值类型包装）取出；getter(base, &ret)；gd_to_js(ret)
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

## 5. 无哈希实体的静态化策略（运算符 / 构造 / 成员）

| 实体 | 引擎侧取指针的键 | 静态化方案 |
|---|---|---|
| 内置方法 | hash（+ 类型与方法名 StringName） | §4.1 |
| 类方法 | hash（+ 类名/方法名 StringName） | §4.3 |
| utility 函数 | hash（+ 函数名 StringName） | §4.2 |
| 构造函数 | **(类型, 重载 index)**——注意构造函数**没有 hash**，`variant_get_ptr_constructor(VT, ctor_index)` 的第二参是 json 里的重载序号 | 同 §4.1 变体：`constructor_thunk<VT, CtorIndex>`，写入未初始化 Variant；重载选择沿用现有「按参数个数+严格类型匹配」逻辑（`jsb_primitive_bindings_reflect.cpp` 构造分发），但候选表由生成器静态发射 |
| ~~析构~~ | —— | **不处理**（已核实：全库无 `variant_get_ptr_destructor` 调用点；`has_destructor` 仅存档从未消费。内置值实例即堆上 `godot::Variant*`，JS GC finalizer 经 `Environment::dealloc_variant` → `variant_allocator_.free` 析构，Variant 自身析构完成引擎侧清理，无需也无法在此插桩优化） |
| 内置成员属性 | (类型, 成员名 StringName)，无 hash | §4.4-A |
| 对象类索引属性 | getter/setter 方法 hash + index | §4.4-B |
| **运算符** | **无 hash**，但每条 operator 明确列出 `(op, left_type, right_type)` | **三元组即编译期键**：`operator_thunk<LeftVT, Op, RightVT>`，首次调用 `variant_get_ptr_operator_evaluator(Op, Left, Right)` 惰性取指针缓存 |
| 全局常量/枚举 | 纯数据 | 保持 api_tool 动态读取（需求允许）；后续可选生成 constexpr 映射表 |

> 运算符的关键认知：不需要把动态查找变成编译期函数体，只需要把「每次调用
> 都查 evaluator 指针」变成「每个 (left, op, right) 组合只查一次」。三元组
> 模板实例化天然实现这一点——api json 已枚举全部组合，生成器可全量发射，
> 无需哈希。

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
├── operators.gen.h           # 运算符三元组 thunks
├── default_values.gen.h      # 各方法默认值常量（§4.0-A 补参用）
└── registry.gen.h/.cpp       # 类级分派表 + hash→thunk 入口
```

- 分派策略：类内 `switch (method_hash)`（整型 switch 编译器优化良好），
  类间再按类名/类 hash 一级分派；避免全量扁平巨型 switch
- 生成物提交入库（可 review、CI 可校验新鲜度：`codegen --check` 比对无 diff）

## 7. 注册与回退（混合模式）

```
类首次加载（module loader 构建 wrapper 时）：
    for each method/member/property:
        if (auto *thunk = static_binding::find(class_id, key))
            → 挂静态 thunk（FunctionCallback 或 accessor 描述符）
        else
            → 挂现行动态闭包（api_tool 查询路径，行为与 main 一致）
```

回退触发条件：
1. `static_binding=no`（宏隔离，注册表整体不存在）
2. `--extension-only` 模式未包含该类（引擎内置类走动态）
3. 引擎版本与生成物不同源 → 按 hash/index 取指针返回空 → 回退 + 警告日志

原则：**任何 miss 都必须落到动态路径**——静态绑定只做加速、不做功能裁剪。

## 8. api_tool 扩展类过滤（`--extension-only`）

- `api_tool_editor` generate 阶段新增可选开关：仅保留
  `APIType ∈ {API_EXTENSION, API_EDITOR_EXTENSION}` 的**类文档**
- 语义限定为「类文档过滤」：builtin 类型与 utility 文档**仍保留**——
  §4.1/§4.2 的 thunk 依赖其 hash 与签名；进一步瘦身另立开关
- 用途：静态绑定模式下减小 store 体积、加快加载；主战场是扩展类（引擎类）
- CI/构建联动：`static_binding=yes` 时若仓库无 extension_api.json → 报错
  并给出两条引导命令（dump → generate --extension-only）

## 9. 与既有系统的交互

| 交互点 | 策略 |
|---|---|
| ApiLoader 文档缓存 | 静态命中后短路动态解析；两套状态不混用 |
| 对象包装器（InstanceHandle）/ 值类型包装 | 不变；thunk 取实例的方式与现有一致；析构链路（GC finalizer → Variant 析构）零改动 |
| 调试器/反射（列出成员） | 仍走 api_tool 文档——静态绑定只改「怎么调」，不改「有哪些」 |
| 热重载 / 引擎版本变化 | 生成物随 extension_api.json 重生成；miss 回退兜底 + 警告 |
| 多线程 | 函数指针缓存用函数局部 static（首调线程安全）；SN 表在 CORE init 后惰性建立 |

## 10. 目录结构

```
src/static_binding/
├── gen/                      # 生成物（提交入库）
├── convert/                  # （V2/ptrcall 阶段）js_to_gd/gd_to_js 直连模板 + traits/concepts
└── thunks/                   # 手写脚手架：thunk 公共骨架（argc 校验、§4.0 编组规则、
                              #   isolate scope、异常→JS 异常转译）
tools/static_binding_codegen.py
```

## 11. 实施计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0** | 分支/宏/SConstruct 接线 + codegen 骨架（仅生成表格，不接线） | `static_binding=no` 产物与 main 一致；`=yes` 可编译 |
| **P1** | builtin 方法 + utility thunks 上线（§4.0 固定/vararg 两族编组 + 默认值常量 + 回退接线；转换复用现有 TypeConvert） | 专项测试：每类绑定至少一条往返用例；固定/可变参数各有缺参、错型、越界用例 |
| **P2** | 类方法 thunks + 首载注册钩子 + `--extension-only` | 同上 |
| **P3** | 属性访问器：§4.4-A 内置成员 + §4.4-B 对象索引属性（含调用点改造） | accessor 读写往返用例（含 index≥0 属性族） |
| **P4** | 运算符/构造 thunks；（可选）js_to_gd/gd_to_js 直连层 + ptrcall V2 | 同上 |
| **P5** | 基准测试（动态 vs 静态 vs ptrcall）、文档完善、CI 加 `static_binding=yes` leg | 性能报告 + CI 绿 |

## 12. 风险与开放问题

1. **hash 随引擎版本整体变化**：生成物必须与引擎同源重生成；跨版本 miss
   靠回退兜底 + 警告（不 crash）
2. **二进制体积**：全量实例化约数千个 thunk；P5 测量后决定是否按项目实际
   用到的子集裁剪（配合 `--extension-only` 与白名单）
3. **StringName 初始化顺序**：SN 表必须在 GDExtension CORE init 之后建立；
   生成器只发射引用，运行时惰性构造（builtin 方法/utility 取指针同样依赖
   方法名 StringName，故 SN 表是全局前置件）
4. **默认值常量生成正确性**：extension_api.json 中默认值为 JSON 标量
   （数字/布尔/字符串/null），生成器将其翻译为 `Variant(...)` 初始化表达式；
   翻译规则须与 api_tool 解析器一致，并用往返用例守护
5. **导出模板**：`EDITOR_EXTENSION` 过滤与导出兼容性需在 P2 验证
6. **运算符求值器的线程亲和性**：evaluator 指针进程全局有效 ✓；但缓存填充
   发生在哪个 isolate 无所谓（纯 C 指针）

## 13. 验收标准

- `static_binding=no`：与 main 行为逐位一致（宏隔离 + 源集隔离，无新符号进产物）
- `static_binding=yes`：现有测试全过 + 新增专项测试全过（覆盖 §4 各类绑定、
  §4.0 两族编组的缺参/错型/越界、§4.4 三类属性访问器与回退路径）
- P5 基准：热点调用（Node.get_child / Vector2 加法 / utility abs）较动态
  路径的每次调用开销有可测量改善（具体指标以 P5 基准报告为准）
