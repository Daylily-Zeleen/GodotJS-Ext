# 设计：ApiMethodBase 移除 godot::MethodInfo + 惰性次要信息

依据 `prd.md`。所有尺寸/计数均为**实测**（`.agent_tmp/` 探针，见 §12）。
本轮按用户 5 条调整重写；其中 3 条推翻了第 1 版设计的决策（见 §1 末与 §10）。

---

## 0. 本轮 5 条调整的落实

| # | 调整 | 落实 |
|---|---|---|
| 1 | `Arr<T>` 省不了内存 → 仍用 `LocalVector<T>` | 方案废止，容器不动（§2）。R8 明确沿用 `LocalVector` |
| 2 | 惰性加载为何按实体而非按方法 | 给出实测依据（§6） |
| 3 | `to_method_info()` 的必要性 | **删除**该设计。它伪造 `id=0`、无法保真，且属 R7 禁止的兼容壳（§10.2） |
| 4 | 默认值相关信息不要进 `ApiMethodBase` | 计数移入 `ApiMemberMethodBase`（class + builtin）；utility 不持有（§5） |
| 5 | 不得因「没被读取」而省略次要字段；`id` 例外 | 全面**保真**（R5）。`hint_flags` 恢复保留；仅删 `id`（§4、§9） |

**推翻第 1 版的两处**（当时判断有误）：

- 第 1 版称「`MethodDecl::id` / `hint_flags` 只写不读，可删」。按调整 #5 的保真原则**撤销**：
  `hint_flags` 是信息承载字段，保留并由真实 `flags` 填充。仅 `id` 删除（json 无此字段）。
- 第 2 版曾**废止** `METHOD_FLAG_NO_RETURN`，理由是「对外暴露合成值」——这是**过度纠正**。
  该位是**内部编码**（供 `has_returns()` / `validated_call()` 查询），契约只要求
  **对外 `get_flags()` 屏蔽它**。现已恢复采用（§4）。

---

## 1. 总体形状

`ApiMethodBase` 常驻 `godot::MethodInfo`（实测 **120 B**）＋ 三个堆数组：
`arguments`（48 B/参数）、`arguments_metadata`（4 B/参数）、`default_arguments`（24 B/缺省值）。
拆分后：

| 层 | 内容 | 何时进内存 | 归属 |
|---|---|---|---|
| **热层** | `name` / `flags`（含内部位 NO_RETURN）/ `hash` / 参数紧凑序列 / 参数个数 / 返回类型 / 返回元数据 | 随实体文件加载 | 方法自身（裸指针） |
| **热层（族专属）** | 缺省值**个数**（class + builtin） | 同上 | `ApiMemberMethodBase` |
| **参数紧凑块** | 每参数 `{uint8_t type; uint8_t meta;}` = 实测 2 B | 同上 | **实体级单块** |
| **惰性 detail** | `return_val`（完整 `PropertyInfo`）+ 参数完整 `PropertyInfo` 数组 | 该实体 detail 首访 | 实体级 |
| **惰性 defaults** | `Variant` 值（class 供 codegen；builtin 供缺参填充） | 按需（§5） | 实体级平铺块 |

**热层不持有所有权**：`args_` 指向实体级连续块，`storage_` 指向共享宿主。
故 `ApiMethodBase` 浅拷贝安全（宿主由 `shared_ptr` 持有、地址稳定），无需深拷贝语义。

---

## 2. 容器：沿用 `godot::LocalVector<T>`（调整 #1）

实测：`sizeof(LocalVector<PropertyInfo>)` = **16**（`U = uint32_t`：count 4 + capacity 4 + ptr 8）。
`Arr<T,U>` 方案实测同为 **16**（`U = uint16_t`：count 2 + pad 6 + ptr 8）——丢掉的 4 B `capacity`
被指针对齐填充完全吸收，**零内存收益**。故废止 `Arr`：

- 不新增任何容器文件；`api_tool_types.h` / `api_tool_doc_types.h` / `api_tool_doc_types` 中的
  `LocalVector` **全部保持原样**。
