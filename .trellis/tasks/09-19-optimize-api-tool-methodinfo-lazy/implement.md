# 实施计划

依据 `prd.md` + `design.md`。步骤按序；每步验收以「可机械核对」为准。

**总体前置**：本 worktree 无任何构建产物（`bin/`、`.sconsign.dblite`、`project/.godot`、`project/gen`、
`project/typings`、`.codegen-baseline` 全不存在）。先做 Step 0，再动一行代码。

---

## Step 0：构建与基线固化（R11 的前提，必须在改代码前完成）

1. 子模块已就位（`third/godot-cpp`、`third/v8/windows_x86_64_release` 均存在），无需 `git submodule update`。
   `third/godot-cpp/gen/include`（godot-cpp 生成头）本 worktree 尚无，首次 scons 时生成。
2. 构建（规范命令，禁 `scons --clean`）：
   ```
   scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6
   ```
3. TS 依赖与编译：
   ```
   cd project && pnpm install && node_modules/.bin/tsc --noCheck
   ```
   （若 `.godot/.tsbuildinfo` 存在而产物缺失，先删 `.tsbuildinfo` 再编。）
4. 首次导入（`project/.godot` 不存在时）：
   ```
   <godot> --headless --editor --path ./project --import
   ```
5. 固化基线 + 自校验 + 确定性复跑（**`--godot` 必须传绝对 exe 路径**）：
   ```
   python misc/verify_codegen.py --godot D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.console.exe --update-baseline
   python misc/verify_codegen.py --godot D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.console.exe
   ```
   两条都须成功；第二条是「基线可信」的唯一授权来源（AC8 的基线）。

   > 实测环境约束：`sh -c 'godot ...'` 能解析到同一 exe，但 python `shutil.which('godot')` 为 `None`、
   > `subprocess(['godot', ...])` 抛 `WinError 2`、`bash -lc` 因 godot 脚本 shebang CRLF 失败
   > → 只能传绝对 exe 路径。

6. 记录「改动前」内存基线：**本任务已预置探针**，直接复用（无需重编）：
   ```
   clang++ -std=c++17 -fsyntax-only -Isrc/api_tool -Isrc \
     -Ithird/godot-cpp/include -I"D:/Dev/godot/GodotJS-Ext/third/godot-cpp/gen/include" \
     -Ithird/godot-cpp/gdextension .agent_tmp/api_tool_sizeof.cpp
   python .agent_tmp/ac2_counts.py
   ```
   预期：改动前 class 族 3,475,812 B（尺寸 `ApiMethodBase`=128 / class=152 / builtin=144 / utility=152）。
   **若与此不符，先查探针，不得带疑进 Step 2。**

   > 注：`third/godot-cpp/gen/include` 在本 worktree 缺失，探针**借用主检出同名目录**，仅用于尺寸探测，
   > 不参与构建（scons 用本 worktree 自己生成的 `gen/include`）。

**验收**：`.codegen-baseline/{gen,typings,tsconfig.json}` 存在；第二次运行退出码 0、零差异（行尾差异除外）。
**产出**：进度记入 `.trellis/tasks/<本任务>/report.md`。

**回滚点 A**：Step 1 之前。此点无需回滚，只需保留基线。

---

## Step 1：热层类型定义（`api_tool_types.h`）

> 本步是本任务唯一改动头文件的大步——**一次改完，只编一次**。

1. `namespace internal`：新增
   ```cpp
   struct ApiMethodArg { uint8_t type; uint8_t meta; };
   ```
   ⚠ 必须是两个 `uint8_t`。实测 `sizeof(Variant::Type) == 4`，写成 `{Variant::Type; uint8_t}` 会
   padding 到 8 B/参数（比现状 4 B 更差）。加 `static_assert(sizeof(ApiMethodArg) == 2)`。
