# api_tool 热/冷字段约定与惰性加载

> 适用范围：`src/api_tool/`（类型定义、store 读写、parser、loader）及其调用点（`src/editor/codegen/`、`src/runtime/bridge/`）。
> 涉及 api store 文件格式改动、方法记录字段增删、惰性加载路径时**必读**。
> 设计依据：`.trellis/tasks/09-19-optimize-api-tool-methodinfo-lazy/design.md`（§3 热层、§6 惰性、§7 三段式、§11 内存）。

## 1. 核心约定：热字段 vs 冷字段

`api_tool` 的方法记录**必须**按访问频率拆成两层。这不是风格选择，而是内存契约（AC2：class 方法 3,475,812 B → 1,110,366 B）。

### 1.1 判定标准

| 层 | 判定 | 存放 |
|---|---|---|
| **热** | 运行时 ptrcall 路径**每次调用**都可能读的字段 | `ApiMethodBase` 及子类**内联成员**，`_FORCE_INLINE_` 访问器 |
| **冷** | 只有 `src/editor/codegen/` 需要，运行时不读 | `internal::ApiMethodDetail` + `ApiMethodDetailStorage`，**首次访问才分配并读文件** |

**保真原则（R5）**：冷层不是「丢弃不用的字段」，而是「**完整保存**所有 json 提供的字段，只是延迟加载」。凡 `extension_api.json` 有的字段一律保真表示，唯一例外是 `id`（json 无此字段，`MethodInfo::id` 恒为 0，无源数据可对齐）。

### 1.2 字段归属（实测基准，`src/api_tool/api_tool_types.h`）

```
热：ApiMethodBase (48 B)
    name_(8) args_(8) storage_(8) hash_(4) flags_(4) return_type_(4)
    arg_count_(2) method_index_(2) return_meta_(1)

热：ApiMemberMethodBase (56 B)  ← class + builtin 的公共基类
    + default_count_(2)          缺省值「个数」；utility 无此字段

热：ApiClassMethod (64 B)    + method_bind(8)
热：ApiBuiltInMethod (72 B)  见头文件
热：ApiUtilityFunction (64 B) 无缺省值（json 实测 114 个函数全为 0）

冷：internal::ApiMethodDetail (64 B)
    godot::PropertyInfo return_val + LocalVector<PropertyInfo> arguments
```

> **缺省值「个数」必须落在 `ApiMemberMethodBase`**，不得进通用基类 `ApiMethodBase`（utility 不持有），也不得进 utility。
> `VariantUtil::check_argc` 在 `default_num > 0` 时允许少传参，class 族有 1040 个方法带缺省值——丢掉这个计数会**拒绝合法调用**。

### 1.3 `ApiMethodArg` 必须是两个字节收窄字段（别名 `VariantType` / `ArgMeta`）

```cpp
using VariantType = uint8_t; // godot::Variant::Type
using ArgMeta = uint8_t;     // GDExtensionClassMethodArgumentMetadata

struct ApiMethodArg {
    VariantType type;
    ArgMeta meta;
};
static_assert(sizeof(ApiMethodArg) == 2, "ApiMethodArg must stay 2 bytes");
```

别名**只表意，零运行时成本**（`using` 是编译期等价，不改机器码）。它存在的原因是让字段读起来像「一个 Variant 类型 / 一个参数元数据」，而不是裸 `uint8_t`。

**收窄写入点**（改字段类型时同步）：`(VariantType)pi.type` 与 `(ArgMeta)...`——`api_tool_parser.cpp` ×2、`api_tool_loader.cpp` ×1。

> **Warning**：`sizeof(godot::Variant::Type)` 实测 = **4**。写成 `{Variant::Type; uint8_t;}` 会被 padding 到 **8 B/参数**（4 + 1 + 3 尾部填充），不是 5。
> `Variant::Type` 成员数实测 40（含 `VARIANT_MAX`），`GDExtensionClassMethodArgumentMetadata` 最大实测 13 → 两者 `uint8_t` 足够。

**实测对比（17,974 参数，json 全量）**：

| 方案 | 参数块 | 全族合计 | 备注 |
|---|---|---|---|
| `{u8,u8}`（现状） | 2 B/arg = 35,948 B | **896,828 B** | 基类 48 B 不变 |
| `{Variant::Type,uint8_t}` 单块 | 8 B/arg = 143,792 B | 1,004,672 B（**+12.0%**） | padding 主导 |
| 拆两数组 `arg_types[]`+`arg_metas[]` | 5 B/arg = 89,870 B | 1,094,230 B（**+22.0%**） | 基类 48→56（+8/方法） |

