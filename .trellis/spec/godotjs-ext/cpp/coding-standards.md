# C++ 编码规范

## 头文件禁用 `using namespace`

**绝对不要**在头文件（`.h` / `.hpp`）中使用 `using namespace` 声明（函数内等局部作用域除外）。

```cpp
// ❌ header.h — 污染所有包含它的翻译单元
#include <string>
using namespace std;

// ✅ 显式使用 std:: 前缀
#include <string>
class MyClass { std::string name; };
```

原因：头文件被多个源文件包含，`using namespace` 会污染所有引用它的翻译单元，导致命名冲突与不可预期的编译行为。

## 临时文件位置

所有临时产物（测试脚本、一次性 `.cpp`/`.h`、调试 dump、实验代码、诊断日志）**统一放 `./.agent_tmp/`**：

1. 禁止在项目根、`src/`、`project/`、`scripts/` 等业务目录散落临时文件
2. 目录不存在时先创建 `mkdir -p .agent_tmp`
3. 任务结束按需清理

## 格式化

- 遵循仓库根 `.clang-format`；批量格式化用 `python misc/format_src.py`
- `misc/format_src.py` 自动跳过 `*.gen.*`、`*.def.*` 与第三方目录，不要手动绕过

## 第三方源码

`third/` 下的库（godot-cpp、quickjs、quickjs-ng、v8、lws、doctest）原则上**只读**；如需适配，在 `src/` 侧（runtime / compat / bridge）写兼容层，不直接改第三方源码。例外须用户明确确认。
