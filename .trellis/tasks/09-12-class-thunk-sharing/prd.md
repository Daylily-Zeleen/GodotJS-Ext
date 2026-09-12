# 静态绑定 class 族 thunk 共用化（形态 shared：签名共享）

> 父任务：09-11-static-binding-size-reduction（子任务 1，P1）。依赖关系：无前置，先行开工；本任务定型形态 shared 的 callback-data 管道，子任务 2/3 复用。

## Goal

在新编译形态 `binding_mode=shared` 下，消除 class_method_thunk 按方法重复实例化：15,370 个实例化 → **1,519** 个唯一签名（字符串感知解析器复核计数 `src/static_binding/gen/dispatch_class.gen.cpp`），每签名一个共享 thunk + 每方法数据经 v8 callback data（`info.Data()`）传入。**形态 A（`binding_mode=static`，现状）零改动**。

## Background（已核实事实）

- `class_method_thunk<HashC, ClassLit, NameLit, IsStaticC, RetT, ArgsT...>`（`src/static_binding/thunks/class_methods.h:65`）：身份参数仅用于 ① 懒解析 method bind（函数内 static，`:49-54`）② 报错文案；marshal 逻辑全部输入是签名参数
- **Def 剥离是形态 shared 的强制推论**（父任务裁决）：`Arg<T, FixedString Def>` 留 Def 则默认值不同的同签名方法无法共享。class gen 带默认值 Arg 1,715 处（唯一 (T,Lit) 对 125 个）；**字面量值在 class 路径运行时不可达**——唯一被消费的是 `has_default` → M 预检查。去重：1,686 → 1,519
- **引擎补默认值证据链（本地引擎源码 4.7-stable-2108-g9d6da1df74）**：
  - `core/extension/gdextension_interface.cpp:1335-1341`：`object_method_bind_call` → `mb->call(o, args, p_arg_count, error)`——argc 原样下传
  - `core/object/method_bind_common.h:82-86/166-170/260-264/355-359/436-438/502-504`：全部 `MethodBind::call` 变体统一走 `call_with_variant_args*_dv(..., get_default_arguments())`
  - `core/variant/binder_common.h:185-203`：`i >= p_argcount` 的参数位 `args[i] = &p_default_values[...]`——引擎补默认值（传共享存储指针，CoW 兜底）
  - 同文件 `:188-194`：`missing > dvs` 防御仅 `#ifdef DEBUG_ENABLED`——release 无检查、缺参越界读 OOB
  - **M 预检查（`class_methods.h:82-84`）因此是真实安全职责**——形态 B 的 min_argc 字段必须承接（不只是提前报错的 UX）
- **class 族不需要任何默认值存储**（父任务裁决）：thunk 只编组实参并传 `argc==provided`（`class_methods.h:96-108` fold 守卫只编组 `I < provided`；`:118-119` 传 provided），默认值全权引擎补；`produce_value` 默认值分支（`thunks_common.h:457-460`）class 调用点不可达
- **eager 解析（父任务裁决）**：绑定时（挂载点 `src/runtime/bridge/jsb_object_bindings.cpp:145`，name+hash 已在手）解析 `GDExtensionMethodBindPtr` 存 callback data；解析失败走既有 "falling back to dynamic binding" 回退（对照 `jsb_primitive_bindings.cpp:816` 模式）。先例：`ReflectBuiltinMethodPointerCall`（`jsb_reflect_binding_util.h:194-298`）
- 挂载侧支持已就绪：`impl::ClassBuilder::Method(name, callback, data)` 模板 data 重载在四引擎 impl 均存在（v8：`jsb_v8_class_builder.h:150`）
- 高频共享签名：`false, Ret<bool>` ×1,163、`false, RetVoid, Arg<bool>` ×933；class 族占 gen 源码 3.03/3.33 MB

## Requirements

- SConstruct：`static_binding`（BoolVariable，`SConstruct:36`）更名替换为 `binding_mode = static | shared | dynamic`（EnumVariable，默认 `static`）；同步迁移面：`.github/workflows/ci.yml:91,101`、`misc/bench_matrix.py:142`、spec `build/scons-build.md:25`、`cpp/generated-files.md:17`（干净切换不留别名）；宏注入：保留 `JSB_WITH_STATIC_BINDINGS`（static/shared 共用门控）+ 新增 `JSB_WITH_SHARED_THUNKS`（shared 门控）；bridge 的 `STATIC_BINDING_ENABLED` 暴露改三态字符串
- codegen（`misc/build/static_binding_codegen.py`）在 shared 形态下发射：共享 thunk 仅按 `(IsStaticC, RetT, ArgsT...纯类型)` 特化 + 每类一张方法表 {name, hash, 签名 id, min_argc}；形态 A 发射逻辑不动
- 每方法数据载体：codegen 发射的静态描述符 `SharedClassMethodData { std::atomic<GDExtensionMethodBindPtr> method_bind; const char *class_name; const char *method_name; int32_t min_argc; }`（DLL .data 存储）经 `v8::External` 挂载——`impl_private::Data<void*>` → External 通道四引擎 impl 均已存在（v8：`jsb_v8_class_builder.h:54-56`）；`method_bind` 用 atomic relaxed（worker 线程各自 Environment 挂载同一静态表，并发写同值需良定义；单字对齐 load/store 零代价）
- 默认值仍全权由引擎 `object_method_bind_call` 补（`argc==provided`），行为不变
- 挂载端（`jsb_object_bindings.cpp:145`）改为携带 data 的 `Method` 调用；解析失败回退 dynamic 绑定
- 行为保持：argc 语义、static/instance 分支、call_error 报错、`translate_return` 均不变
- `IsStaticC` 已实测裁决**保留模板参数**：折叠仅省 34 实例（1,520→1,486，2.2%），不值得热路径多一分支（依据见 design.md §3）

## Acceptance Criteria

- [ ] `binding_mode` 三态构建接线完成，`static`（形态 A）构建产物与改造前行为/性能基线一致（零回归）
- [ ] shared 形态 class 实例化声明数 ≤ 1,530（1,519 + 容差；codegen 发射计数，非引用点）
- [ ] C++ 双套件全绿（exit 0、无 Orphan、无未释放 Resource）——`static` 与 `shared` 两形态各跑一遍
- [ ] TS 集成测试 `GODOTJS_TEST_PROJECT_COMPLETED`（含 class 方法默认参数调用路径）
- [ ] bench 全量 231 case shared 腿 invalid=0；体积对比按 `misc/bench_matrix.py` 纪律采数（dll md5 前后核验；bench_matrix `--leg` 扩 shared、`staticBinding` 腿标适配三态）

## Out of Scope

- builtin / utility / vararg / ctor / operator 族（子任务 2、3）
- 形态 A 的任何改动（含 Def 剥离）
- d.ts 生成与 JS 侧任何行为变化

## Notes

- `*.gen.*` 只改 `misc/build/static_binding_codegen.py` 发射逻辑，不手编
- 采数纪律与验收命令见 `.trellis/spec/godotjs-ext/test/index.md`、`build/scons-build.md`