拆两数组的逐桶归因：**36.5% 方法 0 参数**（白付 +8 B/方法）、**41.2% 单参数**（+8 −3 = +5 B/方法），打平点在参数数 ≥ 3（真实分布 92.9% 的方法 ≤2 参数）→ **100% 的方法上更差**。

**零扩展指令（实测，x86-64 MSVC ABI，clang `-O2`，真实头文件 + 真实编译选项）**：`get_argument_type` 是 `static_cast<Variant::Type>(arg_at(i).type)`（load u8 + zext），在 `validated_call` 每参数循环里。但机器码上它**零代价**：

```
movzbl (%rax,%rcx,2), %eax   ; load u8 + zext 折叠成单条 movzx
movl   (%rcx,%rax,4), %eax   ; 对照：Variant::Type 直读，同为 1 条
```

`zext` 只存在于编译器 IR（中间表示，不进 dll）；指令选择把它折进寻址。`{Variant::Type,uint8_t}` 的唯一额外收益是省掉这条 IR 指令——机器码零差异，不值 +12.0% 内存。

### 1.4 布局由 `static_assert` 强制，不靠注释

`api_tool_types.h` 末尾对上述全部尺寸有 `static_assert`。**布局漂移必须编译失败**（多一个热字段、`godot::MethodInfo` 成员被加回来，都会立刻暴露，而不是悄悄把这次优化撤销）。若刻意改变尺寸 → 同步更新 assert 与 `design.md` §11 / 任务 `report.md` 数字。

### 1.5 `ApiMethodDetailStorage` 的归属（2026-09-20 抽离）

`api_tool_types.h` 是**直接面向调用方**的热层头，除强制内联的热访问器外**不含内部实现细节**。`ApiMethodDetailStorage` 因此**不在**它里面，其 editor-only 面也不在它自己的 `public` 段里：

| 文件 | 内容 | 编译归属 |
|---|---|---|
| `core/api_tool_detail_storage.h` | 类声明（唯一）：runtime 接口 + `push_injected` 等两侧共用项 | 两侧都 include |
| `core/api_tool_detail_storage.cpp` | runtime 路径：`configure_lazy` / `push_injected` / `ensure_details` / `ensure_defaults` / `get_detail` / `get_defaults` | **runtime + editor**（`core/*.cpp` 在两侧 glob） |
| `editor/api_tool_detail_storage_editor.h` | editor 访问器 `ApiMethodColdAccess` 声明（单 friend struct，`ApiMethodDetailStorage&` 作入参） | **仅 editor** |
| `editor/api_tool_detail_storage_editor.cpp` | editor 路径定义：`push_cold` / `seal_cold` | **仅 editor**（`editor/*.cpp` 只在 `editor_globs`） |

`api_tool_types.h` 里只留 `class ApiMethodDetailStorage;` 前向声明（热层成员是 `storage_` 裸指针）。

**editor 面由单 friend struct 收口**（2026-09-20）：`push_cold` / `seal_cold` / `cold_details` / `cold_default_count` / `cold_defaults` 从类的 `public` 段移到 `ApiMethodColdAccess`，类里只加一行 `friend struct ApiMethodColdAccess;`。理由：这 5 个的调用点全在 editor（`api_tool_parser.cpp`、`api_tool_store_writer.cpp`），而 `core/api_tool_detail_storage.h` 是 runtime 也编译的头，不该向 runtime 消费者公示 editor-only 接口。形态照 `ApiMethodHotWriter` 先例（`api_tool_types.h` 前置声明 → 单一 friend → 定义在 `editor/`）。

- **`cold_*` 三个必须保持类内 `_FORCE_INLINE_`**（现位于 `ApiMethodColdAccess` 头内）：writer 在 `for (i < p_method_count)` 循环里逐方法调用，量级 16k+；移成外部函数会变成每方法一次调用。
- **`seal_cold()` 的 `details_loaded_.store(true)` / `defaults_loaded_.store(true)` 不是 editor 状态**：那是 runtime 的「已就绪，别再读文件」短路位（见 `ensure_details` / `ensure_defaults`）。editor 路径靠「已用内存填好」达到同一状态。抽离时不可删。
- 验证 runtime 侧隔离：`ls .build/runtime/ | grep detail_storage_editor` 应为 **0**；`.build/runtime/api_tool*.obj` 中 `ApiMethodColdAccess` 符号数应为 **0**。

> **为什么 editor 路径必须单独成 TU**：`push_cold`/`seal_cold` 是 JSON 解析期的填充路径，运行时不存在——放在 `core/` 会让 runtime 扩展也链进这份代码。SConstruct 的 glob 是唯一接线点（`runtime_globs` 不含 `api_tool/editor/*.cpp`）。

