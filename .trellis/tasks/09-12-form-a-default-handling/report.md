# 09-12-form-a-default-handling 验收交付报告（2026-09-13）
> 全部验证基于官方稳定版 headless 宿主；证据日志在 `.agent_tmp/form-a-baseline/`。
> 状态：**全部验收标准达成，等待人工检查，未提交**。
> 独立质量审查（trellis-check 子代理，只读）结论：**deliverable-as-is**，零 blocker、零 should-fix。

## 一、验收标准逐条核对（PRD → 证据）

### 1. class gen 1,715 处 Def 字面量全消 + 元数检查逐例等价 ✅

- Def 位点 `1,715 → 0`（`dispatch_class.gen.cpp` 正则 `Arg<[^>]*, "` 计 0；before.txt 记录基线 1,040 方法/1,715 位点）
- gen md5：class `ed26d896 → 8f0689d6`（builtin `417bf6a3` / utility `148a4e31` 不变——非 class 族发射面零变化）
- 发射形态：`class_method_thunk<1465444425u, "TabBar", "add_tab", false, 0, RetVoid, Arg<godot::String>, Arg<godot::Object*>>`（M=0 无字面量）；全 15 个 vararg 发射 M=F（OQ4）
- 元数三档：`GArray.bsearch`（M=1/N=2）与 `Curve2D.add_point`（M=1/N=4）各 M-1 抛 / M 过 / N 过 / N+1 抛——双腿 TS 全量内 DefaultArgs `fail=false`
- 生成期断言 `_assert_default_layout`（尾部连续 + vararg 前缀零默认）随每次构建生效（build 日志 `OK: static binding tables generated`）
- FormACheck 独立复核：M 等价性严格成立（Python `default_value` 键存在性 ≡ 旧 C++ `Def.length>0`，稳定版数据 0 个真空串默认，无分歧位点）
### 2. builtin 缺参零逐调用转换 + multibyte "hi" 无 ERR ✅

- `default_arg_slot<I>` magic static 槽（`builtin_methods.h:41-59`）：首调 `default_as → PtrToArg::encode` 一次性预编码，缺参位 `arg_ptrs[I]` 直取槽指针（:122-125）；懒初始化满足"DLL 静态初始化期无 str_to_var"约束
- `PackedByteArray([0x68,0x69]).get_string_from_multibyte_char() === "hi"` 双腿通过；两腿 TS 日志 grep `encoding` = 0（修复前为 os_windows `ERR_FAIL_NULL_V_MSG` + 空串返回）
- `marshal_one` 缺参分支与 `produce_value` 默认分支已删；`default_as` 唯一残留调用方 = 槽初始化器（FormACheck grep 复核）

### 3. dynamic 腿 String 修复可观察 + .d.ts 渲染 ✅

- `api_tool_parser.cpp:243-250` STRING 特判删除，统一 `str_to_var`；与 `thunks_common.h:68-75` 对称（双端零特判残留）
- dynamic 腿（dll `b82734d1`，api store 以 R4 parser 重建）：`TabBar.add_tab()` → `get_tab_title(0)===""`、`CodeEdit.set_code_region_tags()` → 读回 `"region"/"endregion"`——含在 DefaultArgs 全绿内（`ts-dynamic-final2.log`）
- `.d.ts` 零渲染代码改动自动受益：`title?: string /* = 'Alert!' */`（基线 `'"Alert!"'`）、`locale /* = '' */`（基线 `'""'`）——`verify-diff-post2.log` 归因 46 行 String 默认引号修复
- 基线收尾：21 处 diff 全归因（46 String 默认 + 3 default-args 新测试 gen + 5 benchmark 基线缺 + godot11 缺失（dev 引擎伪影）+ 文档漂移）→ `--update-baseline` 快照自校验过 → 复跑全流程 `✅ 校验通过`（0 diff）→ static 恢复后再验一次仍绿（`verify-static-final.log`）

### 4. R3 缺参回归测试双腿落地 ✅

`project/tests/default-args/test-default-args.ts`（196 行，哨兵模式全走 `reportTestFailure`）+ `DefaultArgs.tscn` + `start.ts` 注册（scenes 7→8）：

- builtin 槽路径：float（`limit_length()`→(1,0)）、bool（`bsearch(2)`→1）、Variant（`Dictionary.get('missing')`→null）、String（multibyte→"hi"）
- class 代表类型：String（`add_tab`/`get_csv_line` delim ","/`find_children`）、StringName（`tr`）、int（`add_point` 第二点落 index 1 = 追加语义）、float（`AStar2D.add_point` weight_scale=1.0）、bool/Vector2（`add_point` in/out=(0,0)）、Variant（`get_meta`→null）、Object*（`get_tab_icon(0)`→null）
- 豁免（文档化）：Color/Array/Dictionary 无廉价读回（void sink），由 .d.ts 渲染 + 基线 diff 覆盖
- 元数边界 M-1/M/N/N+1 双方法抽查 + 交叉类型构造回归守卫（见"途中事件 2"）

