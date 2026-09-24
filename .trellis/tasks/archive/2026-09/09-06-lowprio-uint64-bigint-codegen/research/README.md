# 探针使用说明

> 这些脚本是 2026-09-24 调研时的实机探针，用于量出 int64/uint64 与 BigInt 转换的实际行为。
> 它们**不是**项目测试套件的一部分，放在 `research/` 作为证据留存与后续验证的起点。
> 原始日志在 `.agent_tmp/probe_*.log`（未纳入版本控制）。

## 怎么跑

1. 把要跑的脚本与一个场景文件拷进测试项目：
   ```bash
   mkdir -p project/tests/agentprobe
   cp .trellis/tasks/09-06-lowprio-uint64-bigint-codegen/research/probe-uint64.ts project/tests/agentprobe/
   cat > project/tests/agentprobe/Probe.tscn <<'EOF'
   [gd_scene format=3]

   [ext_resource type="Script" path="res://tests/agentprobe/probe-uint64.ts" id="1_probe"]

   [node name="ProbeUint64" type="Node"]
   script = ExtResource("1_probe")
   EOF
   ```
   > 注意：脚本里的类名必须和场景节点名一致（`ProbeUint64` / `ProbeReadback` / …），
   > 否则 `_ready` 不触发。

2. 编译 TS（**不加 `--noCheck`**）：
   ```bash
   cd project && node node_modules/typescript/bin/tsc
   ```

3. 跑：
   ```bash
   godot --audio-driver Dummy --headless --path project res://tests/agentprobe/Probe.tscn
   ```

4. **跑完清干净**（`git status` 必须干净）：
   ```bash
   rm -rf project/tests/agentprobe project/.godot/godotjs_ext/tests/agentprobe
   ```

## 脚本清单

| 脚本 | 类名 | 覆盖 |
|---|---|---|
| `probe-uint64.ts` | `ProbeUint64` | 静态/动态 uint64 写读、字节级精确性、ObjectID 往返 |
| `probe-readback.ts` | `ProbeReadback` | 同一引擎值经两条返回路径的对照 |
| `probe-id.ts` | `ProbeId` | 以 `to_string()` 的 `itos` 输出为无损基准，验证 ObjectID 精确往返 |
| `probe-idnum.ts` | `ProbeIdNum` | 只用 number（不用 BigInt）的 ObjectID 往返 |
| `probe-num.ts` | `ProbeNum` | 纯 number 输入到 uint64/int64 槽的静态 vs 动态对照 |
| `probe-prop.ts` | `ProbeProp` | 属性赋值 vs 方法调用的分歧 |
| `probe-gaps.ts` | `ProbeGaps` | int64 负值返回、BigInt 构造器、`rng.state` Number 写入 |
| `probe-ops.ts` | `ProbeOps` | BigInt 用于内置构造器与运算符（`OP_MULTIPLY` / `new Vector2i` / `new Vector2`） |
| `probe-boolnil2.ts` | `ProbeBoolNil2` | 有默认值的 bool 参数：验证 `undefined` 走默认值替换而非转布尔 |
| `probe-boolplain.ts` | `ProbeBoolPlain` | **无默认值**的 bool 参数（`Projection.create_depth_correction`）——隔离出真实转换行为 |
| `probe-blocksig.ts` | `ProbeBlockSig` | `Object.set_block_signals(enable)`（无默认值，class 方法）——bool 槽转换的推荐探针目标 |
| `probe-boolfinal.ts` | `ProbeBoolFinal` | **判别性**对照组：`String.strip_edges(s, left=true)` 证明 `undefined` 走默认值替换而非真值转换 |

## 已知坑（调研时踩过）

- `Object` 上直接调 `String(obj)` 拿不到 `#id`（走的是 `toString` 而非 Godot 的
  `to_string`）；`probe-id.ts` 用 `(o as { to_string(): string }).to_string()` 绕开。
- `PackedByteArray` 不能直接 `Array.from()`，也不能被 TS 当成 `ArrayLike`；
  用 `.size()` + `.get(i)` 逐字节读。
- `StreamPeerBuffer.get_data(n)` 的 typings 声明返回 `GArray` 而不是 `PackedByteArray`
  （`project/typings/godot7.gen.d.ts:2049`）；读字节用 `data_array` 属性更省事。
- `RandomNumberGenerator` 没有暴露 `set_state` / `get_state` 方法（只暴露 `state` 属性，
  因为 `JSB_EXCLUDE_GETSET_METHODS=1`），所以「方法 vs 属性」的对照要用
  `StreamPeerBuffer.put_u64`（方法）对比 `rng.state =`（属性）。
- 探针脚本被删掉后 `.godot/godotjs_ext/tests/` 下的编译产物要一起删，
  否则场景会引用到不存在的脚本并挂死（不是失败，是永不退出）。
- 场景文件名（`Probe.tscn`）与脚本名无关，但**节点名必须等于脚本的 default export 类名**。
- 场景文件的 `ext_resource` 若指向已删除的脚本，引擎会解析失败并**挂死**（rc=124），
  不是立刻报错。删脚本时务必同时删场景或改回有效路径。
- `node_modules/typescript/bin/tsc` 在 `project/` 下跑；探针放到 `project/tests/agentprobe/`
  时若引用 `godot` 之外的东西（如 `../test-status`）要注意相对路径。
- **测 bool 参数必须选「无默认值」的位置**。有默认值时传 `undefined` 会被替换成默认值。
  可用的**无默认值** bool 参数：`Object.set_block_signals(enable)`（class 方法，推荐；
  `Object` 必然已绑定，可用 `is_blocking_signals()` 读回验证真值映射）。
- **要证明「默认值替换」必须用默认值为 `true` 的 bool 参数**：默认值 `false` 与
  `undefined` 的真值相同，两条假设给出相同结果，无判别力。
  实测（`probe-boolfinal.ts`）：`String.strip_edges("  x", undefined)` → `"x"`，
  即走了默认值 `true`；若走真值转换会得 `"  x"`。**这是「默认值优先」的直接证据。**
  反例（**不可用于判别**）：`Rect2.intersects(b, include_borders=false)` —— 实测
  `intersects(touching, undefined)` 得 `false`，与传 `false` 相同，无法区分两种假设。
- **只有普通 class 受 `build_profile.json` 裁剪，内置类型（builtin）不受影响。**
  `enabled_classes` 只列普通 class（`Object` / `Node` / `Resource` / … 共 65 个），
  `AStarGrid2D` 不在其中 → 运行时拿不到，**连 `set_jumping_enabled(true)` 都 THREW**
  （实测，见 `probe-boolplain.ts` 日志），会被误读成「转换被拒」。
  内置类型（`String` / `Rect2` / `Vector2` / `Projection` …）**不受该 profile 裁剪**：
  实测 `Projection.create_depth_correction(true)` OK、`String.strip_edges("  x")` OK。
  选探针目标时：内置类型随便用；**普通 class 必须先确认在 `enabled_classes` 里**。
- 探针类名必须与场景节点名一致；场景引用已删除的脚本会导致引擎挂死（rc=124）。
