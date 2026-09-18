# 优化 api_tool：ApiMethodBase 移除 godot::MethodInfo，次要信息懒加载

## Goal

`api_tool::ApiMethodBase` 常驻一个 `godot::MethodInfo`（`src/api_tool/api_tool_types.h:104`，实测 120 B）及其三个堆数组，但运行时只读取其中少数几个字段，其余长期占用堆内存。本任务把**运行时热路径字段直接内联**为 `ApiMethodBase` 及子类成员，**其余字段保持完整保真**地打包为惰性类型、首次访问时才分配并从文件加载。

验收口径：v8 + 动态绑定构建下，编辑器代码生成产物与改动前基线**逐字节一致**，C++ 测试与 TS 集成测试全绿。

> 设计原则（本轮明确）：**api_tool 是通用化模块，可直接迁移到其他项目**。凡 `extension_api.json` 提供的字段，都必须**保真**表示（不得因「当前没被读取」而省略或简化；推导出的信息允许以**内部编码**承载，但对外接口必须屏蔽内部位、返回纯 Godot 语义）。唯一例外是 `id`——json 无此字段，无源数据可对齐。

## Background（已核实事实）

### 当前结构与内存占用（尺寸为 clang 实测）

- `ApiMethodBase { godot::MethodInfo method; MethodHash hash; }`，源码自带 `// TODO: 信息懒加载，尤其是 default values`（`api_tool_types.h:103-105`）。
- 实测 `sizeof(godot::MethodInfo)` = **120**、`sizeof(godot::PropertyInfo)` = **48**、`StringName` = 8、`Variant` = 24、`Variant::Type` = 4、`LocalVector<T>` = 16。
- 实测改动前结构：`ApiMethodBase` = 128、`ApiClassMethod` = 152、`ApiBuiltInMethod` = 144、`ApiUtilityFunction` = 152（自带对齐填充）。
- 单方法常驻 = `sizeof(结构)` + `arguments`（48 B/参数）+ `arguments_metadata`（4 B/参数）+ `default_arguments`（24 B/缺省值）。
- 实测规模（`third/godot-cpp/gdextension/extension_api-4-7.json`）：class 方法 16822 / builtin 999 / utility 114，参数 16879 / 902 / 193，缺省值 1715 / 150 / **0**。
- **改动前总常驻 = 3,697,536 B（3.53 MiB）**；class 族 3,475,812 B。

### 运行时实际读取的字段

| 字段 | 运行时用途 |
|---|---|
| `name` | 绑定注册、错误信息、静态绑定查找、兼容 hash 查询 |
| `flags` | static / virtual / vararg / const 判定 |
| `hash` | 绑定查找（`classdb_get_method_bind` / `variant_get_ptr_builtin_method`） |
| `arguments[i].type` + `arguments_metadata[i]` | ptrcall 参数编码 |
| `arguments.size()` | 参数校验、argc 计算 |
| `default_arguments.size()` | **class 与 builtin** 的 argc 校验（见下）；utility 不用 |
| `default_arguments[i]` | **仅 builtin** 实际取值填充（class 侧为死分支，见下） |
| `return_val.type` + `return_val_metadata` | 返回值编码与转换 |
| `return_val.usage & NIL_IS_VARIANT` | `has_returns()` 判定 |

### 「谁需要缺省值信息」——实测结论（本轮核实，修正了先前的错误判断）

| 族 | 需要缺省值**个数** | 需要缺省值**实体值** | 证据 |
|---|---|---|---|
| `ApiClassMethod` | **是** | 否（死分支） | `jsb_object_bindings.cpp:404-407` 调用 `VariantUtil::check_argc(..., default_arguments.size(), method_argc)`，并用 `method_argc - default_arguments.size()` 拼错误信息 |
| `ApiBuiltInMethod` | **是** | **是** | `jsb_variant_info.h:51-53` 读 `.size()`；`jsb_primitive_bindings.cpp:460-485`、`api_tool_types.h:165-180` 取值填充 |
| `ApiUtilityFunction` | 否 | 否 | `jsb_variant_info.h:58-60` 的 `check_argc` 只用 `argument_types.size()`；`jsb_godot_module_loader.cpp:101` 注释「utility functions have no default argument」 |

