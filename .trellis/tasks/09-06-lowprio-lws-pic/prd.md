# 自建 PIC 版 lws 恢复 Linux websocket

> 来源：`.本地文档/低优先级.md` lws 条目（P3，构建/上游重打包）。

## Goal

解决 Linux 预编译 `libwebsockets.a` 非 PIC 无法链入共享库的问题（当前临时处理：Linux 禁用 lws，v8 debugger 的 websocket 功能缺失）。

## Background（已核实）

- `third/lws`（godotjs/GodotJS-Dependencies release 1.1）的 Linux 静态库没用 `-fPIC` 编译，链入共享库报 `R_X86_64_PC32 against stderr@@GLIBC_2.2.5`
- `-Wl,-z,notext` 实测无效（2026-08-06）：非 PIC 代码对**动态符号**的 PC32 引用必须改 GOT 寻址，notext 只放宽 text 段内数据重定位
- lws 官方 CMake 默认带 `-fPIC`（`CMAKE_POSITION_INDEPENDENT_CODE` 对静态库默认 ON）——是上游打包流程丢失了该选项
- macOS/Windows 的 lws 是 PIC 的，不受影响

## Requirements

1. 新建构建 workflow（可 fork `ialex32x/GodotJS-Dependencies`，MIT；参考其 `build_lws_linux_x64.yml`）：多平台 matrix
2. Linux 构建：`git clone https://libwebsockets.org/repo/libwebsockets && git checkout v4.3-stable && cmake -DLWS_WITH_SSL=0 .. && cmake --build . --config Release`
3. 打包 `out/include/` + `out/lib/libwebsockets.a` 为 zip（目录结构与 release 1.1 约定一致：`lws/linux_x86_64_release/`）
4. 上传本项目 GitHub Release（如 tag `lws_4.3_pic`）；`SConstruct` 的 `deps_lws_url`/`deps_lws_version` 指向它
5. 恢复 `SConstruct` 中 linux 的 lws 链接分支（删除 `jsb_platform != "linux"` 判断）

## Acceptance Criteria

- [ ] PIC 版 lws artifact 发布并可下载
- [ ] Linux 构建链入 lws 成功（无重定位错误）
- [ ] Linux 上 v8 debugger websocket 功能恢复（手测或 CI 冒烟）
- [ ] CI linux leg 全绿

## Notes

- 详见 `.trellis/spec/godotjs-ext/build/scons-build.md`（lws 禁用现状）