## 2. `METHOD_FLAG_NO_RETURN`：内部编码的写入与屏蔽

「有无返回值」不占独立热字段，而是编码进 `flags_` 的空闲位：

```cpp
enum MethodFlagsExt : uint32_t {
    METHOD_FLAG_NO_RETURN = 1u << 8,   // bit 256
};
```

- **写入单点**：`internal::ApiMethodAccess::setup(...)`（internal 访问器，3 处调用：`api_tool_parser.cpp` ×2、`api_tool_loader.cpp` ×1）；NO_RETURN 位的合入发生在其内部。绝不手写 `| METHOD_FLAG_NO_RETURN`。
- **内部查询**：`has_returns()` 与子类 `validated_call()` 读**原始** `flags_`。
- **对外契约**：`get_flags()` **必须屏蔽**该位（`flags_ & ~METHOD_FLAG_NO_RETURN`），api_tool 的内部编码不得外泄。
- **store 往返**：writer 直读原始 `flags_`（`serialize_method_hot` 是 `ApiMethodBase` 的 friend）。**不得**用 `get_flags()` 写 store——那会屏蔽掉内部位，往返后 `has_returns()` 无法恢复。
- **无 `get_flags_raw()`**：公开访问器只有屏蔽语义的 `get_flags()`；原始值读取走 friend，不暴露第二个 flags 接口。

bit 256 空闲的依据：引擎 `MethodFlags` 用 1/2/4/8/16/32/64/128，GDExtension 侧最大 `VIRTUAL_REQUIRED = 128`。

### `ApiMethodAccess` 是唯一写入点

`ApiMethodBase` 故意不暴露 setter。写入只经 `internal::ApiMethodAccess` 的静态函数（`setup` / `set_index` / `set_args` / `set_storage` / `set_default_count` / `get_flags_raw`）；`setup` 内部完成 `METHOD_FLAG_NO_RETURN` 的编码（单点，`p_has_returns` 为假时或入该位）。

定义位置（2026-09-20 迁出 `api_tool_types.h`）：

| 文件 | 内容 |
|---|---|
| `core/api_tool_access.h` | `ApiMethodAccess` 声明（`api_tool_types.h` 只留 `struct ApiMethodAccess;` 前向声明） |
| `core/api_tool_access.cpp` | 全部 6 个静态函数实现（原在 `api_tool_types.cpp`） |

理由与 `ApiStoreReader` / `ApiLoader`（core）、`ApiMethodHotWriter`（editor）一致：这四个 friend 访问器都应是「`api_tool_types.h` 前置声明 + 定义归所属子目录」。`ApiMethodAccess` 此前是唯一就地定义在 `api_tool_types.h` 的例外。

- **`friend struct internal::ApiMethodAccess;` 两行不用改**（`ApiMethodBase` / `ApiMemberMethodBase`）：朋友关系与定义位置无关。
- 使用方需 include `api_tool/core/api_tool_access.h`（`core/api_tool_loader.cpp`、`core/api_tool_store.cpp`、`editor/api_tool_parser.cpp`）。`core/*.cpp` 在两侧 glob 内，两侧链接结果不变。

> 用 friend struct 而非 friend class `ApiParser`：parser 侧用**自由函数**填充，`friend class ApiParser` 覆盖不到。

## 3. 惰性加载：按实体，不按方法

`ApiMethodDetailStorage` 是**实体级**（一个 class / builtin / utility 一份），`std::shared_ptr` 持有（含 `mutex` / `atomic`，不可拷贝）。

### 3.1 为什么按实体（勿改回按方法）

1. **IO 收益为零**：`FileAccessCompressed::seek` 按 4096 B 块解压，读单方法 detail 与读整实体 detail 的解压代价相同。
2. **记账**：按方法需为 17935 个方法各维护状态与偏移；detail 是变长记录，定位第 i 条必须顺序解析 → 除非再加偏移表，否则不可行。
3. **实体 detail 实测很小**：上界 class 83,208 B（RenderingServer，535 方法）、builtin 10,752 B（String）。utility 的 detail 非空（193 个参数分布在 114 个函数上，parser 照常填 `return_val` + `arguments`），但**缺省值恒为 0**（json 无数据），故其 defaults 段为空。
4. **消费方本就按实体遍历**（`jsb_codegen_type_db.cpp` 对每个实体遍历其全部方法）。

### 3.2 detail 与 defaults 独立发布

`get_detail(i)` 与 `get_defaults(i)` **各自双检加锁、独立发布**——builtin 缺参时**只**加载 defaults，不拉 `PropertyInfo` detail（否则 String 的 116 个方法参数名会被白白加载）。