`VariantUtil::check_argc`（`src/internal/jsb_variant_util.h:140-147`）在 `default_num > 0` 时允许少传参；
class 族有 1040 个方法带缺省值（共 1715 个）——**丢掉这个计数会拒绝合法调用**。
故缺省值计数必须落在 **`ApiMemberMethodBase`**（class + builtin 的公共基类），既不进通用基类 `ApiMethodBase`，也不进 utility。

class 族缺省值**实体值**在运行时不可达：`_godot_object_method` 的补全分支索引 `index - method_argc`
在 `argc < method_argc` 时恒为负（死分支）；真正的补全由引擎 `MethodBind::call` →
`call_with_variant_args_dv(..., get_default_arguments())`（`core/object/method_bind_common.h:83`）完成。

### 次要（运行时不读）字段

`arguments[i]` 的 `name` / `class_name` / `hint` / `hint_string` / `usage`；`return_val` 除 `type` / `usage`（的 `NIL_IS_VARIANT` 位）/ `return_val_metadata` 外的字段；`hash_compatibility`。
这些只有 `src/editor/codegen/` 需要。**它们必须保真保留**（惰性加载），不得省略。

### `id`：唯一可以去掉的字段

json 方法对象的键集合实测为
`{arguments, hash, hash_compatibility, is_const, is_required, is_static, is_vararg, is_virtual, name, return_value}`
（class）/ `{arguments, hash, hash_compatibility, is_const, is_static, is_vararg, name, return_type}`（builtin）/
`{arguments, category, hash, is_vararg, name, return_type}`（utility）——**没有 `id`**。
故 `MethodInfo::id` 恒为 0：既无源数据，也无法在重建时对齐。因此：

- 删除 `id`（`MethodInfo::id` 不再表示、`MethodDecl::id` 一并删除）。
- 除此之外的字段**一律保真**：`MethodDecl::hint_flags` **保留**，由真实 `flags` 填充。

### `flags`：内部编码 + 对外契约（调整 #5 的精确语义）

`flags` 保存 **Godot 的真实 flags**（`GDEXTENSION_METHOD_FLAG_*`），并额外携带一个
**仅供内部使用**的位 `METHOD_FLAG_NO_RETURN = 1 << 8`。实测 bit 256 空闲：引擎
`MethodFlags`（`core/object/method_info.h:36-45`）用到 1/2/4/8/16/32/64/128；
GDExtension 侧（`gdextension_interface.gen.h:441-450`）最大 `VIRTUAL_REQUIRED = 128`。

- **写入**：加载期由真实判定（`return_val.type != NIL || usage & PROPERTY_USAGE_NIL_IS_VARIANT`）
  推导一次，随热数据落盘。
- **内部查询**：`has_returns()` 与三个子类的 `validated_call()` 读**原始** `flags_`。
- **对外契约**：`get_flags()` **必须屏蔽**该位（`flags_ & ~METHOD_FLAG_NO_RETURN`），
  对外返回纯 Godot flags —— api_tool 的内部编码不得外泄。

这样「有无返回值」（含「返回 `Variant`」的 `NIL_IS_VARIANT` 情形）只占一位、
无需额外热字段，同时不破坏 Godot 的对外 flags 契约。

### 关键不变量（已用 json 逐项实测）

- 缺省参数**恒为尾部连续段**：`default_value` 不为尾段的方法数 = **0**（全 17935 个方法）→「缺省值个数」是充分信息。
- vararg 且带缺省的方法数 = **0**（class / builtin / utility 全为 0）→ 缺省填充与 vararg 不共存。
- 单方法最多 11 个缺省值、14 个参数。
- utility **完全没有**缺省值（114 个函数全为 0，两次复核）。
- 每个实体 detail+defaults 实测上界：class **83,208 B**（RenderingServer，535 方法）、builtin **10,752 B**（String）。
- 全部 builtin 的缺省值合计仅 150 个 `Variant` ≈ 3.6 KB。

### 类型宽度上界（已核实）

