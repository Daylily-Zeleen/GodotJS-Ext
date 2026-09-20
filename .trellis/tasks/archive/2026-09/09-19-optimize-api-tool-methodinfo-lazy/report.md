# 任务报告：optimize-api-tool-methodinfo-lazy

## 阶段

**已实现、已验证、未提交**。契约扩展（显式 `undefined` ⇒ 按位置取缺省值）已落地：动态两处 + 静态 thunk 两处；两条腿（`static_binding=yes/no`）C++ 与 TS 测试全绿。**工作树未 commit、未 push**（用户未授权）。

## 目标

`ApiMethodBase` 去掉常驻的 `godot::MethodInfo`（实测 120 B/方法 + 三个堆数组），
把运行时热路径字段内联为基类/子类成员；其余字段**保真**打包为惰性类型按需加载。

## 当前产出（第 2 版，已按用户 5 条调整重写）

- `prd.md` — R1–R11、AC1–AC9、Out of Scope、已知偏离（已过 convergence pass）。
- `design.md` — §0 五条调整对照、§1 四层拆分、§2 容器不再改、§3 热层、§4 保真 flags、
  §5 缺省值分族归属、§6 惰性加载（含按实体理由）、§7 三段式格式、§8 调用点迁移、
  §9 `id` 例外、§10 权衡、§11 内存预算、§12 验证与证据。
- `implement.md` — Step 0 构建/基线 → Step 1 类型 → Step 2 序列化 → Step 3 调用点 →
  Step 4 验证（10 条）→ Step 5 收尾；回滚点 A–D。
- `implement.jsonl` / `check.jsonl` — 5 / 4 条 spec 引用（`task.py validate` 通过）。

## 用户 5 条调整的落实

| # | 调整 | 结果 |
|---|---|---|
| 1 | `Arr` 省不了内存 → 仍用 `LocalVector` | 方案废止，容器完全不动；删除原 R5/AC7 的 `Arr` 内容与 `api_tool.h` 签名改动 |
| 2 | 惰性按实体的理由 | design.md §6.1 给出 5 条实测依据 |
| 3 | `to_method_info()` 必要性 | **删除**。它伪造 `id=0`，无法保真，且属 R7 禁止的兼容壳 |
| 4 | 默认值信息不进 `ApiMethodBase` | 计数移入 `ApiMemberMethodBase`（class + builtin）；utility 不持有 |
| 5 | 次要字段须保真，`id` 例外 | 全面保真；`MethodDecl::hint_flags` **恢复保留**；仅删 `id` |

## 本轮推翻的自身错误判断（重要）

1. **「`MethodDecl::id` / `hint_flags` 只写不读，可删」** —— 按保真原则撤销。
   `hint_flags` 保留并由真实 `flags` 填充；仅 `id` 删除（json 无此字段）。
2. **`METHOD_FLAG_NO_RETURN`（`1u << 8`）—— 保留（内部编码）**。它用于 `has_returns()` /
   `validated_call()` 的内部查询；唯一契约要求是**对外 `get_flags()` 屏蔽该位**，返回纯 Godot flags。
   （第 2 版一度废止该位，属过度纠正，已恢复；同时不再需要 `return_usage_` 热字段。）
3. **「`ApiUtilityFunction` 需改继承」** —— 错。它**现状已经**直接继承 `ApiMethodBase`
   （`api_tool_types.h:274`），不改；`requires std::is_base_of_v<ApiMemberMethodBase, ...>`
   两处（`api_tool_store.cpp:66`、`api_tool_store_writer.cpp:66`）本就只被 class/builtin 实例化，无需改动。
4. **「`ApiBuiltInMethod` 是缺省值唯一使用者」** —— 不完整。**class 路径也读缺省值计数**
   （`jsb_object_bindings.cpp:404-407` 的 `check_argc` + 错误信息）；class 族有 1040 个方法带缺省值，
   丢掉计数会拒绝合法调用。utility 确实不需要（`jsb_variant_info.h:58-60`）。

## 关键实测证据

