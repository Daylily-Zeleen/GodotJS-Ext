# 静态绑定 class 族 thunk 共用化 — design.md

> 依据：`.trellis/tasks/09-11-static-binding-size-reduction/prd.md`（父，2026-09-12 勘误后收敛版）+ 本任务 prd.md。形态 shared 仅在 `JSB_WITH_STATIC_BINDINGS + JSB_WITH_SHARED_THUNKS` 下编译；形态 A 代码路径零改动。

## 1. 架构总览

### 1.1 调用数据流（shared 形态）

```
JS obj.method(a, b)
  → v8 shared_class_method_thunk<IsStaticC, RetT, ArgsT...>（按签名去重的共享实例）
  → info.Data().As<External>()->Value() → const SharedClassMethodData &md
  → argc 预检查：md.min_argc ≤ provided ≤ sizeof...(ArgsT)
      （M 预检查承接 release 引擎无缺参防御的 OOB 安全职责，证据链见父 PRD）
  → produce_variant 编组仅 I < provided 的实参（缺参位留 NIL 槽，引擎 MethodBind 补默认值）
  → object_method_bind_call(md.method_bind.load(relaxed), instance, arg_ptrs, provided, ...)
  → translate_return<RetT>
```

### 1.2 新增/改动文件

| 文件 | 改动 |
|---|---|
| `SConstruct:36,820-836` | `static_binding` BoolVariable → `binding_mode` EnumVariable（static\|shared\|dynamic，默认 static）；宏注入 + codegen 传参 |
| `src/static_binding/thunks/shared_class_methods.h` | 新增：`SharedClassMethodData` + 共享 thunk 模板（体迁移自 `class_methods.h:66-125`） |
| `src/static_binding/dispatch.h` | 增 `JSB_WITH_SHARED_THUNKS` 分支：`SharedClassMethodBinding` 类型 + `find_class_method_binding()` 声明 |
| `misc/build/static_binding_codegen.py` | `--binding-mode` 参数；shared 发射：签名去重 → 共享 thunk 单点实例化 + 每类绑定表 + find 函数 |
| `src/runtime/bridge/jsb_object_bindings.cpp:145` | 挂载：`JSB_WITH_SHARED_THUNKS` 分支（eager 解析 + data 挂载 + 失败回退） |
| `src/runtime/bridge/jsb_bridge_module_loader.cpp` | `STATIC_BINDING_ENABLED` → `BINDING_MODE` 三态字符串 |
| `project/tests/benchmark/benchmark.ts`、`misc/bench_matrix.py` | `staticBinding` 三态适配 / `--leg shared` |

## 2. 数据契约

### 2.1 每方法描述符与绑定表（codegen 发射，DLL .data 静态存储）

```cpp
struct SharedClassMethodData {
    std::atomic<GDExtensionMethodBindPtr> method_bind; // 挂载期 eager 解析写入（relaxed）
    const char *class_name;  // 报错文案
    const char *method_name; // 报错文案
    int32_t min_argc;         // 原 M = N - D（缺参检查下界）
};

struct SharedClassMethodBinding {
    const char *name;        // godot 方法名（挂载键，与现状 find 相同：hash switch + name 比对）
    uint32_t hash;
    ThunkFn thunk;            // 共享签名 thunk 实例地址
    SharedClassMethodData data;
};
// 每类一张表；find_<Class>(name, hash) 返回 const SharedClassMethodBinding*
```

- `method_bind` 用 `std::atomic`（relaxed）：worker 线程各自 Environment 独立挂载同一静态表，并发写**同值**需要良定义；thunk 读取同样 relaxed load。单字对齐 load/store 在 x64/ARM64 零额外代价
- 表为非 const 静态数组（`.data` 而非 `.rodata`——`method_bind` 需可写）；挂载期零堆分配

### 2.2 共享 thunk 模板

```cpp
template <bool IsStaticC, class RetT, class... ArgsT>
void shared_class_method_thunk(const v8::FunctionCallbackInfo<v8::Value> &info);
```

