# 收尾报告：依赖切换 + node 构建链路修复

日期：2026-09-26
授权范围（本轮用户明确授权）：修改/提交/推送 `GodotJS-Dependencies`、修改/提交/推送 `GodotJS-Ext` 依赖源指向。

---

## 1. 目标

把 `GodotJS-Ext` 的 lws / v8 / libnode 下载源指向我方自维护的
`GodotJS-Dependencies` 产物，并让依赖仓把 node（libnode）构建做对；
最终由本仓 CI 验证三个 node 桌面腿（`windows/x86_64`、`linux/x86_64`、`macos/arm64`）。

---

## 2. 动作 + 证据

### 2.1 依赖仓（`Daylily-Zeleen/GodotJS-Dependencies`，分支 `fix/libuv-console-shutdown`）

本轮共 10 个提交（`8f25b57` … `941f7ed`），全部已推送，工作树干净。

| 提交 | 修的问题 | 本地证据 |
|---|---|---|
| `8f25b57` | **thin 归档解析**（linux/android 只合并到第一个成员）+ **padding 长名**（`/0             `）+ v8 unix ccache 检查 grep 错文件 | 见 2.3 |
| `026ff72` | macOS ccache 用 `xcrun --find clang` 绝对路径 → 绕过 SDK 解析 → node 的 OpenSSL 探针失败 → gyp 静默丢 `ncrypto_engine` | CI 日志实测 |
| `c49e1f6` | `verify_symbols.py` 与 `merge_libnode` 是**同一格式的两份实现**（同样带上述缺陷）→ 委派单一实现 | 双向回归 |
| `2eb0d2c` | windows node 腿也接入 openssl 配置断言 | 该腿历史 0 次探针警告 → no-op |
| `c5c9591` | macOS/iOS 的 `node.target.mk` 用 `$(builddir)/libX.a`（非 linux 的 `$(obj).target/`） | flavor 矩阵测试 |
| `ec39bf5` | macOS 归档的 `__.SYMDEF`/`__.SYMDEF SORTED` 被当普通成员 → libtool 丢弃 → 成员数 3630→3594 | 合成 macOS 形状归档 |
| `5d22397` | **v8 静态构建的 TLS 模型**：linux/macos 默认 `local-exec` → `R_X86_64_TPOFF32` 无法用于共享库 | 见 2.4 |
| `5ec7d44` | TLS 探针缺 `-std=c++20`（`v8config.h` 会 `#error`） | CI 19 秒内暴露 |
| `9d3b068` | mksnapshot 重定向按形状而非单字面量匹配 | 四态 + 幂等 |
| `941f7ed` | **真正**的 mksnapshot 启动行（二进制是第一个参数，路径 rebase 过；旧匹配永不成立） | 见 2.5 |

### 2.2 依赖仓全量构建（run `36152717104`，commit `941f7ed`）

**30 / 30 job success**，包含 `publish`：

| 组件 | 结果 |
|---|---|
| node | **5/5 success**：windows / linux / macos / android / ios |
| lws | **10/10 success** |
| v8 | **10/10 success** |

发布的正式 release：`260925-node-final`（另有 `ci-36152717104` 逐平台包）
资产：`v8_12.4.254.21.zip` 199,329,079 B / `lws_4.3.zip` 10,153,838 B /
`node_v24.x.zip` 906,450,044 B / `SHA256SUMS.txt`

node leg 逐项闸门（run 36152717104，linux 腿实测日志）：

```
ICU configuration passed: selected-locales-full-break-v1
OpenSSL configuration passed: version 0x3050008f >= 0x3000000f
v8 TLS configuration passed: V8_TLS_MODEL "local-dynamic" (library mode 1)
link set source: out/node.target.mk (37 archives)
merged archive written: .../linux/x86_64/libnode.a (245,676,608 bytes, 3597 members from 37 archives)
node archive shape passed (3597 members)
node coverage validation passed (9 cross-library markers)
node link validation passed (links whole-archive into a shared object)
node RTTI validation passed (Delegate RTTI symbols)
```