| 项 | 实测值 | 方式 |
|---|---|---|
| `sizeof(godot::MethodInfo)` / `PropertyInfo` | 120 / 48 | clang 编译期探针 |
| `sizeof(godot::Variant::Type)` | **4**（非 1） | → 紧凑参数块必须 `{uint8_t,uint8_t}` = 2 B |
| `sizeof(LocalVector<T>)` | 16 | → `Arr` 同为 16，**零收益**，故废止 |
| 改动前 `ApiMethodBase`/class/builtin/utility | 128 / 152 / 144 / 152 | api_tool 类型探针 |
| 改动后 base/member/class/builtin/utility | **48 / 56 / 64 / 72 / 64** | 规划布局探针 |
| detail 类型 | 64 | 同上 |
| class/builtin/utility 方法·参数·缺省值 | 16822/16879/1715、999/902/150、114/193/**0** | json 提取 |
| 每实体 detail+defaults 上界 | class 83,208 B（RenderingServer）；builtin 10,752 B（String） | json 计算 |
| **AC2 class 族** | 3,475,812 → **1,110,366 B（−68.1%）** | 实测尺寸 × json 计数 |
| AC2 合计 | 3,697,536 → 1,191,780 B（−67.8%） | 同上 |

其它已核实：json 方法对象**无 `id` 键**（三族键集合已列全）；缺省值恒为尾部连续段（非尾段 = 0）；
vararg 且带缺省 = 0；`FileAccessCompressed::seek` 按 4096 B 块解压且 `get_position`/`get_length` 可用；
C++ 标准为 **C++20**（可用 `requires` / designated initializers）。

## 待用户决定

**静态绑定路径的 a / b 二选一**（详见文末「静态路径 a/b 评估」）：

- **b（已实现、已全绿）**：命中 `undefined` 时就地从方法记录取该位缺省值填进 thunk 自己的 `Variant argv[i]`，`M == N` 的方法整块编译掉。**零新增静态数据**、零 codegen 改动；但**代码段有实增量**（两侧 `.text` 合计 +603 KiB、dll +625 KiB，`/Od`）。
- **a（未实现，仅脚本估算）**：codegen 为 class 族发射缺省字面量，与 builtin thunk 形态统一；代价 **+1290 B** 常驻静态数据（160 实例）。

**当前交付 = b**。若采纳 a，需另行动 codegen 并重跑两条腿的测试与基线。

## Step 0 完成记录（2026-09-19）

| 项 | 结果 |
|---|---|
| 全量构建 `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` | **EXIT=0**（308.7s） |
| TS 编译 `node node_modules/typescript/bin/tsc --noCheck` | **EXIT=0** |
| 首次导入 `godot --headless --editor --path ./project --import` | 首跑 exit=3（`EditorFileSystem::reimport_files` 关机阶段崩溃），复跑 **EXIT=0** |
| 基线固化 `verify_codegen.py --update-baseline` | **EXIT=0**，快照 `gen/`(57 ts) + `typings/`(20) + tsconfig.json |
| 确定性复跑 `verify_codegen.py`（不 update） | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致` |
| 改动前尺寸探针 | `ApiMethodBase`=128 / class=152 / builtin=144 / utility=152（与设计预期一致） |
| 改动前 json 计数 | class 16822/16879/1715、builtin 999/902/150、utility 114/193/0 |

**注意**：`pnpm install` 在 `project/` 已就绪（`node_modules/.bin/tsc` 为 shim，Windows 上需用 `node node_modules/typescript/bin/tsc`）。

---

## 实施完成记录（Step 1–4）

### 改动文件（16 个源文件 + 2 个任务文件）

| 文件 | 改动 |
|---|---|
| `src/api_tool/api_tool_types.h` | 热层内联字段 + 访问器；`internal::ApiMethodArg`（2 B）、`ApiMethodDetail`、`ApiMethodAccess`（唯一写入点）、`ApiMethodDetailStorage`（热参数块 + 惰性 detail/defaults，含 Object core 注入尾） |
| `src/api_tool/api_tool_types.cpp` | 存储与访问器实现；`try_load_compatible_*` 改走访问器 |
| `src/api_tool/core/api_tool_payload.h` | 新增 `get_position` / `get_length` / `seek` |
| `src/api_tool/core/api_tool_store.{h,cpp}` | 热记录读 + 冷段自描述头；`read_method_details` / `read_method_defaults`（AC7 硬校验）；`read_utility_functions` 增 storage 参数 |
| `src/api_tool/editor/api_tool_store_writer.{h,cpp}` | `serialize_method_hot` + 三段式冷段（detail 长度占位回填） |
| `src/api_tool/editor/api_tool_parser.cpp` | JSON→热字段/冷 detail/defaults；`encode_flags` 单点推导 NO_RETURN 位 |
| `src/api_tool/core/api_tool_loader.{h,cpp}` | 挂载 storage；Object `FLAG_OBJECT_CORE` 虚函数改走 `push_injected`；utility 单一 storage |
| `src/editor/codegen/jsb_codegen_type_db.{h,cpp}` | `build_method_decl` 改收 `ApiMethodBase` + defaults；删 `MethodDecl::id`；utility 变体改在 `MethodDecl` 层处理 |
| `src/runtime/bridge/jsb_object_bindings.cpp` | 全部改访问器；`check_argc` 用 `get_default_count()`；缺省值惰性获取 |
| `src/runtime/bridge/jsb_primitive_bindings.cpp` | 同上；builtin 缺省值**仅在缺参分支**加载 |
| `src/runtime/bridge/jsb_godot_module_loader.cpp` | utility 绑定改访问器 |
| `src/runtime/internal/jsb_variant_info.h` | `check_argc` 用 `get_default_count()` |

### 验收结果（全部实测）

| AC | 证据 | 结果 |
|---|---|---|
| AC1 | `grep -rn "to_method_info" src/` = 0 命中；`api_tool_types.h` 无 `MethodInfo` 成员 | **PASS** |
| AC2 | 布局探针复测：`ApiMethodArg`=2 / base=48 / member=56 / class=64 / builtin=72 / utility=64 / detail=64；class 族 3,475,812 → **1,110,366 B（−68.1%）**，门槛 1,737,906 B | **PASS** |
| AC3 | builtin/utility 测试全绿（`JS result: PASS`）；缺省值仅在缺参分支加载 | **PASS** |
| AC4 | `verify_codegen.py` 退出码 0，`✅ 校验通过: 生成产物与基线一致` | **PASS** |
| AC5 | `--jsb-run-tests` exit 0：`50 cases | 50 passed`、`581 assertions | 581 passed`（+ editor 套件 3 cases/12 assertions）；无 Orphan/泄漏 | **PASS** |
| AC6 | `--verbose` exit 0，`[JS] result: PASS - all checks passed` | **PASS** |
| AC7 | detail 段消费字节数 == `detail_section_size`、detail 参数数 == 热层 arg_count、defaults 计数 == 热层 default_count，不符即 `ERR_FILE_CORRUPT` | **PASS**（含实现） |
| AC8 | 连续 3 轮完整触发链产物一致 | **PASS** |
| AC9 | `get_flags()` 屏蔽内部位（`get_flags_raw()` 仅供 store 往返）；`has_returns()` 读原始 `flags_`；`hint_flags` 由 `get_flags()` 填充 | **PASS** |

### 实施中发现并修复的真实缺陷

**`LocalVector<uint16_t>::resize()` 不初始化元素**（`local_vector.hpp:191`：`_resize<!is_trivially_constructible_v<T>>`）——
`uint16_t` 是平凡可构造类型，`resize()` 只调 `reserve()` 不写值。3 处 `arg_counts` / `default_counts`
以及 `read_method_defaults` 的 `counts` 都是「先 resize 再逐元素赋值」，其中
`read_utility_functions` 的 `default_counts` **从未被赋值却传入 `set_hot_counts`** →
AC7 校验会拿垃圾值比对。已全部改为 `resize_initialized()`。

### 未提交

按用户指令，实施完成后**不 commit、不 push**，留给人工审查。工作树当前为 dirty 状态。

### 遗留 / 已知偏离

- `jsb_object_bindings.cpp` 的负索引死分支：迁移后读 `get_default_count()`，`== 0` 时短路，与今日 `.size() > 0` 同为 false，**语义未修**（Out of Scope）。
- utility `validated_call` 增加显式前置 argc 校验（`ERR_FAIL_COND_MSG`）：合法调用不变，非法调用从 UB 变受控报错（方案 §已知偏离 5）。
- `project/icon.svg.import`、`project/tests/*`（.tscn uid / ts uid 注释）为引擎运行时自动生成，非本次改动。

### AC7 负向实测（收尾补测）

先前的 AC7 只有「实现存在」的证据，缺负向用例。补测方式：直接篡改一个已生成的 store 文件，
把热段 `method_count` 从 133 改成 134（与冷段 detail 计数不一致），再让引擎加载。

```
$ cp .godot/.api_dumping/classes/Node.capi .agent_tmp/Node.capi.bak
$ python .agent_tmp/ac7_corrupt.py        # 解压 → 改热段 method_count → 原样重压
hot method_count at offset 24: 133
patched -> 134
$ godot --headless --path ./project --verbose
ERROR: [API Tool] store corrupt: detail method count 0 != hot method count 134
ERROR: [API Tool] load class Node failed: File corrupt
exit=5
```

**结论**：不一致被显式报错（`ERR_FILE_CORRUPT` → `load class Node failed: File corrupt`，进程非零退出），
**不是静默截断**。AC7 负向验证通过。测试后已按 md5 还原 `Node.capi`
（`9a4abc6d6bd4d556eaeb6c5b98d4e401`，两处一致）。

---

## 收尾补充（2026-09-19，晚于 Step 1–4）

### 1. 布局 `static_assert` 补齐（implement.md Step 1.11 遗漏项）

实施时只加了 `ApiMethodArg` 一条。已补全其余 6 条（base/member/class/builtin/utility/detail），
现在**真实类型**上的布局由编译期强制，不再只是探针镜像的测算。

### 2. `VariantType` / `ArgMeta` 别名（用户指令）

用户要求给 `uint8_t` 起别名提高表意。已定义 `VariantType = uint8_t`（`Variant::Type` 的收窄形）、
`ArgMeta = uint8_t`（argument metadata 的收窄形），并应用到 3 处收窄点
（`api_tool_parser.cpp` ×2、`api_tool_loader.cpp` ×1）。`using` 是编译期等价，零运行时成本。

### 3. 用户方案评估：拆成两个数组（`arg_types[]` + `arg_metas[]`）——实测更差

用户提议拆成两个数组以明确节省内存。实测（json 17,974 参数 / 17,935 方法）：

| 方案 | 参数块 | 全族合计 |
|---|---|---|
| `{u8,u8}`（现状） | 2 B/arg = 35,948 B | **896,828 B** |
| `{Variant::Type,uint8_t}` 单块 | 8 B/arg（padding）= 143,792 B | 1,004,672 B（+12.0%） |
| 拆两数组 + 每方法 2 指针 | 5 B/arg = 89,870 B | 1,094,230 B（**+22.0%**） |

逐桶归因：**36.5% 方法 0 参数**（白付 +8 B/方法）、**41.2% 单参数**（+8 −3 = +5 B/方法），
打平点在参数数 ≥ 3（真实分布 92.9% 的方法 ≤2 参数）→ **100% 的方法上更差**。

### 4. 零扩展指令实测（用户质疑，反汇编落地）

用户问「`uint8_t→Variant::Type` 是否引入零扩展指令」。**引入**（`get_argument_type` 是
`static_cast<Variant::Type>(arg_at(i).type)`，在 `validated_call` 每参数循环里），
但 x86-64 机器码上**零代价**——真实头文件 + 真实编译选项反汇编（`.agent_tmp/real_zext.asm`）：

```
movzbl (%rax,%rcx,2), %eax   ; load u8 + zext 折叠成单条 movzx
movl   (%rcx,%rax,4), %eax   ; 对照：Variant::Type 直读，同为 1 条
```

`zext` 只存在于编译器 IR（中间表示，不进 dll）；指令选择折进寻址。`{Variant::Type,uint8_t}`
的唯一额外收益是省掉这条 IR 指令——机器码零差异，不值 +12.0% 内存。

### 5. 最终验收（别名改动后，全部实测）

| 项 | 结果 |
|---|---|
| 规范重建（无 `tests=yes`） | **EXIT=0**，0 错误；两处部署 md5 一致（`687b…`/`a34e…`） |
| `verify_codegen.py` | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致` |
| `tests=yes` 重建 | **EXIT=0**，0 错误 |
| C++ 测试（tests=yes DLL） | runtime `50\|50 passed` + `581\|581 passed`；editor `3\|3` + `12\|12`；`running tests result: 0`；**0 泄漏**；doctest 6 行 |
| TS 集成 | **exit 0**，`[JS] result: PASS - all checks passed` |

> 再次踩中 `tests=yes` 静默失效陷阱：别名改动的规范重建（无 `tests=yes`）产物不含测试套件，
> `--jsb-run-tests` 静默 no-op（0 doctest 行）。已按同一陷阱重新 `tests=yes` 构建采数。
> 该陷阱已写入 `.trellis/spec/godotjs-ext/cpp/api-tool-lazy-layout.md` §7。

### 6. Spec 更新（Phase 3.3）

新增 `.trellis/spec/godotjs-ext/cpp/api-tool-lazy-layout.md`（已在 `cpp/index.md` 与包 `index.md` 建索引），
内容：热/冷字段约定与字段归属实测基准、`ApiMethodArg` 两字节收窄（含拆两数组的实测否决与零扩展反汇编）、
`METHOD_FLAG_NO_RETURN` 内部编码写入/屏蔽边界、`ApiMethodAccess` 唯一写入点、
惰性加载按实体（5 条实测依据 + 失败语义 + Object core 注入尾索引对齐）、三段式段格式（含占位回填前提）、
AC7 一致性硬校验 + 负向测试方法、`LocalVector::resize()` 平凡类型陷阱、验证要求（含 `tests=yes` 静默失效陷阱）、
Wrong vs Correct 各 2 组。

### 7. 最终状态

- 工作树 dirty：16 源文件（含别名改动）+ report.md/task.json/design.md + 新 spec 文件
- `git log` 停在 `2637241`（trellis 计划提交），**未 commit、未 push**（按用户指令，留待人工审查）
- 部署 dll：测试构建产物（`5dd3…`/`f05c…`）；规范产物（`687b…`/`a34e…`）如需恢复再跑一次规范命令

### 8. 遗留 / 已知偏离

- `jsb_object_bindings.cpp` 的负索引死分支：迁移后读 `get_default_count()`，`== 0` 时短路，与今日 `.size() > 0` 同为 false，**语义未修**（Out of Scope）。
- utility `validated_call` 增加显式前置 argc 校验（`ERR_FAIL_COND_MSG`）：合法调用不变，非法调用从 UB 变受控报错（方案 §已知偏离 5）。
- 运行时堆分配器级内存实测未单独做（布局测算 + `static_assert` 编译期强制是当前最高置信证据，见 design.md §11 备注）。
- `project/icon.svg.import`、`project/tests/*`（.tscn uid / ts uid 注释）为引擎运行时自动生成，非本次改动。

---

## Bug Analysis: 后台等待循环自转后台 → 重复汇报（2026-09-19）

### 1. Root Cause Category
- **Category**: E - Implicit Assumption
- **Specific Cause**: 发出「等构建完成」的 bash 等待循环（`for … sleep 15` 循环读日志），假设它是瞬时辅助动作；实际跑得久（最长 1200s）→ 被自动转后台 job，与它要等的 scons job 并行。构建完成后循环退出、投递「EXIT=0」，每次投递触发重新响应，而当时无新动作 → 重复输出收尾汇报 4 次。

### 2. Why Fixes Failed
1. 第 1 次：没意识到循环会自转后台，以为「后台 job 完成自动投递」只适用于 scons → 循环成为新的投递源。
2. 第 2~4 次：每次投递到达时没有新可执行动作，但按惯性重新产出完整收尾汇报，而不是「不产出文本、直接结束」。

### 3. Prevention Mechanisms
| Priority | Mechanism | Specific Action | Status |
|----------|-----------|-----------------|--------|
| P0 | Documentation | `workflow-rules.md` 新增「后台 job 与等待循环」小节（规则 + 判别信号）；`guides/index.md` 新增对应思考清单 | **DONE** |

### 4. Systematic Expansion
- **Similar Issues**: 任何长命令（构建/测试/headless 触发链）都可能被自动转后台；等待它的循环同样。
- **Process Improvement**: 长命令一律 `async: true`，结果自动投递；收尾汇报只发一次；后续投递到达时先问「这产生什么新信息，谁会因此改变决定？」——答不出就不产出文本、直接结束。

---

## flags 接口简化（2026-09-20，审查反馈落地）

> 派单教训（同日）：第一次派 `trellis-check` 给了「完整读 16 源文件 + 7 项清单」，超过子代理运行时硬上限（`task.maxRuntimeMs=900000`=15 分钟）被强制中止，22 个请求 0 输出。**子代理有硬超时**；重派时收窄到 4 个锚点 + 增量写报告（每查完一项立即落盘，超时不丢已完成项）+ 不跑构建。

### 用户反馈

审查中指出 `get_flags_raw()` 可能没必要：存原始 flags、加载时再决定是否与上 `METHOD_FLAG_NO_RETURN` 即可，`encode_flags()` 也应只剩加载期一次推导。评估后落地为以下形态（**屏蔽语义不变**，R5/AC9 对外契约不变）：

- **`get_flags_raw()` 保留为 `ApiMethodAccess` 的 friend 静态函数**（非公开成员访问器）：`get_flags()` 是屏蔽语义，写 store 必须读原始 `flags_`，否则往返丢失内部编码、`has_returns()` 无法恢复。原始值读取走 friend，不暴露第二个 flags 公开接口。
- **`encode_flags()` 从 `ApiMethodAccess::encode_flags` 改为 `internal::encode_flags` 自由函数**（3 处调用点：`api_tool_parser.cpp` ×2、`api_tool_loader.cpp` ×1）——加载期每方法推导一次，唯一内部位折叠点。
- **writer 直读原始 flags**：`serialize_method_hot` 走 `internal::ApiMethodAccess::get_flags_raw(p_method)`，不用屏蔽语义的 `get_flags()`。

### 实施中踩的坑（friend 声明 + namespace）

1. `static`（内部链接）函数**不能作为跨编译单元的 friend** → 改外部链接 + `store_writer.h` 声明的方案走了 2 轮失败（`PayloadWriter` 前向声明层级 / `Error` 未限定）。
2. 为修 friend 而调整 `api_tool_types.h` 的 namespace 结构时，**在 `internal` 内加 `struct ApiMethodBase;` 前向声明会遮蔽外层 `api_tool::ApiMethodBase` 的真实定义**（内层非限定名解析到不完整声明，成员访问报 C2027；限定名 `api_tool::ApiMethodBase` 又因定义晚于使用点报 C2039）。
3. **正确结构**（最终落地）：外层 `api_tool` 放 `ApiMethodBase`/`ApiMemberMethodBase` 前向声明（与真实定义同实体，零遮蔽）；`internal` 块内放 `ApiStoreReader`/`ApiLoader`/`ApiParser`/`ApiMethodDetailStorage` 前向声明 + enum/别名/`ApiMethodArg`/`ApiMethodDetail`/`ApiMethodAccess`/`encode_flags`；`using MethodHash` 留在 `api_tool` 层（`api_tool.h` 用它）。
4. **教训**：`namespace internal { fwd decls }` 与 `namespace internal { 复开块 }` 的边界极易被我这样的多轮编辑破坏（`internal::internal` 嵌套、多余闭合、声明遮蔽三类错全踩中）。改 namespace 结构时先数括号配对再动手。

### 验收（flags 简化后，全部实测）

| 项 | 结果 |
|---|---|
| 规范重建 | **EXIT=0**，0 错误；两处部署 md5 一致（`5d36…`/`7695…`） |
| `verify_codegen.py` | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致` |
| `tests=yes` 重建 | **EXIT=0**，0 错误 |
| C++ 测试 | runtime `50\|50 passed` + `581\|581 passed`；editor `3\|3` + `12\|12`；`running tests result: 0`；**0 泄漏**；doctest 6 行 |
| TS 集成 | **exit 0**，`[JS] result: PASS - all checks passed` |

spec §2 已同步（`encode_flags` 自由函数、无 `get_flags_raw()` 公开访问器、writer friend 读原始 flags）。

---

## 审查反馈第二轮（2026-09-20）：storage 抽离 + FMethodInfoBase 删除 + flags 接口简化

### A. `ApiMethodDetailStorage` 抽离出 `api_tool_types.h`（本次任务引入的类，当场清理）

`api_tool_types.h` 是**直接面向调用方**的热层头；除强制内联的热访问器外不应含内部实现细节。抽离结果：

| 文件 | 内容 | 编译归属 |
|---|---|---|
| `core/api_tool_detail_storage.h`（新建） | 类声明（唯一） | 两侧 |
| `core/api_tool_detail_storage.cpp`（新建） | runtime 路径：`configure_lazy`/`push_injected`/`ensure_details`/`ensure_defaults`/`get_detail`/`get_defaults` | runtime + editor |
| `editor/api_tool_detail_storage_editor.cpp`（新建） | **editor 路径：`push_cold`/`seal_cold`** | **仅 editor** |
| `api_tool_types.h` | 只剩 `class ApiMethodDetailStorage;` 前向声明 + 裸指针成员 | — |
| `api_tool_types.cpp` | storage 实现清零（`grep -c` = 0） | — |

**分离实测**：`.build/runtime/` 无 `*detail_storage_editor*`（0 个）；`.build/editor/` 有 1 个。符号级验证：runtime 侧 obj 的 `push_cold`/`seal_cold` 命中 = 0，editor 侧 = 5。SConstruct 是唯一接线点（`api_tool/editor/*.cpp` 只在 `editor_globs`）。

`api_tool_types.h` 同时删掉随类迁出的 `#include <atomic>` / `<mutex>`。

### B. `FMethodInfoBase` 删除（对应接口调整的顺手优化）

该类是重构前的热字段缓存（`is_vararg` / `return_type` / `Vector<Variant::Type> argument_types` + `JSB_DEBUG` 的 `name_`）。重构后这三个字段全部由 api_tool 热数据直接提供（`_FORCE_INLINE_` 无锁访问器），缓存成了**冗余拷贝**：

- `FBuiltinMethodInfo` / `FUtilityMethodInfo` 改为直接持 `method_info` / `utility_func` 指针，`check_argc` 与调用路径改走 `is_vararg()` / `get_return_type()` / `get_argument_type(i)` / `get_argument_count()`。
- 三个站点的填充循环（`jsb_primitive_bindings.cpp` ×2、`jsb_godot_module_loader.cpp` ×1）删除——每次绑定注册少一次 `Vector` resize + 逐参拷贝。
- `JSB_DEBUG` 的 `name_` **实测 0 读点**（只 `set_debug_name` 写），一并删除；调试名需要时可直接读 `method_info->get_name()`。

**性能**：反汇编实测 4 条访问路径（`.agent_tmp/retarg.asm`）**均为 1 条指令**（`Vector::operator[]` 与 `LocalVector::operator[]` 机器码同为「基址+比例寻址+一条 load」），故**无性能代价**；内存省下 1113 份 `Vector` 头 + 堆块。

### C. flags 接口简化：删除 `internal::encode_flags`

`encode_flags` 的唯一职责是「把 `has_returns` 推导结果折进内部位」，但它被做成独立函数 + 调用方先推导再传值，形成两层。已删除该函数，把**折叠点收进 `ApiMethodAccess::setup()`**（新增 `bool p_has_returns` 参数，`p_flags` 改收**原始** Godot flags）：

```cpp
// setup() 内部：唯一折叠点
r_method.flags_ = p_has_returns ? p_flags : (p_flags | internal::METHOD_FLAG_NO_RETURN);
```

3 个调用点（`api_tool_parser.cpp` ×2、`api_tool_loader.cpp` ×1）改为直接传原始 flags + 布尔，少一层间接与一个中间变量。

### D. 本轮删除的死代码（0 调用点，实测）

| 死代码 | 位置 | 证据 |
|---|---|---|
| `get_flags_raw()` 公开访问器 | `ApiMethodBase` | 改为 `ApiMethodAccess` 的 friend 静态函数（store 往返用），公开接口不再暴露第二个 flags 访问器 |
| `get_method_index()` | `ApiMethodBase` | 0 调用点；成员 `method_index_` 保留（惰性寻址必需） |
| `bind_arg_block` / `bind_injected_arg_block` | `api_tool_store.cpp` | 0 调用点；实际绑定走 `attach_storage()` |

### E. reader/writer 改直接字段序列化（审查反馈 #5）

`PayloadWriter::write` / `PayloadReader::read` 是**模板**，编码长度由入参类型决定——两侧类型必须逐一对齐。此前 writer 走访问器 + `(uint8_t)` 强转，靠「写侧手动强转 + 读侧手动声明同类型」维持一致，易漂移。

现状：新增**外部链接 friend struct `ApiMethodHotWriter`**（`static` 自由函数不能跨编译单元作 friend，这是 C++ 语义）；writer 直读具体成员（`flags_`/`hash_`/`ret_.type`/`ret_.meta`/`arg_count_`/`args_[i]`），读侧**逐字段镜像**并加配对注释（字段顺序 name → flags(RAW) → hash → ret.type → ret.meta → arg_count → default_count）。

实施中我自己引入过一次**字段顺序错位**（reader 漏读 `name`/`hash`）——正是该审查点警告的失效模式；已修复并重新验证。

### F. 返回值折进 `ApiMethodArg`（审查反馈 #4）

`return_type_`(4 B) + `return_meta_`(1 B) 两个独立成员替换为一个 `internal::ApiMethodArg ret_`（2 B）：

| 结构 | 改前 | 改后 |
|---|---|---|
| `ApiMethodBase` | 48 | **40** |
| `ApiMemberMethodBase` | 56 | **48** |
| `ApiClassMethod` | 64 | **56** |
| `ApiBuiltInMethod` | 72 | **64** |
| `ApiUtilityFunction` | 64 | **56** |

7 条布局 `static_assert` 已同步。**零扩展实测**：`movzbl`（load u8 + zext 折叠成单条）vs `movl`，同为 1 条指令（`.agent_tmp/retarg.asm`）。

### G. `validated_call` 整数转换收敛（审查反馈 #6）

循环下标统一 `uint16_t`，删 3 处 `(uint16_t)i` 强转；接口边界（引擎 `int p_argcount` ↔ 热数据 `uint16_t`）的转换保留——那是有实际语义的边界转换，不是噪音。

### H. 本轮验收（全部实测）

| 项 | 结果 |
|---|---|
| 规范重建（storage 抽离后） | **EXIT=0**，0 错误 |
| 规范重建（FMethodInfoBase 删除后） | **EXIT=0**，0 错误 |
| `verify_codegen.py` | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致` |
| `tests=yes` 重建 | **EXIT=0** |
| C++ 测试 | runtime `50\|50 passed` + `581\|581 passed`；editor `3\|3` + `12\|12`；`running tests result: 0`；**0 泄漏**；doctest 6 行 |
| TS 集成 | **exit 0**，`[JS] result: PASS - all checks passed` |

spec `api-tool-lazy-layout.md` 新增 §1.5（storage 归属与 runtime/editor 分离的验证方式）。

### I. `validated_call` 调用点的重复缺省值补齐 —— 已清理（2026-09-20）

原则：**缺省值补齐只发生在 `validated_call()` 内，调用点不得重复补齐**。三个版本的 `validated_call` 各自自包含：

| 版本 | 缺省值行为 |
|---|---|
| `ApiBuiltInMethod::validated_call` | `missing = method_argcount > argc ? 差 : 0`；`missing > 0` 时才 `get_defaults()`，从缺省值尾段补齐不足的参数 |
| `ApiClassMethod::validated_call` | 不碰缺省值——`object_method_bind_call` → 引擎 `MethodBind::call` → `call_with_variant_args_dv(..., get_default_arguments())` 自行补齐 |
| `ApiUtilityFunction::validated_call` | 无缺省值（恒 0）；`argc < method_argcount` 直接报错 |

#### 清理 1：`jsb_primitive_bindings.cpp` `call_builtin_function`

改前：`allocated_argc = MAX(known_argc, argc)` 分配 `argv`/`args`，循环内用 `defaults_resolved`/`defaults`/`default_index`（`index - (known_argc - default_count)`）自行填缺省值，最后以 `allocated_argc` 调 `validated_call`。后果：外部先补一遍、内部再补一遍（且内部 `missing` 恒为 0 → 惰性 defaults 永不触发）。

改后（实测 `grep -c allocated_argc` 4→0）：按**原始 `argc`** 分配与循环，只转换调用方实际传入的参数（`index < known_argc` 用声明类型，否则 vararg 无类型转换），`validated_call(self, argv, argc, ...)`，析构循环同步改 `argc`。

#### 清理 2：`jsb_object_bindings.cpp` class 方法路径

改前无条件 `get_defaults()` 取缺省值，再按 `default_index = index - method_argc` 取用。**该分支在 `check_argc` 通过后恒不可达**（`argc <= method_argc` ⇒ `default_index < 0`），白触发一次惰性加载。改后删除 `default_count`/`defaults`/`default_index` 全部簿记，保留 `check_argc` 校验与错误信息，加注释说明由引擎补齐。

#### 验证

- 全仓 `grep -rn "default_index\|defaults_resolved\|allocated_argc" src/runtime/` = **空**
- `get_defaults(` 在 `src/runtime/` = **0**；仅剩 `src/editor/codegen/jsb_codegen_type_db.cpp:225,373`（codegen 正常用途）
- 两文件 clang `-fsyntax-only` **rc=0 errors=0**
- 构建 EXIT=0；验收链全绿（见 §J）

**教训**：我先前建议「删 `validated_call` 的补缺省分支改其契约」——**错的**。分支之所以看起来死，是因为调用方重复做了同一件事；**该删的是重复方（调用点）**，不是被重复方。

### J. 本轮（重复补齐清理后）验收（全部实测，日志 `.agent_tmp/accept_chain.log`）

| 步 | 命令 | 结果 |
|---|---|---|
| 规范重建 | `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` | **EXIT=0**（128 s；`/WX` 生效 → 无未使用变量告警） |
| A 产物零差异 | `python misc/verify_codegen.py --godot <abs exe>` | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致（忽略行尾差异）` |
| B 测试构建 | `scons ... tests=yes -j5` | **EXIT=0** |
| C C++ 测试 | `godot --headless --path ./project --jsb-run-tests` | **EXIT=0**；editor `test cases: 3 \| 3 passed`、`assertions: 12 \| 12 passed`；runtime `test cases: 50 \| 50 passed`、`assertions: 581 \| 581 passed`；两段均 `Status: SUCCESS!`；`running tests result: 0`；**0 泄漏**（全日志无 orphan/leaked/still-in-use 输出） |
| D TS 集成 | `godot --headless --path ./project --verbose` | **EXIT=0**，`[JS] result: PASS - all checks passed`；8 个场景全部 `START-DIAG ... fail=false` |
| E 恢复规范产物 | `scons ... -j6` | **EXIT=0** |
| F 部署一致 | `md5sum` 四处 | `godotjs-ext` = `f874e4ef83e90d6c48a03de09ed2d350`（bin 与 addon 相同）；`godotjs-ext-editor` = `25703f93709624c600e5a5d087cc1ac4`（bin 与 addon 相同） |

**`tests=yes` 生效判别**：日志出现 6 行 `[doctest]`（含 2 段汇总 + 2 个 `Status: SUCCESS!`）→ 测试套件确实编入，非静默 no-op。

**针对本次改动的定点回归证据**（`project/tests/default-args/DefaultArgs.tscn`，`fail=false`）——该套件正是覆盖被清理路径的用例集：

- **builtin 缺省值槽**（改动 1 的直接打击面）：`Vector2.limit_length()`（float 槽 → 1.0）、`GArray.bsearch(2)`（bool 槽 → true）、`GDictionary.get("missing")`（Variant 槽 → null）全部通过；
- **builtin String 槽**：`PackedByteArray.get_string_from_multibyte_char()` → `"hi"`（缺省 `""` 正确解码）；
- **class 引擎侧补齐**（改动 2 的直接打击面）：`TabBar.add_tab()`→`tab_count=1`、`CodeEdit.set_code_region_tags()`→`"region"/"endregion"`、`Curve2D.add_point(p)`→`get_point_in=(0,0)` 且第二次省略 index 落在 index 1（int 缺省 -1 的追加语义）、`AStar2D.add_point(1,v)`→weight_scale=1.0、`FileAccess.get_csv_line()`→delim `","`、`Node.find_children("zzz_nomatch*")`、`Object.tr("hello")`；
- **元数边界**：`GArray.bsearch()` [M-1 throws] / `bsearch(2)` [M passes] / `bsearch(2,true)` [N passes] / `bsearch(2,true,0)` [N+1 throws]；`Curve2D.add_point` 同构四例。

→ 删除调用点补齐后，**缺省值语义完全不变**：builtin 由 `validated_call` 内部按 `missing > 0` 惰性补齐，class 由引擎 `MethodBind::call → call_with_variant_args_dv(..., get_default_arguments())` 补齐（引擎源码 `core/object/method_bind_common.h:83` 已核对）。

spec `api-tool-lazy-layout.md` 新增 §3.5「缺省值补齐只在 `validated_call()` 内 —— 调用点不得重复补齐」。

### K. 显式传 `undefined`：本轮**未引入新偏离**（与 HEAD 的差异属前序重构，已有账）

先厘清三个状态，避免归因错误：

| 状态 | class 路径对显式 `undefined` 的处理 | 分支是否可达 |
|---|---|---|
| **HEAD**（惰性化重构前） | `if (argument->IsUndefined() && default_arguments.size() > 0) args[index] = default_arguments[index - method_argc];` —— **无界检查** | **可达**，且索引恒为负 |
| **本轮编辑前**（前序重构已落地） | `if (argument->IsUndefined() && default_count > 0 && default_index >= 0 && default_index < (int)default_count)` —— 带上下界 | `default_index < 0` ⇒ **恒不可达** |
| **本轮编辑后** | 无此分支，直接 `js_to_gd_var` | — |

**关键结论：本轮删除的分支在编辑前就已经是死代码**（`default_index = index - method_argc`，而 `check_argc` 保证 `argc <= method_argc` ⇒ 恒 `< 0`；`src/internal/jsb_variant_util.h:140-148`）。因此**本轮清理对「显式 `undefined`」不产生任何行为变化**——编辑前该分支不执行，编辑后同样不执行，两者都落到 `js_to_gd_var`。

#### K.1 三族对照：只有 class 族尝试过「显式 `undefined` → 缺省值」

`git show HEAD` 逐文件实测（`grep -c IsUndefined`）：

| 族 | 文件/站点 | `IsUndefined` 命中 | 缺省值填充的触发条件 |
|---|---|---|---|
| **class** | `jsb_object_bindings.cpp:421` | **1** | `argument->IsUndefined() && default_arguments.size() > 0` —— **按值判定**（显式 `undefined` 也进） |
| **builtin**（含同函数的 `utility=true` 变体） | `jsb_primitive_bindings.cpp:468-480` | **0** | `index >= argc` —— **纯按位置判定**（省略才进；`f(a, undefined)` 的 `argc` 已含该位） |
| **utility** | `jsb_object_bindings.cpp:333-372` | **0** | 无缺省值（json 恒 0），且 `validated_call` 要求 `argc >= M` |
| 静态绑定腿 | `src/static_binding/thunks/` | 0 | `(int)J < provided`（`provided = info.Length()`）+ `default_arg_slot` —— **同样纯按位置** |

**所以：是，只有 class 族做过「显式 `undefined` → 缺省值」的替换；builtin（primitive）族没有。**

- builtin 的缺省值**只**在实参被**省略**时生效：JS 的 `f(a)` → `info.Length()==1` → `index==1 >= argc` → 取 `default_arguments[1 - (M - DC)]`。而 `f(a, undefined)` → `info.Length()==2` → `index==1 < argc` → 走「已提供」分支做 typed 转换。
- 因此 builtin 下显式 `undefined` 的结果**由声明类型决定**（`js_to_gd_var(undefined, declared)`）：`OBJECT` → `nullptr` 且成功（`jsb_type_convert.cpp:178-182`）；`NIL`（Variant 形参）→ 回退无类型转换 → `Variant()`（`jsb_type_convert.cpp:315-317`）；`INT`/`FLOAT`/`STRING`/… → 转换失败 → 抛 `bad argument: N`。
- class 族那条按值判定的分支**条件是可达的**（如 `node.find_children("x", undefined)`：`argc=2`、`index=1`、`default_arguments.size()=3>0`），但**一旦进入必然崩**——`default_arguments[1 - 4]` 负索引（见下）。
- 这也解释了 TS 侧的动机：`typings` 把带缺省值形参渲染为 `type?: string /* = '' */`（`project/typings/godot5.gen.d.ts:1306`），`strict: true` 下 `f(a, undefined)` 是**合法写法**——class 族分支想兜的正是它；builtin 族从未尝试兜。

**唯一与 HEAD 的差异属前序重构的必然伴生**（非本轮）：

- HEAD：`default_arguments[index - method_argc]`（负索引）→ **`LocalVector<Variant>::operator[]`** → `CRASH_BAD_UNSIGNED_INDEX(p_index, count)` → `GENERATE_TRAP()`。依据（已逐条复核，修正了此前凭记忆写错的两处）：
  - 容器是 `LocalVector<Variant>`，**不是** `godot::Vector`（`third/godot-cpp/include/godot_cpp/core/object.hpp:69`）。
  - `LocalVector<T, U = uint32_t>` 的索引类型 `U` 默认 **`uint32_t`**（`templates/local_vector.hpp:47`）→ 负的 `int` 转成 `uint32_t` 后是巨值 → 命中 `>= count`；用的是 **`CRASH_BAD_UNSIGNED_INDEX`**（`local_vector.hpp:205`），不是 `CRASH_BAD_INDEX`。
  - `GENERATE_TRAP()` 的分支依据是 **编译器（`_MSC_VER` vs GCC），不是 dev/release**（`error_macros.hpp:63-73`）：本仓库 MSVC 构建下为 `__debugbreak()`，**release 也照样 trap**——此前「release 下越界读」的说法是错的。
- 现状：交给 `js_to_gd_var(undefined, 声明类型)`，按类型分支（`src/runtime/bridge/jsb_type_convert.cpp:149+`）——`OBJECT` 可空 → `nullptr` 且成功（`:178-182`）；`NIL` → null；其余类型 → 返回 false → 抛 `"Bad argument: N"` 受控报错。

方向与 PRD「已知偏离 #5」完全一致（非法调用从 UB/崩溃变为受控结果），属移除 `default_arguments` 后的伴生现象，**PRD 已记账**。合法调用（省略参数）行为不变，由 `default-args` 套件实测覆盖。**显式 `undefined` 当时无测试覆盖，本轮已补齐**：`project/tests/default-args/test-default-args.ts` 的 `"explicit undefined takes the position's default"` 段覆盖中间位替换、N+1 越界、必填位抛错与 `set_code_region_tags()` 回归，两条腿（静态/动态）跑同一套断言。

### L. 独立审查派单说明（未取得结论）

按工作流两次派 `trellis-check` 做独立复核（`CheckDefaultsRemoval`、`CheckPaddingRemoval`），**均因 15 分钟硬限（`task.maxRuntimeMs=900000`）被 abort 且零输出**（分别 25 / 40 次请求后被杀，transcript 显示其把预算全花在重新推导引擎内部机制上）。这是**派单范围设计失败，不是审查发现**；同一 scope 再派必然同样超时，故不再派单。

上述 §I/§J/§K 的结论由主会话自行完成复核，依据均为一手证据：`git show HEAD` 对照、`check_argc` / `CRASH_BAD_INDEX` 宏体、引擎 `method_bind_common.h:83`、AC7 硬校验点 `api_tool_store.cpp:498`、`get_defaults` 计数语义（`api_tool_detail_storage.cpp:108-117`）、以及 `project/tests/default-args` 套件的运行时实测结果。**独立第三方复核仍然缺失**，建议人工审查时优先看这一项。

---

## 需求实现（2026-09-20）：显式传 `undefined` 视同「省略」

### M. 契约与实现

**需求**：`f(a, undefined)` 在所有族都按「使用缺省值」处理，等价于 `f(a)`。用户明确要求 class 与 builtin 两族都实现（不是只修 class 的历史崩溃）。

**契约（最终采用，第二版）**：**按位置替换**。`i ∈ [min_argc, known_argc)` 且该位传了显式 `undefined` ⇒ 取 `defaults[i - min_argc]`；其余位置照常按声明类型转换。**不移位、不跳过、不裁剪元数**。

**第一版（已废止）**：曾把「尾部连续的 `undefined`」折算成更小的 `argc`（入口裁剪），并为此加了 `TypeConvert::trim_trailing_undefined`。**该函数已整体删除**，理由（勿改回）：

1. **位置错**：它挂在 `TypeConvert` 上，而该层的职责是「JS 值 → Godot 值」的类型转换，与「调用元数」无关；把元数折叠塞进类型转换层，等于让这一层替调用层决定实参个数。
2. **语义错**：裁剪只对「尾部连续」成立，表达不了中间位替换。`set_code_region_tags(undefined, "END")`（第 1 位取缺省、第 2 位仍是 `"END"`）在裁剪语义下**必然抛错**；若改成裁中段，后续实参会前移、调用语义被静默改写。
3. 裁剪把「调用方传了几个位置」与「其中几个生效」两个不同的量压成一个 `argc`。

**判定位置必须在 bridge，不能在 `validated_call`**：`Variant` 无法表示「显式 `undefined`」，一旦按 `Variant` 数组进 `validated_call` 该信息即丢失；只有 bridge 还持有 `v8::Value`。

**实现（四处，全部就地替换）**：

| 路径 | 位置 | 替换方式 |
|---|---|---|
| builtin 动态 | `jsb_primitive_bindings.cpp` `call_builtin_function` | 命中位直接取 `method->get_defaults(...)[i - min_argc]` 填入该位 `Variant` 槽，**不进转换** |
| class 动态 | `jsb_object_bindings.cpp` `_godot_object_method` | 同上（`min_argc = method_argc - method_default_count`） |
| builtin 静态 thunk | `thunks/builtin_methods.h` | 判定收成 `use_default_mask`；命中位改道 `default_arg_slot<>()`，**不进 `marshal_one`** |
| class 静态 thunk | `thunks/class_methods.h` | thunk **无缺省字面量**（codegen `class_entry_expr` 明写），命中位从方法记录取 `defaults[i - M]` 填入**本 thunk 自己的 `Variant argv[i]`** |

**class 静态 thunk 的接线**（缺一不可）：

- 声明：`static_binding/dispatch.h` 的 `class_method_defaults(const void *, uint32_t &)`。
- 定义：`jsb_object_bindings.cpp`（`#if JSB_WITH_STATIC_BINDINGS` 内）——把 api_tool 类型挡在静态绑定头之外。
- 传参：注册处 class 方法循环对 **`default_count > 0`** 的静态 thunk 传 `(void *)&method_info` 作 Data；`M == N` 的方法不传。
- **不是「整通回退动态路径」**（第一版曾这么实现，已废弃）：那样等于把静态腿白扔掉——重解析 `this`、重查 method bind、全部参数再走一遍 Variant 装箱与 `validated_call`。class thunk **本来就在建 `Variant argv[]` 调 `object_method_bind_call`**，缺省值只需就地填进它自己的槽。
- 取默认值**只在命中时发生**（惰性：`defaults == nullptr` 时才调），miss 路径不碰惰性加载；`M == N` 的方法整块 `if constexpr` 编译掉。
- 记录长度不足时该位退化为普通转换（逐位范围检查），不做越界读。

