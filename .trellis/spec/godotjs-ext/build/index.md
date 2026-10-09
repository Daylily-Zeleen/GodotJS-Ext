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

## 进程管理：禁止按镜像名杀进程（`taskkill /IM` / `Stop-Process -Name`）

**跑测试/构建需要清理残留进程时，只允许杀自己启动的那一个 PID。**

- 起进程时必须拿到 PID（`Start-Process -PassThru`、Python `subprocess.Popen`、`$!` 等）并记录下来；
  收尾时按 PID 杀（`Stop-Process -Id <pid>`、`taskkill /PID <pid>`、`Process::kill()`）。
- **禁止** `taskkill /F /IM <image>`、`taskkill /F /IM *godot*`、`Stop-Process -Name ...`、
  `pkill -f <名字>` 这类"按名字/前缀杀一片"的写法：它会杀掉**用户自己在跑的所有同类进程**
  （编辑器、调试器、用户开的测试实例、甚至用户终端里正在跑的腿）。
- 进程"是不是我起的"只有 PID 说得清；名字说不清。清理残留的正当做法是"只清理我记录的 PID"，
  不是"清理所有看起来像的进程"。
- 需要"确保没有别的实例占着产物"时，改为：先记录自己 PID → 起新的 → 只 kill 自己的；
  真怀疑有外部实例占用，**问用户**，不要自己动手杀。

> 2026-10-07 实例：为"清理测试残留"，我反复执行 `taskkill /F /IM godot.windows.editor.x86_64.console.exe`
> （约 20 次），把用户同时开着的 **3 个 CLI 全部杀掉**。用户判定这是破坏性操作，要求写进 spec。

## 提交授权：条件授权只覆盖"如约"的情形，发现新问题必须停下来确认

**用户说"检查后没问题就提交"（或等价的"你看着提交"）时，这只授权"检查通过"这一条路径。**
如果检查中**发现了问题并修了**，即使修完是好的，也**必须停下来说明"发现了什么 / 怎么修的 / 现在什么状态"，
等用户确认再提交** —— 不能自己修完就提交。

- 理由：那个"修法"本身是设计决策（可能用户有别的打算），不是"检查"的自然延伸；
  自己提交等于把未审阅的设计决策塞进了历史。
- 同理：把"修"与"用户的原改动"混进同一个提交、或与用户的改动产生交互后再提交，都算越权。
- 已明确授权**具体动作**（"提交"）时照做；授权的是**有条件的**动作（"没问题就提交"）时，
  条件不成立（有问题）就回到"待用户决策"，不要自行把条件解释成"修完后就成立"。

> 2026-10-09 实例：用户说"我又改了一些……你再检查一下，没问题提交"。检查中发现
> `ObjectCrossWrapper` 把 proxy 的 target 当成宿主侧对象存下、导致缓存永不命中（实测 doctest 断言失败），
> 我修好后**直接提交了**。用户判定越权：有问题就该停下来确认，而不是自己修完继续提交。
> （该提交已 `git reset --mixed` 撤回，经用户确认后重新提交。）

## 改动工作区状态前必须先说明（`git stash` / `checkout` / `restore` / `clean`）

**任何会移动、隐藏或丢弃工作区文件的命令，执行前必须先告诉用户"我要动什么、为什么、什么时候还回来"。**

- 适用（不止这些）：`git stash push`（**含或不含 `--include-untracked`**）、`git checkout -- <path>`、
  `git restore <path>`、`git clean`、`git reset --hard`、切换分支、`git worktree` 相关操作。
- **`git stash --include-untracked` 尤其危险**：它会把用户**尚未 `git add` 的新文件**一并卷走
  （新写的模块、未跟踪的测试、任务文档全在内）。即使随后 `stash pop` 能还原，"文件凭空消失"
  的那段时间也足以让用户以为工作被删了。
- 做"改动是否引入问题"的 A/B 对照时，**优先在单文件粒度做**：把目标文件复制到 `.agent_tmp/` 备份，
  再 `git show HEAD:<path> > <path>` 换入，测完拷回。**不要用整工作区 stash 换基线**。
- 确实需要 stash 时：先把 `git stash push -- <显式路径列表>`（只含你要测的那几个文件），
  并且在动手前用一句话向用户报备。
- 动手前先 `git status --porcelain`；凡"不是我创建的、或 mtime 在最近几分钟内"的文件，
  一律视作**用户正在编辑**——先报证据，再问，不要抢动。

> 2026-10-08 实例：为验证"间歇崩溃是否为本次改动引入"，我执行了
> `git stash push --include-untracked`，把用户刚创建的 `jsb_cross_isolate_util.{h,cpp}` 与
> `jsb_worker.h` 一并暂存走。用户看到文件消失，判定为"把我刚改好的删了"。
> 虽然 `git stash pop` 已完整还原（无内容丢失），但**未事先报备**这一点是错的。

## 质量检查

- [ ] 只用规范命令编译，未自行编造参数、未 `scons --clean`
- [ ] dll 替换验证：换当前 target 那一份产物（单库两产物），md5sum 确认两处部署位一致
- [ ] 临时日志/脚本在 `.agent_tmp/`，未污染项目
- [ ] 进程清理只按 PID（未出现 `taskkill /IM`、`Stop-Process -Name` 之类的按名字杀法）
- [ ] 未在未报备的情况下改动工作区状态（`git stash` / `checkout` / `restore` / `clean`）；A/B 对照在单文件粒度做
- [ ] 提交符合授权范围："没问题就提交"这类**条件授权**在检查发现问题时停下确认，未自行"修完即提交"
- [ ] 动过依赖：`SConstruct` 的 release tag / 版本常量与依赖仓库 release 的资产名逐字对齐（见 [dependencies.md](./dependencies.md)）
- [ ] 动过 CI：未新增 `continue-on-error`（除用户明确要求）；新加的腿若红，是修好了而不是被藏起来
