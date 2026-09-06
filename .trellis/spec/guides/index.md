# Thinking Guides

> **目的**：写代码前扩展思考面，捕捉"没想到"的问题。大多数 bug 与技术债来自"没想到"，不是不会写。

---

## 项目硬性守则（每轮必守）

→ [workflow-rules.md](./workflow-rules.md)：git 提交/推送/还原必须经用户确认、临时文件一律 `.agent_tmp/`、文档用中文。

---

## 思维指南索引

| 指南 | 用途 | 何时读 |
|-------|------|--------|
| [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md) | 先搜索再写新代码，消灭重复 | 发现自己在复制/重写既有逻辑时 |
| [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md) | 跨层数据流推演 | 功能跨越多层（JS ↔ 桥接 ↔ api_tool ↔ godot-cpp ↔ 引擎）时 |
| [workflow-rules.md](./workflow-rules.md) | git / 临时文件 / 任务管理硬约束 | 任何涉及 git 操作或文件创建的时刻 |

---

## 何时思考跨层问题

- [ ] 功能触及 3+ 层（JS 运行时、桥接层、api_tool/static_binding、godot-cpp、Godot 引擎）
- [ ] 数据格式在层间转换（JS value ↔ Variant ↔ EncodeT 内存布局）
- [ ] 多个消费方需要同一份数据
- [ ] 不确定逻辑该放哪一层
- [ ] 在新增事件类型、JSONL 记录、RPC 载荷或配置字段
- [ ] Godot 侧对象生命周期与 JS 侧引用交叉（Ref/RefCounted、Orphan StringName）

→ 读 [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md)

## 何时思考代码复用

- [ ] 正在写与既有代码相似的逻辑
- [ ] 同一模式出现 3+ 次
- [ ] 在多个地方新增同一字段
- [ ] **正在修改任何常量或配置**（如 `API_VERSION`、`all_encode_types`）
- [ ] **正在创建新的工具/辅助函数** ← 先搜索！
- [ ] 两个文件各自用本地 cast 读同一无类型载荷字段

→ 读 [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md)

---

## 核心原则

改任何值之前先搜索它的全部出现点（防"忘了同步 X"类 bug）；30 分钟的推演省 3 小时的调试。