2. 新增 `class ApiMethodDetailStorage`（design.md §6.3）：
   - 非拷贝（`std::mutex` + `std::atomic`）；实体级持 `shared_ptr`。
   - 热侧：`allocate_arg_block(uint32_t)` 一次性分配 `ApiMethodArg` 块。
   - 冷侧：`const ApiMethodDetail &get_detail(uint16_t) const`（触发整实体 detail 加载）、
     `const godot::Variant *get_defaults(uint16_t, uint16_t &r_count) const`（触发 defaults 加载；
     utility 恒返回 nullptr / 0）。
   - `owner_name_`：class 名（去重 16822 份重复 `StringName` → 1036 份）；builtin / utility 为空。
   - 双检加锁；失败不发布指针，返回静态空对象 + `ERR_PRINT_ONCE`。
   - **内存用 RAII 包装**（避免裸 `new[]`）：`DetailBlock{ LocalVector<ApiMethodDetail> methods; }`、
     `DefaultsBlock{ LocalVector<Variant> values; LocalVector<uint32_t> starts; }`，
     经 `std::atomic<Block *>` 发布；`starts` 为**前缀和**（长度 = 方法数 + 1），切片与校验都简单。
   - **发布顺序约束**：块内容写完后才 `store(release)`；读取方先 `load(acquire)`，
     非空后再访问块内成员（保证偏移表与值同批可见）。
3. 新增 `struct ApiMethodDetail { godot::PropertyInfo return_val; godot::LocalVector<godot::PropertyInfo> arguments; }`
   （design.md §6.2）。**不含 `id`**。实测 `sizeof` = 64。
4. 重写 `ApiMethodBase`（design.md §3）：热字段 + 访问器 + `get_detail()` 声明。
   - `flags_` 保存 Godot 真实 flags（`GDEXTENSION_METHOD_FLAG_*`），无返回值时额外置内部位
     `internal::METHOD_FLAG_NO_RETURN`（`1u << 8`；实测引擎与 GDExtension 侧均只用到 1..128，bit 256 空闲）。
   - 该内部位在加载期由真实判定（`return_type != NIL || (usage & PROPERTY_USAGE_NIL_IS_VARIANT)`）
     推导一次并随热数据落盘；`has_returns()` 与 `validated_call()` 读**原始** `flags_`；
     **`get_flags()` 必须屏蔽**该位（`flags_ & ~METHOD_FLAG_NO_RETURN`）后返回。
   - `args_` 指向 storage 的实体级参数块。
5. `ApiMemberMethodBase` 增加 `uint16_t default_count_`，**只**作用于 class + builtin 两个族。
   - `ApiUtilityFunction` **现状已经**直接继承 `ApiMethodBase`（`api_tool_types.h:274`），
     **不改继承关系**；它继续持 `func` + `category` 两个热字段。
   - `api_tool_store.cpp:66` / `api_tool_store_writer.cpp:66` 的
     `requires std::is_base_of_v<ApiMemberMethodBase, ...>` 本就只被 class/builtin 实例化，
     utility 走独立的 `serialize/deserialize_utility_function_info` —— **无影响，两处都不用动**。
6. 三个子类改为访问器（`validated_call` 主体、`get_func_ptr`、`get_method_bind_ptr`、`is_virtual`、`is_static`）；
   **删除全部 `mutable bool is_static_ / is_vararg_ / has_returns_`**。
7. `ApiUtilityFunction::category` **保留**（json 有该键，保真）。
8. `ApiClassMethod::owner_class_name` 移除 → `storage_->owner_name_`。
9. `ApiClass` / `ApiBuiltinClass` 增加私有 `std::shared_ptr<internal::ApiMethodDetailStorage> storage_`。
10. **不引入** `to_method_info()` 或任何 `godot::MethodInfo` 重建接口（R7）。
11. 加 `static_assert`（实测值）：`sizeof(ApiMethodBase) == 48`、`sizeof(ApiMemberMethodBase) == 56`、
    `sizeof(ApiClassMethod) == 64`、`sizeof(ApiBuiltInMethod) == 72`、`sizeof(ApiUtilityFunction) == 64`。
    （平台/编译器有异则改 `<=` 并注明。）

**验收**：编译通过（本步会全量重编一次，属预期）；`static_assert` 全部成立；
`grep -rn "godot::MethodInfo" src/api_tool/*.h` 在 API 类型定义处无**成员**命中。
**回滚点 B**：Step 2 之前。

---

## Step 2：序列化重构（4 个写读函数 + 解析器）

**文件**
- `editor/api_tool_store_writer.cpp`：`serialize_method_hot` + detail 段 + defaults 段
- `core/api_tool_store.cpp`：`deserialize_method_hot`（记录偏移）+ 新增
  `read_method_details(path, offset, count, block)` / `read_method_defaults(path, offset, block)`
- `editor/api_tool_parser.cpp`：填热字段 + `ApiMethodArg` 序列 + `default_count`；冷字段装进 detail
  （`parse_method` :186-252、utility :398-431、`:548,646`）
