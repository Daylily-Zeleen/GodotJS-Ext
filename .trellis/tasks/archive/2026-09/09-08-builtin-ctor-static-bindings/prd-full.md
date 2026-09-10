# 内置类型构造器静态绑定（中断：SEGV 未定位）

> 状态：**中断**。实施代码已归档于本目录（builtin_constructors.h / static_binding_codegen.py 快照），工作区已恢复到 29289d2 干净基线。

## 已确认的事实（有对照实验证据）

1. **`ReflectConstructorCall<T>` 是 JSB_FAST_REFLECTION 反射加速层，不是静态绑定体系**。静态绑定的正路 = codegen 生成 per-(type, ctor-index) 模板实例 + `variant_get_ptr_constructor(VT, index)` 引擎指针直调（对齐 `class_method_thunk` 模式）。codegen 已收集 `constructors: 156`（vt + ctor_index + args），数据齐全，只缺 thunk 与发射。
2. **api json 的 `right_type: "Variant"` 构造行**（引擎 dump 的 NIL 档）与 ==/!= 的 Nil 行语义完全不同，勿混淆。
3. **editor.gdextension 无 `windows.release.*` 键**，template_release 引擎加载它直接 exit 5（8 行日志即退，无场景加载）。release 采数前必须把它移出 addons 并同步 extension_list.cfg。
4. **static 腿 + 全量 152 ctor case = 0xC0000005**；且**非 ctor 组（--only=Vector2）同样崩**——即当前 dll 的模块加载/启动路径已被污染，与 case 内容无关（editor dll 上 --only=bool 通过但 CTOR-ENTER=0，说明 thunk 根本没被执行到就崩了）。
5. 单 case 隔离结果（editor dll）：bool 组 4 case OK、Vector2i 4 参 OK、AABB 全崩、Array 的 Packed* 参全崩、Rect2/Rect2i 的跨类型拷贝（Rect2i→Rect2）崩——**同 arity 异型重载**与**跨类型拷贝**是高危区。
6. `get_opaque_typed<VTC>(NIL Variant)` 返回无效指针——ctor 的 base 必须来自"已初始化为目标类型"的 Variant。曾尝试 `*instance = Variant(VTC)` 但 godot-cpp 无 `Variant(Type)` 构造器，`Variant(VTC)` 隐式转 int64_t → 值为枚举数值的 int Variant，类型错误。

## 已实现待修复的代码（本目录快照）

- `builtin_ctor_thunk<VT, CtorIndex, Args...>`：ptrcall 编组 + 引擎 ctor 直调（结构对齐 class_method_thunk）。
- `find_ctor_<Type>(info)`：argc 分段 + probe 参数类型精确匹配。
- `find_ctor_adapter(name)`：name → per-type 适配器指针。
- 接线：`get_class_builder` static-first。

## 下次接手的方向

1. **先修环境**：release 采数前移出 editor.gdextension（或给 SConstruct 加 release 下构建 editor 扩展的支持）。
2. **base 初始化**：用引擎接口按 VTC 初始化 instance（variant_new_nil 后由 ctor 覆写，或查 godot-cpp `VariantInternal::init_*` 是否有按类型的 init），保证 `get_opaque_typed` 拿到有效指针。
3. **同 arity 异型重载**：probe 链方案可行（bool 组 4 case OK 佐证），但需逐 case 复测。
4. **跨类型拷贝 ctor（Rect2i→Rect2 等 68 个）**：全部崩，根因未明，优先与引擎 `Variant::construct` 源码对照。
5. **用 editor dll（有符号）调试**，attach 后拿真实崩溃栈——不要再靠日志猜。
