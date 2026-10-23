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

`third/` 下的库（godot-cpp、quickjs、quickjs-ng、v8、lws、doctest）是**外部依赖子模块**，原则上**只读**；如需适配，在 `src/` 侧（runtime / compat / bridge）写兼容层，不直接改第三方源码。例外须用户明确确认。

godot-cpp 的具体使用约束（`_` 前缀虚函数、`godot::` 类型、接口表初始化）见 [godot-cpp-usage.md](./godot-cpp-usage.md)。

## 容器存值优先，避免「map 存指针 + 并行容器手工所有权」

**What**：要按名索引一批结构体时，直接 `HashMap<String, XDecl>` 存值。不要写
`HashMap<String, XDecl *>` + `Vector<XDecl *> owned_*` 双登记 + 手工析构函数来配对。

**Why**：
- godot-cpp 的 `HashMap` entry 是**逐个堆分配**的节点；重哈希只重建索引数组并复用节点指针，
  Robin Hood 冲突交换的是指针槽而不是节点内容；迭代顺序就是插入顺序
  （`third/godot-cpp/include/godot_cpp/templates/hash_map.hpp`）。所以常见顾虑
  「值会随重哈希搬家、`getptr()` 悬空」在这里**不成立**。
- 双登记会漂移，所有权靠人记：本项目就发生过漏写析构 ⇒ 每次 `--generate-types` 漏掉整份 decl
  （`PagedAllocator: VariantPools` 两条 ERROR + 一批对象泄漏）。改成值语义后，这套手工释放路径
  连同它的 bug 一起消失。
- 顺带省掉每项一次 `memnew` 与一层间接访问。

**唯一要守的规程**（`HashMap::insert` 只有 `const TValue&` 重载，会深拷贝整份值）：

```cpp
// key 先算好；跳过/校验分支必须前置（operator[] 会默认插入）
if (p_api_class == nullptr) continue;
const String key = NamingUtil::get_class_name(p_api_class->name);
ClassDecl &decl = classes[key];        // 原地默认构造，无拷贝
decl.super = p_api_class->inherits;
```

- **不要** `classes.insert(key, decl)`——深拷贝整份 decl（含所有 `LocalVector`）。
- 不要指望 `operator[]` 帮你判重：重复 key 会**累加**进同一份 decl，而 `insert` 是**覆盖**，
  两者产物不同；发现 key 可能撞名时先做重复检测。

**实例**：`src/editor/codegen/jsb_codegen_type_db.h/.cpp`、`jsb_codegen_docs.h/.cpp`
（2026-10-23 从指针版改成值语义，同输入 A/B 差分证明产物零影响）。

## 注释语言

- **新增注释一律用简短中文**（源码、测试、构建脚本同样适用）；一句话能说清就不写三行。
- 只有定论值得留：说清「为什么」或「与直觉相反的点」，不复述代码在做什么。
- 存量英文注释不强制改写；改到附近时顺手中文化。
- 标识符、API 名、专有名词保留原文；`NOTE` / `TODO` 等标记词保留英文。
- commit message 仍用英文（见仓库根 `AGENTS.md`）。