**必填位 / vararg 位传 `undefined` 不是缺省语义**：`i < min_argc` 时 `undefined` 是值，按声明类型转换、不符即报错（`Curve2D.get_point_position(undefined)` 仍抛错）；vararg 位同理（缺省值与 vararg 不共存）。**元数校验仍用调用方实际传入的数量**，`Vector2.limit_length(undefined, undefined)`（N+1）仍抛错。

**未改动的族（有意）**：

- **全局 utility**：json 实测 114 个函数**全部**无缺省值（两次复核），无可命中位；静态 thunk 里同样 `M == N`。**有意保持原样**。
- **String / StringName 内建方法**（借 `utility=true` 绑定，`info[0]` 是接收者）：它们**是**带缺省值的 builtin 方法，走 `call_builtin_function`，替换与其同源（`p_base = 1`）。

### M.1 范围说明：本需求**必须**触及静态绑定（超出 PRD R10 的原始边界）

PRD 的 **R10** 与 **Out of Scope** 写了「仅 v8 + 动态绑定路径构建与验证；不涉及静态绑定」。**本需求是这条边界的例外，且是不可避免的**，理由是一手实测：

1. `SConstruct:36` 的 `static_binding` 默认值 = **True**，而**规范构建命令**（`scons target=editor ... -j6`）**不含** `static_binding=no` ⇒ 默认产物**带静态绑定**（实测 `cl` 命令行含 `-DJSB_WITH_STATIC_BINDINGS`）。
2. 实测（`dispatch_class.gen.cpp` 反查 json）：**1040 / 1040** 个「非 vararg 且有缺省值」的 class 方法**全部**有静态 thunk，**0 个**落到动态路径；builtin 侧同理。即：**默认产物里几乎全部带缺省值的方法都走静态 thunk**。
3. 因此若只改动态两条路径，**用户要求的特性在默认产物里完全不生效**——这不是「顺手扩大范围」，而是该需求的落实面本身。