- `api_tool.h:125-126`、`api_tool_loader.h:76-77` 的 `const LocalVector<MethodHash> *` **签名不变**。
- 连带删除原 R5 / AC7 的 `Arr` 相关内容（越界硬校验改由文件格式一致性校验承担，见 §7）。

---

## 3. 热层结构

```cpp
namespace api_tool::internal {
// 每参数 2 字节的紧凑记录。
// ⚠ 必须是 uint8_t/uint8_t：sizeof(Variant::Type) 实测 = 4，写成
//   {Variant::Type type; uint8_t meta;} 会 padding 到 8 B/参数，比现状（4 B）更差。
struct ApiMethodArg {
    uint8_t type;   // godot::Variant::Type（成员数实测 40，含 VARIANT_MAX）
    uint8_t meta;   // GDExtensionClassMethodArgumentMetadata（最大实测 13）
};
class ApiMethodDetailStorage;
}

struct ApiMethodBase {
private:
    godot::StringName name_;                       //  8
    const internal::ApiMethodArg *args_ = nullptr; //  8   实体级块，长度 = arg_count_
    internal::ApiMethodDetailStorage *storage_ = nullptr; // 8  惰性宿主
    uint32_t hash_ = 0;                            //  4
    uint32_t flags_ = 0;                           //  4   真实 flags | 内部位 NO_RETURN
    godot::Variant::Type return_type_ = godot::Variant::NIL; // 4
    uint16_t arg_count_ = 0;                       //  2
    uint16_t method_index_ = 0;                    //  2   实体内方法下标
    uint8_t return_meta_ = 0;                      //  1
protected:
    _FORCE_INLINE_ internal::ApiMethodArg arg_at(uint16_t p_index) const;
public:
    // —— 热访问器（_FORCE_INLINE_，无锁无 IO）——
    const godot::StringName &get_name() const;
    MethodHash get_hash() const;
    uint32_t get_flags() const;                    // **屏蔽**内部位后返回（见 §4）
    uint16_t get_argument_count() const;
    godot::Variant::Type get_argument_type(uint16_t p_index) const;
    GDExtensionClassMethodArgumentMetadata get_argument_metadata(uint16_t p_index) const;
    godot::Variant::Type get_return_type() const;
    GDExtensionClassMethodArgumentMetadata get_return_metadata() const;
    bool has_returns() const;                      // 内部查询：读原始 flags_（见 §4）
    bool is_vararg() const;                        // flags_ & METHOD_FLAG_VARARG
    // —— 冷访问器（首访触发该实体惰性加载）——
    const internal::ApiMethodDetail &get_detail() const;
};
```

实测 `sizeof`：`ApiMethodBase` = **48**、`ApiMemberMethodBase` = **56**、
`ApiClassMethod` = **64**、`ApiBuiltInMethod` = **72**、`ApiUtilityFunction` = **64**。

### 移除全部缓存 bool

子类现有 `mutable bool is_static_ / is_vararg_ / has_returns_`
（`api_tool_types.h:121-123,227-229,280-281`）**全部删除**，改为：

| 访问器 | 实现 |
|---|---|
| `is_static()` | `flags_ & GDEXTENSION_METHOD_FLAG_STATIC` |
| `is_vararg()` | `flags_ & GDEXTENSION_METHOD_FLAG_VARARG` |
| `is_virtual()` | `flags_ & (VIRTUAL \| VIRTUAL_REQUIRED)` |
| `has_returns()` | `!(flags_ & internal::METHOD_FLAG_NO_RETURN)`（读**原始** flags_） |

均为 `_FORCE_INLINE_`，无额外代价；同时消除「缓存 bool 与 flags 失同步」的隐患。

---

## 4. `has_returns()`：内部位 `METHOD_FLAG_NO_RETURN`（调整 #5，第 3 版恢复）

原 `has_returns`（`core/api_tool_internal.h:769-772`）：
`return_val.type != NIL || (return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT)`。