- `core/api_tool_loader.cpp`：构造并挂载 storage、填参数块；utility 用单一 storage；
  `clear()` 先清 cache 再 reset `shared_ptr`，并释放 storage 内的惰性块

**要点**
- 三段式布局见 design.md §7；`detail_offset` 取 `file->get_position()`（hot 段解析完即得，无 trailer）；
  `defaults_offset = detail_offset + 4 + 8 + detail_section_size`（头部自描述）。
- `ApiToolPayload` 新增位置接口：`get_position()` / `get_length()` / `seek()`。
- **一致性校验（AC7）**：detail 段消费字节数 == `detail_section_size`；两个 `method_count` 与热层方法数三者相等；
  每条 detail 的 `arguments.size()` == 热层 `arg_count`；每条 `default_counts[i]` == 热层 `default_count_`。
  不符一律 `ERR_FILE_CORRUPT`，不静默截断。热层计数超位宽时报错。
- **utility 文件也写 defaults 段**（计数全 0），保持三段结构一致，读取方无需分族分支。
- defaults 段：先写 `u32 count` + `u16 counts[count]`，再写 `Variant` 平铺块。
  读入 `LocalVector<Variant>` 用 `resize()`（`Variant` 非平凡析构，**不可** `resize_uninitialized`）。
  前缀和 `starts` 在读取时构建。
- `defaults_method_count` 与 detail 段同源，保证 builtin 缺参路径**不触碰 detail 段**。
- detail 段不携带 `id`。

**验收**
- `--godotjs-api-generate` 成功产出 `.api_dumping`（产物存在性判据）。
- 一次性脚本：加载若干类后经 `get_detail()` / `get_defaults()` 读回的数据，与同轮
  `extension_api.json` 对应条目**逐项相等**（抽样 ≥ 20 个方法，含 RenderingServer 的多参数多缺省方法、
  String 的多缺省 builtin 方法）。**该脚本同时充当 §9 的保真性回归依据。**

**回滚点 C**：Step 3 之前。

---

## Step 3：调用点迁移

**运行时**（design.md §8 表）：`jsb_object_bindings.cpp`、`jsb_primitive_bindings.cpp`、
`jsb_godot_module_loader.cpp`、`internal/jsb_variant_info.h`、`api_tool_types.cpp`

- `jsb_object_bindings.cpp:404-407`：`default_arguments.size()` → `get_default_count()`
  （`check_argc` 与错误信息两处）；`:415-417` 参数类型 → `get_argument_type()`；`:449` → `get_return_type()`。
- builtin 缺省值：`validated_call` 中**仅在 `p_argcount < arg_count_` 分支内**调 `get_defaults(...)`
  （AC3 的「仅实际缺参时加载」）；`jsb_primitive_bindings.cpp:460-485` 同理。
- `jsb_object_bindings.cpp:421-422` 保持等价（`get_default_count() == 0` 时短路，与今天 `.size() > 0` 同为 false）。
- `internal/jsb_variant_info.h:51-53`：`check_argc` 用 `get_default_count()`。
- utility `validated_call` 增加显式前置 `ERR_FAIL_COND_MSG(p_argcount < get_argument_count(), ...)`
  （design.md §8 末：合法调用行为不变，非法调用从 UB 变受控报错）。

**editor / codegen**（`jsb_codegen_type_db.cpp`）
- `build_method_decl` 签名改为：
  ```cpp
  void build_method_decl(MethodDecl &r_decl, const api_tool::ApiMethodBase &p_method,
                         const godot::Variant *p_defaults, uint16_t p_default_count);
  ```
  内部逐字段改走访问器（`get_name` / `get_flags` / `get_argument_*` / `get_return_*` /
  `get_detail().return_val` / `get_detail().arguments`）。**不用模板、不建 `MethodInfo`。**
- 三个调用点（`:222` class、`:376` builtin、`:518` utility）分别传各自 defaults；
  utility 传 `nullptr, 0`。
- `:366-372` 的 utility 变体（插隐式 `target` 参数 + 强制 static）改为在 `MethodDecl` 层处理，
  语义不变（`hint_flags` 等保真字段照旧填充）。
- `:257-262` 的 signal 分支保持原样（`SignalDecl::method` 是独立类型，不属本任务）。
- **删除** `MethodDecl::id`（`jsb_codegen_type_db.h:54`）与写入点（`:113`）——json 无 `id`，无源可保真。
- **保留** `MethodDecl::hint_flags`（`:56`），由 `get_flags()` 填充。