- `GDExtensionClassMethodArgumentMetadata` 实测最大 `OBJECT_IS_REQUIRED = 13` → `uint8_t` 足够。
- `godot::Variant::Type` 成员数实测 40（含 `VARIANT_MAX`）→ `uint8_t` 足够。
- ⚠ `sizeof(godot::Variant::Type)` 实测 = **4**，故紧凑参数块必须写成 `{uint8_t type; uint8_t meta;}`（实测 2 B）；写成 `{Variant::Type; uint8_t}` 会 padding 到 8 B/参数，比现状更差。
- `uint16_t` 计数上界充分：最大 class.methods=535、enum.values=233、builtin.constants=146、builtin.methods=116、operators=94、方法参数=14、缺省值=11，余量 ≥ 60 倍。

### 构建/校验前提（本 worktree）

- 本 worktree 全新：`bin/`、`.sconsign.dblite`、`project/.godot/`、`project/node_modules/`、`project/gen/`、`project/typings/`、`.codegen-baseline/` **均不存在** → 需全量构建 + TS 依赖安装，且 `--update-baseline` 现场建立。
- `third/godot-cpp` 子模块已就位；`third/godot-cpp/gen/include`（构建生成头）本 worktree 尚无，首次 scons 时生成。
- 引擎宿主：`D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.console.exe`（`4.8.dev.custom_build.9d6da1df7`；`SConstruct` 的 `API_VERSION = "4.7"`）。
- `misc/verify_codegen.py --godot` **必须传绝对 exe 路径**（实测：`sh -c 'godot'` 可解析，但 python `which('godot')` 为 `None`、`subprocess(['godot'])` 抛 `WinError 2`、`bash -lc` 因 shebang CRLF 失败）。
- api store 格式：magic `GCPF` + deflate；`FileAccessCompressed::seek` 按 4096 B 块解压，`get_position()` / `get_length()` 返回解压后偏移 / 总长，可做分段随机读取。

## Requirements

- **R1** `ApiMethodBase` 及子类的 API 类型定义中不再含 `godot::MethodInfo` **成员**（加载期由引擎数据转换而临时构造 `MethodInfo` 不受此限）。
- **R2** 三个子类（`ApiBuiltInMethod` / `ApiClassMethod` / `ApiUtilityFunction`）的运行时热路径字段直接作为自身成员（含继承自 `ApiMethodBase` 的部分），不引入间接层、不改变调用约定。移除子类全部 `mutable bool is_static_ / is_vararg_ / has_returns_` 缓存字段，改为直接读 `flags` 位（含内部位 `METHOD_FLAG_NO_RETURN`）。
- **R3** 次要信息打包为新的惰性类型，**首次被访问时动态分配内存并从文件加载**，之后缓存复用；新增访问器暴露。
- **R4** 缺省值信息分族处理：
  - 缺省值**个数**：置于 `ApiMemberMethodBase`（class + builtin 的公共基类），**不得**置于通用基类 `ApiMethodBase`，utility 不持有（json 无数据）。
  - 缺省值**实体值**：惰性。class 与 builtin 需要时加载；utility 永不需要。
  - builtin 的缺省值**仅在外部传参不足、确实需要时**才触发加载（不得因调用 `validated_call` 而无条件加载）。
- **R5** **保真原则**：`extension_api.json` 提供的字段必须完整表示，不得因「当前代码未读取」而删除或简化。推导信息可用内部编码（如 `METHOD_FLAG_NO_RETURN`），但**对外接口必须屏蔽内部位**。当前明确的唯一例外是 `id`（json 无此字段）。
- **R6** 同步重构 `ApiStoreWriter` / `ApiParser`（`src/api_tool/editor/`）与 `ApiStoreReader` / `ApiLoader`（`src/api_tool/core/`）及其文件读写格式。
- **R7** 迁移全部调用点（`src/editor/codegen/`、`src/runtime/bridge/`、`src/api_tool/`）；不留别名、兼容壳或弃用路径。**不得引入 `to_method_info()` 之类的重建壳**。
- **R8** 容器统一沿用 `godot::LocalVector<T>`（不引入自定义容器；`Arr<T,U>` 方案已废止）。
- **R9** 不考虑生成数据的版本与兼容（可自由改文件格式、magic、布局）。
- **R10** 仅 v8 + 动态绑定路径构建与验证；不涉及多 JS 引擎与静态绑定。
- **R11** 实施前先构建 + 跑编辑器代码生成，固化 `project/gen`、`project/typings` 基线；重构后重跑同一触发链，产物 diff 必须为空。

## Acceptance Criteria

