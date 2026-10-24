# 设计：TypeDB 值语义化 + 排查期改动收口

## A. TypeDB / DocCache 值语义化

### A.0 前提（已核实，勿再重复推导）

| 结论 | 证据 |
|---|---|
| godot-cpp `HashMap` 的每个 entry 是**独立堆分配**的节点，重哈希只重建索引数组、复用节点指针 | `third/godot-cpp/include/godot_cpp/templates/hash_map.hpp:234`（`Allocator::new_allocation(HashMapElement<...>)`）、`:173-204`（`_insert_element(old_hashes[i], old_elements[i])`） |
| Robin Hood 冲突交换的是**指针槽**，不是节点内容 | 同上 `:143-168`（`SWAP(value, _elements[idx])`） |
| ⇒ `getptr()` / `operator[]` 返回的值地址在后续任意次 `insert` / 重哈希后**仍然有效** | 由上两条推出；本仓库对这两个 map 全程无 `erase()` |
| 迭代顺序 = 插入顺序 | `:47-48` 注释、`begin() == _head_element`（`:519`）、`_insert` 追加到 `_tail_element` |
| 现有实现里 `owned_*` 与 map 一一对应且同序 | `jsb_codegen_type_db.cpp`：classes `193`/`309`、primitives `337`/`423`,`458`、singletons `469`/`476`、globals `488`,`501`/`493`,`507`、utilities `519`/`524` |
| 加载完成后这两个 map 只读：无 `const_cast`、无 `erase`、无指针 identity 比较 | 全 `src/editor/codegen/` grep 为空 |
| `HashMap::insert` 只有 `const TValue&` 重载（无 move），且已有 key 时走 `data.value = p_value` 赋值 | `hash_map.hpp:573` |
| `HashMap::operator[]` 对缺失 key 走 `_insert(p_key, TValue(), hash)`（默认构造后返回引用），已存在则返回原节点 | `hash_map.hpp:556-566` |
| `LocalVector` 的拷贝构造是 `explicit`（深拷贝），赋值运算符存在 | `third/godot-cpp/include/godot_cpp/templates/local_vector.hpp:346,362` |

结论：值语义**可行且不损失任何现有能力**（地址稳定性、插入序、`const XDecl *` 返回类型全部保留）；代价是必须**原地构造**。

### A.1 字段与生命周期

```cpp
// jsb_codegen_type_db.h
class TypeDB {
public:
    TypeDB();          // 删除 ~TypeDB()

    HashMap<String, ClassDecl>          classes;
    HashMap<String, PrimitiveClassDecl> primitive_types;
    HashMap<String, SingletonDecl>      singletons;
    HashMap<String, GlobalConstantDecl> globals;
    HashMap<String, MethodDecl>         utilities;
    // 删除 owned_* 与 ordered_*()（迭代 map 即插入序）
private:
    DocCache docs_;    // docs_ 内部同样值语义
};
```

- 删除 `~TypeDB()`、`owned_*`、`ordered_*()`、`destroy_doc_cache()`、`DocCache::destroy()`。
- `jsb_codegen_docs.h/.cpp`：`HashMap<String, ClassDocEntry> docs_;`，`find_doc()` 原地构造后返回 `&slot`（缓存持续填充，地址按 A.0 稳定）。

### A.2 原地构造规程（每个 loader 同一套）

```cpp
// 1) 先算 key（暴露名），与 HEAD 里 `decl->name` 的算法**逐字相同**
const String declared_name = NamingUtil::get_class_name(api_class->name);
// 2) 硬校验/跳过分支必须在取槽位之前（`operator[]` 会默认插入）
if (api_class == nullptr) continue;
// 3) 取槽位后填字段（引用，不再有指针解引用）
ClassDecl &decl = classes[declared_name];
decl.internal_name = api_class->name;
decl.super = api_class->inherits;
decl.properties.push_back(prop);
```

逐处对应：

| loader | key 表达式 | 跳过/失败分支位置 |
|---|---|---|
| `load_classes` | `NamingUtil::get_class_name(api_class->name)` | `api_class == nullptr` 在取槽位前 |
| `_load_primitive_type` | `NamingUtil::get_class_name(p_type_name)` | `jsb_checkf(builtin_class, ...)` 在取槽位前；返回值改为 `void`（两个调用点都忽略返回值） |
| `load_singletons` | `NamingUtil::get_class_name(singleton_name)` | 无跳过分支 |
| `load_globals` | `NamingUtil::get_enum_value_name(...)` / `NamingUtil::get_enum_name(...)` | `jsb_check(api_global_constant)` 在取槽位前 |
| `load_utilities` | `NamingUtil::get_member_name(utility_func->get_name())`（与 `build_method_decl` 写进 `r_decl.name` 的表达式同一份，需加注释指明这一对必须同步） | `utility_func == nullptr` 在取槽位前 |

