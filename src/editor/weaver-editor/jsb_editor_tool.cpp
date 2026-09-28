/************************************************************************/
/*  jsb_editor_tool.cpp                                                 */
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

#include "jsb_editor_tool.h"

#if JSB_USE_TYPESCRIPT

#	include "jsb_editor_plugin.h"

#	include <godot_cpp/classes/file_access.hpp>
#	include <godot_cpp/classes/json.hpp>
#	include <godot_cpp/classes/project_settings.hpp>
#	include <godot_cpp/classes/time.hpp>

#	include <chrono>

namespace jsb::editor {

namespace {
// 常驻工具进程的入口产物名（安装进项目数据目录）。与 `JSB_EDITOR_TOOL_NAME` 同一定义。
constexpr const char *kToolName = JSB_EDITOR_TOOL_NAME;
} //namespace

EditorToolClient::EditorToolClient() {
	// 刻意不依赖 `GodotJSEditorPlugin::get_singleton()`：本类只用项目路径与 `Process`，
	// 而测试套件（`--jsb-run-tests`）在插件 NOTIFICATION_READY 之前就会构造它。
}

EditorToolClient::~EditorToolClient() {
	// 析构必须收尾：读取线程持有 `Process` 裸指针，未 join 就销毁 `ProcessImpl` 是 use-after-free
	// （`Process::stop()` 本身幂等）。
	stop();
}

bool EditorToolClient::start(const String &p_project_root) {
	project_root_ = p_project_root;
	if (process_ != nullptr) {
		return true;
	}
	if (!_spawn()) {
		return false;
	}
	// 启动时先握手：确认它真的在工作（`require("typescript")` 失败之类会在此时暴露）。
	// 失败不算致命：调用方的下一次触发会再试，而握手本身已经把错误打到日志里了。
	Dictionary response;
	request("ping", Dictionary(), response);
	return true;
}

bool EditorToolClient::_spawn() {
	const String tool_res_path = "res://" + jsb::internal::settings::get_project_data_dir_name().path_join(kToolName);
	if (!FileAccess::file_exists(tool_res_path)) {
		// 项目文件尚未安装。不报错：`try_install_project_files` 完成后会再触发。
		JSB_LOG(Verbose, "editor tool not installed at '%s', skipped", tool_res_path);
		return false;
	}

#	ifdef WINDOWS_ENABLED
	const String exe_path = "node.exe";
#	else
	const String exe_path = "node";
#	endif

	Vector<String> args;
	args.push_back(ProjectSettings::get_singleton()->globalize_path(tool_res_path));
	args.push_back("--project");
	args.push_back(project_root_);

	process_ = jsb::internal::Process::create("editor-tools", exe_path, args);
	if (process_ == nullptr) {
		ERR_PRINT("failed to create the editor tool process ('" + exe_path + "')");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		eof_ = false;
		eof_reason_ = String();
	}
	process_->set_line_callback([this](const String &p_line) { _on_line(p_line); });
	process_->set_eof_callback([this] {
		// 读取线程：**不能**在这里 `stop()`（会 join 自己）。只记状态，由请求方处理。
		std::lock_guard<std::mutex> lock(mutex_);
		eof_ = true;
		if (eof_reason_.is_empty()) {
			eof_reason_ = "the process exited (stdout reached EOF)";
		}
		cv_.notify_all();
	});
	return true;
}

void EditorToolClient::stop() {
	// 关键：**必须**走 `Process::stop()`（它 join 读取线程），不能直接 `reset()`。
	// 读取线程持有 `ProcessImpl*` 裸指针，未被 join 就销毁 `ProcessImpl` 是 use-after-free ——
	// 这正是 09-26 为短命进程修过的那一类缺陷（`jsb_process.cpp` 的 `Process::stop()` 注释）。
	_teardown_process();
	std::lock_guard<std::mutex> lock(mutex_);
	eof_ = true;
	responded_.clear();
	cv_.notify_all();
}

/** 收尾旧进程（join 读取线程）并清空句柄。可重复调用；**不得**用 `process_.reset()` 代替。 */
void EditorToolClient::_teardown_process() {
	if (process_ != nullptr) {
		process_->stop();
		process_.reset();
	}
}

void EditorToolClient::_on_line(const String &p_line) {
	if (!_queue_response(p_line)) {
		// 非协议行（工具的诊断/警告走 stderr，但万一漏出来）：只记录，不破坏配对。
		JSB_LOG(VeryVerbose, "[editor-tools] %s", p_line);
	}
}

/** @return true = 这一行是一条合法的应答报文（已配对）。 */
bool EditorToolClient::_queue_response(const String &p_line) {
	// `parse_string` 是静态入口（`parse` 需要实例）。解析失败返回 nil，直接当非协议行。
	const Variant parsed = JSON::parse_string(p_line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary message = parsed;
	if (!message.has("id")) {
		return false;
	}
	const int id = message["id"];
	{
		std::lock_guard<std::mutex> lock(mutex_);
		responded_.insert(id, message);
	}
	cv_.notify_all();
	return true;
}

void EditorToolClient::_on_eof(const String &p_eof_reason) {
	std::lock_guard<std::mutex> lock(mutex_);
	eof_ = true;
	eof_reason_ = p_eof_reason;
	cv_.notify_all();
}

bool EditorToolClient::request(const String &p_op, const Dictionary &p_payload, Dictionary &r_response) {
	// 崩溃恢复：本次请求最多重试一次（重启进程后再来一遍）。
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (process_ == nullptr || !process_->is_running()) {
			if (attempt == 0) {
				// **不静默**：先说清楚发生了什么、接下来做什么（用户裁决）。
				ERR_PRINT("the editor tool process is not running; restarting it and retrying '" + p_op + "'");
			}
			_teardown_process();
			if (!_spawn()) {
				return false;
			}
		}

		int request_id = 0;
		Dictionary payload = p_payload;
		payload["op"] = p_op;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			request_id = next_request_id_++;
			payload["id"] = request_id;
			// 每个 id 只被本次请求消费；上一轮的残留在 `stop()` 里清过。
			responded_.erase(request_id);
		}

		const String line = JSON::stringify(payload);
		if (process_->write_stdin(line)) {
			// 等待应答（上限 5s）。超时或 EOF 都要报出来，不静默返回。
			const uint64_t deadline = Time::get_singleton()->get_ticks_msec() + (uint64_t)kRequestTimeoutMsec;
			std::unique_lock<std::mutex> lock(mutex_);
			for (;;) {
				if (const Dictionary *hit = responded_.getptr(request_id)) {
					r_response = *hit;
					responded_.erase(request_id);
					break;
				}
				if (eof_ || process_ == nullptr || !process_->is_running()) {
					lock.unlock();
					if (attempt == 0) {
						ERR_PRINT("the editor tool process died while handling '" + p_op + "'; restarting it and retrying");
						break; // 进下一次 attempt
					}
					ERR_PRINT("gave up on '" + p_op + "': the editor tool process keeps dying ("
							+ (eof_reason_.is_empty() ? String("no reason reported") : eof_reason_) + ")");
					return false;
				}
				const uint64_t now = Time::get_singleton()->get_ticks_msec();
				if (now >= deadline) {
					lock.unlock();
					ERR_PRINT("the editor tool request timed out after " + itos(kRequestTimeoutMsec) + "ms: op='"
							+ p_op + "', request=" + line
							+ " (the tool process was kept alive; it may still be working on it)");
					return false;
				}
				cv_.wait_for(lock, std::chrono::milliseconds((int)(deadline - now)));
			}
			if (r_response.is_empty()) {
				continue; // 进程死了，走重试
			}
			if (!(bool)r_response.get("ok", false)) {
				// 工具报的业务错误（例如 tsconfig 缺失）：报出来，但不重启、不重试。
				ERR_PRINT("the editor tool request failed: op='" + p_op + "', error=" + String(r_response.get("error", "unknown")));
				return false;
			}
			return true;
		}

		// 写失败 = 管道已断（进程刚死）。attempt 0 时重来一次。
		if (attempt == 0) {
			ERR_PRINT("failed to write to the editor tool process while sending '" + p_op + "'; restarting it and retrying");
			_teardown_process();
			continue;
		}
		ERR_PRINT("failed to write to the editor tool process; gave up on '" + p_op + "'");
		return false;
	}
	return false;
}

} //namespace jsb::editor

#endif // JSB_USE_TYPESCRIPT
