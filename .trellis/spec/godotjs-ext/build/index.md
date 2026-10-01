# 构建规范（godotjs-ext）

> 适用范围：SCons 构建、静态绑定 codegen 接线、构建产物（dll）部署验证。
> C++ 测试、TS 集成测试、benchmark、代码生成基线校验见 [../test/index.md](../test/index.md)。

## 开发前检查单

- [ ] 编译前读 [scons-build.md](./scons-build.md)（强制命令、禁 clean、增量机制）
- [ ] 构建产物（dll）部署验证？读 [scons-build.md](./scons-build.md) 的部署节
- [ ] 动依赖（lws / v8 / libnode）或 `GodotJS-Dependencies/`？读 [dependencies.md](./dependencies.md)（依赖仓库形态、改依赖的正确流程、打补丁约定）
- [ ] 构建后要跑测试？测试命令与验收标准见 [../test/index.md](../test/index.md)
- [ ] 动发布（`misc_release.yml` / `verify-release-artifacts` / 构建矩阵的 engine 标签）？读 [release-packaging.md](./release-packaging.md)（唯一派生点、键↔文件 1:1、门禁口径）
- [ ] 动编辑器图标（`icons/*.svg` / `[icons]` 段）？读 [editor-icons.md](./editor-icons.md)（必须纯路径、ThorVG 不渲染 `<text>`、只有约定色 `#e0e0e0`、16px 实测法）
- [ ] 动 CI 的 workflow / job / 矩阵？读下文「CI 门禁是硬性的」

## CI 门禁是硬性的（`continue-on-error` 需用户明确确认）

**除非用户在本轮明确要求，禁止在 `.github/workflows/` 的任何 job/step 上使用
`continue-on-error`。**

- 不得用它把失败的 job/step 变成「不影响结论」。那会让 `success` 这个信号失真：
  别人看到绿就以为全过，而失败被埋在 run 摘要里 —— 这比直接红更糟，因为**红是可修的
  信号，假绿不是**。
- **任务目标若是「让 CI 过」，唯一被接受的路径是把失败修好**；把失败藏起来不算完成，
  也不得作为中间手段（「先挡住、以后再修」不成立）。
- 加了一条**新**的 CI 腿却修不好它时，同样不得用 `continue-on-error` 收场。要么修好，
  要么**连同该腿一起移除**（回到加它之前的状态），要么**先问用户**三选一：
  修好 / 移除该腿 / 用户明确同意它 informational。
- 既有的 `continue-on-error: true` 是**先例，不是许可**：`.github/workflows/ci.yml` 里
  「Debug first-generation teardown crash (diagnostic)」那条是**纯诊断**步骤
  （不产出验收结论、只为抓崩溃回溯），性质不同；新增用法一律按本规则处理。

> 2026-10-01 实例：为让 CI 变绿，我给新加的 host-jsc 测试腿加了
> `continue-on-error: ${{ matrix.runtime == 'host-jsc' }}`，于是 run 显示 `success`
> 而 jsc 腿实为 `failure`。用户判定这是**造假信号**，要求写进 spec 并修好。

## 质量检查

- [ ] 只用规范命令编译，未自行编造参数、未 `scons --clean`
- [ ] dll 替换验证：换当前 target 那一份产物（单库两产物），md5sum 确认两处部署位一致
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
- [ ] 动过依赖：`SConstruct` 的 release tag / 版本常量与依赖仓库 release 的资产名逐字对齐（见 [dependencies.md](./dependencies.md)）
- [ ] 动过 CI：未新增 `continue-on-error`（除用户明确要求）；新加的腿若红，是修好了而不是被藏起来