- **AC1** `src/api_tool/` 的 API 类型定义中不再出现 `godot::MethodInfo` 成员；且不存在 `to_method_info()` 之类的 `godot::MethodInfo` 重建接口（`grep -rn "to_method_info" src/` 无命中）。
- **AC2** 所有 class 方法常驻内存以「改动前 3,475,812 B → ≤ 1,737,906 B（降 ≥ 50%）」为门槛；**实测布局测算为 1,110,366 B（降 68.1%）**。改动后须给出运行时实测数字复核。
- **AC3** builtin 缺省参数在改动后仍能正确填充：`jsb_primitive_bindings.cpp` 与 `api_tool_types.h` 的 `validated_call` 路径取到与改动前完全相同的 `Variant` 值与类型；且该数据**仅在实际缺参时**才被加载。
- **AC4** `python misc/verify_codegen.py --godot <abs exe>` 退出码 0，`gen/`、`typings/` 与基线**零差异**（忽略 CRLF→LF 归一化后的行尾差异）。
- **AC5** C++ 测试（`godot --headless --path ./project --jsb-run-tests`，`tests=yes` 构建）全绿：exit code 0 且无泄漏（无未释放 Resource、无 Orphan StringName）。
- **AC6** TS 集成测试（`godot --headless --path ./project --verbose`）全绿。
- **AC7** 惰性加载的段长 / 计数一致性有硬校验：detail 段消费字节数必须等于段长、detail 参数个数与缺省值个数必须与热层计数一致，不符即 `ERR_FILE_CORRUPT`（不静默截断）。
- **AC8** 改动后连续两次完整触发链（清理 → dump → api-generate → generate-types）产物一致，确定性成立。
- **AC9** 保真性：`get_flags()` **屏蔽内部位 `METHOD_FLAG_NO_RETURN`** 后与 json 的 Godot flags 完全一致；`has_returns()` 对「返回 `Variant`」（`usage & NIL_IS_VARIANT`）的方法仍为 true；`MethodDecl::hint_flags` 仍被正确填充。

## Out of Scope

- 多 JS 引擎（quickjs-ng / QuickJS / Node.js / JSC / Web）路径的构建与验证。
- 静态绑定（`static_binding`）的构建与验证。
- 文档类型（`ApiClassDocument` 等）的惰性化——它们已是按需加载、用完即释放的临时对象；且其容器**保持 `LocalVector`**（R8），本次不改。
- api 数据的版本号、向后兼容、迁移逻辑。
- `jsb_object_bindings.cpp:421-422` 负索引死分支的**行为修复**（仅保持行为不变）。
- `ApiSignalInfo` / `ApiPropertyInfo::arguments` 的结构改造（运行时只用 `name` / `property.name` / `index`，保持等价）。

## Open Questions

（无阻塞项。设计决策与依据见 `design.md`。）

## 已知偏离（相对初始设想，均有实测依据）

1. **`Arr<T,U>` 方案废止**：实测 `sizeof(Arr<T>)` = 16 = `sizeof(LocalVector<T>)`，丢掉的 `capacity` 被指针对齐填充吸收，**零内存收益**。改为统一沿用 `LocalVector`（R8）。
2. **删除 `to_method_info()`**：它会伪造出 `id = 0` 的 `godot::MethodInfo`，无法保真；且属 R7 禁止的兼容壳。codegen 直接消费 api_tool 访问器。
3. **`hint_flags` 保留**：`MethodDecl::hint_flags` 是信息承载字段，先前「只写不读故可删」的判断按 R5 保真原则**撤销**。
4. **`METHOD_FLAG_NO_RETURN` 保留为内部编码**：以空闲 bit 256（实测 1..128 已占用）承载「有无返回值」的推导结果，避免额外热字段；`get_flags()` 屏蔽该位后返回纯 Godot flags，`has_returns()` 读原始 `flags_`，判定与原 `has_returns` 完全一致。（第 2 版曾废止，属过度纠正，已恢复。）
5. **utility 的 `validated_call` 增加显式 argc 前置校验**：utility 无缺省值，移除 `default_arguments` 后，今天那条「缺参时用负索引取缺省值」的路径（dev 下 `CRASH_BAD_UNSIGNED_INDEX`、release 下越界读）被替换为受控报错。合法调用行为不变；非法调用从 UB 变为显式失败——这是移除动作的必然伴生，非额外语义修复。
