# 64 位整数接口清单（Godot 4.7.2.stable.official）

> 数据源：`project/extension_api.json`（`--dump-extension-api-with-docs` 产物）。
> 生成脚本：从 api json 直接统计，非人工摘录。

## 1. 总量

| 位置 | int64 | uint64 | 合计 |
|---|---:|---:|---:|
| 类方法**返回值** | 132 | 115 | 247 |
| 类方法**参数** | 366 | 75 | 441 |

## 2. 关键事实：64 位 meta 只出现在 **class methods**

| 位置 | 64 位 meta 数量 |
|---|---:|
| `classes[].methods[].return_value` | 247 |
| `classes[].methods[].arguments[]` | 441 |
| `classes[].properties[]` | 0 |
| `classes[].signals[].arguments[]` | 0 |
| `builtin_classes[].methods[]`（返回 + 参数） | 0 |
| `builtin_classes[].constructors[].arguments[]` | 0 |
| `builtin_classes[].members[]` | 0 |
| `utility_functions[]`（返回 + 参数） | 0 |
| `singletons[]` | 0 |

属性（`properties[]`）**自身不声明 meta**；它的 64 位语义来自其 `getter`/`setter` 方法，
因此上面两栏为 0 不代表属性不涉及 64 位。

### 对实现的意义

- 需要 meta 才能区分 `int64` / `uint64` 的路径**只有** class method 的返回与参数。
- `builtin` 构造器 / 运算符、`utility`、setter 路径**拿不到 64 位 meta**，只能按
  `Variant::INT` 处理 —— 这正是 `probe_vt` 把 BigInt 归入 `Variant::INT` 的原因。
- 生成绑定表实测：`Ret<uint64_t>` 11 个签名、`Args<uint64_t>` 6 个签名。

## 3. 完整逐条清单

见同目录 `64bit-api-surface-full.md`（不注入上下文，按需查阅）：
- 返回 64 位的类方法 247 条（uint64 115 / int64 132）
- 含 64 位参数的类方法 441 个参数位（uint64 75 / int64 366）

## 4. 与本任务的决策关系

- **typings**：别名只影响 class methods 的返回与参数位置；`builtin`/`utility` 侧不存在 64 位 meta，
  因此别名的实际作用面就是这 688 个位置。
- **出口三态**（`JSB_64BIT_RETURN_FIXED_BIGINT`）：影响上表 247 个返回值。
  其中最热的是 `Object.get_instance_id`、`Time.get_ticks_msec/usec`、`Engine.get_process_frames`、
  `FileAccess.get_position` —— "恒 bigint"的分配代价主要落在这几个上。
- **参数接受面**：75 + 366 个参数位在运行时本就同时接受 `number` 与 `bigint`（`to_int64`/`to_uint64`），
  所以"入参别名恒为 `number | bigint`"不需要改运行时，只改声明。
