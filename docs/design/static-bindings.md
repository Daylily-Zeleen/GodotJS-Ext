# 静态绑定（Static Bindings）设计方案

> 分支：`feature/static-bindings` · 状态：**设计稿（未实施）**
> 目标：将 JS→Godot 的函数查找从「每次调用的名称解析」降为「编译期已知哈希的一次性指针获取 + 参数编组」

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
constructor / utility function 都携带官方 **hash**。GDExtension 接口本身
支持按 hash 直接取原生函数指针。静态绑定 = 构建期把 hash 烧进 C++ 模板
实参，运行时首次调用惰性取指针并缓存，之后每次调用只剩参数编组。

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
│ JS 调用 → thunk<Hash>(info)                               │
│   ├─ 首次：gdextension_interface 按 Hash 取原生函数指针    │
│   │        （函数局部 static 缓存，线程安全）               │
│   ├─ 参数：js_to_cpp<T> 直连转换（concepts 分派）          │
│   ├─ 调用：Variant 数组路径（V1）/ ptrcall（V2，后期）     │
│   └─ 返回：cpp_to_js<R>                                   │
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
- CI 矩阵后续增加一条 `static_binding=yes` 的 leg 做守护

## 3. 类型转换层重构（TypeConvert v2）

### 3.1 分层

```
Level-0（现有）：gd_var_to_js / js_to_gd_var     —— Variant 为中心，对外签名不变
Level-1（新增）：js_to_cpp<T> / cpp_to_js<T>     —— 绕过 Variant 的直连模板族
```

Level-0 内部改为委托 Level-1（经 `Variant::from<T>()` / `Variant::operator T()`），
行为完全不变；静态绑定 thunk 只依赖 Level-1。

### 3.2 Traits 与 Concepts（C++20）

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

### 3.3 直连转换示例

```cpp
template <GdFundamental T>
T js_to_cpp(v8::Isolate *isolate, v8::Local<v8::Value> v) {
    return static_cast<T>(v->NumberValue(isolate).FromMaybe(0));
}
template <> String js_to_cpp<String>(...)    // v8::String → UTF8 → String
template <> Vector2 js_to_cpp<Vector2>(...)  // v8::Object → x/y 属性读取
...
```

覆盖清单（与 `Variant::Type` 一一对应）：bool / int / float / String /
StringName / NodePath / Vector2(2i,3,3i,4,4i) / Rect2(2i) / Transform2D /
Vector3(3i) / Transform3D / Basis / Quaternion / Plane / AABB / Color /
Projection / RID / Object / Callable / Signal / Dictionary / Array /
PackedByteArray|Int32Array|Int64Array|Float32Array|Float64Array|
StringArray|Vector2Array|Vector3Array|ColorArray

Object 类走既有实例包装器往返；Packed 数组后续优先对接 TypedArray/
ArrayBuffer 视图减少拷贝（P2+ 优化点）。

## 4. 函数 Thunk 模板族（核心）

统一形制——**编译期键作为模板实参，运行时惰性取原生指针**：

### 4.1 内置类型方法（有 hash）

```cpp
template <GDExtensionVariantType VT, uint64_t MethodHash>
void builtin_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    static const auto fn = gdextension_interface::variant_get_ptr_builtin_method(VT, MethodHash);
    // magic static：并发首调安全；指针进程级有效

    constexpr int N = kArgCountForThisMethod;      // 生成器发射定长数组
    godot::Variant args[N];
    for (int i = 0; i < info.Length(); ++i)
        args[i] = js_to_gd_var(info.GetIsolate(), info[i]);   // Level-1 直连

    godot::Variant base = js_to_gd_var(info.This());
    godot::Variant ret;
    fn(&base, args, &ret, info.Length());          // 内置方法按 argc 支持默认参数

    info.GetReturnValue().Set(cpp_to_js(info.GetIsolate(), ret));
}
```

### 4.2 全局 utility 函数（有 hash）

与 4.1 同构：`variant_get_ptr_utility_function(MethodHash)`，无 base 参数。

### 4.3 类方法（有 hash）

需要 StringName：启动期（CORE init 之后）建立**类名/方法名 SN 缓存表**，
由生成器发射引用（`string_names.gen.h`），运行时惰性构造。

```cpp
template <uint64_t ClassId, uint64_t MethodHash>
void class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    static GDExtensionMethodBind *mb = classdb_get_method_bind(
        SN_TABLE.get<ClassId>(), SN_TABLE.get<MethodHash>(), MethodHash);
    // V1：Variant 路径（支持默认参数）
    //   object_method_bind_call(mb, instance, args, argc, &ret, &err)
    // V2：无默认参方法 → object_method_bind_ptrcall（typed 解包，零 Variant）
}
```

**V1/V2 分期**：静态化的主要收益来自**跳过查找**，编组优化是第二阶段。
V2 由生成器按「无默认参数 && 全参数类型已知」标记自动选择 ptrcall。

### 4.4 索引属性（无 hash，用 index）

现状：`Vector2.x` 这类带索引的属性走普通函数调用、把 index 当运行时参数。
静态化后直接把 index 烧进模板：

```cpp
template <GDExtensionVariantType VT, int Index>
void indexed_getter_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
    static const auto fn = variant_get_ptr_indexed_getter(VT, Index);
    godot::Variant ret; fn(&base, &ret);
    info.GetReturnValue().Set(cpp_to_js(...));
}
template <GDExtensionVariantType VT, int Index>
void indexed_setter_thunk(...);   // variant_get_ptr_indexed_setter
```

