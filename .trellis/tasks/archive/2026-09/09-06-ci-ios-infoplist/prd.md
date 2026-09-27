# iOS xcframework Info.plist 核查

## Goal

核对 SConstruct 合成的 iOS xcframework 是否带正确 Info.plist（缺失时 Xcode 集成告警）

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.

---

# 结论：已实现（2026-09-28，superseded）

本任务担心的问题**不存在**：`xcodebuild -create-xcframework` 会自行生成
xcframework 的 `Info.plist` 清单，不会缺失。实测核对过 CI 产物。

## 实测（下载 run 36291137069 的 ios-template_release-arm64-qjs-ng 解开检查）

`ios/godotjs-ext.ios.template_release.xcframework/Info.plist` 存在且完整：

- `CFBundlePackageType = XFWK`、`XCFrameworkFormatVersion = 1.0`
- `AvailableLibraries` 含两个 slice：
  - `ios-arm64`：device，`SupportedPlatform=ios`
  - `ios-arm64-simulator`：`SupportedPlatformVariant=simulator`
- 每个 slice 均有 `LibraryIdentifier` / `LibraryPath` / `BinaryPath` /
  `SupportedArchitectures=[arm64]`，且与磁盘实际文件一一对应
- 两个 dylib 均为 `Mach-O 64-bit arm64 dynamically linked shared library`

生成实现见 `SConstruct:1068-1123`。

## 两个容易误判的点（记录以免后人重开）

1. **xcframework 内无 headers 目录**（创建时未传 `-headers`）。当前用途是 Godot
   导出模板（运行时加载 dylib），不需要头文件，**够用**；仅当有人要把它当
   Swift/ObjC 库在 Xcode 内直接 link 时才会缺，属新需求而非本任务缺陷。
2. **ios-template_release-arm64-v8 那个 artifact 里没有 xcframework**，只有
   device dylib —— 这是设计使然：`.github/actions/scons-build/action.yml:209`
   对 `ios && engine != v8` 才构建 simulator 并合成 xcframework，因为 v8 预编译
   无 iOS Simulator 版本（`SConstruct:401-403` 有对应拒绝逻辑）。非缺陷。

本任务归档为 superseded（已实现，卡片滞后），非新增实现。
