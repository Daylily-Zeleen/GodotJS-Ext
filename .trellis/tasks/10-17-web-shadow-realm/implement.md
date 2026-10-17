# 执行计划

1. web shim 多 engine（`monolith.ts`：engine 表 + 真实 id + GetEngine/FreeEngine 校验）→ web 构建 + web 腿回归
2. `src/jsb.config.h` 打开 web 的 `JSB_SHADOW_REALM_ENABLED` → web 编译修复（迭代）
3. shadow env 的模块加载器/全局类注册幂等化（同 realm 重复注册）
4. cross-environment web 腿：shadow 后端场景验证（object-transfer ×4、error-reporting ×2）
5. native 回归：node 构建、doctest、host-node
6. 文档站更新（web 后端能力矩阵 / shadow realm 页面）
7. 提交、推送、PR（base: feature/web-export-glue）、CI 观察
