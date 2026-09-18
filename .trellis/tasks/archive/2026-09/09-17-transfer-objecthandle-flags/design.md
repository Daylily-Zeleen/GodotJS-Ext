# 对象转移与退出生命周期修复设计

> 2026-09-18 用户调整后，本文件下方为上一版设计记录，不再是现行实现：当前 `TransferData::flags` 整体快照/恢复，prepare 为 void 且要求源句柄已存在，persistent 不是幂等 mark，计数为 int32_t。本轮只适配测试、跑测及说明夹具，不自行改回用户实现；结果见 report.md 最新章节。

## 授权与边界

用户已批准实施，追加要求先在现有 worker 集成测试复现与验证 PRD 疑点，再修改运行时；本轮必须修复已知退出崩溃，V8 与 QuickJS-NG 本地构建和集成测试均通过。不得提交推送，留待人工审查。

## 数据与生命周期契约

- TransferData 保存不可从 Object 类型恢复的源 binding 语义：JS-owned 与 persistent。GD_OBJ / GD_REFCOUNTED 在接收端由类型重建。
- 发送先在句柄有效且尚未 finalize_transfer_out 时保存标志，不提前改变源状态；发送失败仍归源环境。
- 所有权接收保持既定 C2：绑定后合并标志，不修改 gd_obj_to_js 公共签名。已有绑定同样合并，不重复持久登记。
- persistent count 对应环境的真实持久句柄数；标记只在首次置位增加，移除持久句柄时减一次，包括 None 解绑定。计数与 finalizer 是否调用必须解耦；None 不能开始调用 finalizer。
- free_callback 可能在其他线程同步移除句柄；计数变动的同步策略须跟现有 BindingObjectDB 锁设计一起审查，不以加一处裸 decrement 引入竞态。
- RefCounted 仅在接收创建新绑定时补偿传出保留的环境引用；已有绑定分支须按实际转移持有引用配对，不能猜测。
- native worker、shadow realm 的对象路径已有 prepare_transfer_out；web 自动 clone 的手写元数据排除 OBJECT，不能盲目新增对象 flag 注入。

## 退出崩溃

失败事实已知，不重复确认。先用现有失败日志与调试栈定位真实析构/回调顺序；修复源码生命周期，不抑制断言、不忽略退出码、不把 SUCCESS 作为进程成功。与转移是否同根因由证据决定。

## 文件边界

运行时预计改 jsb_message.h、jsb_environment.cpp/.h，必要时改实际退出生命周期所属源文件。测试在 project/tests/worker；只对退出回归必需处改现有测试。禁止直接改 gen/def 或第三方代码，不改 runtime matrix 脚本，不处理无关 benchmark 缺陷。

## 验收证据

旧实现跑新增 worker 场景失败，修复后同场景通过。每个引擎记录构建命令、部署 DLL 身份、C++ 双套件汇总、集成成功哨兵以及进程 exit 0。不可用某引擎旧 DLL/旧日志证明另一引擎通过。