**已一并完成两腿验证**（见 §N）：静态腿 = 规范构建（默认 `static_binding=yes`），动态腿 = `static_binding=no` 构建。两腿 md5 各自两处部署位一致。

**给后续维护者的边界结论**：改「绑定边界」（参数个数归一化、缺省值触发条件、传递语义）时必须**同步两条腿**，否则按构建开关分叉；改纯动态绑定的内部实现（如 `validated_call` 的细节）才可只动动态腿。

### N.1 独立第三方复核（已取得，与 §L 的两次失败形成对照）

**派单设计**：本轮按 `.trellis/spec/guides/workflow-rules.md` §派单硬规则重做——**10 个可判定项**（每项只要 `PASS/FAIL/UNVERIFIED` + 一行证据）、把已核实事实原样塞进 `context` 并明写「不要重新推导」、**写死收尾条件「12 次工具调用立即 yield，未决项标 UNVERIFIED」**、给 `outputSchema`、禁止跑构建/测试。

**结果**：**10/10 PASS，defects = 0**，耗时 **2 分 47 秒**（对照 §L 两次「15 分钟被杀、零输出」）。

关键复核结论（均为该 agent 独立给出的行号证据）：

- **不变量 (a) 下界**：site 1 `known_argc - (int)get_default_count()` 中 `known_argc` 已是 `const int`，**强制有符号减法、无 unsigned 下溢**；site 2 两个 `uint16_t` 提升为 `int`（不会回绕）。两处都等于 `check_argc` 接受的最小元数。
- **不变量 (b) 上界**：vararg 下 `argc > p_max_argc` 使循环零迭代；且 vararg 无缺省值 ⇒ `p_min_argc == N`，`argc == N` 也不触发 ⇒ 原样透传，`undefined` 保持为真实实参。并指出**静态腿结构上免疫**：vararg 走独立模板（`builtin_vararg_method_thunk` / `class_vararg_method_thunk` / `utility_vararg_function_thunk`），都不调用裁剪。
- **不变量 (c)**：四个站点**无一**把裁剪后的值回喂元数校验。
- **索引安全**：裁剪体只在 `argc >= p_min_argc + 1` 时执行 ⇒ 下标 `>= p_base`；`p_argc == 0` 需 `p_min_argc < 0` 才会进体，而四站点的 `p_min_argc` 均 `>= 0`。site 1 的 `provided` 唯一可能为 `-1` 的情形（`utility=true` 且 `info.Length()==0`）已被 `_utility_method` 前置拒绝。
- **检查 7（我标记为最重要）**：该 agent **独立分辨出两个 `utility` 是两回事**，并给出与我一致的结论——`utility_functions.h` 无裁剪是对的（`ApiUtilityFunction` 无缺省值，`M == N`，裁剪不可达），而 site 1 的 `utility == true` 是 **builtin 方法的「utility 形式」绑定**（String 内建方法，**有**缺省值），`reflect_bind_utilities` 不查静态表 ⇒ 该分支**活跃且必要**。它同时确认了我事后补写的 spec §3.5b「两个 utility」表格与代码一致。