第 2 版曾以「对外暴露合成值」为由废止 `METHOD_FLAG_NO_RETURN`，属**过度纠正**：
该位是**内部编码**，用途是让 `has_returns()` / `validated_call()` 在不额外占用热字段的前提下
查询「有无返回值」；契约只要求**对外 `get_flags()` 屏蔽它**，不要求它不存在。现已恢复：

- **写入**：加载期由真实判定（`return_val.type != NIL || usage & PROPERTY_USAGE_NIL_IS_VARIANT`）
  推导一次，随热数据落盘（`flags_` = 真实 flags | 内部位）。
- **内部查询**：`has_returns()`（§3 访问器表）与三个子类的 `validated_call()` 读**原始** `flags_`。
- **对外契约**：`get_flags()` 屏蔽该位（`flags_ & ~METHOD_FLAG_NO_RETURN`）后返回纯 Godot flags。

`return_usage_` 热字段**不再需要**（「返回 `Variant`」的 `NIL_IS_VARIANT` 情形已编码进该位）。
若通用消费方需要完整 `return_val.usage`，走惰性 `get_detail().return_val.usage`（保真）。

bit 256 空闲实测依据：引擎 `MethodFlags`（`core/object/method_info.h:36-45`）用到 1/2/4/8/16/32/64/128；
GDExtension 侧（`gdextension_interface.gen.h:441-450`）最大 `VIRTUAL_REQUIRED = 128`。

---

## 5. 缺省值：分族归属与惰性（调整 #4）

### 5.1 谁需要缺省值信息（实测核实，修正先前的错误判断）

| 族 | 需要**个数** | 需要**值** | 证据 |
|---|---|---|---|
| `ApiClassMethod` | **是** | 是（仅 codegen） | `jsb_object_bindings.cpp:404-407` 用 `default_arguments.size()` 做 `check_argc` 与错误信息 |
| `ApiBuiltInMethod` | **是** | **是（运行时）** | `jsb_variant_info.h:51-53`；`jsb_primitive_bindings.cpp:460-485`；`api_tool_types.h:165-180` |
| `ApiUtilityFunction` | 否 | 否 | `jsb_variant_info.h:58-60` 只用 `argument_types.size()`；json 实测 0/114 |

`VariantUtil::check_argc`（`src/internal/jsb_variant_util.h:140-147`）：`default_num > 0` 时允许少传参。
class 族有 **1040 个**方法带缺省值（共 1715 个）→ 丢掉计数会拒绝合法调用。

### 5.2 归属（落实调整 #4）

```cpp
// 通用基类：**不含**任何缺省值信息
struct ApiMethodBase { /* §3 */ };

// 已有中间基类（class + builtin），缺省值计数放这里
struct ApiMemberMethodBase : public ApiMethodBase {
    uint16_t default_count_ = 0;
};

struct ApiClassMethod     : public ApiMemberMethodBase { /* method_bind */ };
struct ApiBuiltInMethod   : public ApiMemberMethodBase { /* func, variant_type */ };
struct ApiUtilityFunction : public ApiMethodBase        { /* func, category */ };  // 无缺省值
```

- 计数**不在** `ApiMethodBase`（满足调整 #4 的字面要求：不进通用基类）。
- 计数在 `ApiMemberMethodBase` 而非各子类各写一份：class 与 builtin 都需要，正是该中间基类的存在意义。
- **实测代价**：中间基类的 `uint16_t` 无法复用基类尾部填充（基类含非平凡成员，非 standard-layout），
  故 class/builtin 各 +8 B（`ApiMethodBase` 48 → `ApiMemberMethodBase` 56）。
  合计 +142,568 B（16822×8 + 999×8）；相对总计 1.19 MB 为 12%。替代方案（直接写进各子类）
  builtin 可回到 64 B，仅省 8 KB，但字段重复——取舍见 §10.4。
- 缺省值**值**一律惰性（§6.3）；utility 永不需要。

---

## 6. 惰性加载

### 6.1 为什么按实体（调整 #2 的答案）