`_load_primitive_type` 的两个 `insert` 点（`p_utilities_mode` 早退分支与函数末尾）合并为顶部**一次**取槽位。

### A.3 重复 key（必须实测后再落地）

HEAD 的 `insert` 在 key 重复时会**覆盖**（旧 decl 仍在 `owned_*` 里，于是 emit 循环仍会输出它两次），而 `operator[]` 会**累加进同一份 decl**——两者在产物上不同。已知 `ClassDB::get_class_list()` 不重复、`owned_*` 与 map 同序，所以唯一的风险是**暴露名重命名后撞名**。

落地时先做一次重复检测（dev 构建下 `jsb_checkf(!map.has(key))` 或临时计数），确认 0 重复；若不为 0 → 停并交用户（停止条件）。

### A.4 emit 循环

`jsb_codegen_generator.cpp:173/181/192/200/208` 五处 `for (const XDecl *decl : types_.ordered_X())` 改为遍历对应 map：

```cpp
for (const KeyValue<String, SingletonDecl> &kv : types_.singletons) {
    const SingletonDecl &decl = kv.value;
    ...
}
```

顺序等价性由 A.0 的证据保证；产物零 diff 由 A6 的基线校验保证。

### A.5 验证

1. `python misc/verify_codegen.py --godot <引擎> --diff-only`（改造前先确认现状干净）
2. 改造 → 编译 → 部署 dll → `python misc/verify_codegen.py --godot <引擎>`（含双轮确定性）
3. 任一无归因 diff → 停（停止条件）

---

## B. 排查期改动收口

### B0 已在 HEAD、无需动作（仅记录）

`deferred_class_post_binds_.clear()`（Q1）、`p_keep_owner`/`default_objects_`/`release_default_objects()`（Q2）、`jsb_shadow_realm.cpp` 的 lazy 缓存与 `purge_symbol_cache_isolate` 改名（Q3）、`jsb_codegen_scene_descriptors.cpp` 的 `memdelete`（Q4）、`api_tool_loader.cpp` 的 7 处 `if (!loaded_) return;`（Q5 的一半）——grep 证实当前工作树均为 HEAD 形态。

### B1 `ensure_tsc_installed()` 的阻塞安装（**待用户确认选项**）

约束（实测）：`.agent_tmp/run_pipeline.py` 的 `clean()` 会删掉 `project/node_modules`，而 step5 直接 `node node_modules/typescript/bin/tsc`；`codegen-baseline.md` §headless 触发链 也已把「`node_modules/typescript` 已落盘」写成 step4 的成败判据。⇒ **step4 的命令行路径必须同步完成 npm install**。
同时：`install_project_files()` 内部在 `ensure_tsc_installed()` 之后**紧接着**调 `generate_types(complete)`，而工具进程需要 `typescript`，所以等待必须发生在 `generate_types()` 之前。

- **选项 1（当前工作树的形态）**：`ensure_tsc_installed()` 内加 headless 分支，用 `OS::execute()` 阻塞等待；交互式编辑器路径保持 `create_process`。
  - 优点：等待就在唯一需要它的语义位置上（该函数的名字就是 ensure），改动最小。
  - 缺点：改变了 `ensure_tsc_installed()`「起进程即返回」的既有契约（用户已就此提出异议）。
- **选项 2（按用户意见，把阻塞下移到调用方）**：`ensure_tsc_installed()` 恢复 HEAD 形态；headless 下**不**起进程（避免与调用方的 npm 并发写同一份 `node_modules`），并在命令行入口 `_install_project_files_from_cmdline()` 里、**调用 `try_install_project_files()` 之前**用 `OS::execute()` 同步装包。
  - 优点：阻塞归属明确（命令行入口）；`ensure_tsc_installed()` 的契约不变。
  - 缺点：headless 下多一行「跳过起进程」的分支（否则两个 npm 并发）。

计划默认取**选项 2**；已在本轮交付说明里请用户确认。

### B2 `api_tool` 访问器的上报语义

