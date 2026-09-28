/************************************************************************/
/*  test_jsb_process.h                                                  */
/*                                                                      */
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// `jsb::internal::Process` 的双向管道：写 stdin 一行 → 从 stdout 收到一行。
//
// 常驻编辑器工具进程（函数/信号签名提取 + 帮助文档提取）依赖这条通道，而它此前只有单向
// stdout 读取（09-26 为一次性子进程加的）。这里的用例**起一个真实的 node 回声子进程**，
// 断言三件事：
//  1. `write_stdin()` 的一行确实走子进程 stdin，并被子进程处理后从 stdout 回来；
//  2. 读取线程会把完整行交给 `set_line_callback()` 注册的回调（**每条一行**，不粘连）；
//  3. `stop()` 之后 `write_stdin()` 返回 false（宿主一侧不能继续往死管道里写）。
//
// 之所以不 mock：这条路径的全部风险都在真实 OS 管道语义（Handle/fd 继承、行分帧、关闭顺序），
// mock 掉就等于不测。

#include "../internal/jsb_path_util.h"
#include "../internal/jsb_process.h"
#include "../tests/jsb_test_utils.h"

#include <atomic>
#include <mutex>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/vector.hpp>

#if defined(WINDOWS_ENABLED) || (defined(UNIX_ENABLED) && !defined(__EMSCRIPTEN__))

namespace jsb::tests {

namespace process_fixture {

/** 回声脚本：每收到一行 stdin，就回一行 `echo:<原文>`。 */
constexpr const char *kEchoScriptName = "jsb_process_echo_probe.js";

constexpr const char *kEchoScript = R"JS(
const readline = require("readline");
const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line) => { process.stdout.write("echo:" + line + "\n"); });
)JS";

/**
 * 夹具：把回声脚本落到 `user://`，返回它的**绝对**路径。
 *
 * 用 `user://` 而不是 `res://`：测试运行期间项目目录会被 EditorFileSystem 扫描，
 * 落一个 `.js` 进去会污染资源树并可能被当成脚本资源。`user://` 在引擎之外，但仍是
 * 引擎能 globalize 的真实路径。
 */
struct EchoScript {
	String absolute_path;
	String res_path;

	EchoScript() {
		res_path = String("user://").path_join(kEchoScriptName);
		const Ref<FileAccess> file = FileAccess::open(res_path, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string(kEchoScript);
			absolute_path = jsb::internal::PathUtil::to_platform_specific_path(res_path);
		}
	}

	~EchoScript() {
		if (!res_path.is_empty()) {
			DirAccess::remove_absolute(res_path);
		}
	}
};

/** 收集回调行的线程安全容器（回调在读取线程上运行）。 */
struct LineCollector {
	std::mutex mutex;
	Vector<String> lines;

	void push(const String &p_line) {
		std::lock_guard<std::mutex> lock(mutex);
		lines.push_back(p_line);
	}

	bool contains(const String &p_line) {
		std::lock_guard<std::mutex> lock(mutex);
		return lines.has(p_line);
	}

	int count() {
		std::lock_guard<std::mutex> lock(mutex);
		return lines.size();
	}
};

/** 有界等待：最多 `p_timeout_msec`，每 10ms 轮询一次判据。 */
template <typename Predicate>
bool wait_until(Predicate p_predicate, int p_timeout_msec = 10000) {
	const uint64_t deadline = godot::Time::get_singleton()->get_ticks_msec() + (uint64_t)p_timeout_msec;
	while (godot::Time::get_singleton()->get_ticks_msec() < deadline) {
		if (p_predicate()) {
			return true;
		}
		OS::get_singleton()->delay_msec(10);
	}
	return p_predicate();
}

} //namespace process_fixture

TEST_CASE("[runtime] [jsb.process] stdin write is echoed back through the stdout line callback") {
	using namespace process_fixture;

	EchoScript script;
	// 前置失败必须显式失败，否则下面会退化成"进程起不来 ⇒ 断言全部空转"。
	REQUIRE(!script.absolute_path.is_empty());
	REQUIRE(FileAccess::file_exists(script.res_path));

	LineCollector collector;

#	ifdef WINDOWS_ENABLED
	const String exe_path = "node.exe";
#	else
	const String exe_path = "node";
#	endif

	Vector<String> args;
	args.push_back(script.absolute_path);
	std::shared_ptr<jsb::internal::Process> process = jsb::internal::Process::create("process_probe", exe_path, args);
	REQUIRE(process != nullptr);
	REQUIRE(process->is_running());

	process->set_line_callback([&collector](const String &p_line) { collector.push(p_line); });

	// 两条请求：既证明单次往返成立，也证明**行分帧**正确（不把两行粘在一起）。
	REQUIRE(process->write_stdin("ping"));
	REQUIRE(process->write_stdin("pong"));

	CHECK_MESSAGE(wait_until([&collector] { return collector.contains("echo:ping"); }),
			"the child must echo the line written to its stdin");
	CHECK_MESSAGE(wait_until([&collector] { return collector.contains("echo:pong"); }),
			"a second line must round-trip as well");
	CHECK_MESSAGE(collector.count() == 2,
			"lines must be framed one-per-callback (got ",
			collector.count(),
			" lines)");

	// 关闭后宿主一侧不能再写（管道已归零）。
	process->stop();
	CHECK(!process->write_stdin("after-stop"));
	CHECK(!process->is_running());
}

TEST_CASE("[runtime] [jsb.process] a child that never reads stdin still yields a clean stop") {
	using namespace process_fixture;

	// 与常驻工具进程的崩溃路径同形：子进程不做任何 stdin 处理，宿主写完就收尾。
	// 关节点的风险是"写端未关 ⇒ 读取线程 ReadFile 永久阻塞 ⇒ join 挂死"，本用例覆盖它。
	EchoScript script;
	REQUIRE(!script.absolute_path.is_empty());

	LineCollector collector;

#	ifdef WINDOWS_ENABLED
	const String exe_path = "node.exe";
#	else
	const String exe_path = "node";
#	endif

	Vector<String> args;
	args.push_back("-e");
	args.push_back("setTimeout(() => {}, 30000)");
	std::shared_ptr<jsb::internal::Process> process = jsb::internal::Process::create("process_idle_probe", exe_path, args);
	REQUIRE(process != nullptr);
	REQUIRE(process->is_running());
	process->set_line_callback([&collector](const String &p_line) { collector.push(p_line); });

	// 子进程不会读 stdin，但管道缓冲区足够大，写这一行不会阻塞。
	CHECK(process->write_stdin("ignored"));

	process->stop(); // 必须能返回：写入端关闭 + 子进程被终止 + 读取线程 join
	CHECK(!process->is_running());
}

} //namespace jsb::tests

#endif // WINDOWS_ENABLED || (UNIX_ENABLED && !__EMSCRIPTEN__)