1. **数据局部性**：同实体所有数据在同一文件、同一 deflate 流内连续存放。
   `FileAccessCompressed::seek` 按 **4096 B 块**解压——读取单方法 detail 与读取整个实体 detail
   的**解压代价相同**（最多差一个块）。按方法拆分的 IO 收益为零。
2. **记账成本**：按方法惰性需为 17935 个方法各维护「已加载」状态与偏移（指针或标志），
   而 detail 是变长记录，必须顺序解析才能定位第 i 条——按方法定位实际不可行（除非加偏移表）。
   按实体则只需每个实体一个状态位 + 一个指针。
3. **实体 detail 实测很小**：上界 class **83,208 B**（RenderingServer，535 方法）、
   builtin **10,752 B**（String）、utility ≈ 0。粒度粗带来的最大浪费是「只要 1 个方法却加载 83 KB」，
   仅发生在编辑器 codegen 路径。
4. **消费方本就是按实体遍历**：`jsb_codegen_type_db.cpp:210,363` 对每个实体遍历其全部方法；
   按实体加载与访问模式一致。
5. **一次分配 + 一把锁**：实体级 vs 方法级＝ 1244 次 vs 17935 次分配/加锁。

反方向（按方法）的唯一优势是「只要 1 个方法时不加载整实体」；上界 83 KB 且仅在编辑器路径，不值其复杂度。

### 6.2 三个 detail 类型 → 实际收敛为**一个**

原先设想的「三个 detail 类型 + 继承」不再必要，因为缺省值**值**移出 detail（§6.3），
detail 只剩「`MethodInfo` 中除热字段与 `id` 外的**完整保真**内容」，三族完全同构：

```cpp
namespace api_tool::internal {
struct ApiMethodDetail {
    godot::PropertyInfo return_val;                          // 完整（保真）
    godot::LocalVector<godot::PropertyInfo> arguments;       // 完整（保真）
};
}
```

实测 `sizeof(ApiMethodDetail)` = **64**。utility 的实例恒为空数组（json 无对应数据），无需特化。
**detail 不含 `id`**：json 无此字段（§9）。

### 6.3 惰性 defaults：实体级平铺块

defaults **值**独立于 detail 存放在文件另一段（§7），读取不触发 detail 解析：

```
struct ApiMethodDetailStorage {                 // 实体级，不可拷贝
    godot::String path_;
    uint64_t detail_offset_ = 0;
    uint64_t defaults_offset_ = 0;
    uint32_t file_method_count_ = 0;

    internal::ApiMethodArg *arg_block_ = nullptr;   // 热：加载时一次性分配
    uint32_t arg_block_size_ = 0;

    mutable std::atomic<ApiMethodDetail *> details_{ nullptr };   // 整实体数组
    mutable std::atomic<godot::Variant *> defaults_{ nullptr };   // 平铺块
    mutable uint32_t *default_offsets_ = nullptr;                 // 每方法起始下标
    mutable std::mutex mutex_;
    std::vector<ApiMethodDetail> injected_;         // Object FLAG_OBJECT_CORE 补全
    godot::StringName owner_name_;                  // class 名去重；builtin/utility 空
};
```

- `defaults_` 是一整块 `Variant[]`，`default_offsets_[i]` 给出第 i 个方法在块中的起点，
  长度取热层 `default_count_`。**避免 17935 个小 `LocalVector` 分配**。
- `get_detail(i)` 与 `get_defaults(i)` 各自双检加锁、独立发布 → builtin 缺参时**只**加载 defaults，
  不拉 `PropertyInfo` detail（否则 String 的 116 个方法的参数名会被白白加载）。
- class 的 codegen 路径按需分别调用两者；utility 的 `get_defaults` 恒返回空。

### 6.4 惰性加载路径

```
p = details_.load(acquire)
if (p == nullptr) {
    lock(mutex_);
    p = details_.load(relaxed);
    if (p == nullptr) {
        p = new ApiMethodDetail[total];       // 首访才分配
        ApiStoreReader::read_method_details(path_, detail_offset_, file_method_count_, p);
        for (i) p[file_method_count_ + i] = std::move(injected_[i]);
        details_.store(p, release);
    }
}
return p[p_index];
```