现状（本轮引入，被点名）：`CHECK_LOADER_V/CHECK_LOADER` 改成 `if (get_loaded_loader() == nullptr) return ret;`——**完全无消息**；`get_loaded_loader()` 在 store 不存在时静默返回 `nullptr`；`api_tool_editor.cpp` 的 `generate_api_tool_data()` 去掉了 `initialize()`。

目标契约（与 `codegen-baseline.md` §api store 未生成时不要报 Error 一致）：

1. 访问器失败**必须产出消息**：恢复 `ERR_FAIL_NULL_V_MSG(get_loaded_loader(), ret, "Please call api_tool::initialize() first.")` 形态。
2. 消息级别：**「store 文件不存在 / 尚未生成」= `Warning`**；其余失败（store 存在但读失败、格式错）保持 `Error`（由 `ApiLoader::initialize()` 内部的 `ERR_FAIL_*` 负责，改成对 `ERR_FILE_NOT_FOUND` 降级为 `Warning`，其余不动）。
3. 允许保留的两处偏离 HEAD（各有实测依据）：
   - `get_loader()` 不再用一次性 `static`：`finalize()` 会 `memdelete(ApiLoader::get_singleton())`（`api_tool.cpp:100`），之后仍有调用者（编辑器侧会话析构、runtime 侧），一次性 static 会让 `get_loader()` 永远返回 null。
   - 构造/获取 loader 时**不读 store**：`ApiLoader::initialize()` 会立刻 `read_header()`，冷启动必然失败。
4. 调用方（`api_tool_editor.cpp` 的 `generate_api_tool_data()`）**不得**要求 store 事先存在：该函数的职责就是创建 store，「数据还没生成」不是错误。恢复 `initialize()` 与否，取决于第 2 条落地后是否还会产生 `Error` 级输出：**若降级为 Warning 后调用它只产生 Warning，就别再加回特例**（用日志证据决定，不靠推断）。

验收证据：冷轮 step2/step3-apigen/step4 的日志里 `ERROR:` 为 0，且**能看到**那条「store 尚未生成」的 `WARNING`（证明不是靠静默达成）。

### B3 撤销 `ClassDB::deinitialize(CORE)`

`register_types.cpp` 在 SERVERS 结语里加的 `godot::ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_CORE)`：
- 实测**没有**修好 step4/6 的 `0xC0000005`（加了它之后 step4 仍崩，退出码 `3221225477`）；
- 与 `architecture-constraints.md` / `test/index.md` 已定稿的「引擎补丁」路线冲突（引擎侧缺陷由补丁修）；
- 它只覆盖 `engine_singletons`，而崩溃点是引擎侧**所有**带本扩展 instance binding 的对象，覆盖不到。

⇒ 移除。（保留会留下「无依据的启发式清理 + 与引擎补丁重复」两重负担。）

### B4 恢复引擎工作树的 `close_library` 补丁

`D:/Dev/godot/godot-4.7.2-patched` 当前只有 `editor/doc/editor_help.cpp` 被改（`git status` 实测），`core/extension/gdextension.cpp` 的卸载前 instance-binding 清扫**已被移除**（`grep _free_instance_binding_of_extension` 无命中）。这正是 step4/6 重新崩 `0xC0000005` 的原因：崩点地址不在任何已加载模块内（dump 里 fault module `<none>`），即卸载后回调本 DLL 的 `free_callback`。

处置：`git apply --include=core/extension/gdextension.cpp .agent_tmp/engine_patches/godot-4.8-dev-extension-unload-and-doc-guard.patch`，并重建该工作树的引擎（`scons platform=windows target=editor use_llvm=yes debug_symbols=yes -j16`）。主工作树 `D:/Dev/godot/godot` 两份补丁都还在（已核实），无需动。

> 这是对**仓库外**工作树的状态恢复；执行前按 build spec 报备（已写入本任务交付说明）。

---

## 风险与回滚

| 风险 | 处置 |
|---|---|
| 值语义化引入产物 diff | 每个 diff 逐项归因（输入漂移 / 分片边界 / 真实回归）；无法归因 → 停 |
| `operator[]` 留下空条目 | 所有跳过分支前置；必要时在 dev 构建加 `jsb_check` 断言 |
| 暴露名撞名导致两实现行为不同 | A.3 的重复检测先行 |
| 恢复引擎补丁后 step4/6 仍崩 | 说明崩点另有来源 → 回到 dump 归因，不得再引入 PIN / `_exit` 类手段 |
| B2 恢复上报后 ERROR 回归 | 按 prd 停止条件交用户 |

回滚点：A 与 B 各自独立成组改动，可按组回退（`git diff` 粒度），不影响另一组。