**要点**
- `grep -rn "to_method_info" src/` 必须无命中。
- 若 `jsb_object_bindings.cpp:421-422` 死分支因结构变化自然消失，记入下方「已消除项」，不做语义修复。

**验收**：全量构建通过（本步跑通所有迁移点）。

---

## Step 4：验证（AC1–AC9）

| 序 | 命令 / 动作 | 判据 |
|---|---|---|
| 4.1 | `python misc/verify_codegen.py --godot <abs exe>` | 退出码 0；`gen/`+`typings/` 零差异（AC4） |
| 4.2 | 再跑一次 4.1（不 `--update-baseline`） | 仍零差异（AC8） |
| 4.3 | 复用 `.agent_tmp/planned_layout_sizeof.cpp` + `ac2_memory.py` 复算 | class 族 ≤ 1,737,906 B，记录实测（AC2） |
| 4.4 | `scons ... tests=yes -j5` 后 `<godot> --headless --path ./project --jsb-run-tests` | exit 0 且无泄漏（无未释放 Resource、无 Orphan StringName）（AC5） |
| 4.5 | `<godot> --headless --path ./project --verbose` | TS 集成全绿（AC6） |
| 4.6 | Step 2 的一次性对照脚本 | 缺省值与类型一致，且**仅缺参时**才加载（AC3） |
| 4.7 | `grep -rn "godot::MethodInfo" src/api_tool/` | 类型定义处无**成员**命中（AC1 前半） |
| 4.8 | `grep -rn "to_method_info" src/` | 无命中（AC1 后半） |
| 4.9 | 保真性断言 | `get_flags()` 屏蔽内部位后与 json 一致；返回 `Variant` 的方法 `has_returns()` 为 true；`hint_flags` 被填充（AC9） |
| 4.10 | 越界 / 段长不一致用例 | 报错而非静默截断（AC7） |

**任一 diff 出现时**：逐项归因到具体 emitter / 输入漂移，不得直接 `--update-baseline` 接受。

**回滚点 D**：4.1 出现不可归因差异 → 回退到 Step 2 的写读函数（保留 Step 1），独立排查。

---

## Step 5：收尾

- 清理 `.agent_tmp/` 一次性脚本（或确认已在 gitignore 内且不影响产物）。
- 确认无残留旧接口（别名 / 兼容壳 / 弃用路径）。
- 按 `trellis-update-spec` 把「api_tool 热/冷字段约定 + 惰性实体级加载 + 三段式 detail/defaults 段格式」
  写入 spec。
- 回填 design.md §11 的「改动后」实测数字（若与布局测算有差）。

---

## 已消除项（实施中登记）

（空白，待 Step 3 填写：`jsb_object_bindings.cpp:421-422` 死分支的最终处置、
`MethodInfo::id` / `MethodDecl::id` 的删除确认。）

---

## 风险点回顾

- **全量构建耗时**：Step 1、Step 3 各触发一次全量重编（改头文件，单次 100–190s）。
  **不要**为措辞/风格额外编译；要对照编译就一次列全变体。
- **`jsb_check` 是 dev 门控断言**（`dev_build=yes` 编入、release 移除）——本次构建即 dev 形态，断言路径会被走到。
- **进程残留锁 dll**：构建后若 cp 报 busy，先 `taskkill /F /IM godot*`；cp 后用 `md5sum` 确认
  （addons 下主 dll 与 editor dll **两份都要换**）。
- **`--godotjs-api-generate` 会消费删除 `project/extension_api.json`**——`verify_codegen.py` 已固化
  「dump → 备份 → 生成 → 放回」顺序；手工跑时严格按此序，否则 store 静默为空。
- **`.godot/.tsbuildinfo` 陷阱**：产物被删后 `tsc` 不补发；先删 `.tsbuildinfo` 再重编。
- **退出码不可信**：headless 任务完成后引擎退出阶段崩溃（0xC0000005）是已知问题；
  判成败用**产物存在性**。
- **`ApiUtilityFunction` 改继承基类**会影响 `requires std::is_base_of_v<ApiMemberMethodBase, ...>`
  的既有约束（`api_tool_store.cpp:66`、`api_tool_store_writer.cpp:66`）——Step 1 必须同步处理，否则编译失败。
- **惰性块的线程安全**：`detail` 与 `defaults` 两个块各自独立发布；块内成员只能在对应的 acquire load
  成功之后访问（发布顺序约束见 Step 1.2）。