### 5. 行为基线双腿全绿 + 体积记录 ✅

| 腿 | dll（md5，双处部署一致） | TS 集成 | C++ 双套件 | bench |
|---|---|---|---|---|
| static 终版 | `55bfb6c6`（112,736,256 B） | COMPLETED / 0 FAILED / DefaultArgs fail=false（`ts-static-restore.log`） | 38 用例 540 断言 + editor 3 用例 12 断言全 SUCCESS / viaEditorTest / 0 Orphan | staticBinding:true / invalid:0 / 0 回退警告 |
| dynamic 终版 | `b82734d1` | COMPLETED / 0 FAILED（`ts-dynamic-final2.log`） | 540+12 断言 SUCCESS（`cpp-tests-dynamic-final.log`） | staticBinding:false / invalid:0 / Constructors 组含 Vector2(Vector2i) 通过（`bench-dynamic-final.log`） |

editor dll `05d53e77` 双处一致；release dll `3f3408ce` 不变（未重编）。

### 6. R5 工件修订复核 ✅

父 PRD（09-11）`:28/:85/:95/:103/:112` 形态 A 条款全部改写指向本任务；三子任务 PRD 迁移源/基线注记、class implement.md 基线闸门（"static diff 为空"基线 = 本任务落地后重采）均已到位；FormACheck 逐文件复核无矛盾。

## 二、体积报告

| 产物 | 任务前 | 任务后 | Δ | 归因 |
|---|---|---|---|---|
| dispatch_class.gen.cpp | 3,027,695 B（md5 ed26d896） | 3,058,576 B（8f0689d6） | **+30,881 B（+1.0%）** | M 模板参数 ×15,385 方法 + 注释展开 > 1,715 处 Def 字面量剥离收益；行数 30,279 不变 |
| dispatch_builtin / utility gen | 417bf6a3 / 148a4e31 | 不变 | 0 | 发射面未动 |
| manifest unique_default_values | 200 | **16** | −184 | class Def 全消（125 对）+ 计数面对齐发射面（跳过 String/StringName 接收者 59 对） |
| 主 dll（dev editor，R4 后 → R1R2 后） | 111,099,904 B（03:13 实测） | 110,825,984 B（04:08，999e1af2） | **−273,920 B（−0.25%）** | 本任务干净归因窗口：R1 死码消除（113 个 class-only `default_as` 实例 + Def 字面量出 .rdata）+ R2 后死码删除 |
| 主 dll（终版 55bfb6c6） | — | 112,736,256 B | — | 与 R1R2 版差 +1.9 MB **不可归因本任务**：混入用户三个 refactor commit（c144135/ec2e7ba/c041f81）+ 腿切换全量重链 |

PRD 预期"class gen 源码收缩"与实测相反（+30,881 B）——如实修正：**真实瘦身在 dll（−273,920 B），gen 源文件因 M 参数净增**。before 侧 dll 字节数存在记录缺口（before.txt 只存 md5；111,099,904 取自 03:13 `~` 副本 stat）。

## 三、改动文件清单（未提交工作树，15 文件 +205/−149）

**任务主体**：
- `src/static_binding/thunks/class_methods.h` — R1（M 模板参数、注释重写；与用户已提交的 resolve 重构叠加）
- `src/static_binding/thunks/builtin_methods.h` — R2（default_arg_slot + fold 守卫）
- `src/static_binding/thunks/thunks_common.h` — R4（default_as 统一 str_to_var）+ 死码（produce_value 默认分支）
- `src/api_tool/editor/api_tool_parser.cpp` — R4 dynamic 腿
- `misc/build/static_binding_codegen.py` — R1 发射（class_entry_expr 模板串 + M）+ `_assert_default_layout` + manifest uniq_defaults 口径
- `project/tests/default-args/`（新）+ `project/tests/start.ts` — R3

**越界小修（请单独裁决）**：
- `src/runtime/bridge/jsb_static_binding_util.h` — 删过严 dev 断言（见"途中事件 2"；pre-existing bug，非本任务 diff）
- `src/runtime/bridge/jsb_primitive_bindings.cpp` — 1 行错误消息更正（`Realm::` → `Variant::can_convert_strict`，指名实际函数）