```
p = details_.load(acquire);          // 快路径：一次 acquire load + 下标，无锁
if (p == nullptr) {
    lock(mutex_);
    p = details_.load(relaxed);
    if (p == nullptr) {
        p = new ApiMethodDetail[total];   // 首访才分配
        ApiStoreReader::read_method_details(...);   // 失败则不发布指针
        for (i) p[file_method_count_ + i] = std::move(injected_[i]);
        details_.store(p, release);
    }
}
```

- **失败语义**：读取非 OK 时**不发布指针**，返回静态空 detail + `ERR_PRINT_ONCE`，与既有「加载失败打日志并降级」一致。
- `defaults_` 是一整块 `Variant[]`，`default_offsets_[i]` 给起点 → **避免 17935 个小 `LocalVector` 分配**。

### 3.3 双路径共用同一访问对

| 路径 | 填充方式 |
|---|---|
| editor（parser → writer） | `push_cold()` / `seal_cold()`，writer 从内存读回落盘 |
| runtime（loader，读文件） | `configure_lazy()` + 首访 `read_method_details` / `read_method_defaults` |

两条路径共用 `get_detail()` / `get_defaults()`，所以 editor 与 runtime 的行为差异只存在于**填充侧**。

### 3.4 Object core 注入尾

`Object` 的 `FLAG_OBJECT_CORE` 虚函数在 store 文件中**不存在**（GDExtension 未暴露），由 loader 在运行时合成：

- `push_injected()` 把 detail 追加到 file 记录**之后**；`build_injected_arg_block()` 建参数块。
- 注入方法的 `method_index_` = `file_method_count + i`。**索引空间对齐是这里唯一的出错点**：`details_` 数组 = file 记录 + `injected_` 尾，`get_detail(i)` 直接按 `method_index_` 下标访问，两侧顺序必须一致。
- 合成方法的 `hash` 保持 **0**（`godot::MethodInfo` 不带 hash，与原实现一致）。

### 3.5 缺省值补齐**只**发生在 `validated_call()` 内 —— 调用点不得重复补齐

三个 `validated_call` 各自**自包含**缺省值语义：

| 版本 | 缺省值行为 |
|---|---|
| `ApiBuiltInMethod::validated_call` | `missing = method_argcount > argc ? 差 : 0`；`missing > 0` 时才 `get_defaults()`，不足的参数从缺省值尾段补齐 |
| `ApiClassMethod::validated_call` | 不碰缺省值——`object_method_bind_call` → 引擎 `MethodBind::call` → `call_with_variant_args_dv(..., get_default_arguments())` 自行补齐 |
| `ApiUtilityFunction::validated_call` | 无缺省值（恒 0）；`argc < method_argcount` 直接报错 |

**因此调用点必须传「调用方实际传了几个」的原始 `argc`。** 调用点若自行把 `argc` 补齐到 `method_argcount`（或先填一遍缺省值），则：

1. `validated_call` 内部的 `missing` 恒为 0 → 惰性 defaults **永远不触发**，实现形同虚设；
2. builtin 路径会**触发两次** `get_defaults()`（调用点一次 + 内部一次），白付懒加载代价；
3. 调用点需要自己维护 `allocated_argc` / `known_argc` / `default_index` 一整套簿记，与实际参数数目脱钩。

**已清理的历史重复**（2026-09-20）：

- `jsb_primitive_bindings.cpp` `call_builtin_function`：曾把 `argc` 补齐到 `allocated_argc = MAX(known_argc, argc)` 并自行从缺省值尾段填 `args[]`——删除整个补齐块，`argv`/`args` 按原始 `argc` 分配，`validated_call(..., argc, ...)`。
- `jsb_object_bindings.cpp` class 方法路径：曾无条件 `get_defaults()` 并按 `default_index = index - method_argc` 取缺省值——**该分支在 `check_argc` 通过后恒不可达**（`argc <= method_argc` ⇒ `default_index < 0`），且白触发一次懒加载。删除，改由引擎补齐。

**判定新调用点是否违规**：

```bash
grep -rn "allocated_argc\|defaults_resolved" src/runtime/   # 必须为空（「调用点补齐 argc」的簿记）
grep -rn "get_defaults(" src/api_tool/api_tool_types.h      # 只应命中 builtin 的 validated_call
grep -rn "get_defaults(" src/editor/codegen/                # codegen 的正常消费，允许
```

⚠ **`src/runtime/` 下确实存在 `get_defaults(` 与 `default_index`，但性质不同，勿据此「清理」**（2026-09-20 更新）：

