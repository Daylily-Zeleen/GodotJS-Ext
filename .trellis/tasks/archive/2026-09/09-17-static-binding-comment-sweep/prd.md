# 静态绑定代码注释审查：清理过时内容与缩短冗长描述

## Goal

完整审查静态绑定相关代码的注释，删除或修正**与代码不符**的，缩短**冗长**的。改后必须跑测，确保仅注释变更、无行为改动。

## 范围（已定位的确切文件清单）

### A. 手写 C++（可直接编辑）

| 文件 | 说明 |
|---|---|
| `src/static_binding/dispatch.h` | 静态绑定总入口 |
| `src/static_binding/thunks/thunks_common.h` | thunk 公共工具 |
| `src/static_binding/thunks/builtin_constructors.h` | builtin 构造 thunk |
| `src/static_binding/thunks/builtin_members.h` | builtin 成员 thunk |
| `src/static_binding/thunks/builtin_methods.h` | builtin 方法 thunk |
| `src/static_binding/thunks/builtin_operators.h` | builtin 运算符 thunk |
| `src/static_binding/thunks/class_methods.h` | class 方法 thunk |
| `src/static_binding/thunks/class_indexed_properties.h` | class 索引属性 thunk |
| `src/static_binding/thunks/type_compatible.h` | 类型兼容性检查 |
| `src/static_binding/thunks/utility_functions.h` | utility 函数 thunk |
| `src/runtime/bridge/jsb_primitive_bindings.cpp` / `.h` | 原始类型绑定 |
| `src/runtime/bridge/jsb_static_binding_util.h` | 静态绑定工具 |

### B. 代码生成器（Python）

| 文件 | 说明 |
|---|---|
| `misc/build/static_binding_codegen.py` | 主生成器（79 KB，**注释量最大**） |
| `misc/build/generate_primitive_operators.py` | 运算符表生成 |
| `misc/verify_codegen.py` | 生成结果校验 |

### C. 基准测试入口（Python）

| 文件 | 说明 |
|---|---|
| `misc/bench_matrix.py` | 基准矩阵驱动 |

### D. 基准测试（TypeScript）

| 文件 | 说明 |
|---|---|
| `project/tests/benchmark/benchmark.ts` | 基准入口 |
| `project/tests/benchmark/cases.builtin.ts` | builtin 用例（33 KB） |
| `project/tests/benchmark/cases.object.ts` | object 用例 |

### 明确不在范围内

- **`*.gen.*` 文件**：`src/static_binding/gen/*.gen.*`、`project/gen/**`。生成物禁止直接编辑——若其中注释有误，改**生成器**（B 组）里的模板字符串，再重新生成。
- `third/` 下的第三方代码。

## 审查标准

对每条注释，判断它属于哪类并相应处理：

| 类别 | 处理 |
|---|---|
| **过时（与代码不符）** | 修正为与当前实现一致；若注释描述的机制已不存在，删除 |
| **冗长（超过必要）** | 压缩到关键信息；多行叙述能一行说清就一行 |
| **重复代码本身** | 删除（如 `// increment counter` 对着 `counter++`） |
| **有价值（解释 why / 陷阱 / 不变量）** | 保留，可缩短但不删语义 |

**保留优先于删除**——本次目标是准确与简洁，不是减少行数。

## 已知的过时注释样例（写作时作为参照，实现时需逐条核实）

- `jsb_primitive_bindings.cpp` 中「已废弃的 Variant finalizer」注释曾标 `NOTE`，后改为 `/** @deprecated */`——检查其他同类表述是否统一
- `jsb_static_binding_util.h` 内的检查宏注释需与宏实际行为核对

## 约束

- **仅改注释**。任何可执行代码（含宏定义、模板参数、字符串字面量）不得改动。
- 生成物 `*.gen.*` 不得手改。
- 改后必须跑测验证（见下）。

## Acceptance Criteria

- [ ] 上列 A–D 组全部文件已逐一审查（附审查记录：哪些文件改了、改了哪类）
- [ ] `git diff` 中**不存在**任何非注释的代码改动（可用 `git diff -U0` 逐 hunk 核对）
- [ ] 注释与代码一致性抽检通过（抽查改动过的注释，对照代码确认描述准确）
- [ ] **跑测通过**，证明无行为改动：
  - [ ] 编译通过（规范命令，`scons target=editor compiledb=yes debug_symbols=yes dev_build=yes verbose=yes -j6`）
  - [ ] `--jsb-run-tests` → `PASS - all checks passed`
  - [ ] 基准测试可运行（`misc/bench_matrix.py` 至少跑通一轮，确认入口未坏）
- [ ] 若改动了生成器（B 组），重新生成后 `*.gen.*` **产物无差异**（证明只改了注释）

## Notes

- 相关 spec：`.trellis/spec/godotjs-ext/build/`（构建）、`.trellis/spec/godotjs-ext/cpp/`（编码规范）
- **成本项**：改 `.h` 文件（A 组多数是头文件）会让所有包含者重编，单次 100~190s。因此**编译验证只做一次**，在所有注释改动完成后；不要边改边编，也不要为措辞反复重编
- 生成器改动后需跑 `misc/verify_codegen.py` 确认产物一致
