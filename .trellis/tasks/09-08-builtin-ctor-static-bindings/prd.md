# 内置类型构造器静态绑定

> 状态：**进行中**（Step1 已实施：剔除 JS 原生基础类型构造；Step2/3 进行中）。

## 目标

以 static_binding 体系（per-(type, ctor-index) 模板实例 + `variant_get_ptr_constructor`
引擎指针直调，对齐 `class_method_thunk` 模式）替代 JSB_FAST_REFLECTION 反射构造。
codegen 已收集 `constructors: 156`（vt + ctor_index + args），数据齐全。

## 已确认的事实（有对照实验证据）

1. **`ReflectConstructorCall<T>` 是 JSB_FAST_REFLECTION 反射加速层，不是静态绑定体系**。它是手写的逐类型 if-else 链 + `StaticBindingUtil<T>::get` 逐参解值，仅加速转换；其作者留有 TODO"应改为构建时生成"——本任务即该 TODO 的落地。
2. **api json 的 `right_type: "Variant"` 行**是引擎 dump 右参类型遍历含 NIL 档的产物，与 ==/!= 的 Nil 行语义不同，勿混淆。
3. **editor.gdextension 无 `windows.release.*` 键**，template_release 引擎加载它直接 exit 5（8 行日志即退，无场景加载）。release 采数前必须把它移出 addons 并同步 extension_list.cfg。
4. ~~static 腿 + 全量 152 ctor case = 0xC0000005~~ **已定位根因**：ctor 的 base 若传入 `variant_new_nil` 初始化的 Variant，引擎 ctor 只写数据**不更新类型标签**（标签仍 NIL），产出"标签 NIL、数据为 struct"的损坏 Variant，首次使用即 SEGV。
5. ~~单体 OK 组合崩~~ **误判**：多轮二分期间 dll/case 文件串线导致的现象误读。隔离后真实分布：bool/Vector2i/Rect2/Transform2D 等 118/121 case 全 OK；确定性崩例仅三类——①跨类型拷贝（Rect2i→Rect2、Rect2→Rect2i）②Array from Packed* 参 ③Array/Dictionary 的含 Variant/StringName 多参重载。
6. **正确路径 = base 用栈缓冲（MaxSizeEncodeArgType，未初始化语义 ✓）→ ctor 写入 → `arg_ptr_to_var` 重建完整 Variant（类型标签+数据）→ `bind_valuetype`**。`get_opaque_typed<VTC>(NIL)` 返回无效指针、`Variant(VTC)` 不存在（隐式转 int64_t）——两条歧路都已在实施中踩过并排除。

## Step1 决策（2026-09-07 用户指示，已实施）

bool/int/float/String/StringName 在 JS 中即 boolean/number/string 原生类型：
- 其构造**不生成**静态绑定（`emit_ctor_dispatch` 跳过）、**不建**基准 case
- 未来若需暴露，走静态工厂函数形式（子任务 09-08-static-factory-primitive-ctors）

## 当前实现状态

- `builtin_ctor_thunk<VT, CtorIndex, Args...>`：逐参 `var_to_arg_ptr` 编码到 MaxSizeEncodeArgType 槽 → `ctor(栈缓冲 base, args)`（UninitializedTypePtr 语义）→ `arg_ptr_to_var` 重建完整 Variant（类型标签+数据）→ `bind_valuetype` 绑 This
- `find_ctor_<Type>(info)`：argc 分段 + probe 参数类型精确匹配（同 arity 异型重载必需）
- `find_ctor_adapter(name)`：name → per-type 适配器（get_class_builder 注册期查表挂载）
- `ctor_tables.gen.h` 声明 + `dispatch_builtin.gen.cpp` 定义（static_binding 体系内）
- cases.builtin.ts：32 类型 142 case（已剔除 5 原生类型 + Array 的 Packed* 参重载 + Object 参重载）

## 待办

1. Step1 收尾：重编验证 + 提交（本提交）
2. Step2：operator_tables.gen.h → builtin_operator_tables.gen.h；ctor_tables.gen.h → builtin_ctor_tables.gen.h
3. Step3：移除 bool/int/float 左参的运算符生成（JS 原生类型，无静态绑定意义）
4. SEGV 专项（如需恢复全量 case）：逐组合定位——单体 OK 组合崩，优先与引擎 `Variant::construct` 源码对照；用 editor dll（有符号）attach 拿真实栈