| 位置 | 用途 | 性质 |
|---|---|---|
| `jsb_primitive_bindings.cpp` `call_builtin_function` | 显式 `undefined` 命中可缺省位 ⇒ 取该位缺省值（§3.5b） | **必要**：`Variant` 无法承载「显式 `undefined`」，只有 bridge 能判 |
| `jsb_object_bindings.cpp` `_godot_object_method` | 同上 | **必要**，同上 |

两处都**不改写 `argc`**（`argc` 恒等于调用方传入的数量），且只在**首次命中**时 `get_defaults()`。这里被禁止的是「调用点自行把 `argc` 补齐/填一遍缺省值，再交给 `validated_call`」——那才是重复补齐。判据不是「有没有出现 `get_defaults`」，而是**`argc` 有没有被调用点改写**（查 `allocated_argc` / `defaults_resolved` 更准）。

**只有「非 vararg 且 `default_count > 0`」的 builtin / class 方法才可能被少传参**（`check_argc` 对 vararg 与 `default_num == 0` 都要求参数数精确匹配），所以懒加载 defaults 只在缺参调用时发生。

### 3.5b 显式 `undefined` ⇒ 取**该位置**的缺省值 —— 按位置替换，**不是**裁剪 arity

`f(a, undefined)` 必须等价于 `f(a)`（JS default initializer 语义：`undefined` 触发缺省值）。**契约是按位置替换**：

> `i ∈ [min_argc, known_argc)`（`min_argc = argument_count - default_count`）且调用方在该位传了显式 `undefined`
> ⇒ 该位取 `defaults[i - min_argc]`；其余位置照常按声明类型转换。**不移位、不跳过、不裁剪元数**。

**为什么不能裁剪 arity**（第一版曾把「尾部连续 `undefined`」折算成更小的 `argc`，并为此加了 `TypeConvert::trim_trailing_undefined`；**该函数已整体删除，勿改回**）：

1. **位置错**：它挂在 `TypeConvert` 上，而 `TypeConvert` 的职责是「JS 值 → Godot 值」的类型转换，与「调用元数」无关。把元数折叠塞进类型转换层，等于让这一层替调用层决定实参个数。
2. **语义错**：裁剪只对「尾部连续」成立，表达不了中间位的替换。`set_code_region_tags(undefined, "END")` 这种调用（第 1 位取缺省、第 2 位仍是 `"END"`）在裁剪语义下**必然抛错**；若改成裁中段，后面的实参会前移、调用语义被静默改写。
3. 裁剪把「调用方传了几个位置」与「其中几个生效」两个**不同**的量压成一个 `argc`，任何后续依赖「按位置」的逻辑都会失准。

**判定位置必须在 bridge，不能在 `validated_call`**：`Variant` 无法表示「显式 `undefined`」——一旦按 `Variant` 数组传进 `validated_call`，该信息已经丢失。只有 bridge 还持有 `v8::Value`，`IsUndefined()` 才是可判定的。

**必填位 / vararg 位传 `undefined` 都不是缺省语义**：`i < min_argc` 时 `undefined` 是值，按声明类型转换，不符即报错（`Curve2D.get_point_position(undefined)` 仍抛错）；vararg 位同理——缺省值与 vararg 不共存（§3.5），其上的 `undefined` 是真实实参。

**元数校验仍用「调用方实际传入的数量」**（`argc_passed` / `provided`）：`Vector2.limit_length(undefined, undefined)`（N+1）继续抛错。

**三个实现点**（新增调用点必须同步，否则腿间行为分叉）：

| 路径 | 位置 | 替换方式 |
|---|---|---|
| builtin 动态 | `jsb_primitive_bindings.cpp` `call_builtin_function` | 命中该位 ⇒ 直接取 `method->get_defaults(...)[i - min_argc]` 填入该位的 `Variant` 槽，**不进转换** |
| class 动态 | `jsb_object_bindings.cpp` `_godot_object_method` | 同上（`min_argc = method_argc - method_default_count`） |
| builtin 静态 thunk | `thunks/builtin_methods.h` | 判定收成 `use_default_mask`；命中位在 marshal **之前**改道 `default_arg_slot<>()`，即该位不进 `marshal_one` |
| class 静态 thunk | `thunks/class_methods.h` | 判定收成 `use_default_mask`；命中位从方法记录取 `defaults[i - M]` 填入**本 thunk 自己的 `Variant argv[i]`** |

- 动态两处都只在**首次命中**时才 `get_defaults()`（`defaults == nullptr` 才取），故 AC3「builtin 缺省值仅在实际需要时加载」不变：触发条件从「少传参」扩为「少传参 **或** 命中替换位」。
- 静态 builtin thunk 的 `default_arg_slot<>()` 是函数内 magic static，仍是首次命中才构造。

