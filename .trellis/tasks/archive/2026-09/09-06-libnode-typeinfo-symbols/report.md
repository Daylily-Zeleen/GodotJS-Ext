# 结题报告：libnode V8 Delegate typeinfo 符号缺失（macOS）＋ Linux libgcc 版本错配

更新日期：2026-09-26
状态：**已解决**（三条 node 腿全绿，见"端到端证据"）

## 结论

| 平台 | 状态 | 关键证据 |
|---|---|---|
| windows | 已解决（本轮之前） | MSVC 标记存在，`verify_symbols.py` 通过 |
| **macOS** | **已解决** | 打开 darwin RTTI 后归档含 `ZTI/ZTS/ZTV` 三件套；本仓 macos node 腿 success |
| **Linux** | **已解决** | 本仓 linux node 腿改用 gcc-12 链接；success |

## 修订：上一轮报告里的一处符号名错误

上一轮写的是 `_ZTIN2v816ValueDeserializer8DelegateE`（"16"），**这个名字是错的**，
因此"`ValueDeserializer` 的元数据在任何成员中都不存在"这个结论是**测量错误**造成的假象：
Itanium mangling 里的数字是**类名长度**，`ValueSerializer` 是 15 个字符，
`ValueDeserializer` 是 **17** 个字符。正确名字是 `_ZTIN2v817ValueDeserializer8DelegateE`。

用正确名字重测（`enum_delegate4.py`，逐成员按符号表解析）：

| 符号 | linux 归档 | macOS 归档（旧/发布的） |
|---|---|---|
| `ZTI/ZTS/ZTV` ValueSerializer::Delegate | 三个都有 | **只有 ZTV** |
| `ZTI/ZTS/ZTV` ValueDeserializer::Delegate | 三个都有 | **只有 ZTV** |

所以 macОS 上**两个类都缺 typeinfo**，而且是同一个原因——不是"key function 归属不对称"。

## 真正的根因：`patch_rtti.py` 对 darwin 是空操作，且理由本身是错的

`patch_rtti.py` 的 darwin 分支只"检查布局"，依据是文档字符串里的一句断言：
**"gyp 的 make 生成器忽略 xcode_settings，所以 macos/ios 上 RTTI 本来就是开的"**。
这句话是**错的**：

- `tools/gyp/pylib/gyp/generator/make.py` 第 33/864/1420 行 → 编译参数由
  `gyp.xcode_emulation.XcodeSettings.GetCflagsCC()` 产生
- `xcode_emulation.py:723-724`：
  ```python
  if self._Test("GCC_ENABLE_CPP_RTTI", "NO", default="YES"):
      cflags_cc.append("-fno-rtti")
  ```
- `common.gypi`（node v24.x）里该设置位于 `target_defaults`（行 138）→ `conditions` →
  `OS=="mac"` → `xcode_settings`（行 714），**作用于所有目标**
- CI 日志实证：macOS 的 `deps/v8/src/api/api.cc` 编译行含 `-fno-rtti`，
  而**整条日志里 `-frtti` 出现 0 次**

"有 vtable、没有 typeinfo/type-string"正是 `-fno-rtti` 编译单元的签名：vtable 仍会发出
（供虚调用），但类型元数据被完全抑制。

**不是本仓引入的回归**：moluopro 的参考 macOS 归档（`libnode/24.18.0`）**同样只有 ZTV**，
所以不存在"对齐参考产物"的修法，符号必须**新增**。

## 修法

### 1. `GodotJS-Dependencies/scripts/node/patch_rtti.py`（commit `1482868`）
darwin 分支真正把 `'GCC_ENABLE_CPP_RTTI': 'NO'` 改为 `'YES'`（连同行尾 `# -fno-rtti`
注释一起纠正），保持幂等与 fail-closed，并订正文档字符串里那句错误断言。

### 2. `GodotJS-Dependencies/scripts/verify_symbols.py`（同一 commit）
新增 `validate_node_delegate_typeinfo`：断言两个 Delegate 类的 Itanium typeinfo
**被真正定义且可见**。这是本任务拖了这么久的直接原因——老版本只断言 out-of-line 虚函数
（`WriteHostObject` / `ReadHostObject`），而**一个下游链不进去的归档照样能通过**。
注：`.monkeycode/specs/ci-build-complete/design.md:187` 最初要求的正是 typeinfo，
是后来被改成虚函数断言的。

实现细节：不调 `nm`（Linux runner 上读不了 Mach-O 归档，Apple nm 开关也不同），
直接解析 ELF/Mach-O 符号表；并要求符号**非 LOCAL、非 hidden**
（LOCAL/hidden 同样满足不了下游引用，放行等于重建同一盲区）。

