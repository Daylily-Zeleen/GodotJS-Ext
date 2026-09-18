# 实施与验证顺序

1. 先修改 project/tests/worker 的真实生命周期回归用例，旧运行时执行并记录可解释失败；不先改生产实现。
2. 同期只读分析已知退出崩溃；必要时 debugger 抓退出异常栈，不重复整套测试。
3. 根据复现落实标志传输、持久计数登记/注销及实际退出根因修复；保留用户 int32_t 改动。
4. 所有代码和测试编辑收敛后统一审查与格式化，禁止代理中途构建。
5. V8：scons target=editor compiledb=yes debug_symbols=yes dev_build=yes tests=yes verbose=yes -j6；确认部署两 DLL，编译 TS，执行 C++ 单测与完整 TS 集成，均要求 exit 0。
6. QuickJS-NG：上条构建叠加 use_quickjs_ng=yes；相同部署与完整测试门槛。
7. 若真实失败需要修复，定位后仅做必要增量构建；不为措辞和形式重编。临时诊断/日志放 .agent_tmp。
8. 更新执行报告及必要规范，移除本次临时诊断代码。不得 commit/push，保留完整差异供用户审查。