注册侧同步变化：类包装构建时这类成员注册为 **v8 accessor 属性**
（getter/setter 描述符）而非 Function——JS 侧 `v.x` 直接触发。

## 5. 无哈希实体的静态化策略（运算符 / 构造 / 成员）

| 实体 | api json 中的键 | 静态化方案 |
|---|---|---|
| 内置方法 | hash | §4.1 |
| 类方法 | hash | §4.3 |
| utility 函数 | hash | §4.2 |
| 构造函数 | hash（每个重载一条） | 同 §4.1 变体：`variant_get_ptr_constructor(VT, H)`，写入未初始化 Variant |
| 析构 | type 级 | `variant_get_ptr_destructor`，挂包装器释放路径（一次性） |
| 索引成员 | (type, index) | §4.4 |
| 非索引成员 | 以 getter/setter 方法形式存在于 methods[]（有 hash） | 归入 §4.3 |
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
├── string_names.gen.h        # 类名/方法名 StringName 引用表（集中惰性初始化）
├── builtin_methods.gen.h     # 按 builtin 类型分组的 thunk 实例化
├── class_methods.gen.h       # 按 class 分组（MethodBind 惰性获取）
├── utility_functions.gen.h
├── constructors.gen.h
├── indexed_props.gen.h       # 索引 getter/setter
├── operators.gen.h           # 运算符三元组 thunks
└── registry.gen.h/.cpp       # 类级分派表 + hash→thunk 入口
```

- 分派策略：类内 `switch (method_hash)`（整型 switch 编译器优化良好），
  类间再按类名/类 hash 一级分派；避免全量扁平巨型 switch
- 生成物提交入库（可 review、CI 可校验新鲜度：`codegen --check` 比对无 diff）

## 7. 注册与回退（混合模式）

```
类首次加载（module loader 构建 wrapper 时）：
    for each method/member:
        if (auto *thunk = static_binding::find(class_id, member_hash))
            → 挂静态 thunk（FunctionCallback 或 accessor）
        else
            → 挂现行动态闭包（api_tool 查询路径，行为与 main 一致）
```

回退触发条件：
1. `static_binding=no`（宏隔离，注册表整体不存在）
2. `--extension-only` 模式未包含该类（引擎内置类走动态）
3. 引擎版本与生成物不同源 → 按 hash 取指针返回空 → 回退 + 警告日志

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
| 对象包装器（InstanceHandle） | 不变；thunk 取实例的方式与现有一致 |
| 调试器/反射（列出成员） | 仍走 api_tool 文档——静态绑定只改「怎么调」，不改「有哪些」 |
| 热重载 / 引擎版本变化 | 生成物随 extension_api.json 重生成；miss 回退兜底 + 警告 |
| 多线程 | 函数指针缓存用函数局部 static（首调线程安全）；SN 表在 CORE init 后惰性建立 |

## 10. 目录结构

```
src/static_binding/
├── gen/                      # 生成物（提交入库）
├── convert/                  # TypeConvert v2：concepts + traits + 直连模板
└── thunks/                   # 手写脚手架：thunk 公共骨架（argc 校验、
                              #   isolate scope、异常→JS 异常转译）
tools/static_binding_codegen.py
```

## 11. 实施计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0** | 分支/宏/SConstruct 接线 + codegen 骨架（仅生成表格，不接线） | `static_binding=no` 产物与 main 一致；`=yes` 可编译 |
| **P1** | TypeConvert v2 直连层 + concepts；存量 Level-0 改为委托 | 行为等价，现有测试全过 |
| **P2** | builtin 方法 + utility thunks 上线（含回退接线） | 专项测试：每类绑定至少一条往返用例 |
| **P3** | 类方法 thunks + 首载注册钩子 + `--extension-only` | 同上 |
| **P4** | 索引属性真·访问器化 + 运算符/构造 thunks | 同上 |
| **P5** | 基准测试（动态 vs 静态 vs ptrcall）、文档完善、CI 加 `static_binding=yes` leg | 性能报告 + CI 绿 |

## 12. 风险与开放问题

1. **hash 随引擎版本整体变化**：生成物必须与引擎同源重生成；跨版本 miss
   靠回退兜底 + 警告（不 crash）
2. **二进制体积**：全量实例化约数千个 thunk；P5 测量后决定是否按项目实际
   用到的子集裁剪（配合 `--extension-only` 与白名单）
3. **类方法默认参数**：Variant 路径天然支持；ptrcall 不支持 → 含默认参数的
   方法留在 Variant 路径，或生成器展开默认值产出多份 thunk（P2 决策点）
4. **StringName 初始化顺序**：SN 表必须在 GDExtension CORE init 之后建立；
   生成器只发射引用，运行时惰性构造
5. **require-missing-module 的 SIGSEGV**（CI 单测中发现的独立 bug）：与本
   设计无关，但需单独修复，避免干扰静态绑定的验证
6. **导出模板**：`EDITOR_EXTENSION` 过滤与导出兼容性需在 P3 验证
7. **运算符求值器的线程亲和性**：evaluator 指针进程全局有效 ✓；但缓存填充
   发生在哪个 isolate 无所谓（纯 C 指针）

## 13. 验收标准

- `static_binding=no`：与 main 行为逐位一致（宏隔离 + 源集隔离，无新符号进产物）
- `static_binding=yes`：现有测试全过 + 新增专项测试全过（覆盖 §4 各类绑定
  与回退路径）
- P5 基准：热点调用（Node.get_child / Vector2 加法 / utility abs）较动态
  路径的每次调用开销有可测量改善（具体指标以 P5 基准报告为准）