**class 静态 thunk 没有缺省字面量**（codegen `class_entry_expr` 明写「class thunks carry NO default literals」；缺省值由引擎 `MethodBind` 补齐），所以它向方法记录取该位缺省值：

- **不是「整通回退动态路径」**（第一版曾这么写，已废弃）：那样会把静态腿白扔掉——重解析 `this`、重查 method bind、全部参数再走一遍 Variant 装箱与 `validated_call`。class thunk **本来就在建 `Variant argv[]` 并调 `object_method_bind_call`**，缺省值只需就地填进它自己的槽，调用**不离开静态腿**。
- 取记录：注册处（`jsb_object_bindings.cpp` class 方法循环）对 **`default_count > 0`** 的静态 thunk 传 `(void *)&method_info` 作 Data；thunk 经 `static_binding::class_method_defaults()`（`dispatch.h` 声明、`jsb_object_bindings.cpp` 定义，把 api_tool 类型挡在静态绑定头之外）取回缺省值数组。
- 取默认值**只在命中时发生**（首次命中才调 `class_method_defaults`：缓存指针为 `nullptr` 时），miss 路径不碰惰性加载。
- 记录长度不足时该位退化为普通转换（逐位范围检查），不做越界读。

#### 三条实现硬约束（实测代价，违反即二进制膨胀）

> 这三条由**聚合体积实测**（整份 `dispatch_class.gen.obj` 的 `.text` 总和，15370 个实例化在改动前/后同一位置构建）逼出来。单实例化抽样**看不到**下列问题。

1. **整个替换块必须落在 `if constexpr (M < N)` 内，`else` 分支逐字复述改动前的语句。** 缺省值缓存（`cached_defaults` / `cached_count`）与加载判定都不得留在 `if constexpr` 之外——那样**无缺省位的实例化也会付初始化指令**。实测：把两个声明放在外面，无缺省位的 14330 个实例化各多付 18 条（合计 +2.3 MiB）。修正后对照方法家族 **521 → 521（Δ 0，逐符号相同）**。
2. **替换辅助函数必须是单个非泛型可调用体**（如 `auto substitute_default = [&](int i) -> bool`），**不能**写成 per-position 泛型 lambda（`helper.template operator()<I>()`）——后者为每个可选位各生成一份完整函数体。实测差异：泛型版 TARGET 家族 +198 条，非泛型版 **+130** 条。
3. **无缺省位的方法（`M == N`）必须是零增量**。这是「1040 个带缺省位 vs 14330 个不带」的数量级差距决定的——任何 per-instantiation 固定开销都会被后者放大 14 倍。

**builtin thunk 同样适用约束 1**：`use_default_mask` 的构造循环、marshal 趟、可选位 arg_ptrs 趟必须整体落在 `if constexpr (M < N)` 内。`slots` / `ok` / `arg_ptrs` 三个声明提到 `if constexpr` 外（两分支都要用），`use_default_mask` 留在内层。

> **`use_default_mask` 必须声明在 `if constexpr` 内——已实测代价。** 试图把它提到分支外（配合 `!(M < N && ...)` 的形式消掉两分支重复）会让 680 个无缺省位实例在 `/Od` 下各付一个栈槽：`dispatch_builtin.gen.obj` `.text` **+29,010 B**。改为「mask 留在分支内、marshal 趟写两遍」后回到 **+0**。
>
> **唯一已确认可安全共用的是 `[0, M)` 前缀指针趟**（2026-09-20）：它只写 `[0, M)`，与分支内写的 `[M, N)` 下标不重叠、先后无关，故提到分支外共享。共享的是这一趟，**不是** marshal 趟——marshal 趟读 mask，且必须在 `if (!ok) return;` 之前。`default_arg_slot<>` 有惰性初始化副作用，其调用位置相对 `ok` 检查的次序**不可重排**（改动前次序为：先判 `ok`，再构造可选位指针）。

**utility thunk（全局 utility 函数，见下文第 1 类）已于 2026-09-20 统一为与 builtin 相同的形态**：同一个 `if constexpr (M < N)` 栅栏、同一个 mask 判定、同样的 `[0, M)` 共享趟。此前它无栅栏，可选位机制无条件存在，且判据是「`(M+J) < provided`」（只看有没有传，**不**把显式 `undefined` 视为取默认），与 builtin 的 mask 语义不一致。实测该改动使 `dispatch_utility.gen.obj` `.text` **294,973 → 278,428 B（−16,545 B）**：102 个实例全是 `DefVs<>`（`M == N`）→ 全走新 `else` 分支，不再实例化可选位机制。