- 快路径无锁（一次 acquire load + 下标）；慢路径双检，同实体并发首访只解析一次。
- 失败语义：读取非 OK 时不发布指针，返回静态空 detail 并 `ERR_PRINT_ONCE`，
  与现有「加载失败打日志并降级」风格一致。

### 6.5 所有权与生命周期

- `ApiMethodDetailStorage` **不可拷贝**（`mutex` / `atomic`）→ 以 `std::shared_ptr` 持有。
- 容器持有：`ApiClass` / `ApiBuiltinClass` 各带一个 `std::shared_ptr<ApiMethodDetailStorage>`；
  utility 由 `ApiLoader` 持有一个。
  → `ApiClass` 仍可拷贝（`TypedCache::insert(const T&)` 依赖此性质，`api_tool_loader.h:97-101`），
    拷贝后方法裸指针仍指向同一宿主，参数块与 detail 不会被二次拷贝或二次释放。
- **不能**把 `owner_name_` 指进 `ApiClass::name`：`ensure_class` 先在栈上构造再拷入 `std::deque`
  （`api_tool_loader.cpp:281-320`），`ApiClass` 自身地址会变 → 悬垂。放堆上宿主里，
  同时把 16822 份重复 `StringName`（≈134 KB）去重为 1036 份。
- `ApiLoader::clear()` 先清 cache、再 reset storage 的 `shared_ptr`。

---

## 7. 文件格式：三段式

仍「一实体一文件」，magic 与压缩方式不变（`GCPF` + deflate），payload 重排为三段：

```
[hot 段]      与今天同序，但方法只写热字段 + 参数紧凑块
              u32 detail_method_count
              u64 detail_section_size
────────────────────────────────────────────────────────
[detail 段]   method_detail[0 .. detail_method_count)   总长 = detail_section_size
────────────────────────────────────────────────────────
[defaults 段] u32 defaults_method_count
              u16 default_counts[defaults_method_count]
              Variant[...]                              总长由计数决定
```

- `detail_offset` = **detail 段首字节**的解压偏移 =
  读完 hot 段那 12 字节头部后的 `file->get_position()`
  （`FileAccessCompressed::get_position()` 返回真实解压偏移）→ **无 trailer**。
- `defaults_offset` = **defaults 段首字节** = `detail_offset + detail_section_size`（自描述）。
  → builtin 可**只读 defaults 段**，完全不解析 detail。
- `method_detail` 记录（按 hot 段内方法顺序）：
  ```
  PropertyInfo return_val                 (type,name,class_name,hint,hint_string,usage)
  LocalVector<PropertyInfo> arguments     (完整；长度须 == 热层 arg_count)
  ```
  **无 `id`**。
- **一致性校验（承担原 AC7 的角色）**：
  - detail 段解析完，消费字节数必须 == `detail_section_size`，否则 `ERR_FILE_CORRUPT`。
  - `defaults_method_count` 必须 == `detail_method_count` == 热层方法数。
  - 每条 `default_counts[i]` 必须 == 该方法的 `default_count_`。
  - 热层计数超 `uint16_t` / 超 `uint32_t` 时报错，不静默截断。
- `ApiToolPayload` 需新增位置接口：`get_position()` / `get_length()` / `seek()`
  （现仅有 `read`/`write`，见 `core/api_tool_payload.h`）。

### 受影响的写读函数

| 文件 | 位置 | 改动 |
|---|---|---|
| `ApiParser` | `editor/api_tool_parser.cpp` | 填热字段 + `ApiMethodArg` 序列 + `default_count`；冷字段装进 detail（`parse_method` :186-252、utility :398-431、`:548,646`） |
| `ApiStoreWriter` | `editor/api_tool_store_writer.cpp` | `serialize_method_hot` + detail 段 + defaults 段；保留 `serialize_property_info` 供复用 |
| `ApiStoreReader` | `core/api_tool_store.cpp` | `deserialize_method_hot`（记录偏移）+ 新增 `read_method_details` / `read_method_defaults` |
| `ApiLoader` | `core/api_tool_loader.cpp` | 构造并挂载 storage、填参数块；utility 用单一 storage |