**唯一 hardening 建议（已采纳）**：helper 的索引安全依赖 `p_min_argc >= 0` 这一数据不变量。若未来出现损坏记录（`default_count > argument_count`），`p_min_argc` 为负 ⇒ `argc == 0` 时读 `p_info[p_base - 1]`。当前数据无法产生（parser 保证 + codegen 尾段连续性断言），但本模块对损坏数据是**硬校验**取向（AC7），故直接加上 `argc > 0` 使其**自防御**，不依赖外部不变量。

**未决项（该 agent 明示）**：两条 json 比例事实（114 个 utility 全无缺省值；`default_count <= argument_count`）它按只读预算**未独立重测**，标注为 `accepted`——这两条主会话已两次实测。

### N. 验收（2026-09-20，全部实测）

| 步 | 命令 | 结果 |
|---|---|---|
| 规范重建 | `scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6` | **EXIT=0**（140.9 s，`/WX` 生效 → 无告警） |
| A 产物零差异 | `python misc/verify_codegen.py --godot <abs exe>` | **EXIT=0**，`✅ 校验通过: 生成产物与基线一致（忽略行尾差异）` |
| TS 编译 | `cd project && node node_modules/typescript/bin/tsc --noCheck` | **EXIT=0** |
| TS 集成 | `godot --headless --path ./project --verbose` | `[JS] result: PASS - all checks passed`；8 场景全 `fail=false`；`DefaultArgs: default-argument checks finished, fail=false` |

