# 按 JS 引擎分包发布（并配套正确的 .gdextension）

> 来源：`.trellis/tasks/09-06-ci-release-packaging`（P3，ci）。
> 本文件于 2026-09-28 按用户回忆的真实意图重写：原描述只记了「zip 名不符」与
> 「jsc/node 无 upload job」两个现象，未写清目标形态。

## Goal

**按 JS 引擎为单位发布**：每个 release 包对应一个 JS 引擎，且**只含该引擎实际支持的
平台 / 架构**；同时包内的 `.gdextension` 只声明该引擎真实提供的库条目，避免用户拿到
一个声称支持某平台、实际却缺对应二进制的包。

## Background（当前实现，核对 .github/workflows/misc_release.yml 与 run 36291137069）

现有发布流程只有两个 job，按**后缀 glob**抓取并各打一个 zip：

| job | glob | 产出 zip |
|---|---|---|
| `upload-v8` | `*-v8`（misc_release.yml:114） | `godotjs-ext-v8-windows-linux-macos.zip`（:146） |
| `upload-qjs-ng` | `*-qjs-ng`（:184） | `godotjs-ext-qjs-ng-windows-linux-macos.zip`（:216） |

两个问题：

1. **jsc / node 完全没有发布路径。** `*-v8` / `*-qjs-ng` 只覆盖 v8 与 quickjs-ng；
   node 的 3 条腿与 jsc 的 2 条腿永远进不了任何 release 包。且
   `verify-release-artifacts`（ci.yml:702-742）也不会检查它们（它们本就不在
   merge flow 里），所以这个漏洞是静默的。
2. **zip 名与实际内容不符。** 名字写 `...-windows-linux-macos`，但 glob 命中的是
   全部平台，实际还含 android / ios / web。

### 各引擎实际产出的平台 / 架构（run 36291137069 实测 artifact 清单）

| 引擎 | editor 腿 | template_release 腿 |
|---|---|---|
| **v8** | windows x86_64、linux x86_64、linux arm64、macos arm64 | android arm64、android x86_64、ios arm64、web wasm32（threads + nothreads） |
| **qjs-ng** | windows x86_64、linux x86_64、linux arm64、macos universal | android arm64、android x86_64、ios arm64、web wasm32（threads + nothreads） |
| **jsc** | macos arm64 | ios arm64 |
| **node** | windows x86_64、linux x86_64、macos arm64 | —（无 template 腿） |

（构建矩阵见 ci.yml:409-555；v8 在 macos 为 arm64-only，ci.yml:452-458 说明 v8 预编译
无 universal 变体；node 无 template 腿。）

## Requirements

1. **发布单位改为「引擎」**：为每个引擎产出一个包（v8 / qjs-ng / jsc / node），替换
   现有的两个按后缀 glob 的 job。glob 抓取需覆盖全部四种引擎，不再漏 jsc / node。
2. **包内容 = 该引擎实际支持的平台 / 架构**，以构建矩阵为唯一依据（不硬编码第二份
   平台清单 —— 现有 `verify-release-artifacts` 就是「由矩阵派生」的做法，沿用）。
   例如 jsc 包只含 macos-arm64（editor）与 ios-arm64（template）；node 包只含三个
   桌面 editor 腿。
3. **每个包自带正确的 `.gdextension`**：`[libraries]` 只列出该引擎真实提供的条目，
   从包内实际存在的文件派生。禁止保留「声称支持但包里没有」的条目。
   - 注意 macOS 的解析顺序：Godot 先匹配带 arch 的 key，再回落到 arch-less key
     （ci.yml:858-880 有相关说明）。v8 是 arm64-only，故只能声明
     `macos.<target>.<arch>.arm64`，**不可**声明 arch-less 的 universal 条目，
     否则会去找 `...universal.dylib` 而这个引擎根本不产出。
   - qjs-ng 的 macos 产物是 universal，走 arch-less key。
4. **web 的 threads / nothreads 两个变体都要进包**，并对应 `.gdextension` 中
   `web.<target>.threads.wasm32` 与 `web.<target>.wasm32` 两组 key。
5. **iOS 用 xcframework**：ios 条目指向 `...xcframework` 目录（由
   `xcodebuild -create-xcframework` 生成，见 SConstruct:1068-1123）。注意 v8 无
   iOS Simulator 预编译，故 v8 的 iOS 腿只产出 device dylib、**没有 xcframework**
   （scons-build action.yml:209 对 v8 跳过合成），该引擎的 ios 条目需据此处理。
6. **`[dependencies]` 的 node.dll 只属于 node 引擎**：现有 `.gdextension` 的
   `[dependencies]` 给 windows 各条目都挂了 `bin/windows/node.dll`，那是 node
   引擎专属；非 node 的包不应带此依赖。

## Acceptance Criteria

- [ ] 四种引擎（v8 / qjs-ng / jsc / node）**各有一个** release asset，无引擎被漏掉
- [ ] 每个包内只包含其引擎构建矩阵所声明的平台 / 架构；zip 名如实反映所含范围
- [ ] 每个包内 `.gdextension` 的 `[libraries]` 条目与包内实际文件**一一对应**，
      不存在「声明了但包里没有」的条目
- [ ] macOS 条目符合各引擎的架构实情（v8 → arm64-only；qjs-ng → universal）
- [ ] web 的 threads / nothreads 变体均被包含且 key 正确
- [ ] `[dependencies]` 的 node.dll 只出现在 node 包
- [ ] 发布门禁仍由 `verify-release-artifacts` 覆盖，且新增引擎后**不会**静默漏包

## Notes

- 现有实现位置：`misc_release.yml:110-224`（两个 job 结构对称，改造时可参考）。
- 门禁：`ci.yml:702-742`（expected 由构建矩阵派生，single source of truth）。
- 相关归档任务：`09-06-ci-ios-infoplist`（已确认 xcframework 的 Info.plist 由
  `xcodebuild` 自行生成，无缺失问题；其结论中「v8 的 iOS 无 xcframework」与
  本任务的第 5 条直接相关）。
- 本轮只完善任务描述，**未改动任何构建 / 发布代码**。