---

## 8. 调用点迁移

### 运行时热路径

| 文件 | 说明 |
|---|---|
| `api_tool_types.h` | 三个 `validated_call`：`method.arguments[i].type` → `get_argument_type(i)`；`arguments_metadata[i]` → `get_argument_metadata(i)`；`return_val.type` → `get_return_type()`；`return_val_metadata` → `get_return_metadata()`；builtin 的 `default_arguments` → 仅缺参分支 `get_defaults(method_index_)` |
| `core/api_tool_loader.cpp` | Object 的 `FLAG_OBJECT_CORE` 补全改走 storage 注入；`is_exists` / `list_utility_functions` 用 `get_name()` |
| `bridge/jsb_object_bindings.cpp` | 绑定注册（:88-92,138-160）、`_godot_object_method`（:383-460）的 `check_argc` 用 `get_default_count()`、`.size() > 0` 用 `get_default_count() != 0` |
| `bridge/jsb_primitive_bindings.cpp` | 内建方法绑定（:665-800,886-912）与缺省填充（:455-500） |
| `bridge/jsb_godot_module_loader.cpp` | utility 绑定（:94-115） |
| `internal/jsb_variant_info.h` | `check_argc`（:51-53）用 `get_default_count()` |
| `api_tool_types.cpp` | `try_load_compatible_*`（:87-109）`method.name` → `get_name()`、`owner_class_name` → storage 的 `owner_name_` |

### editor / codegen（直接消费访问器，**无重建壳**）

`jsb_codegen_type_db.cpp:111-156` 的 `build_method_decl(MethodDecl&, const MethodInfo&)` 改为
直接接收 api_tool 方法对象（模板或重载），逐字段从访问器读取：

| 原字段 | 新来源 |
|---|---|
| `p_method.name` | `get_name()` |
| `p_method.flags` | `get_flags()`（**保真**，供 `hint_flags` / `is_static` / `is_const` / `is_vararg`） |
| `p_method.return_val.type` | `get_return_type()` |
| `p_method.return_val.usage & NIL_IS_VARIANT`（`has_return_value`） | `has_returns()`（内部位，见 §4） |
| `p_method.return_val` 完整 `PropertyInfo`（含 `usage`） | `get_detail().return_val`（惰性） |
| `p_method.return_val_metadata` | `get_return_metadata()` |
| `p_method.arguments[i]` | `get_detail().arguments[i]` |
| `p_method.arguments_metadata[i]` | `get_argument_metadata(i)` |
| `p_method.default_arguments` | `get_defaults()`（惰性） |
| `p_method.id` | **无对应**（json 无 `id`）→ `MethodDecl::id` 一并删除 |

`:211`、`:364`、`:518` 三处不再需要局部 `MethodInfo`；`:366-372` 的 utility 变体
（插隐式 `target` 参数、`|= METHOD_FLAG_STATIC`）改为在 `MethodDecl` 层处理，语义不变。

`:257-262` 的 signal 分支构造 `MethodInfo mi` 仅用于局部派生（`SignalDecl::method` 是
`godot::MethodInfo`，属**另一个类型**，不由本任务改造），保持原样。

### 保留 / 删除

- **保留** `MethodDecl::hint_flags`（调整 #5）：由 `get_flags()` 填充。
- **删除** `MethodDecl::id`（`jsb_codegen_type_db.h:54`）及其写入点 `jsb_codegen_type_db.cpp:113`：
  json 无 `id`，无源数据，无法保真（§9）。
- **不引入** `to_method_info()` 或任何 `godot::MethodInfo` 重建接口（R7 / 调整 #3）。

### 不做（保持行为等价）