> 与 class thunk 不同，builtin 的 `else` 分支**不与改动前等价，而是更小**：改动前即使 `[M, N)` 为空区间也照样实例化可选位辅助 lambda，`else` 分支不再引用它们。实测（同位置构建 `dispatch_builtin.gen.obj`）：`.text` 2,904,967 → 2,814,296 B（**−89 KiB**），无缺省位的 680 个实例化共省 23,800 条指令，带缺省位的 87 个实例化付出 4,776 条。class 侧相反：对照方法家族 **521 → 521（Δ 0）**，因为改动前的 class thunk 本就没有可选位机制。**两侧合计 `.text` +617 KiB**（class +691 KiB、builtin −89 KiB）；dll 端到端 **112,743,424 → 113,382,912 B（+625 KiB）**。


**⚠ 「utility」在本模块有**两个**不同所指，勿混为一谈**（混了会得出「site 1 的 utility 分支该删掉」的错误结论）：

| # | 所指 | 注册/分派处 | 有无缺省值 | 是否替换 |
|---|---|---|---|---|
| 1 | **全局 utility 函数**（`sin` / `print` / `type_convert` …，共 114 个） | 静态：`thunks/utility_functions.h` + `dispatch_utility.gen.cpp`；动态：`ObjectReflectBindingUtil::_godot_utility_func` | **恒 0** | **否**（无可命中位，分支数学上不可达 → 死代码） |
| 2 | **String / StringName 的内建方法**（`strip_edges` / `substr` / `split` …） | `VariantBind<String>::reflect_bind_utilities` → `_utility_method` → `call_builtin_function(..., utility = true)` | **有**（如 `strip_edges(left=true, right=true)`、`substr(from, len=-1)`） | **是**（它们是带缺省值的 builtin 方法，只是绑定形式借用 `utility=true`；`info[0]` 是接收者） |

- 第 1 类**无需替换**：json 实测全部无缺省值 ⇒ 无可命中位；静态 thunk 里同样 `M == N`。`thunks/utility_functions.h` 因此**无替换语义可命中**（2026-09-20 已把它的静态形态统一为与 builtin 相同的栅栏+mask 形式，见上文 §3.5 那条注记；该改动不改变任何可达语义，只是消除死代码与形态漂移）。
- 第 2 类**必须替换**：走的是 `call_builtin_function`，`p_base = 1`（`info[0]` 是接收者），命中判定与 builtin 方法同源。`reflect_bind_utilities` **不查静态表**（实测该函数内 `find_builtin_thunk` 命中 0），所以 String 方法**永远走动态腿**——这条正是 `call_builtin_function` 里 `utility ? 1 : 0` 的用途，也是「两个 utility 不是一回事」的直接证据。

### 3.6 `owner_name_` 必须放堆上宿主

`ApiClassMethod::get_owner_name()` 从 storage 读取。**不能**把 `owner_name_` 指进 `ApiClass::name`：`ensure_class` 先在栈上构造 `ApiClass` 再拷入 `std::deque`，对象地址会变 → 悬垂。放在堆上的 storage 里，同时把 16822 份重复 `StringName`（≈134 KB）去重为 1036 份。

## 4. 文件格式：三段式

一实体一文件，magic `GCPF` + deflate 不变，payload 重排为三段：

```
[hot 段]      方法只写热字段 + 参数紧凑块，同序
              u32 detail_method_count
              u64 detail_section_size
────────────────────────────────────────────────────────
[detail 段]   method_detail[0 .. detail_method_count)    总长 = detail_section_size
              每条 = PropertyInfo return_val + u32 arg_count + PropertyInfo[arg_count]
────────────────────────────────────────────────────────
[defaults 段] u32 defaults_method_count
              u16 default_counts[defaults_method_count]
              Variant[...]                               总长由计数决定
```

- `detail_offset` = 读完 hot 段那 12 字节头部后的 `file->get_position()`（`FileAccessCompressed::get_position()` 返回**解压**偏移）→ **无 trailer**。
- `defaults_offset` = `detail_offset + detail_section_size`（自描述）→ builtin 可**只读 defaults 段**，完全不解析 detail。
- **detail 长度占位回填**：写入时长度未知，先写 `u64 0` 占位，写完 detail 段后 `seek` 回去 patch。**依赖 `FileAccessCompressed` 写模式支持 seek 回写**——换 payload 实现时须重新确认这一前提。
- defaults 段先写全部计数再写平铺值块 → 只要计数的读者无需解码任何 `Variant`。

## 5. AC7 一致性硬校验（不静默截断）

惰性段的自我描述必须在两处交叉校验，不符即 `ERR_FILE_CORRUPT`：