**定点回归证据**——`project/tests/default-args/test-default-args.ts` 的 `section("explicit undefined takes the position's default")`，**两条腿都跑同一套断言**（第二版起不再有静态腿跳过门控）。实测这些方法都有静态 thunk，即本机构建实际走静态腿：`grep -c` 命中 `bsearch`=12、`limit_length`=4、`add_tab`=1、`set_code_region_tags`=1、`find_children`=1、`add_point`=7、`get`=15。

- **builtin 缺省槽**：`Vector2.limit_length(undefined)` → length=1；`GArray.bsearch(2, undefined)` → 1（bool 缺省 true）；`GDictionary.get("missing", undefined)` → null（Variant 槽）；
- **builtin 中间位**：`GArray.slice(0, undefined, 2)` → size=3、值 `[1,3,5]`（中间位取缺省、**后续实参不前移**——这是按位置替换区别于裁剪的直接判别）；
- **class**：`TabBar.add_tab(undefined, undefined)` → tab_count=1、title=""；`TabBar.add_tab(undefined, null)` → tab_count=2、title=""（第 1 位取缺省、第 2 位仍是 `null`）；`Curve2D.add_point(p, undefined)` → point_count+1、`get_point_out(2)=(0,0)`；`Curve2D.add_point(p, undefined, undefined, undefined)` → 追加语义（index 缺省 -1）；`Node.find_children("zzz_nomatch*", undefined)` → size=0；
- **class，`undefined` 之后仍有真实值（旧裁剪语义下的必错用例）**：`CodeEdit.set_code_region_tags(undefined, "END")` → start=`"region"`、end=`"END"`。第一版（裁剪）在这条上**必然抛错**，第二版（按位置替换）通过——这条是语义修正的定点判据；
- **回归**：`CodeEdit.set_code_region_tags()`（全省略）→ `"region"`/`"endregion"`（引擎侧补齐未被替换逻辑破坏）；
- **边界（三条都是 `expectThrows`）**：
  - `Curve2D.get_point_position(undefined)` [必填位] → 仍报错（替换只作用于 `[min_argc, known_argc)`，必填位上的 `undefined` 仍是值）；
  - `Vector2.limit_length(undefined, undefined)` [N+1] → 仍报错（元数校验用调用方实际传入的数量）；
  - `Curve2D.add_point(undefined)` [必填 `Vector2`] → 仍报错。

**先前手写 `(x as any)` 的边界探针已按项目 TS 规范改写**：改用一次带理由的 `as unknown as`（`callable()` 辅助），不再使用 `any`。

**第二版验收（2026-09-20，全部实测）**——两腿各自构建后 `md5sum` 两处部署位一致再跑：

| 腿 | dll md5（`bin/` = addon 两处一致） | C++ 测试 | TS 集成 |
|---|---|---|---|
| 静态（默认 `static_binding=yes`） | `1ff28f3427e03b0ea24f0e332cbe16dc` | `3\|3`、`50\|50`、`581\|581`、`running tests result: 0` | `result: PASS - all checks passed`、`fail=false`、`FAIL` 命中 0 |
| 动态（`static_binding=no`） | `09cdf419bc9a3cd2b669e1b3c8829ef5` | 同上 | 同上 |

静态腿日志另证「确实跑在静态腿且未回落动态」：`explicit-undefined probes skipped` 命中 **0**（门控已删）、全日志仅 **2** 条 `static binding not found`，且都是无关方法（`OS.get_preferred_locales`、`ResourceLoader.get_resource_type`）——本套件用到的方法**全部**命中静态 thunk。

**与 §K 的关系**：§K 记录的「class 族 HEAD 上那条显式 `undefined` 分支必然崩（负索引 → `CRASH_BAD_UNSIGNED_INDEX` → trap）」在本轮之后**不再是一个需要维持的偏离**——显式 `undefined` 现在在可缺省位被替换为该位缺省值（bridge 层的 `Variant` 替换 / 静态 thunk 的缺省槽），不再落到 typed 转换后崩/报错。PRD「已知偏离 #5」关于 utility 的那部分仍然成立（全局 utility 无缺省值可补，非法调用仍是受控报错）。

---

## 方案评估（2026-09-20）：`ApiBuiltInMethod` 缺省值缓存（虚函数 + 模板 + 子类 + 指针数组）

用户提出的方案：`ApiBuiltInMethod` 加虚函数 + 模板 `validated_call_impl<bool has_defaults>` + 子类 `ApiBuiltInMethodWithDefaults` + `methods` 改指针数组；问是否值得做，以及是否也适用于 `ApiClassMethod`。

### O. 结论

**不建议实施。** 三点理由，均有实测支撑：

1. **收益面 6.5%，代价面 100%**——vptr 要付在**每一个**方法上，而只有带缺省值的方法可能获益。
2. **子类方案在结构上必然要求指针数组**（`LocalVector<T>` 按值存储 → 存子类必然切片），于是代价不是「一个 vptr」，而是「vptr + 容器间接层 + 拥有权模型重做 + 约 64 处迁移」。
3. **class 族更弱**：class 的缺省值**实体值**在运行时是死分支（引擎补齐），只有**计数**被读（`check_argc`），所以子类化最多省掉「不做 `missing > 0` 判断」，而那个判断已被证明是良预测的单条比较。

**真正该做的替代方案**见 §O.4（零 vptr、零容器改动）。

### O.1 代价：vptr 实测（clang 实测，非估算）

```
ApiMethodBase        40 → 48   (+8)
ApiMemberMethodBase  48
ApiBuiltInMethod     64 → 72   (+8)
ApiClassMethod       56 → 64   (+8)
ApiUtilityFunction   56 → 64   (+8)
```

实测方法：`.agent_tmp/vptr_cost_probe.cpp`——用一个**派生类引入自身虚函数**来测量（引入 vptr 会把它那份无 vptr 的基类子对象整体后移 `sizeof(void*)`），因此无需改动真实头文件即可得到准确数字。

**摊到全部方法上**（json 实测：class 16822 / builtin 999 / utility 114，合计 17935）：

| 族 | 方法数 | 带缺省值 | 加 vptr 的额外常驻 |
|---|---|---|---|
| class | 16822 | 1040 | +134,576 B |
| builtin | 999 | 126 | +7,992 B |
| utility | 114 | **0** | +912 B（纯赔） |
| **合计** | 17935 | **1166（6.5%）** | **≈ +143.5 KB** |

→ **16769 个方法（93.5%）白付 vptr**。utility 尤为直接：json 两次复核 114 个函数**全部**无缺省值，子类化对它毫无意义。
同时 `api_tool_types.h:849-856` 那组 `static_assert` 要同步改（布局漂移的报警器会被迫放宽）。

### O.2 代价：指针数组是子类方案的**结构性前提**，不是可选项

`ApiBuiltinClass::methods` / `ApiClass::methods` 都是 `godot::LocalVector<ApiBuiltInMethod>` / `<ApiClassMethod>`，**按值存储**。要把 `WithDefaults` 子类放进去而不切片，只能存指针 → `LocalVector<ApiBuiltInMethod*>`。于是连带：

1. **拥有权模型必须重做**，且两族语义本就不同：
   - `ApiBuiltinClass` 是 `memnew(ApiBuiltinClass)`（`api_tool_loader.cpp:280`）——堆对象。
   - `ApiClass` 是**栈上值再拷进 deque**：`ApiClass data;` → `class_cache_.insert(p_name, data)`（`api_tool_loader.cpp:290,369`），`TypedCache::insert` 走 `items.push_back(p_item)`（`api_tool_loader.h:134`）。
   - 因此若 `methods` 持 `memnew` 出来的方法指针，栈上 `data` 拷入 deque 后原对象析构 → **double-free**。必须自定义拷贝/移动 + 深拷贝，或改成 deque 内就地构造。
2. **`attach_storage` 要改写**（`api_tool_store.cpp:129-136`）：它依赖 `r_methods[i]` 的 `operator[]` **与连续参数块** `p_arg_block + arg_offset`。指针数组下，「连续」不再由容器保证。
3. **迁移面 66 处** `methods` 迭代/取址点（实测 `grep -ho "\.methods\b\|->methods\b" src/api_tool src/editor/codegen | wc -l` = 66；分文件：`api_tool_loader.cpp` 9、`api_tool_store.cpp` 10、`api_tool_parser.cpp` 19、`api_tool_store_writer.cpp` 10、`api_tool_editor.cpp` 1、`jsb_codegen_generator.cpp` 6、`jsb_codegen_type_db.cpp` 4、`jsb_codegen_docs.cpp` 3、`jsb_codegen_writer.cpp` 2），外加 `ApiMethodHotWriter` 这个 friend struct 与 `ApiStoreReader` / `ApiLoader` / `ApiMethodAccess` 三个 friend 声明。
4. **每次访问多一次间接跳转**，而热路径（`get_argument_type` / `get_default_count` / `is_vararg`）本来是一条 `L1` 命中的 load。

### O.3 为什么收益本就不存在（分支与内联两条路都被堵）

用户方案里「模板 `validated_call_impl<bool has_defaults>`」想省的，是 builtin 现有的这一条判断（`api_tool_types.h:287`）：

```cpp
const uint32_t missing = method_argcount > (uint32_t)p_argcount ? method_argcount - (uint32_t)p_argcount : 0;
const Variant *default_values = missing > 0 ? get_defaults(default_value_size) : nullptr;
```