- `jsb_object_bindings.cpp:421-422` 负索引死分支：class 族缺省补全由引擎
  `MethodBind::call` → `call_with_variant_args_dv(..., get_default_arguments())`
  （`core/object/method_bind_common.h:83`）完成。迁移后该分支读 `get_default_count()`，
  `== 0` 时短路，与今天 `.size() > 0` 同为 false，**不修语义**。
- `ApiSignalInfo` / `ApiPropertyInfo` 结构不改。

### utility 的 `validated_call`：移除 defaults 的必然结果

utility 无缺省值（实测 0/114），今天 `p_argcount < arg_count` 时
`default_values[i - p_argcount + (0 - missing)]` 恒为负 → dev 下 `CRASH_BAD_UNSIGNED_INDEX`、
release 下越界读，是**既有坏路径**。移除 defaults 后改为显式前置校验
（`ERR_FAIL_COND_MSG(p_argcount < get_argument_count(), ...)`）。合法调用行为不变；
非法调用从 UB 变为受控报错——移除动作的必然伴生，非额外语义修复。
（JS 侧本就由 `VariantUtil::check_argc` 先行拦截——`jsb_godot_module_loader.cpp:101` 注释即为此。）

---

## 9. `id` 是唯一不保真的字段

json 方法对象键集合实测：

| 族 | 键集合 |
|---|---|
| class | `arguments, hash, hash_compatibility, is_const, is_required, is_static, is_vararg, is_virtual, name, return_value` |
| builtin | `arguments, hash, hash_compatibility, is_const, is_static, is_vararg, name, return_type` |
| utility | `arguments, category, hash, is_vararg, name, return_type` |

**没有 `id`** → `godot::MethodInfo::id` 从来就是 0（`api_tool_store.cpp:57` / `api_tool_store_writer.cpp:57`
只是在往返一个常量）。既无源数据，重建也**无法对齐**——这正是 `to_method_info()` 不可取的原因。
除 `id` 外，上述键集合中的每个键都有对应的保真载体（热字段或惰性 detail）。

---

## 10. 权衡与已知代价

1. **按实体而非按方法惰性**：见 §6.1。最大额外加载 83 KB，仅编辑器路径。
   - 备选「detail 拆独立文件」→ 免重解压 hot，但新增目录、要改 `get_api_data_files`（影响导出打包）、文件数翻倍。**否决**。
   - 备选「editor 加载实体时顺手读 detail」→ 编辑器峰值内存翻倍，违背懒加载目标。**否决**。
2. **不建 `to_method_info()`**（调整 #3）：它能少改 codegen，但会伪造 `id = 0`、
   违反保真与 R7。改为 codegen 直接消费访问器——改动面略大，但语义正确。
3. **容器不动**（调整 #1）：`Arr` 实测零收益，废止。
4. **`default_count_` 放 `ApiMemberMethodBase`**（调整 #4）：满足「不进通用基类」，代价 +8 B/方法
   （class + builtin，合计 ≈143 KB，占总量 12%）。替代「各子类各写一份」可省 8 KB（仅 builtin），
   但字段重复、语义上 class/builtin 本就共享该属性 → 选中间基类。
5. **storage 由 `shared_ptr` 持有、方法持裸指针**：所有权不对称（容器持有 / 方法引用），
   换取方法内不出现 `shared_ptr`；生命周期约束见 §6.5。
6. **`ApiClass` / `ApiBuiltinClass` 各 +8 B**（新增一个 `storage_` 指针；实测 `ApiClass` 104 → 112，
   `ApiBuiltinClass` 144 → 152——都因尾部对齐而只增一个指针槽）；1036 + 38 个实体 ≈ **+8.4 KB**（占总量 0.7%）。

---

## 11. 内存预算（实测，AC2 依据）

| 族 | 方法数 | 参数数 | 缺省值数 | 改动前 | 改动后 | 降幅 |
|---|---|---|---|---|---|---|
| class | 16822 | 16879 | 1715 | 3,475,812 B | 1,110,366 B | **68.1%** |
| builtin | 999 | 902 | 150 | 194,360 B | 73,732 B | 62.1% |
| utility | 114 | 193 | 0 | 27,364 B | 7,682 B | 71.9% |
| **合计** | 17935 | 17974 | 1865 | **3,697,536 B (3.53 MiB)** | **1,191,780 B (1.14 MiB)** | **67.8%** |