| 校验点 | 位置 |
|---|---|
| detail 段**消费字节数 == `detail_section_size`** | `read_method_details` 结尾 |
| detail 每方法 `arg_count` == 热层 `arg_counts[i]` | `read_method_details` 循环内 |
| `defaults_method_count` == detail 方法数 == 热层方法数 | `read_method_defaults` 开头 |
| 每 `default_counts[i]` == 该方法的 `default_count_` | `read_method_defaults` 循环内 |
| 热层计数超 `uint16_t` / `uint32_t` | 写入时报错，不截断 |

> 这三重校验承担的是原 `MethodInfo` 结构自带的一致性保证——去掉结构体后必须显式补回。
> **负向测试方法**：篡改一个已生成的 store 文件的热段计数（如 `.godot/.api_dumping/classes/Node.capi` 的 `method_count` 133→134），引擎应报 `store corrupt` 并 `load class Node failed: File corrupt`、非零退出。测后按 md5 还原。

## 6. ⚠ `LocalVector::resize()` 对平凡类型**不初始化**

**症状**：计数向量读到垃圾值。

**成因**：`LocalVector<T>::resize()` 对 **trivially constructible** 类型走 `_resize<false>`（`third/godot-cpp/include/godot_cpp/templates/local_vector.hpp:191`）→ 只 `reserve()`，不写元素。`uint16_t`、`uint32_t` 都中招。

**后果（本项目实际踩中）**：`read_utility_functions` 的 `default_counts` **从未被赋值却传入 `set_hot_counts`**，AC7 会拿垃圾值比对。

**规则**：
- 「先 `resize(n)` 再逐元素赋值」的计数向量 → 用 `resize_initialized(n)`。
- 「先 `resize(n)` 再逐元素读取填充」→ 同样 `resize_initialized(n)`（读之前值无意义，但 AC7 比对在个别路径会提前碰到）。
- 仅当**保证每个元素都会被无条件赋值**时才可裸 `resize()`；拿不准就用 `resize_initialized()`（代价是一次 memset）。

## 7. 验证要求（改动本模块时的最小验收）

| 项 | 命令 | 判据 |
|---|---|---|
| 产物零差异 | `python misc/verify_codegen.py --godot <**绝对** exe 路径>` | exit 0，`✅ 校验通过` |
| C++ 测试 | `scons ... tests=yes -j5` 后 `godot --headless --path ./project --jsb-run-tests` | runtime `50\|50` + editor `3\|3`、`running tests result: 0`、无泄漏 |
| TS 集成 | `godot --headless --path ./project --verbose` | `[JS] result: PASS` |
| 一致性负向 | 篡改热段计数后加载 | 报 `store corrupt` 并非零退出 |

> **`tests=yes` 静默失效陷阱**：规范编译命令（**不带** `tests=yes`）的产物**不含测试套件**，在其上跑 `--jsb-run-tests` 会**静默 no-op**（无 `test cases:` 汇总行、`grep -c doctest` = 0），而 TS 集成测试仍正常。
> 判别：日志出现 doctest 汇总行才算生效。采完测试证据如需恢复规范产物，再跑一次规范命令。

## 8. Wrong vs Correct

### Wrong：把冷字段塞回热结构

```cpp
// ❌ 每方法常驻 48 B/参数 + MethodInfo 120 B —— 这次优化要消灭的正是它
struct ApiMethodBase {
    godot::MethodInfo method;
    MethodHash hash;
};
```

### Correct：热层内联 + 冷层惰性

```cpp
// ✅ 热路径字段内联；冷字段首访才从文件加载
struct ApiMethodBase {
    // ... 48 B 热字段，见 §1.2
    _FORCE_INLINE_ uint32_t get_flags() const { return flags_ & ~internal::METHOD_FLAG_NO_RETURN; }
    const internal::ApiMethodDetail &get_detail() const;   // 冷，首访触发
};
```

### Wrong：手写内部位

```cpp
// ❌ 内部编码从多处写入 → 迟早漏一处或写出不一致的判别
method.flags_ = godot_flags | internal::METHOD_FLAG_NO_RETURN;
```

### Correct：单点编码

```cpp
// ✅ 写入只经 ApiMethodAccess（internal 访问器）；判别只经 has_returns()
//    NO_RETURN 位的编码在 setup 内部完成，调用方不接触该位
ApiMethodAccess::setup(m, name, hash, json_flags, has_return, ...);
```

## 9. 相关规范

- ptrcall 参数内存分配：[ptrcall-encoding.md](./ptrcall-encoding.md)
- 生成文件保护（`project/gen/` 属生成产物）：[generated-files.md](./generated-files.md)
- 基线校验方法论：[../test/codegen-baseline.md](../test/codegen-baseline.md)
- C++ 头文件禁令与临时文件位置：[coding-standards.md](./coding-standards.md)