**spec 沉淀（trellis-update-spec）**：
- `build/scons-build.md` — ① `--bench` 命令文档纠错（实码 `get_cmdline_user_args()`，所有开关都在 `--` 后；旧文写反）② 并行编辑竞态归因规程（git log + mtime 先于回滚）
- `cpp/architecture-constraints.md` — jsb_check dev 门控断言陷阱（release 全绿 ≠ dev 安全；断言契约须与被检函数语义一致）

**R5 工件**：09-11 父 PRD + 三子任务 PRD/implement.md 注记修订。

## 四、途中事件（已全部定性与闭环）

1. **用户并行重构竞态**：06:04–07:19 用户提交 c144135（删重复 get_opaque_typed）、ec2e7ba（resolve 步骤重构，8 文件）、c041f81（MethodBind 回局部静态）。07:38 的 static rebuild 曾撞上 mid-edit 半改状态（`HashC undeclared`/`NameLit not found`）——属编辑窗口期编译，非损坏；用户提交后同源码重编即绿。本任务未提交 diff 全程未被触碰（FormACheck 双重核对）。
2. **dynamic 腿 dev 构建 bench 首跑崩**（pre-existing，越界修复）：`jsb_static_binding_util.h:39` dev 断言把 hinted `js_to_gd_var` 的宽松回退当精确类型契约，`new Vector2(Vector2i)`（reflect ctor 交叉尝试链）在 dev 构建必崩；release 断言编译期移除故历史 bench（全 release 形态）从未暴露。证据链：复现（`bench-dyn-only-ctors.log` EXIT=3）→ 定位（jsb_type_convert.cpp FALLBACK 分支原样返回 wrapper 类型，赋值时才 `Variant::operator T()` 转换）→ 断代（`git log -S` = 9d17eaa 2026-08-21，先于全部静态绑定任务）→ 修复（删断言 + 3 行契约注释）→ 双腿回归守卫（cross-ctor 测试）+ bench Constructors 组全过（双腿）。教训已沉淀 spec。
3. **`~` 副本 md5 疑点闭环**：`~` 副本 ≠ 源 md5 是设计行为——引擎 editor 模式拷贝后经 `copy_and_rename_pdb` 修补 DLL 内嵌 PDB 路径（实测源/副本仅差 78 字节、集中尾部 PDB 区，且副本 md5 两次读取稳定）；非 editor 运行直接加载源 dll，任务期间全部 TS/C++/bench 验证不受影响。

## 五、非回归噪音（如实列出）

- TS 腿 static `3 resources still in use at exit`（warrior/mage/player-resource.ts）+ 8 个 Orphan StringName：任务前日志同款逐字存在（`cpp_tests_editor.log:81`），属性 getter 方法名字面量，`static: 0` 全程；dynamic 腿 TS 与两腿 C++ 套件 0 Orphan。
- `ERROR: The object does not have any 'meta' values with the key 'missing'`：`get_meta` 测试用例的引擎预期报错（断言的是返回 null），非 multibyte 编码错误（后者已消，grep=0）。
- 构建日志 `WARNING: Unknown SCons variables ... tests=yes` 为表面噪音——doctest 双套件（540+12 断言）确实从该 dll 跑通。
- verify 链 `--dump/--generate-types` 退出码 3221225477：spec 明文的已知关机崩溃，按产物存在性判（全流程产物完整 + 二次复验 `✅ 校验通过`）。

## 六、遗留与建议（不在本任务内，供后续任务规划）

- **F2（建议独立任务）**：`builtin_methods.h:205-207` vararg nulling 循环（pre-existing，eb3d748 2026-08-25）在 static 腿挂载的 builtin vararg 方法（`Callable.call/bind/rpc_id`）带 ≥1 参调用时会把全部 arg_ptrs 置 null 后传入 `fn`——引擎 `variant_call.cpp:561-577` 逐参解引用，即休眠崩溃。当前测试/bench 无此调用面（dormant）。已由 FormACheck 对照引擎源码核实。
- F1：`default_arg_slot` 注释"per (method, position)"实际粒度是 per (签名, position)——现数据零可观察差异（OQ3 仅 Variant "null" 共享且 NIL 不可变），注释措辞留待后续任务顺带修正。
- F3：`_assert_default_layout` 可扩覆盖 utility_funcs（今日 utility 默认 = 0，纯对称性）。

## 七、验证命令速查（复核用）

- TS：`godot --headless --path ./project --verbose` → 判 `GODOTJS_TEST_PROJECT_COMPLETED` / 无 FAILED / DefaultArgs fail=false
- C++：同 + `--jsb-run-tests` → exit 0、`viaEditorTest`、双 doctest SUCCESS
- bench：`--audio-driver Dummy --headless --path ./project -- --bench` → `invalid:0`
- verify：`python misc/verify_codegen.py --godot <exe>` → `校验通过`