- 改动前 = `sizeof(实测结构)` + `MethodInfo` 三堆数组（48 B/参数 + 4 B/参数 + 24 B/缺省值）。
- 改动后 = `sizeof(热层)`（class 64 / builtin 72 / utility 64）+ `ApiMethodArg` 2 B/参数。
- **AC2 按 class 计：3,475,812 → 1,110,366 B，降 68.1% ≥ 50%，通过。**
- 上表为**布局测算**（探针实测尺寸 × json 计数）；实施后须以运行时实测复核并回填。

---

## 12. 验证策略与证据文件

| 阶段 | 命令 / 证据 |
|---|---|
| 前置（改代码前，Step 0） | `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6`；`cd project && pnpm install && node_modules/.bin/tsc --noCheck`；`python misc/verify_codegen.py --godot <abs exe> --update-baseline`；再跑一次不带 `--update-baseline` |
| 产物零差异（AC4） | `python misc/verify_codegen.py --godot <abs exe>` 退出码 0 + 零差异 |
| 确定性（AC8） | 连续两次完整触发链产物一致 |
| C++ 测试（AC5） | `scons ... tests=yes -j5` 后 `godot --headless --path ./project --jsb-run-tests` |
| TS 集成（AC6） | `godot --headless --path ./project --verbose` |
| 内存复测（AC2） | 复用 `.agent_tmp/ac2_memory.py` ＋ `.agent_tmp/planned_layout_sizeof.cpp` |
| 一致性校验（AC7） | 段长 / 计数不符即 `ERR_FILE_CORRUPT` |
| 保真性（AC9） | `get_flags()` 屏蔽内部位后与 json 一致；返回 `Variant` 的方法 `has_returns()` 为 true；`hint_flags` 被填充 |

引擎宿主（实测）：`D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.console.exe`。
`sh -c 'godot ...'` 亦可解析到同一 exe；但 python `shutil.which('godot')` 为 `None`、
`subprocess(['godot'])` 抛 `WinError 2`、`bash -lc` 因 shebang CRLF 失败
→ **`--godot` 必须传绝对 exe 路径**。

回滚点：格式改动集中在 4 个写读函数；热/冷拆分与 codegen 迁移彼此正交。

### 证据文件（`.agent_tmp/`，一次性探针）

| 文件 | 用途 |
|---|---|
| `sizeof_probe.cpp` / `sizeof_diag.cpp` | godot-cpp 尺寸：`MethodInfo`=120、`PropertyInfo`=48、`StringName`=8、`Variant`=24、`Variant::Type`=4、meta=4、`LocalVector`=16 |
| `api_tool_sizeof.cpp` | 改动前结构：`ApiMethodBase`=128、`ApiClassMethod`=152、`ApiBuiltInMethod`=144、`ApiUtilityFunction`=152 |
| `planned_layout_sizeof.cpp` | 改动后布局：base=48、member=56、class=64、builtin=72、utility=64、detail=64、`ApiMethodArg`=2 |
| `ac2_counts.py` | json 计数与键集合提取 |
| `ac2_memory.py` | AC2 前后常驻字节测算（§11 表） |

编译方式（实测可用）：
```
clang++ -std=c++17 -fsyntax-only -Isrc/api_tool -Isrc \
  -Ithird/godot-cpp/include -I"D:/Dev/godot/GodotJS-Ext/third/godot-cpp/gen/include" \
  -Ithird/godot-cpp/gdextension .agent_tmp/planned_layout_sizeof.cpp
```
（用未定义模板 `Show<N,Tag>` 触发诊断打印尺寸；`third/godot-cpp/gen/include` 在本 worktree 缺失，
借用主检出同名目录，**仅用于尺寸探测，不参与构建**。）
