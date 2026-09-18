# 任务报告：optimize-api-tool-methodinfo-lazy

## 阶段

**planning**（未 `task.py start`，未改任何产品代码）。等待用户批准最终规划摘要。

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

批准最终规划摘要后执行 `task.py start`，进入 Step 0（全量构建 + 基线固化）。
