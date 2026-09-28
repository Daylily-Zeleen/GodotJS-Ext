# 实测证据（2026-09-28，CI run 36291137069 = 现流程下的完整构建矩阵）

取数方式：`gh run download 36291137069 -n <artifact> -D ...`（GH 已登录，token scopes 含 repo/workflow），
以及 `gh release download v1.0.1` 取现网发布包。原件落在 `.agent_tmp/`（gitignore），本文件只留结论与清单。

## 1. run 36291137069 的 artifact 清单（23 条构建腿 + scripts-out）

```
android-template_release-arm64-qjs-ng     android-template_release-arm64-v8
android-template_release-x86_64-qjs-ng    android-template_release-x86_64-v8
ios-template_release-arm64-jsc            ios-template_release-arm64-qjs-ng
ios-template_release-arm64-v8
linux-editor-arm64-qjs-ng                 linux-editor-arm64-v8
linux-editor-x86_64-node                  linux-editor-x86_64-qjs-ng
linux-editor-x86_64-v8
macos-editor-arm64-jsc                    macos-editor-arm64-node
macos-editor-arm64-v8                     macos-editor-universal-qjs-ng
web-template_release-wasm32-qjs-ng        web-template_release-wasm32-qjs-ng-nothreads
web-template_release-wasm32-v8            web-template_release-wasm32-v8-nothreads
windows-editor-x86_64-node                windows-editor-x86_64-qjs-ng
windows-editor-x86_64-v8
scripts-out
```

## 2. 引擎 ↔ 腿映射（23 条腿的划分，7+9+2+3+2 = 23）

| 引擎 | 腿 |
|---|---|
| v8（7） | windows-x86_64-editor、linux-x86_64-editor、linux-arm64-editor、macos-arm64-editor、android-arm64-tr、android-x86_64-tr、ios-arm64-tr |
| qjs-ng（9） | windows-x86_64-editor、linux-x86_64-editor、linux-arm64-editor、macos-universal-editor、android-arm64-tr、android-x86_64-tr、ios-arm64-tr、web-wasm32-tr(threads)、web-wasm32-tr(nothreads) |
| jsc（2） | macos-arm64-editor、ios-arm64-tr |
| node（3） | windows-x86_64-editor、linux-x86_64-editor、macos-arm64-editor |
| web（2） | web-wasm32-tr(threads)、web-wasm32-tr(nothreads) |

注意：表中 `web` 的两条腿在**当前** ci.yml 里标的是 `engine: v8`（见第 3 条）；改标 `web` 后
artifact 名变为 `web-template_release-wasm32-web[-nothreads]`。

## 3. `engine: v8` 的 web 腿 = 纯 web 引擎（浏览器宿主 JS），不是 v8

wasm 二进制内字符串计数：

| artifact | `impl/web/` | `jsb_web` | `impl/quickjs/` | `JS_NewRuntime` | `impl/v8/` |
|---|---|---|---|---|---|
| `web-template_release-wasm32-v8` | 83 | 484 | 0 | 0 | 0 |
| `web-template_release-wasm32-qjs-ng` | 0 | 0 | 84 | 6 | 0 |

（两者的 `v8::` 字样均为静态绑定生成的模板签名文本，非 v8 引擎实现；`impl/v8/` 才是引擎实现 TU。）

机理（`SConstruct`）：

```
379 use_quickjs = None if node_support else (third/quickjs if use_quickjs else (third/quickjs-ng if use_quickjs_ng else None))
383 if node_support is None and quickjs_support is None and jsc_support is None and is_library_supported(v8_prebuilt_libs):
384     download_dependency("v8", ...)          # v8_prebuilt_libs 无 web 条目 -> is_library_supported 为假
386 v8_support = validate_library_support(v8_prebuilt_libs) if quickjs_support is None and jsc_support is None else None  # web 下为 None
426 CompileDefines("JSB_WITH_WEB", 1 if jsb_platform == "web" and quickjs_support is None else 0)   # -> 1
```

结论：platform=web 且不带引擎标志时，产物必然是 `JSB_WITH_WEB`；v8 在 web 上没有后端。
所以 `*-v8` glob 把浏览器 JS 的 wasm 扫进了「v8 包」。

## 4. 各 artifact 内的实际文件（只列非 LICENSE/非调试附属）