- 函数体 = 现 `class_method_thunk`（`class_methods.h:66-125`）**逐行等价迁移**，仅三处替换：
  1. `resolve_class_method<HashC, ClassLit, NameLit>()`（magic static）→ `md.method_bind.load(std::memory_order_relaxed)`；空 → 报错抛异常（文案用 `md.class_name`/`md.method_name`）
  2. 编译期 `M` → `md.min_argc`
  3. `ClassLit`/`NameLit` 报错字面量 → `md.class_name`/`md.method_name`
- `IsStaticC` 保留模板参数（见 §3 权衡）
- 形态 A 的 `class_method_thunk` 模板保留不删（形态 A 继续用）；shared 构建下 gen 不再引用它，未实例化零成本

### 2.3 codegen（`--binding-mode shared`）

- 签名收集：全部 class 方法（fixed-arity）按 `(IsStatic, Ret, ArgTypes...)` 去重，排序键固定保证输出确定性
- 发射顺序：共享 thunk 单点实例化（按去重签名）→ 每类 `SharedClassMethodBinding` 表 → `find_<Class>` 函数（模式同现状：hash switch + name 比对，返回 binding 指针）
- **自检锚点**：`--binding-mode static` 的产物必须与改造前 codegen 输出逐字节一致（形态 A 零改动的可验证判据）；class_vararg 族本任务不动（子任务 3），shared 模式下仍按形态 A 发射
- 同名 gen 文件（`dispatch_class.gen.cpp`）按模式重生成（SCons 每次构建自动跑 codegen，见 spec `scons-build.md:25`），无互踩

### 2.4 挂载端（eager，`jsb_object_bindings.cpp:145`）

```cpp
#if JSB_WITH_STATIC_BINDINGS
#ifdef JSB_WITH_SHARED_THUNKS
    if (const auto *binding = jsb::static_binding::find_class_method_binding(
                p_class_name, method_info.method.name, method_info.hash)) {
        // eager：挂载点 name+hash 已在手（class_methods.h:41-47 的解析入口）
        auto mb = jsb::static_binding::thunks::resolve_class_method_bind(...);
        if (mb) {
            binding->data.method_bind.store(mb, std::memory_order_relaxed);
            static_builder.Method(member_name, binding->thunk, (void *)&binding->data);
            continue;
        }
        JSB_LOG(Warning, "static binding resolve failed: %s.%s, falling back to dynamic", ...);
    }
#else
    // 现状形态 A 路径原样保留（find_class_method_thunk → Method(name, thunk)）
#endif
#endif
```

- 解析失败（引擎版本漂移等）→ 落入既有 dynamic 路径，与 `jsb_primitive_bindings.cpp:816` 回退模式一致
- `Method(name, callback, data)` 的 `Data<void*>` → `External` 特化四引擎 impl 均已存在（v8：`jsb_v8_class_builder.h:54-56`；jsc/quickjs/web 同构，前轮 grep 核实）

## 3. 权衡记录

- **External(void\*) vs int32 索引**：选 External——描述符是 codegen 静态存储，索引徒增一层全局表间接；reflect 先例同构（`jsb_reflect_binding_util.h:208`），dynamic 路径的 int32 collection_index 是运行时构建数组的下标（场景不同）
- **IsStaticC 折叠**：拒绝——实测去重 1,520→1,486 仅省 34 实例（2.2%），热路径每调用多一分支不值
- **method_bind 原子 relaxed**：并发写同值的良定义，无锁零代价；不引入 per-Environment 拷贝（表共享，绑定期幂等写）
- **签名重叠注记**：class_method 与 class_vararg 有 9 个 `(IsStatic, Ret, Args)` 键重叠，但模板名不同不合并——class ≤1,530 与 class_vararg ≤10 分别计数，无歧义

## 4. 回滚

- `binding_mode=static`（默认）即回形态 A；gen 每次构建重生成，无状态残留
- 实施分步独立提交（见 implement.md），任一步可单独回退

## 5. 风险

- shared 版 gen 体积：表数据约 48B/方法 × 15k ≈ 700KB 源码，远小于现状 15k 段模板实例化文本——预期净减；实测记录进报告
- 查询 miss 语义：`find_class_method_binding` 返回 nullptr（表不含该方法）→ 回退 dynamic，与形态 A 的 `find_class_method_thunk` miss 语义一致
- `EnumVariable` 需在 SConstruct 顶部补 import（现状只 import BoolVariable）
