---
description: "Use when: C/C++ 编码规范。禁止头文件中使用命名空间声明，所有临时文件统一放到 .agent_tmp 目录。"
applyTo: "**/*.h","**/*.hpp","**/*.cpp","**/*.cc"
---

# C/C++ 编码规范

## 规则 1：头文件中禁止使用 `using namespace`（函数内等局部作用域除外）

**绝对不要**在头文件（`.h`、`.hpp`）中使用 `using namespace` 声明，函数内等局部作用域除外。

### 错误示例 ❌

```cpp
// header.h — 禁止这样做！
#include <string>
using namespace std;  // ❌ 污染全局命名空间

class MyClass {
    // ...
};
```

### 正确示例 ✅

```cpp
// header.h — 正确做法
#include <string>

class MyClass {
    std::string name;  // 显式使用 std:: 前缀
};
```

### 原因

- 头文件会被多个源文件包含，`using namespace` 会污染所有引用它的翻译单元
- 导致命名冲突和不可预期的编译行为
- 违反 C++ 最佳实践

---

## 规则 2：临时文件统一放到 `.agent_tmp` 目录

**所有**临时测试文件、零散文件、临时产物必须统一放到项目根目录下的 `./.agent_tmp/` 文件夹中。

### 强制要求

1. **禁止**在项目根目录或其他位置创建临时文件
2. **禁止**在 `src/`、`GodotJS/`、`project/` 等业务目录中散落测试文件
3. 临时文件包括但不限于：
   - 测试用的 `.cpp` / `.h` 文件
   - 调试产物（`.o`、`.obj` 等，除非是正式的 build 输出）
   - 临时脚本、草稿代码
   - 实验性代码片段

### 正确做法 ✅

```
.GodotJS-Ext/
├── .agent_tmp/          ← 所有临时文件统一放这里
│   ├── test_something.cpp
│   ├── debug_dump.h
│   ├── experiment.cc
│   └── ...
├── src/                 ← 只有正式代码
├── GodotJS/             ← 子模块/正式模块
└── project/             ← 正式项目
```

### 原因

- 保持项目目录整洁，避免"到处拉屎"
- 防止临时文件被意外提交到版本控制
- 便于清理：删除 `.agent_tmp/` 即可清除所有临时内容
