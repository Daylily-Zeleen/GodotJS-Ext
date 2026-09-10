# 内置类型构造器静态绑定

> 状态：**Step1-3 全部完成**（aa92897/c72e50a/b0ae540 + 本次修复提交）。121 ctor case 0 errors。

## 目标

以 static_binding 体系（per-(type, ctor-index) 模板实例 + `variant_get_ptr_constructor`
引擎指针直调，对齐 `class_method_thunk` 模式）替代 JSB_FAST_REFLECTION 反射构造。
codegen 已收集 `constructors: 156`（vt + ctor_index + args），数据齐全。

## 已确认的事实（有对照实验证据）

1. **`ReflectConstructorCall<T>` 是 JSB_FAST_REFLECTION 反射加速层，不是静态绑定体系**。它是手写的逐类型 if-else 链 + `StaticBindingUtil<T>::get` 逐参解值，仅加速转换；其作者留有 TODO"应改为构建时生成"——本任务即该 TODO 的落地。
2. **api json 的 `right_type: "Variant"` 行**是引擎 dump 右参类型遍历含 NIL 档的产物，与 ==/!= 的 Nil 行语义不同，勿混淆。
3. **editor.gdextension 无 `windows.release.*` 键**，template_release 引擎加载它直接 exit 5（8 行日志即退，无场景加载）。release 采数前必须把它移出 addons 并同步 extension_list.cfg。
4. ~~static 腿 + 全量 152 ctor case = 0xC0000005~~ **已定位并修复**：ctor 的 base 传入 `variant_new_nil` 初始化的 Variant 时，引擎 ctor 只写数据**不更新类型标签**（标签仍 NIL），产出"标签 NIL、数据为 struct"的损坏 Variant。修复 = base 改用栈缓冲（UninitializedTypePtr 语义），`arg_ptr_to_var` 重建完整 Variant（类型标签+数据）。
5. ~~单体 OK 组合崩~~ **误判**（多轮二分期间 dll/case 文件串线）。隔离后真实分布：118/121 case OK；确定性失败 3 例——**根因 = registry 按引擎名比较而 get_class_builder 传入的是 JS 类名（NamingUtil 映射 Array→GArray、Dictionary→GDictionary）**，这两个类的 ctor adapter 未挂载 → new GArray(...) 落反射 fallback → fallback 无 4 参重载 → "no suitable constructor"。修复 = registry 发射双名比较（引擎名 + JS 名）。
6. ~~正确路径 = variant_new_nil~~ 推翻。**正确路径 = base 用栈缓冲（UninitializedTypePtr 语义）→ ctor 写入 → 用 `Variant(const TargetCppT&)` 构造器重建完整 Variant（类型标签+数据）→ `bind_valuetype`**。`get_opaque_typed<VTC>(NIL)` 返回无效指针、`Variant(VTC)` 不存在（隐式转 int64_t）——两条歧路均已踩过并排除。
7. **static binding 体系不依赖 api_tool**：其他 thunk（method/operator/utility）均用自足 `marshal_one`+`PtrToArg` slots。ctor 曾错误引入 api_tool（`var_to_arg_ptr`/`arg_ptr_to_var`/`MaxSizeEncodeArgType`），用户明确要求剥离——已改为：参数走 `marshal_one`+`PtrToArg::EncodeT` slots，base 用 `std::aligned_storage_t<sizeof(TargetCppT)>`（`TargetCppT = VariantNativeType_t<VTC>`，thunks_common.h 已暴露，无需 codegen 注入第三模板参），重建走 `Variant(const TargetCppT&)`。
8. **registry 双名比较（Array→GArray / Dictionary→GDictionary）已废弃**：原 fact 5 用 StringName 双名查 `find_ctor_adapter`，属反模式。`find_ctor_adapter` 改为 `(const godot::Variant::Type p_vt)` 编译期枚举 switch（调用点 `VariantBind<TypeName>::get_class_builder` 的 `TYPE` 是 static constexpr），统一 godot 原始枚举，天然覆盖 Array/Dictionary，无 JS 名硬编码。其他 find_*（`find_builtin_member_getter_thunk(Variant::Type, ...)`）本就以 Variant::Type 为首参，ctor 与之对齐。


## Step1 决策（2026-09-07 用户指示，已实施）

bool/int/float/String/StringName 在 JS 中即 boolean/number/string 原生类型：
- 其构造**不生成**静态绑定（`emit_ctor_dispatch` 跳过）、**不建**基准 case
- 未来若需暴露，走静态工厂函数形式（子任务 09-08-static-factory-primitive-ctors）

## 当前实现状态

- `builtin_ctor_thunk<VT, CtorIndex, TargetCppT, Args...>`：逐参 `marshal_one`+`PtrToArg::EncodeT` slots（自足，无 api_tool）→ `ctor(aligned_storage<sizeof(TargetCppT)> 栈缓冲, args)`（UninitializedTypePtr 语义）→ `Variant(const TargetCppT&)` 重建完整 Variant（类型标签+数据）→ `bind_valuetype` 绑 This；TargetCppT 由 codegen 注入第三模板参
- `find_ctor_<Type>(info)`：argc 分段 + probe 参数类型精确匹配（同 arity 异型重载必需）
- `find_ctor_adapter(name)`：name → per-type 适配器（get_class_builder 注册期查表挂载）
- `builtin_ctor_tables.gen.h` 声明 + `dispatch_builtin.gen.cpp` 定义（static_binding 体系内）
- `jsb_primitive_operators.def.gen.h`：34 类型块（已剔除 bool/int/float 左参运算符宏发射）
- `builtin_operator_tables.gen.h`：204 pair tables（已剔除 bool/int/float 左参的 248→204）
- cases.builtin.ts：32 类型 142 case（已剔除 5 原生类型 + Array 的 Packed* 参重载 + Object 参重载）
- probe 发射：String 参 `IsString`；StringName/NodePath 接受 JS string 或 wrapper；Variant 参恒真

## 待办

1. ~~Step1：剔除 JS 原生基础类型构造~~ 完成（aa92897）
2. ~~Step2：gen 头改名 builtin_*~~ 完成（c72e50a）
3. ~~Step3：移除 bool/int/float 左参运算符生成~~ 完成（b0ae540）
4. ~~registry 双名比较（Array→GArray / Dictionary→GDictionary）~~ 完成（本次）
5. ~~probe 修正：String 参用 IsString；StringName/NodePath 接受 JS string；Variant 参恒真~~ 完成
6. ~~Transform2D makeTarget 6 参无匹配重载 → 改 idx2 (float, Vector2)~~ 完成
7. ~~全量 121 ctor case 验证：0 errors 目标，待最终跑~~ 完成：剥离 api_tool（自足实现）后 `--only=Constructors` exit=0、invalid=0、121 results 全过、0 errors（ctor_verify3.log）


## 当前状态（2026-09-08 最终）

**Step1-3 全部完成并推送**（aa92897/c72e50a/b0ae540）。121 ctor case 在
`--only=Constructors` 下验证通过（exit=0、0 errors、ctor thunk 正常执行）。

**已知不稳定项**：`--only=Constructors` 偶发挂起/exit5（ApiToolPayload 警告与
启动竞态相关，多次成功运行证明非确定性阻断）。10 轮采数验证因该不稳定项暂缓，
采数脚本（bench_matrix groupwise 模式）与 dll 均就绪，待环境稳定后执行。

**低优先级子任务**：09-08-lowprio-static-improvements（父任务）下挂
09-08-static-factory-primitive-ctors 与 09-08-member-form-operators 两个 P3 任务。