### 3. Linux：`__extendhfdf2`（本任务"关联"项的收尾）
归档内 `print.o`（v8 的 SIMD 库 **Highway**，`hwy::detail::PrintArray/ToString/TypeName`）
引用 `__extendhfdf2`（`_Float16`→`double`），这是 **GCC 12 才加入 libgcc** 的 helper。
依赖仓用 gcc-12 构建，本仓 linux 腿用 ubuntu-22.04 的 `g++`（11）并加 `-static-libgcc` 链接。
**同样非回归**：moluopro 的参考 linux 归档也只有 `print.o` 引用它。
修法（方向 B，用户认可）：本仓 `setup-godotjs-ext` 为该腿装 gcc-12 并预检 libgcc 含该符号，
`scons-build` 追加 `CC=gcc-12 CXX=g++-12`。

## 本地复现（每个机制都先复现再改）

| 复现 | 结果 |
|---|---|
| `-fno-rtti` vs `-frtti` 编译「基类有 key function + 子类引用基类 typeinfo」的最小工程 | `-fno-rtti` → vtable=1/typeinfo=0 → 链接失败，报错**与 CI 完全相同**；`-frtti` → 两者齐全（visibility `V`）→ 通过 |
| 用 node v24.x **真实的** gyp `xcode_emulation.py` 驱动 `GetCflagsCC()` | patch 前 `['-fno-rtti','-fno-exceptions']`；patch 后 `['-fno-exceptions']` |
| 解包 jammy 的 `libgcc-11-dev` / `libgcc-12-dev` | gcc-11 里名为 `extendhfdf2` 的成员 **0** 个；gcc-12 **1** 个 |
| 拿归档里真实的 `print.o` 做 `ld -shared -Wl,--no-undefined` | gcc-11 报出**2 条**与 CI 相同的未定义；gcc-12 干净通过 |
| 收紧后的门槛跑真实归档 | macOS（含 moluopro 参考）→ **拒绝**；linux → **通过**；解析器自检确认能读到 macOS 里**存在**的 vtable，故拒绝不是解析失败所致 |

## 端到端证据（CI）

1. **依赖仓作用域构建** `36218184001`（`platforms=macos-arm64,linux-x86_64`）：
   **11/11 success 含 `publish`**，产出 `ci-36218184001`。
2. **下载新 macOS 归档核对符号**：
   | 符号 | 新归档 | 旧归档 |
   |---|---|---|
   | `__ZTI…ValueSerializer8DelegateE` | **PRESENT** | ABSENT |
   | `__ZTS…ValueSerializer8DelegateE` | **PRESENT** | ABSENT |
   | 同上（Deserializer） | **PRESENT** | ABSENT |
3. **本仓 CI** `36218815302`（`deps_run=36218184001`，`deps_engines=node`）：三条 node 腿全绿
   ```
   success  Build (linux, x86_64, editor, node)
   success  Build (macos, arm64, editor, node)     <-- 修复前为 failure
   success  Build (windows, x86_64, editor, node)
   ```
   其中 linux 腿日志实证 `scons ... use_node=yes CC=gcc-12 CXX=g++-12` 与
   链接行 `g++-12 -o bin/linux/godotjs-ext ... -static-libgcc ... libnode.a ...`。

## 遗留

1. **`build_v8.yml` 的 android 腿（既有缺陷，与本任务无关）**：
   全量构建 `36206778340` 里 node 5/5、lws 8/8 全绿，但 3 条 v8-android 腿在 `gn gen` 失败
   （`Unable to load ".../third_party/catapult/tracing/BUILD.gn"`）。根因：v8 源码缓存 key
   只看 `runner.os`，android 与 linux 共用同一个 Linux 条目；而 `v8/fetch` 只在**冷启动**时
   写 `.gclient` 的 `target_os=['android']`，命中缓存时 android 专属依赖（v8 DEPS 里带
   `'condition': 'checkout_android'`，如 catapult）永远不会 checkout。
   后果：`publish` job 要求 `v8.result == 'success'`，因此**版本化 release 被整体跳过**
   （只出 `ci-<runid>`）。因 Android 的 v8 版本与已发布的 `260925-node-final` 一致，
   现网不受影响，仅"重新构建"时暴露。已记入
   `.trellis/spec/godotjs-ext/build/dependencies.md`（commit `12b1bc1`）。
2. **`deps_release_tag` 未改动**：`260926-node-final` 因上述 v8-android 失败**没有发布**，
   故 `SConstruct` 保持 `260925-node-final`（已确认与 HEAD 字节一致，未产生改动）。
   修掉 android 问题后跑一次全量发布，再切 tag。
3. 主仓 test job 的 host-node macos 腿（`ci.yml:657` 已定义）可考虑打开。