| artifact | 文件 |
|---|---|
| windows-editor-x86_64-v8 | `windows/godotjs-ext.windows.editor.x86_64.dll` + `.exp/.lib/.pdb`、`windows/godotjs-ext-editor.windows.editor.x86_64.dll` + `.exp/.lib/.pdb` |
| windows-editor-x86_64-qjs-ng | `windows/godotjs-ext.windows.editor.x86_64.dll`、`windows/godotjs-ext-editor...dll`、`windows/libgodotjs-ext(-editor).windows.editor.x86_64.a` |
| windows-editor-x86_64-node | 上列 v8 的两份 dll + **`windows/node.dll`**、`windows/node.def/.exp/.lib`、`windows/godotjs-ext.exe`、`windows/godotjs-ext.ilk/.pdb` |
| linux-editor-x86_64-v8 | `linux/godotjs-ext.linux.editor.x86_64.so`、`linux/godotjs-ext-editor.linux.editor.x86_64.so` |
| linux-editor-arm64-v8 | 同上的 arm64 版 |
| linux-editor-x86_64-node | 同上 + `linux/godotjs-ext`（node helper 可执行，运行时按 res 路径加载） |
| macos-editor-arm64-v8 | `macos/godotjs-ext.macos.editor.arm64.dylib`、`macos/godotjs-ext-editor.macos.editor.arm64.dylib` |
| macos-editor-arm64-node | 同上 + `macos/godotjs-ext`（helper） |
| macos-editor-universal-qjs-ng | `macos/godotjs-ext.macos.editor.universal.dylib`、`macos/godotjs-ext-editor.macos.editor.universal.dylib` |
| macos-editor-arm64-jsc | arm64 dylib ×2 |
| ios-template_release-arm64-v8 | **只有** `ios/godotjs-ext.ios.template_release.arm64.dylib`（无 xcframework） |
| ios-template_release-arm64-qjs-ng | dylib + `ios/…xcframework/{Info.plist, ios-arm64/…, ios-arm64-simulator/…}` |
| ios-template_release-arm64-jsc | 同上（xcframework） |
| android-template_release-{arm64,x86_64}-{v8,qjs-ng} | `android/godotjs-ext.android.template_release.<arch>.so` |
| web-template_release-wasm32-v8(|-nothreads) | `web/godotjs-ext.web.template_release.wasm32.wasm` / `…wasm32.nothreads.wasm` |
| web-template_release-wasm32-qjs-ng(|-nothreads) | 同上（同名文件，内容为 quickjs wasm） |

引擎身份探针（`impl/<engine>/` 字符串计数）：macos arm64 v8 → `impl/v8/`4；node → `impl/node/`11 + `libnode`74；
jsc → `impl/jsc/`49 + `JSContextGroupCreate`2；qjs-ng universal → `impl/quickjs/`100 + `JS_NewRuntime`4；
ios v8 / android v8 → `impl/v8/`1。

## 5. 现网 v1.0.1 包（2026-08-13，早于 editor/runtime 拆分）

`godotjs-ext-v8-windows-linux-macos.zip`（399,807,693 B）内：
`godotjs-ext.gdextension`（`compatibility_minimum = 4.6`）、`bin/linux/godotjs-ext.linux.editor.x86_64.so`、
`bin/windows/godotjs-ext.windows.editor.x86_64.{dll,pdb,exp,lib,ilk}`、`bin/macos/godotjs-ext.macos.editor.arm64.dylib`。

- 只含 editor target 产物 ⇒ 桌面「只支持编辑器内使用」是既有事实（非本任务引入）。
- 无任何 `godotjs-ext-editor.*` ⇒ 该包早于双 gdextension 拆分（拆分 commit 2b51de7 = 2026-08-31）。
- 包内目录名 = zip 名。

## 6. 当前发布步骤的实际行为（读 misc_release.yml:110-224）

- `cp -r "$artifact_dir"* "$ADDON_DIR/bin/"` ⇒ 把 artifact 顶层内容整体倒进 `bin/`，包括
  scons-build action 额外放的 `addons/godotjs-ext.daylily-zeleen/LICENSE`（会变成
  `bin/addons/.../LICENSE`）与全部 editor 二进制。
- 只 `cp` runtime 的 `godotjs-ext.gdextension` 一份；editor 的 `.gdextension` 与 `.uid` 不进包。

## 7. 相关源码/配置锚点

- 构建矩阵：ci.yml:409-555；artifact 名：ci.yml:697-700；门禁：ci.yml:702-742
- scons-build 的 engine→flag 映射与 ios 特判：`.github/actions/scons-build/action.yml:131-139, 205-214`
- 引擎选择与 `JSB_WITH_*`：`SConstruct:369-430`；产物名：`SConstruct:940-946`；xcframework：`SConstruct:1059-1114`
- 现有 superset `.gdextension`：`project/addons/godotjs-ext.daylily-zeleen/godotjs-ext.gdextension`（runtime）、
  `godotjs-ext-editor.gdextension`（editor）
- 引擎实现目录：`src/runtime/impl/{v8,quickjs,node,jsc,web}`
- Godot 侧库解析（全 tag 匹配 + tag 数最多者胜）：`third/godot-cpp`… 及引擎 `core/extension/gdextension_library_loader.cpp:74-97`；
  导出门（arch 细分、xcframework/dylib 处理）：`editor/export/gdextension_export_plugin.h:86-160`
- 发布链路：`release.yml`（workflow_run → should-publish → misc_release mode=full → mode=upload-only）、
  `upload_assets.yml`（手动重传）
