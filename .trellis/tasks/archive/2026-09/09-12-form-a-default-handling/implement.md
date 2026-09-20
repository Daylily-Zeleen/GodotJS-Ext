# implement.md — 形态A默认值处理改进

## 执行清单（有序，每步一提交；git 一切经用户确认）

1. **基线采集**（不提交，`.agent_tmp/`）
   - 改造前 static gen 快照（`dispatch_class.gen.cpp`/`dispatch_builtin.gen.cpp` 行数 + md5）与形态 A dll md5/大小——供步骤 5 对比报告
2. **String 双端修复**（独立可回滚）
   - `thunks_common.h`：删 `default_as` String 特判（:69-70 分支）+ 注释更正（:62-65）
   - `api_tool_parser.cpp`：删 STRING 特判（:243-252 一带，实施时以实际行号为准；**勿碰 `api_tool_types.h` 用户 WIP**）
   - 重新生成 `.godot/.api_dumping/` api 二进制数据（TS 流程的 api 数据生成步骤，spec `test/index.md`）
   - 验证：
     - 构建 `scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes -j5`
     - static 腿：`new PackedByteArray([0x68,0x69]).get_string_from_multibyte_char() === "hi"`（修复前 `""` + 控制台 ERR）
     - dynamic 腿（api 数据再生成后）：`TabBar.add_tab()` → `get_tab_title(0) === ""`（修复前两引号字符）；`CodeEdit.set_code_region_tags()` 缺参行为正确
     - `.d.ts`：`--generate-types` 基线纪律（spec `test/codegen-baseline.md`）抽查 String 默认字面量渲染无引号失真
3. **class R1**：模板加 M + codegen class 发射
   - `class_methods.h`：两 thunk 加 `int M`（IsStaticC 后）、删 `:67-69/:134-135` D/M 折叠、注释 `:59-64` 更正
   - `static_binding_codegen.py`：`arg_template_expr` 加 `emit_default` 开关（class 传 False）+ class 发射插 M（固定元数 `M=N-default_count`、vararg `M=F`）+ 两条生成期断言（尾部连续 / vararg 前缀 0 默认）
   - 验证：scons 重生成 → `dispatch_class.gen.cpp` Def 字面量全消（grep 抽查 `"null"`/`"-1"` 类默认字面量归零 + 行数对比）；元数边界抽查（M-1 抛错 / M / N / N+1 抛错）若干方法；C++ 双套件 + TS 全量
4. **builtin R2 + 死码清理**
   - `builtin_methods.h:162-178` 槽直取（design §2 代码形态，`default_slot` lambda + fold 改写）
   - `thunks_common.h`：`produce_value` 缺参默认分支（:457-460）与 `marshal_one` 缺参分支删除
   - 验证：`Array.bsearch(x)`、`Vector2.limit_length()`、`Dictionary.get(k)`、`get_string_from_multibyte_char()` 缺参结果正确 + 元数边界；C++ 双套件 + TS 全量
5. **测试补齐**（project/tests，按既有用例模式注册）
   - class 缺参代表：String/StringName/bool/int/float/Color/Variant/Array/Dictionary/Object*（引擎补全路径，双腿）
   - builtin 缺参：标量 + String + Variant（槽直取路径）
   - dynamic 腿同套件复跑（api 数据已再生成）
   - C++ 双套件：`godot --headless --path ./project --jsb-run-tests`（exit 0、无 Orphan、`viaEditorTest` 出现）
   - TS：api 数据生成 + `cd project && node_modules/.bin/tsc --noCheck` + 跑至 `GODOTJS_TEST_PROJECT_COMPLETED`
   - bench：`python misc/bench_matrix.py --rounds 4 --out .agent_tmp/matrix`（invalid=0）
   - 体积对比：class gen 源码收缩 + 形态 A dll 前后（步骤 1 快照）
   - 规划工件提交方式届时问用户或随首步提交

## 风险文件 / 回滚点

- `class_methods.h` / `thunks_common.h`（模板签名，全 gen 依赖——错=编译期全面失败）→ 步骤 3/4 各自单独提交
- `static_binding_codegen.py`：断言先于发射改动落地（断言触发 = 前提失效，立即停下核对 json 而非放宽）
- `api_tool_parser.cpp`（dynamic 全路径共用）→ 步骤 2 单独提交；`api_tool_types.h` 用户 WIP 永不触碰
- 步骤 2/3/4 互相独立可单独回滚；gen 一律经 codegen 重生成

## Definition of Done

执行清单全勾 + prd.md AC 全绿 + 体积对比数字记录 + R5 修订复核（09-11 族工件与实际落地一致）
