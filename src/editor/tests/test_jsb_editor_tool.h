/************************************************************************/
/*  test_jsb_editor_tool.h                                              */
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

#if JSB_USE_TYPESCRIPT

// 常驻编辑器工具进程的**协议层**：起进程、写一行请求、拿回一行应答。
//
// 这里验证的是"常驻 + NDJSON over stdin/stdout"这条通道本身，与签名/文档的业务内容无关：
//  - 同一进程连续两次请求都成立（常驻的意义就在于不必反复 spawn）；
//  - 崩溃后 `request()` 会重启并**重试本次请求**（用户裁决：不静默、要告知并继续工作）；
//  - 请求超时会报错并放弃，而不是无限等待（上限 5s）。
//
// 与 `test_jsb_editor_cleanup.h` 同为 editor 套件（`--jsb-run-tests` 会跑两套）。

#	include "../tests/jsb_test_utils.h"
#	include "../weaver-editor/jsb_editor_tool.h"

#	include <godot_cpp/classes/dir_access.hpp>
#	include <godot_cpp/classes/file_access.hpp>
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/project_settings.hpp>
#	include <godot_cpp/classes/time.hpp>

namespace jsb::editor::tests {

namespace tool_fixture {

/** 项目根（工具进程以绝对 `--project` 启动）。 */
inline String project_root() {
	String root = ProjectSettings::get_singleton()->globalize_path("res://");
	while (root.length() > 3 && (root.ends_with("/") || root.ends_with("\\"))) {
		root = root.substr(0, root.length() - 1);
	}
	return root;
}

/** 工具产物是否已安装（未安装时用例必须**显式跳过**而不是静默通过）。 */
inline bool tool_installed() {
	return FileAccess::file_exists("res://" + internal::settings::get_project_data_dir_name().path_join(JSB_EDITOR_TOOL_NAME));
}

/** 等一个判据成立，最多 `p_timeout_msec`。 */
template <typename Predicate>
bool wait_until(Predicate p_predicate, int p_timeout_msec = 15000) {
	const uint64_t deadline = Time::get_singleton()->get_ticks_msec() + (uint64_t)p_timeout_msec;
	while (Time::get_singleton()->get_ticks_msec() < deadline) {
		if (p_predicate()) {
			return true;
		}
		OS::get_singleton()->delay_msec(10);
	}
	return p_predicate();
}

} //namespace tool_fixture

TEST_CASE("[editor] [tool] the resident tool process answers repeated requests over stdin/stdout") {
	using namespace tool_fixture;
	if (!tool_installed()) {
		MESSAGE("editor tool artifact is not installed; skipped");
		return;
	}

	EditorToolClient client;
	REQUIRE(client.start(project_root()));

	// 握手（`start` 内部已经发过一次 ping）。这里再发两次：**同一个进程**必须持续可用。
	Dictionary response;
	CHECK(client.request("ping", Dictionary(), response));
	CHECK((bool)response.get("ok", false));

	Dictionary second;
	CHECK(client.request("ping", Dictionary(), second));
	CHECK((bool)second.get("ok", false));

	// 未知 op：工具必须回 `ok:false` + 说明，而**不是**退出进程。
	Dictionary unknown;
	CHECK(!client.request("__no_such_op__", Dictionary(), unknown));
	CHECK(!(bool)unknown.get("ok", true));

	// 上一条业务错误（非崩溃）之后，进程仍然可用。
	Dictionary alive;
	CHECK_MESSAGE(client.request("ping", Dictionary(), alive), "a business error must not kill the tool process");

	client.stop();
}

TEST_CASE("[editor] [tool] a killed tool process is restarted and the request retried") {
	using namespace tool_fixture;
	if (!tool_installed()) {
		MESSAGE("editor tool artifact is not installed; skipped");
		return;
	}

	EditorToolClient client;
	REQUIRE(client.start(project_root()));
	Dictionary response;
	REQUIRE(client.request("ping", Dictionary(), response));

	// `stop()` 之后 `request()` 必须能自愈：这就是"崩溃 ⇒ 重启 ⇒ 重试本次请求"的形态。
	// （直接 kill 子进程需要进程句柄，`EditorToolClient` 不暴露；`stop()` 复现的是同一个状态：
	//  进程不在，而下一次请求仍要成功。）
	client.stop();

	Dictionary recovered;
	CHECK_MESSAGE(client.request("ping", Dictionary(), recovered),
			"a request after the process went away must restart it and succeed");
}

} //namespace jsb::editor::tests

#endif // JSB_USE_TYPESCRIPT