- **分支**：一条良预测的整数比较。它所在的路径已经做了 `alloca` 三块、逐参 `var_to_arg_ptr`（每个参数一次约 40 分支的类型 switch）、一次完整 ptrcall。省掉这一条的收益量级是噪声。
- **`get_defaults` 慢路径本就不会被触发**：`ensure_defaults()` 的**首行**是 `defaults_loaded_.load(std::memory_order_acquire)`，命中即返回（`api_tool_detail_storage.cpp:58`）——无锁、无 IO。这正是 AC3 要的语义，且已经实现。
- **内联假设不成立**：本仓库 `dev_build=yes` ⇒ `DEV_ENABLED` 已定义 ⇒ `_FORCE_INLINE_` 退化为**普通 `inline`**（`third/godot-cpp/include/godot_cpp/core/defs.hpp:119-132`），不是 `__forceinline`；`/Od` 下不会被内联。所以「模板版把分支消掉、编译器再内联」在开发构建里根本不会发生，而发布构建里它本来就只是一条预测分支。
- **静态腿已经证明这条路怎么走才对**：`thunks/builtin_methods.h` 的 `DefsT::count` 让 `has_defaults` 成为**编译期模板参数**（codegen 逐方法实例化，`M = N - DefsT::count`）。也就是说「按方法特化、省掉运行时判断」这个**目标已经达成**，而达成方式是**调用点模板参数**——没有 vptr、没有子类、没有指针数组。用户方案想在动态腿复刻一个静态腿已经用模板解决的问题，却要引入静态腿刻意回避的间接层。

### O.4 若仍要拿到「无分支」：建议的替代方案（零 vptr）

热字段里已经有判别位，且**已经在每次调用时被读**（`check_argc` 要 `get_default_count()`）：

```cpp
// 同一族内两个非虚成员函数，按已有热字段二选一 —— 无 vptr、无容器改动、无拥有权重做
const int argc = (int)p_argcount;   // 元数不再被改写（第二版契约：按位置替换，不裁剪）
if (get_default_count() == 0) {
    validated_call_no_defaults(base, argv, argc, ret);   // 循环里不判 missing
} else {
    validated_call_with_defaults(base, argv, argc, ret);
}
```

> 注：这段是**当时（第一版裁剪语义下）**的示意。第二版已废止裁剪，`argc` 恒等于调用方传入的数量，替换发生在循环内的位置判定上；此处保留原样仅为记录该轮评估的上下文。

理由：`default_count_` 在 `ApiMemberMethodBase` 内，实测该结构 48 B（相对 `ApiMethodBase` 的 40 B 只多 8 B，已被填充吸收），与 `name_`/`flags_` 同处一条 cache line——判断它的成本 ≈ 0。分支可被内联，`validated_call` 无需变虚，`methods` 不必改指针数组，`attach_storage`、writer、friend 声明全部不动。

**但更诚实的建议是：什么都不做。** 目前 builtin 的 `missing > 0`（第二版起再加上 §3.5b 的按位置替换判定）已是 1 次比较，且它位于一条以 ptrcall 为主导的路径上；在能给出**可测收益**（反汇编对照 `.agent_tmp/retarg.asm` 先例，或 benchmark 场景 A/B）之前，任何为此引入的间接层都是净负。

### O.5 对 `ApiClassMethod` 的适用性：更弱，不建议

- class 路径**运行时根本不读取省值实体值**——`ApiClassMethod::validated_call` 只把 `p_args` 转交 `object_method_bind_call`，补齐由引擎 `MethodBind::call → call_with_variant_args_dv(..., get_default_arguments())` 完成（引擎 `core/object/method_bind_common.h:83` 已核对）。
- class 侧读到的只有**计数**（`check_argc` 与错误信息里的 `method_argc - method_default_count`），而那是**已在热字段**的 `uint16_t`，无需任何加载。
- 所以子类化在 class 上能省的，仅剩「不读 `default_count_`」——把一个已在 cache line 上的字段读取换成一次**虚调用**，方向相反。
- 反证规模：class 有 16822 个方法，加 vptr = **+134,576 B**，而带缺省值的只有 1040 个（6.2%）。

### O.6 评估用证据清单（均为一手）

| 事实 | 出处 |
|---|---|
| `api_tool_types.h` 当前**全无虚函数** | `grep -n "virtual"` 仅命中 `is_virtual()` 方法名 |
| vptr 实测 40→48 / 64→72 / 56→64 | `.agent_tmp/vptr_cost_probe.cpp`（clang 实测） |
| 缺省值分布 class 16822/1040、builtin 999/126、utility 0 | json 实测（两次复核） |
| 构造器带缺省值 = 0 | json 实测（方案无需覆盖 ctor） |
| `ApiClass` 值拷贝进 deque、`ApiBuiltinClass` memnew | `api_tool_loader.cpp:280,290,369`、`api_tool_loader.h:134` |
| `attach_storage` 依赖连续参数块 | `api_tool_store.cpp:129-136` |
| `methods` 迭代/取址 ≈ 64 处 | 分文件 `grep -c` 实测 |
| `get_defaults` 首行无锁快路径 | `api_tool_detail_storage.cpp:58` |
| dev_build 下 `_FORCE_INLINE_` = 普通 `inline` | `third/godot-cpp/include/godot_cpp/core/defs.hpp:119-132`；`DDEV_ENABLED` 已在 cl 命令行实测 |
| 静态腿已用模板参数解决同一问题 | `thunks/builtin_methods.h` `DefsT::count` / codegen `defs_pack_expr` |
| class 缺省值实体值运行时不可达 | `api_tool_types.h:372-383` 仅转交引擎 |

**若用户仍决定实施**：请指定「可测收益」的判据（benchmark 场景 + 阈值，或反汇编对照），否则无法判定成败；且建议**只做 §O.4 的替代方案**（改动局限在 builtin `validated_call` 内部，不触碰容器与拥有权）。

---

## 静态路径 a/b 评估（2026-09-20，用户指定口径）

**问题**：静态 class thunk **不携带缺省字面量**（`class_entry_expr` 注释明写；缺省值由引擎 `MethodBind` 在 `call_with_variant_args_dv` 时补齐）。于是「显式 `undefined` 落在可缺省位」这个信息在 thunk 内**无法就地解析**。两条路：

| | 方案 | 形态 |
|---|---|---|
| **a** | codegen 为 class **发射缺省值** | class thunk 获得与 builtin 同形的 `DefVs<...>` 槽 ⇒ 命中位就地改道缺省槽 |
| **b** | 命中 `undefined` 时**向方法记录就地取该位缺省值** | thunk 保持无字面量；命中位从 Data 载的方法记录取 `defaults[i - M]` 填入**本 thunk 自己的 `Variant argv[i]`**，调用**不离开静态腿**（**已实现**） |

> ⚠ **b 的第一版实现是错的、已废弃**：当时写成「命中即整通回退给动态回调 `_godot_object_method`」。那不是「用静态腿 + 取缺省值」，而是**把静态腿整个扔掉重来**——重解析 `this`、重查 method bind、全部参数再走一遍 Variant 装箱与 `validated_call`（实测 **622 条**指令）。class thunk **本来就在建 `Variant argv[]` 调 `object_method_bind_call`**，缺省值只需就地填进它自己的槽。下表数值均为**修正后**的最终产物。

### A 侧：二进制增量（脚本算，非实现后测）

`.agent_tmp/planA_footprint.py` 读 `extension_api.json`，类型映射复用 codegen 自己的 `arg_template_expr`（与 builtin 侧同源），`sizeof` 用 clang **编译期真实探测**：

| 项 | 数量 | 体积 |
|---|---|---|
| 类族带缺省值的参数总数 | 1715 | — |
| 去重共享实例（同 C++ 类型 + 同值） | 118 | **954 B** |
| `godot::Array` / `Dictionary`（引用类型，共享静态会把**同一对象**发给所有调用点 ⇒ 逐处实例） | 42 | **336 B** |
| **合计静态实例** | **160** | **1290 B（1.26 KiB）** |
| （若统一落成 24 B `Variant` 槽） | 160 | 3840 B |

口径要点：**引用类型必须按 C++ 类型判定**，不能只按 json 名字——`typedarray::*` 经 `arg_template_expr` 映射为 `godot::Array`，同为引用类型，须逐处实例化（只按 json 名会得 124+26=150，漏 10 个）。最大项：`int32_t` 22、`float` 19、`godot::String` 16、`int64_t` 13。

### B 侧：已实现形态（**就地替换，不重跳**）

- **builtin thunk**：可选位判定收成一次 `uint32_t use_default_mask`（`M..N` 各一位），marshal 与 arg_ptrs 两趟都读位、不重复探 `IsUndefined`；命中位不进 `marshal_one`，直达 `default_arg_slot<>()`。
- **class thunk**：命中位**从方法记录就地取该位缺省值，填进本 thunk 自己的 `Variant argv[i]`**，其余位置照走原快路径，调用**不离开静态腿**。判定在 marshal 循环内单趟完成；记录只在首次命中时惰性取回。
- **接线**：注册处对 `default_count > 0` 的静态 thunk 传 `(void *)&method_info` 作 Data；thunk 经 `static_binding::class_method_defaults()`（`dispatch.h` 声明、`jsb_object_bindings.cpp` 定义）取回缺省值数组。

> ⚠ **第一版 B 是错的（已废弃，勿改回）**：当时写成「命中即整通回退给动态回调 `_godot_object_method`」。那不是「静态腿 + 从 api_tool 取缺省值」，而是**把静态腿整个扔掉重来**——重解析 `this`、重查 method bind、全部参数再走一遍 Variant 装箱与 `validated_call`。class thunk **本来就在建 `Variant argv[]` 调 `object_method_bind_call`**，缺省值只需就地填进它自己的槽。

### 指令数对比（真实反汇编，`/Od` dev 构建，非探针）

**受控 A/B**：把 `class_methods.h` 换成 HEAD（pre-B）版、**同一个 `dispatch_class.gen.obj` 位置重建**，量**同一实例化**的差（`llvm-nm` 复核 pre-B obj 的 `class_method_defaults` 引用数 = **0**，确认确为改动前产物）。每个实例化 = thunk 本体 + 其 outlined lambda，**只数 thunk 体会严重低估**（前几版漏了 lambda）。

| 项 | 实例 | M/N | pre-B 家族 | B（修正后）家族 | Δ |
|---|---|---|---|---|---|
| **A** builtin `Vector2.limit_length` | 静态 thunk | 0/1 | — | 可选位处理区 **30** | — |
| **A** builtin `GArray.slice` | 静态 thunk | 1/4（3） | — | 可选位处理区 **30** | — |
| **B** class `CodeEdit.set_code_region_tags` | 静态 thunk | 0/2 | **521**（364+101+24+21+11，5 符号） | **651**（371+127+83+27+21+11+11，7 符号） | **+130** |
| 对照 class `CodeEdit.add_auto_brace_completion_pair`（`M=N=2`） | 静态 thunk | 2/2 | **521**（同上 5 符号） | **521**（逐符号相同，5 符号） | **+0** |

### 聚合体积（决定性读数）—— 第一版 B 曾在此翻车