macOS 腿同理通过，产物 168,015,880 B / 3593 成员（与旧 Moluopro 参考版 ~172 MB 量级一致，
证 `patch_debug_info.py` 生效）；android 腿同样产出（`merge_libnode` 报
`out/Release/obj.target/deps/histogram/libhistogram.a: thin member /0 ... is missing` 的
那个错误已消失）。

### 2.3 解析器修复的本地证据

- 合成 thin 归档（两种长名方言）：修复前 **1/3** 成员且名字为 `'/0             '`
  （与 CI 报错**逐字节一致**：`2f30` + 13×`20`）；修复后 3/3，`payload_len` 精确
- 真实 `ar crsT` 归档：修复前 1/2 成员 → 修复后 2/2
- 端到端 `merge_libnode.py` CLI（gyp 形状 thin 输入 + 重名 `sweeper.o`）：
  5 成员、重名两份都保留、**逐字节一致**
- 厚归档无回归：CI 产出 `libnode.lib` 3,650 成员、moluopro 3,641 成员
- macOS `__.SYMDEF`：合成归档（2 目标 + 2 SYMDEF）修复前 4 成员/后被 libtool 丢弃，
  修复后恰好 2 成员
- makefile flavor 矩阵：linux 拼写、mac 拼写、混合多 target 的 gyp 风格 makefile
  （只取被引用者）

### 2.4 v8 TLS 模型（本轮最深的坑）

`deps/v8/src/common/thread-local-storage.h`：

```
#if defined(COMPONENT_BUILD) || defined(V8_TLS_USED_IN_LIBRARY)
#define V8_TLS_LIBRARY_MODE 1
#if V8_TLS_LIBRARY_MODE      -> "local-dynamic"
#else win->"initial-exec"; android->"local-dynamic"; else -> "local-exec"
```

`v8::internal::g_current_isolate_` / `g_current_local_heap_` 带
`__attribute__((tls_model(V8_TLS_MODEL)))`；static 构建在 linux/macos 得到 local-exec，
其重定位不能进共享库。node 只在 `node_shared=="true"` 时才定义该宏。

证据链（全部可复现）：
- 预处理真实头文件：现状 `"local-exec"`；加 define 后 `"local-dynamic"`（MODE 0→1）
- 上游参考包（moluopro 24.18.0）的 linux `api.o` **0 个 TLS 重定位**
- 参考归档**通过**同一条整档 `.so` 链接测试，而我们的失败 → 门禁正确、归档有错
- 修复后 CI：linux 腿 `node link validation passed`

### 2.5 v8 windows-arm64 mksnapshot 重定向

从 v8 12.4.254.21 的 `BUILD.gn` 读真身：`run_mksnapshot` 模板设 `script = "tools/run.py"`，
把二进制作为**第一个参数**并 rebase 到 build dir，生成行形如
`../../tools/run.py ../../out.gn/arm64.release/mksnapshot --turbo_...` ——
**没有**紧贴脚本路径的 `mksnapshot` 字面量，故前两版匹配永不成立。
修复后 CI（run `36140708974`）：`mksnapshot reroute: patched=2`，两阶段完成，job success。
本地四态 + 参数/二进制路径逐字节保留 + 幂等：ALL PASS。

### 2.6 本仓（`GodotJS-Ext`）

- `SConstruct`：`deps_release_tag` → `260925-node-final`（提交 `ff8f3c5`，已推送 `main`）
- 切换前实测三资产 URL 均 **HTTP 200**，尺寸与 release 一致
- 8 项「资产名 / 顶层目录 / 库路径 / 头文件」契约逐平台核验通过
  （`v8/linux.x86_64.release`、`v8/windows_x86_64_release`、`lws/linux_x86_64_release`、
  `libnode/{linux/x86_64,macos/arm64,android/arm64,ios/arm64,windows/x64}`）
- 本仓 CI（run `36180801109`）：23 success / 2 failure / 5 skipped；
  `Build (windows, x86_64, editor, node)` **success**
