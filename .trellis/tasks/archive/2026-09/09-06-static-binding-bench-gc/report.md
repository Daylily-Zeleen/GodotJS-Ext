# 双腿基准最终报告（static vs dynamic，GC 隔离采数）

> 任务 `09-06-static-binding-bench-gc` 产出。数据：2026-09-06，引擎 `D:/Dev/godot/godot/bin/godot.windows.editor.x86_64.exe`（4.8.dev 定制），release flavor，每数据点 = 3 轮中位数，107 cases 全通过（invalid=0、exit=0）。

## 1. 采数身份（dll 以 md5 + 构建命令为准）

| 数据文件（`.agent_tmp/`） | dll md5 | 构建命令 | 说明 |
|---|---|---|---|
| `final_sta_gc_{1,2,3}.log` | `ce7884d4` | `scons ... ` 默认（static_binding=yes） | static 腿 + `--gc` |
| `final_sta_nogc_{1,2,3}.log` | `ce7884d4` | 同上 | static 腿无 `--gc`（污染量化 A） |
| `final_dyn_gc_{1,2,3}.log` | `5f605c8c` | `scons ... static_binding=no` | dynamic 腿 + `--gc` |
| `final_dyn_nogc{,_2,_3}.log` | `5f605c8c` | 同上 | dynamic 腿无 `--gc`（污染量化 B） |

- 两份 gdextension（`godotjs-ext.gdextension` + `godotjs-ext-editor.gdextension`）指向的 dll 替换后 md5 均已核对一致。
- 报告中 `staticBinding` benchmark JSON 字段不可信（dynamic 路径也注册 `IN`），dll 身份只认上表。
- 历史数据 `opsfull_final.log`、`ops_dyn_leg.log` 均为 dynamic dll 所跑，作废旧结论见 §4。

## 2. 双腿对比（`--gc` 下，static/dynamic 三轮中位数比）

**总体**：107 cases，static/dynamic 中位 **0.91x**（P10 0.37x，P90 1.02x）。**没有任何 case static 慢于 dynamic 15% 以上**（>1.15x 计 0 个）；41 个 case static 快 ≥13%（<0.87x）。

| 分组 | static 中位比 | 说明 |
|---|---|---|
| Operators（23 case） | **0.97x** | A2 表函数直连后与 dynamic 持平略快 |
| Node 等对象方法（12 case） | 1.01x | 持平 |
| Vector 标量取值类（18 case） | **0.86x** | static 直挂 thunk 免 Variant 装箱 |

关键 case（ns/call，小者快）：

| case | static | dynamic | 比 |
|---|---|---|---|
| 比较运算符（Basis/Vector2 EQUAL、Vector2i LESS） | 95–104 | 115–132 | static 快 ~20% |
| Operators.Vector2.ADD(Vector2) | 513.3 | 546.5 | 0.94x |
| Operators.Projection.MULTIPLY(Projection) | 619.1 | 694.4 | 0.89x |
| Node.get_child_count(0) | 72.6–76.5 | 87.2 | 0.83x |
| Engine.get_frames_drawn(0) | 90.9 | 107.9 | 0.84x |
| Array.size(0) / Dictionary.size(0) | 30.2 / 30.8 | 154.4 / 159.4 | **0.20x** |
| Vector2.angle(0) / Quaternion.length(0) | 44.7 / 29.6 | 99.3 / 85.2 | 0.45x / 0.35x |
| Constructors.new Vector2(x, y) | 423.3 | 424.9 | 1.00x |
| ResourceLoader.exists(1)（磁盘 IO 主导） | 37778.9 | 37458.6 | 1.01x |

## 3. GC 污染量化（同 dll A/B，no-gc/gc > 1 表示无 `--gc` 更慢即被污染）

| 腿 | 中位 | P90 | 最大 |
|---|---|---|---|
| static（`ce7884d4`） | 1.05x | 1.13x | 1.22x（Signal.is_null） |
| dynamic（`5f605c8c`） | 1.03x | 1.06x | 1.10x |

- 验收对照：Node 组全量 `--gc` 76.5ns vs 同 dll 单组（`--only=Node`）75.3ns，偏差 1.6%（≪±20% 达标）；旧污染值 385ns 不再复现。
- **最终采数规程以 `--gc` 为准**（最坏情况保护），无 `--gc` 差异中位仅 ~5%。
- 旧 5x 污染（单组 80ns vs 全量 385ns）系旧版 static 构建的阳性对照观测；A1/A2 定形后同 dll A/B 已不复现，属历史现象，归因不再追述。

## 4. 结论修正（针对归档 PRD `09-06-bench-operators-expansion`）

旧结论「dynamic 全面快 ~2x / static 全面慢 4-5x → 支持维持运算符不恢复 ptrcall thunk」**作废**，两处缺陷：
1. **dll 身份错标**：旧两份数据文件均由 dynamic dll 跑出，从未真正对比过两腿；
2. **GC 污染 + 调用层查表**：旧 static 构建每调用二分查 497 条全局表（A1/A2 已去查表化），且全量无 `--gc` 下存在污染。

修正后结论：运算符经 (left,op) 局部 switch 表分发后，static 腿**不慢于** dynamic（Operators 组 0.97x，比较类反快 ~20%）；标量取值类 static 快 2–5x（免装箱）。运算符 ptrcall thunk 恢复与否的裁决请引用本报告数据，并到 `static-ctor-thunks` 任务复核。