单实例化抽样不足以下结论。口径：整份 `dispatch_class.gen.obj` 的 `.text$mn` 段总和（15370 个实例化），pre-B 与 B **在同一位置分别构建**测得。

| `dispatch_class.gen.obj` `.text` 总和 | 字节 | 相对 pre-B |
|---|---|---|
| pre-B (HEAD) | 45,928,099 | — |
| B 第一版（per-position 泛型 lambda + 局部声明在 `if constexpr` 外） | 48,355,376 | **+2,427,277 B（+2.31 MiB）** |
| **B 修正后**（单非泛型 helper + `M==N` 整块编译掉） | **46,635,799** | **+707,700 B（+691 KiB）** |

**dll 端到端实测**（两侧 thunk 头同时回退 HEAD vs 同时为 B，同参数、同位置各构建一次）：

| dll（`bin/windows/godotjs-ext.windows.editor.x86_64.dll`） | 字节 | 相对 HEAD |
|---|---|---|
| HEAD（两侧 thunk 均为改动前） | 112,743,424 | — |
| **B 修正后** | **113,382,912** | **+639,488 B（+625 KiB）** |
| （参考）B 第一版 | 115,584,000 | （当时基线不同，不作差） |

**第一版 B 是错的**（体积口径的错，不是语义）：`produce_arg` 写成 **per-position 泛型 lambda**（`produce_arg.template operator()<I>()`），于是**每个可选位都生成一份完整函数体**；且两个 defaults 局部声明放在 `if constexpr` **之外**，导致**无缺省位的 14330 个实例化也各付 +18**。外推交叉验证：$1040 \times 198 + 14330 \times 18 = 463{,}860$ 条 × 5.23 B/条 ≈ 2.43 MB，与实测吻合。

**修正**（已落地）：

1. **`M == N` 整块 `if constexpr` 掉**：缺省值替换块整个移进 `if constexpr (M < N)`，`else` 分支**逐字复述 pre-B 的语句**（含 `(N <= (int)provided || (int)I < provided ? ... : true)` 这个短路形式）。**class 侧已实测验证**：对照方法家族 **521 = pre-B 521（Δ 0，逐符号相同）**——HEAD 的 class thunk 本就没有可选位机制，所以复述即等价。（builtin 侧不同，见第 4 条。）
2. **改用单个非泛型 helper** `auto substitute_default = [&](int i) -> bool`：一份 out-of-line 函数体服务全部可选位，不再每位置复制一份。TARGET 家族因此从第一版的 +198 降到 **+130**。
3. **缓存提升为每实例化一对**（`cached_defaults` / `cached_count`），且**放在 `if constexpr` 内**——`M == N` 时连静态状态都不生成。两者都是常量初始化，无 guard variable、无 atomic，命中路径只是一次普通指针读取。
4. **builtin thunk 同样修正**：`use_default_mask` 的构造循环与 marshal/arg_ptrs 两趟全部移进 `if constexpr (M < N)`，`M == N` 时走 `else` 分支。`slots` / `ok` / `arg_ptrs` 三个声明提到 `if constexpr` 外（两分支都要用），`use_default_mask` 留在内层。**builtin 带缺省值的方法只有 126 个**（json 实测；class 16822 个方法中 1040 个带缺省值）。

> ⚠ **此处与 class 侧不同，`else` 分支并不与 HEAD 等价，而是更小**（实测，见下）。class 侧对照方法家族是 **521 → 521（Δ 0）**；builtin 侧无缺省位的家族反而 **变小**：HEAD 即使 `[M, N)` 为空区间，也照样实例化那两个可选位辅助 lambda（符号签名 `integer_sequence@_K$S@std@@` 与 `AEAY01PEAX`＝arg 指针数组），B 的 `else` 分支不再引用它们，符号随之消失。抽样家族 **521 → 486 条指令、7 → 5 个符号**。

### 两侧 `dispatch_*.gen.obj` 的实测聚合（同位置分别构建）

| 目标文件 | HEAD | B（修正后） | Δ |
|---|---|---|---|
| `dispatch_class.gen.obj` `.text` | 45,928,099 | 46,635,799 | **+707,700 B（+691 KiB）** |
| `dispatch_builtin.gen.obj` `.text` | 2,904,967 | 2,814,296 | **−90,671 B（−89 KiB）** |
| **合计 `.text`** | 48,833,066 | 49,450,095 | **+617,029 B（+603 KiB）** |

builtin 侧按符号桶细分的指令数增减（同一份 obj，符号名判别 `DefVs` 是否为空）：

| 桶 | 实例数 | HEAD 指令 | B 指令 | Δ |
|---|---|---|---|---|
| 无缺省位 `method_thunk` | 680 | 226,593 | 215,033 | −11,560 |
| 无缺省位 lambda | — | 74,206 | 61,966 | −12,240 |
| 带缺省位 `method_thunk` | 87 | 32,034 | 35,079 | +3,045 |
| 带缺省位 lambda | — | 17,844 | 19,575 | +1,731 |

即 **builtin 侧净减**：无缺省位省下 23,800 条，带缺省位付出 4,776 条。这与 class 侧「无缺省位 Δ0」的结论**不矛盾**：HEAD 的 class thunk 本就没有可选位机制，自然无可删；HEAD 的 builtin thunk 有，且对空区间也照样实例化。

**两侧合计 `.text` +617,029 B**；dll 端到端实测（同一份 HEAD 与 B，同位置各构建一次）：**112,743,424 → 113,382,912 B，+639,488 B（+625 KiB）**。

**class 侧 +691 KiB 已全部归因于 1040 个真带缺省位的实例化**：$707{,}700 \div 1040 \approx 680$ B/实例，与「+130 条 × 5.23 B/条 ≈ 676 B」吻合；**无缺省位的 14330 个实例化为零增量**（对照实测 Δ 0）。

**这是「聚合口径」而非「抽样口径」才暴露的缺陷** —— 单看 `CodeEdit.set_code_region_tags` 一个实例化永远看不到那 14330 个实例化的 +18。

**关键读数**：

1. **pre-B 两方法家族完全相同（521）**——HEAD 的 class thunk 与 `M` 无关（无缺省位概念，只按 `provided` 传几个）。
2. **修正后 B 的真实增量 = +130 条/实例化（`M<N`）**，其中 thunk 本体 **+7**（364→371）；其余在 outlined lambda。**修正前第一版是 +198**（thunk 本体 +8，其余是 per-position 泛型 lambda 的重复函数体）。更早报的「+4」则是拿 B 的 TARGET 去比**另一个方法**的 CTRL，口径错。
3. **A 的形态是 builtin thunk 的既成做法**：可选位处理区 **30 条**，且从 1 位到 3 位**都是 30**（逐位线性扫描，不随位数展开）。
4. **class 侧 `M == N` 现在零增量**（对照实测：家族 521 → 521，逐符号相同）。修正前它白付 +18 —— 那是把 `defaults`/`defaults_count` 声明放在 `if constexpr` 之外所致，已移入并整块折叠掉。**builtin 侧不同**：HEAD 的 builtin thunk 对空 `[M, N)` 区间也照样实例化可选位 lambda，B 的 `else` 分支不再引用它们，故 builtin 侧是**净减**（−89 KiB）。
5. **二进制（实测聚合，同位置分别构建）**：A 需 **+1290 B 常驻静态数据**（160 实例，脚本估算）；B 的静态数据 **0 B**，但**代码段有实增量**——class `.text` **+691 KiB**、builtin `.text` **−89 KiB**、两侧合计 **+603 KiB**；dll 端到端 **112,743,424 → 113,382,912 B（+625 KiB）**。**「B 零二进制增长」的说法是错的**，先前只在单实例化抽样下得出。

> ⚠ 以上为 `/Od` dev 构建。lambda 的**拆分/outlining 是 /Od 行为**，`/O2` 下会内联回去，**+130 会显著下降**；聚合的 `+603 KiB` 同理。此处只作**同一构建下的相对量级**，不是发布构建的性能预测。[INFERENCE]

**结论（供决策）**：修正后的 B **命中时不再有「622 条重放」**——缺省值就地落进 thunk 自己的 `Variant argv[i]`，命中路径与 miss 路径同属一条静态腿。因此真正的对比是：

| | 一次性成本 | 每调用成本（`M<N` 实例化） | 命中成本 | 无缺省位方法 |
|---|---|---|---|---|
| **A** | **+1290 B** 常驻静态数据（160 实例） | 可选位扫描（builtin 既成形态 **30** 条） | 就地选槽（≈0） | 无增量 |
| **B**（已实现） | 0 B 静态数据；**代码段两侧合计 +603 KiB**（dll +625 KiB） | +130 条（/Od） | 取记录一次 + 拷贝一个 `Variant`（≈0） | class **Δ 0**；builtin **净减** |

- **B 的优势**：零新增静态数据、零 codegen 改动、class 与 builtin thunk 的缺省值语义统一（都从 `defaults[i - M]` 就地取值）。class 侧无缺省位的方法**完全不付代价**（实测 Δ 0）；builtin 侧不仅不付，还因 `else` 分支不再引用可选位 lambda 而**净减**。
- **B 的代价**：**不是零二进制增长**——1040 个真带缺省位的 class 实例化各约 +680 B（`/Od`），class `.text` +691 KiB；builtin 侧 −89 KiB 抵掉一部分，两侧合计 +603 KiB、dll +625 KiB。`/O2` 下 lambda 内联回去后应显著下降。
- **A 的优势**：codegen 为 class 族发射缺省字面量后，class 与 builtin thunk **形态完全统一**（同一套 `DefVs` 槽 + `default_arg_slot<>()`），无需每实例化的记录取用代码。
- **A 的代价**：**+1290 B** 常驻静态数据（160 实例，脚本估算）+ 需改 codegen；且引用类型（`godot::Array` / `Dictionary`）必须逐处实例化，不能共享。
- **不建议两者叠加**：A 落地后 B 的记录取用分支即永不可达（数据源重复）。
- **当前选择**：B **已实现并全绿**（两条腿 C++ + TS）。相对 A 的取舍是「约 625 KiB 代码段 vs 1.26 KiB 静态数据 + codegen 改动」——`/Od` 下 B 明显更贵，但该差主要由 `-O0` 的 lambda outlining 造成，**`/O2` 下需重新测定再决策**。**需你决定是否值得切 A。**

> ⚠ 所有指令数与体积均为 `/Od` dev 构建、同一位置受控对比（两侧 thunk 头同时回退 HEAD vs 同时为 B，各构建一次）。lambda 的拆分/outlining 是 `/Od` 行为，`/O2` 下 `+130` 与 `+603 KiB` 均会显著下降；A 侧的 `+1290 B` 是数据段、与优化级别无关。**不把这些绝对数当作发布构建的性能预测。** [INFERENCE]