- spec `.trellis/spec/godotjs-ext/build/dependencies.md` 已补：thin 归档语义、padding 长名、
  BSD SYMDEF、makefile flavor 拼写、v8 TLS 模型、断言脚本与两条 ccache 陷阱

---

## 3. 最终状态

| 项 | 状态 |
|---|---|
| 依赖仓 node 构建（5 平台） | ✅ 全绿，已发布 `260925-node-final` |
| 依赖仓 lws（10 腿） | ✅ 全绿 |
| 依赖仓 v8（10 腿） | ✅ 全绿 |
| 本仓指向新 release | ✅ `ff8f3c5` 已推送 |
| 本仓 node windows 腿 | ✅ success |
| 本仓 node linux / macos 腿 | ❌ 仍失败（**既有缺陷，非本轮引入**，见下） |
| `09-06-lowprio-lws-pic` | ✅ 已归档（含 CI 实测 PIC 校验通过的报告） |
| `09-06-libnode-typeinfo-symbols` | ⏸ 未归档（macOS 仍失败），已写入符号级根因与下一步 |

---

## 4. 遗留（两条，均已定位到根因，不在本轮授权范围内实施）

### 4.1 macOS / linux node 腿（**既有失败**）

证据：切换依赖**之前**的 commit `ee451d2`（run `36021766774`）里三条 node 腿同样 failure，
macOS 报同一组符号。故非本次升级引入。

**(A) macOS：`typeinfo for v8::Value{De,}Serializer::Delegate` 缺失**
对 release 归档逐成员扫描：
- `_ZTIN2v815ValueSerializer8DelegateE` 存在（4 处，位于 node_serdes.o / node_messaging.o
  等**引用方** TU）
- `_ZTIN2v816ValueDeserializer8DelegateE` **0 处**（整档不存在）
- `vtable for v8::ValueSerializer::Delegate` 存在；8 个 Delegate out-of-line 虚函数为 `T`

即两个 Delegate 的 key-function 归属不对称，`ValueDeserializer` 的元数据无人发出，
本仓子类 typeinfo 无处解析。**注意**：依赖仓 `verify_symbols.py` 目前只断言 MSVC 标记 +
out-of-line 虚函数，因此**归档缺 Itanium typeinfo 时 CI 仍报 success** —— 这是该问题长期
未被发现的原因。

**(B) Linux：`__extendhfdf2` 未定义**
归档内 `print.o` 引用、全档未定义。它是 libgcc helper（`_Float16`→`double`），
**GCC 12 起才提供**（本机 gcc-15 的 `libgcc.a` 实测含 `extendhfdf2.o`）。
依赖仓用 gcc-12 构建，本仓 linux 腿用 ubuntu-22.04 默认 `g++`(11) 链接 → 解析不到。

建议（需按平台回归）：依赖仓为该 translation unit 保留 Delegate 类型元数据
（如显式 visibility）+ 扩 `verify_symbols.py` 断言；linux 侧或让依赖仓避免生成
`_Float16` 代码，或本仓 linux 腿改用 `gcc-12/g++-12` 链接。

### 4.2 运行时验证

`09-06-lowprio-lws-pic` 的「Linux websocket 功能恢复」属运行时验收，构建链路已通但未手测。

---

## 5. 验证方式汇总（本报告所有断言出处）

- CI：run `36152717104`（依赖全量，30/30）、`36140708974`（v8 windows-arm64）、
  `36180801109`（本仓，依赖切换后）、`36021766774`（本仓，切换前对照）
- 本地：`.agent_tmp/` 下 `thin_synth_test.py`、`thick_regress_test.py`、`mk_flavor_test.py`、
  `symdef_test.py`、`thin_e2e_test.py`、`merge_cli_test.sh`、`tls_patch_test.py`、
  `reroute_test.ps1`、`release_contract.py`、`pic_gate_check.sh`、`linux_symbol_probe.sh`、
  `typeinfo_probe.sh`、`tls_macro_proof.sh`
- 过程记录：`.agent_tmp/progress.md`
