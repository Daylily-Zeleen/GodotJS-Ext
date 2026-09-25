# 实施清单（C：开关收口）

> 共享技术方案见父任务 `../09-06-lowprio-uint64-bigint-codegen/design.md` §3.3。
> 本文件只列执行顺序、验证命令与回滚点。

## 前置

**必须等 A（`09-24-uint64-bigint-return`）与 B（`09-24-uint64-bigint-arg`）都完成。**
本任务只把已有行为收进开关，不实现转换逻辑本身；A/B 未落地时本任务无法完成
AC3.1 / AC3.2，**不要在这种情况下声称完成**。

与 D（`09-24-uint64-bigint-ctor-operators`）**无依赖**：D 改的是入口侧数值槽，
本开关只管出口表示，两者互不影响，先后随意。

## 顺序

| 步 | 文件 | 内容 |
|---|---|---|
| 1 | 全仓库 grep `JSB_WITH_BIGINT` | 列出全部引用点，判断哪些该归新宏、哪些留在原宏 |
| 2 | `src/jsb.config.h` | 新增独立宏（建议名 `JSB_BIGINT_FOR_64BIT`），写明与 `JSB_WITH_BIGINT` 的区别与交互 |
| 3 | A 落地的出口路径（`jsb_primitive_conv.h` 的 `new_integer` / `new_unsigned_integer`） | 按新宏门控 BigInt 出口；关掉时退化为 `Number`（接受丢位） |
| 4 | 配置校验 | 非法组合（如新宏开而 `JSB_WITH_BIGINT` 关）给编译期断言或配置期拒绝 |
| 5 | `project/tests/int64/` | TS 场景：断言两种模式下的差异行为（AC3.7） |
| 6 | `project/tests/start.ts` | 登记新场景（若 A/B 已登记则跳过） |

### 交点（与其他子任务）

- `src/runtime/impl/jsb_primitive_conv.h`：**由 A 创建**，本任务在步 3 给它门控
  （`new_integer` / `new_unsigned_integer` 的 BigInt 分支）。A 已落地后再改，**串行**。
- `src/jsb.config.h`：本任务独占。
- `project/tests/int64/` 与 `start.ts`：A/B/D 也会往这里加场景/登记；
  登记 `scenes` 列表时要**追加**而不是覆盖（若 A/B 已登记，只追加本任务场景）。

### 注意

- **入口不受新宏影响**：传 number / BigInt 在两种模式下都不抛异常（由 B 保证）。
  不允许出现「关掉开关后传参又开始抛异常」的组合。
- 默认值需与现状兼容（当前行为 = 开）。
- `JSB_MAX_SAFE_INTEGER` 的值不动。
- `jsb.config.h` 是全局头，改一次全量重编；两种模式各编一次，**不要为措辞反复编译**。

## 验证

```bash
# 模式开（默认）
scons platform=windows target=editor binding_mode=shared compiledb=no debug_symbols=no dev_build=no verbose=no
godot --audio-driver Dummy --headless --path project

# 模式关
#   （改 jsb.config.h 后重编）
godot --audio-driver Dummy --headless --path project
```

**期望值**：

| 断言 | 开关开 | 开关关 |
|---|---|---|
| `instance_from_id(get_instance_id()) === obj`（RefCounted） | `true` | 允许 `false`（与今天同） |
| 超出 2^53-1 的返回值 `typeof` | `bigint` | `number`（接受丢位） |
| `put_u64(2^63)` 抛异常 | 否 | **否**（不得抛） |
| 静态/动态写入字节相同 | 是 | 是 |
| `2^53-1` 及以下的 `typeof` | `number` | `number` |
| 全量 TS 集成测试 | exit 0 + COMPLETED | exit 0 + COMPLETED |

## 回滚点

`jsb.config.h` + 出口门控，整体可回滚（回到「总是 BigInt」的默认行为）。

## 完成前检查

- [ ] 两种模式各编一次、各跑一次全量集成测试
- [ ] `src/jsb.config.h` 注释能让下一个人不查代码就答出「两个宏分别管什么」
- [ ] 合法组合都编过；非法组合被拒绝（负向验证）
- [ ] TS 场景能在**两种模式**下都通过（AC3.7）
- [ ] AC3.1 - AC3.7 逐条勾选
