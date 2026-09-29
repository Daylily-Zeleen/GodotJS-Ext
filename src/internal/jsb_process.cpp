/************************************************************************/
/*  jsb_process.cpp                                                     */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
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

#include "jsb_process.h"
#include "jsb_logger.h"
#include "jsb_path_util.h"
#include "jsb_thread_util.h"

#if defined(WINDOWS_ENABLED) || (JSB_GDEXTENSION && defined(WIN32))
#	define WIN32_LEAN_AND_MEAN
#	include <dwrite.h>
#	include <dwrite_2.h>
#	include <windows.h>
#	include <windowsx.h>
#endif // WINDOWS_ENABLED

#if defined(UNIX_ENABLED)

#	include <errno.h>
#	include <signal.h>
#	include <sys/wait.h>
#	include <unistd.h>

#endif // UNIX_ENABLED

#include <godot_cpp/classes/thread.hpp>

#define JSB_PROCESS_LOG(Severity, Format, ...) JSB_LOG_IMPL(JSProcess, Severity, Format, ##__VA_ARGS__)

// On macOS (LP64) `uintptr_t` is `unsigned long`, which godot-cpp has no GetTypeInfo specialization for.
// Use `uint64_t` there (equivalent to `uintptr_t` on the other supported platforms).
#if defined(MACOS_ENABLED) || defined(IOS_ENABLED)
using process_thread_param_t = uint64_t;
#elif defined(WEB_ENABLED)
using worker_thread_param_t = uint32_t;
#else
using process_thread_param_t = uintptr_t;
#endif

namespace jsb::internal {
#if defined(WINDOWS_ENABLED)
class ProcessImpl : public Process {
	struct ProcessInfo {
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
	} pi = {};

	String proc_name;
	HANDLE rd_pipe = nullptr;
	Vector<char> rd_line;
	Ref<Thread> thread;
	volatile bool is_closing = false;
	// int bytes_in_buffer = 0;

	// OS::ProcessID pid = 0;
	// HANDLE pipe[] = { nullptr, nullptr };
	HANDLE wr_pipe = nullptr; // our end of the child's stdin (host -> child)

	String _quote_command_line_argument(const String &p_text) const {
		for (int i = 0; i < p_text.length(); ++i) {
			const char32_t c = p_text[i];
			if (c == ' ' || c == '&' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '=' || c == ';' || c == '!' || c == '\'' || c == '+' || c == ',' || c == '`' || c == '~') {
				return "\"" + p_text + "\"";
			}
		}
		return p_text;
	}

	void _flush() {
		if (rd_line.is_empty()) return;

		// Decode the raw bytes into a String.
		//
		// Node (and tsc) always write UTF-8, so UTF-8 is tried first and the ANSI code page is only
		// the fallback for a child whose encoding is unknown. Validating UTF-8 is done by
		// round-trip: `String::utf8` substitutes U+FFFD for malformed sequences, so a malformed
		// input cannot re-encode to its own bytes. Doing the check matters because this same line
		// now feeds a protocol (`Process::set_line_callback`), not just a log message.
		const String utf8_text = String::utf8(rd_line.ptr(), rd_line.size());
		const CharString utf8_round_trip = utf8_text.utf8();
		const bool is_utf8 = utf8_round_trip.length() == rd_line.size()
				&& memcmp(utf8_round_trip.get_data(), rd_line.ptr(), rd_line.size()) == 0;

		String output;
		if (is_utf8) {
			output = utf8_text;
		} else {
			// Fallback: the historical default-ANSI-code-page conversion.
			LocalVector<wchar_t> buffer;
			const int num = MultiByteToWideChar(CP_ACP, 0, rd_line.ptr(), rd_line.size(), nullptr, 0);
			if (num > 0) {
				// `MultiByteToWideChar` only writes a terminator when the input length is -1, and
				// this call passes an explicit length -- so the terminator must be written here.
				// Without it `String(buffer.ptr())` reads past the conversion and every line picks
				// up trailing garbage from the uninitialized tail.
				buffer.resize(num + 1);
				if (MultiByteToWideChar(CP_ACP, 0, rd_line.ptr(), rd_line.size(), buffer.ptr(), num) == 0) {
					buffer.clear();
				} else {
					buffer[num] = 0;
				}
			}
			output = buffer.is_empty() ? utf8_text : String(buffer.ptr());
		}

		if (!output.is_empty()) {
			JSB_PROCESS_LOG(Log, "[%s] %s", proc_name, output);
			// The line is consumed here and now: `rd_line` is cleared right below and lives on the
			// reader thread only, so a callee must copy what it needs.
			emit_line(output);
		}
		rd_line.clear();
	}

	virtual Error on_start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) override {
		const String path = p_path.replace("/", "\\");
		String command = _quote_command_line_argument(path);
		HANDLE pipe[2] = { nullptr, nullptr };
		HANDLE in_pipe[2] = { nullptr, nullptr };
		for (const String &E : p_arguments) {
			command += " " + _quote_command_line_argument(E);
		}

		// ProcessInfo pi;
		ZeroMemory(&pi.si, sizeof(pi.si));
		pi.si.cb = sizeof(pi.si);
		ZeroMemory(&pi.pi, sizeof(pi.pi));
		LPSTARTUPINFOW si_w = (LPSTARTUPINFOW)&pi.si;

		{
			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = true;
			sa.lpSecurityDescriptor = nullptr;

			ERR_FAIL_COND_V(!CreatePipe(&pipe[0], &pipe[1], &sa, 0), ERR_CANT_FORK);
			ERR_FAIL_COND_V(!SetHandleInformation(pipe[0], HANDLE_FLAG_INHERIT, 0), ERR_CANT_FORK); // Read handle is for host process only and should not be inherited.

			// stdin: the child gets the read end, we keep the write end (not inheritable, same rule).
			if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0)) {
				CloseHandle(pipe[0]);
				CloseHandle(pipe[1]);
				ERR_FAIL_V(ERR_CANT_FORK);
			}
			if (!SetHandleInformation(in_pipe[1], HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(in_pipe[0]);
				CloseHandle(in_pipe[1]);
				CloseHandle(pipe[0]);
				CloseHandle(pipe[1]);
				ERR_FAIL_V(ERR_CANT_FORK);
			}

			pi.si.dwFlags |= STARTF_USESTDHANDLES;
			pi.si.hStdOutput = pipe[1];
			pi.si.hStdError = pipe[1];
			pi.si.hStdInput = in_pipe[0];
		}
		// constexpr DWORD creation_flags = IDLE_PRIORITY_CLASS | CREATE_NO_WINDOW;
		constexpr DWORD creation_flags = NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
		int ret = CreateProcessW(nullptr, (LPWSTR)(command.utf16().ptrw()), nullptr, nullptr, true, creation_flags, nullptr, nullptr, si_w, &pi.pi);
		if (!ret) {
			CloseHandle(pipe[0]); // Cleanup pipe handles.
			CloseHandle(pipe[1]);
			CloseHandle(in_pipe[0]);
			CloseHandle(in_pipe[1]);
		}
		ERR_FAIL_COND_V_MSG(ret == 0, ERR_CANT_FORK, "Could not create child process: " + command);
		CloseHandle(pipe[1]);
		CloseHandle(in_pipe[0]); // the child owns the read end now
		rd_pipe = pipe[0];
		wr_pipe = in_pipe[1];
		proc_name = p_name;
		{
			//TODO use async io instead of threading;
			thread.instantiate();
			thread->start(callable_mp_static(&ProcessImpl::_thread_run).bind(reinterpret_cast<process_thread_param_t>(this)), Thread::PRIORITY_LOW);
		}
		return OK;
	}

	// static void _thread_run(void* p_userdata)
	static void _thread_run(process_thread_param_t p_userdata) {
		ProcessImpl *impl = reinterpret_cast<ProcessImpl *>(p_userdata);
		int start_state = 0;
		char buffer[4096];

		ThreadUtil::set_name(impl->proc_name);
		while (!impl->is_closing) {
			// Read StdOut and StdErr from pipe.
			DWORD read;
			if (!ReadFile(impl->rd_pipe, (LPVOID)buffer, std::size(buffer), &read, NULL)) {
				break;
			}

			// Assume that all possible encodings are ASCII-compatible.
			// Break at newline to allow receiving long output in portions.
			for (DWORD i = 0; i < read; ++i) {
				if (start_state == 0) {
					if (buffer[i] == '\x1b') {
						start_state = 1;
						continue;
					}
					start_state = 2;
				} else if (start_state == 1) {
					start_state = 2;
					if (buffer[i] == 'c') {
						continue;
					}
				}

				if (buffer[i] == '\n' || buffer[i] == '\r') {
					impl->_flush();
					start_state = 0;
				} else {
					impl->rd_line.push_back(buffer[i]);
				}
			}
		}
		JSB_PROCESS_LOG(Verbose, "[%s] closed", impl->proc_name);
		impl->emit_eof();
	}

	virtual bool _write_stdin(const String &p_text) override {
		if (wr_pipe == nullptr) {
			return false;
		}
		const String line = p_text + String("\n");
		const CharString utf8 = line.utf8();
		DWORD written = 0;
		if (!WriteFile(wr_pipe, utf8.get_data(), (DWORD)utf8.length(), &written, nullptr)) {
			return false;
		}
		return written == (DWORD)utf8.length();
	}

	virtual bool _is_running() const override {
		if (is_closing || pi.pi.hProcess == nullptr) {
			return false;
		}
		DWORD dw_exit_code = 0;
		if (!GetExitCodeProcess(pi.pi.hProcess, &dw_exit_code)) {
			return false;
		}
		if (dw_exit_code != STILL_ACTIVE) {
			return false;
		}
		return true;
	}

	virtual void on_stop() override {
		// 先判活再置 `is_closing`：`_is_running()` 会读 `is_closing`，顺序反了会让活进程永不被终止，
		// 子进程不死 ⇒ 管道不关 ⇒ 读取线程的 ReadFile 永久阻塞 ⇒ 下面的 join 挂死。
		// 进程已自行退出时跳过 `TerminateProcess`（句柄可能已无效）。
		const bool was_running = _is_running();
		is_closing = true;
		if (was_running) {
			JSB_PROCESS_LOG(Verbose, "[%s] terminating...", proc_name);
			TerminateProcess(pi.pi.hProcess, 0);
		}
		// 关闭后置空：`stop()` 可被重复调用（如 kill + 析构），必须幂等。
		if (pi.pi.hProcess != nullptr) {
			CloseHandle(pi.pi.hProcess);
			pi.pi.hProcess = nullptr;
		}
		if (pi.pi.hThread != nullptr) {
			CloseHandle(pi.pi.hThread);
			pi.pi.hThread = nullptr;
		}
		if (rd_pipe != nullptr) {
			CloseHandle(rd_pipe);
			rd_pipe = nullptr;
		}
		if (wr_pipe != nullptr) {
			// Closing our end is what makes the child see EOF on stdin.
			CloseHandle(wr_pipe);
			wr_pipe = nullptr;
		}
		// 启动失败的进程没有读取线程（见 on_start 的提前返回）。
		if (thread.is_valid() && thread->is_started()) {
			thread->wait_to_finish();
		}
		JSB_PROCESS_LOG(Log, "[%s] terminated", proc_name);
	}
};
#elif defined(UNIX_ENABLED) && !defined(__EMSCRIPTEN__)
// 已在 Linux 覆盖：`src/runtime/tests/test_jsb_process.h` 的两个用例在
// `WINDOWS_ENABLED || (UNIX_ENABLED && !__EMSCRIPTEN__)` 下都注册，而 CI 的 test job
// 有 `ubuntu-22.04` 的 host-v8 / host-qjs / host-node 三条腿跑 `--jsb-run-tests`
// （`.github/workflows/ci.yml:993-1019`）。用例覆盖 stdin 往返、行分帧，以及
// 「子进程不读 stdin 时 stop() 不挂死」这条与常驻工具进程同形的崩溃路径。
class ProcessImpl : public Process {
	String proc_name;
	// `-1` 而非 `0`：`0` 是 stdin，若 `pipe()` 失败（on_start 提前返回、无读取线程）
	// 会让任何按 fd 有效性判定的清理路径误关标准输入。
	int pipefd[2] = { -1, -1 };
	// 子进程 stdin 的宿主写端与子进程读端（`-1` 同因：0 是 stdin）。
	int in_fd[2] = { -1, -1 };
	pid_t child_id_ = -1;
	Ref<Thread> thread;
	bool is_closing = false;
	Vector<char> rd_line;

public:
	ProcessImpl() : Process() {}

	virtual Error on_start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) override {
		proc_name = p_name;
		if (pipe(pipefd) == -1) {
			return ERR_CANT_CREATE;
		}
		if (pipe(in_fd) == -1) {
			close(pipefd[0]);
			close(pipefd[1]);
			pipefd[0] = pipefd[1] = -1;
			return ERR_CANT_CREATE;
		}

		child_id_ = fork();
		if (child_id_ < 0) {
			close(pipefd[0]);
			close(pipefd[1]);
			close(in_fd[0]);
			close(in_fd[1]);
			pipefd[0] = pipefd[1] = in_fd[0] = in_fd[1] = -1;
			return ERR_CANT_FORK;
		}

		if (child_id_ == 0) {
			setsid();

			Vector<CharString> builder;
			builder.push_back(p_path.utf8());
			for (auto it = p_arguments.begin(); it != p_arguments.end(); ++it) {
				builder.push_back(it->utf8());
			}

			Vector<char *> args;
			for (int i = 0; i < builder.size(); ++i) {
				args.push_back((char *)builder[i].get_data());
			}
			args.push_back(0);

			close(pipefd[0]);
			dup2(pipefd[1], STDOUT_FILENO);
			close(pipefd[1]);
			// stdin：读端给子进程，写端留在宿主。
			close(in_fd[1]);
			dup2(in_fd[0], STDIN_FILENO);
			close(in_fd[0]);
			execvp(args[0], &args[0]);
			JSB_PROCESS_LOG(Error, "failed to create process %s", p_path);
			raise(SIGKILL);
		}

		close(pipefd[1]);
		close(in_fd[0]); // 读端归子进程
		in_fd[0] = -1;
		{
			thread.instantiate();
			thread->start(callable_mp_static(&ProcessImpl::_thread_run).bind(reinterpret_cast<process_thread_param_t>(this)), Thread::PRIORITY_LOW);
		}
		return OK;
	}

	static void _thread_run(process_thread_param_t p_userdata) {
		ProcessImpl *impl = reinterpret_cast<ProcessImpl *>(p_userdata);
		char buffer[4096];
		int start_state = 0;

		ThreadUtil::set_name(impl->proc_name);
		while (!impl->is_closing) {
			ssize_t bytes_read = 0;
			while ((bytes_read = read(impl->pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
				for (ssize_t i = 0; i < bytes_read; ++i) {
					if (start_state == 0) {
						if (buffer[i] == '\x1b') {
							start_state = 1;
							continue;
						}
						start_state = 2;
					} else if (start_state == 1) {
						start_state = 2;
						if (buffer[i] == 'c') {
							continue;
						}
					}

					if (buffer[i] == '\n' || buffer[i] == '\r') {
						impl->_flush();
					} else {
						impl->rd_line.push_back(buffer[i]);
					}
				}
			}
			if (bytes_read < 0 && errno != EINTR) {
				JSB_PROCESS_LOG(Error, "[%s] failed to read pipe", impl->proc_name);
				break;
			}
		}
		close(impl->pipefd[0]);
		impl->pipefd[0] = -1;

		int status;
		waitpid(impl->child_id_, &status, 0);
		JSB_PROCESS_LOG(Verbose, "[%s] closed (%d)", impl->proc_name, WEXITSTATUS(status));
		impl->emit_eof();
	}

	void _flush() {
		if (rd_line.is_empty()) return;
		const String line = String::utf8(rd_line.ptr(), rd_line.size());
		if (!line.is_empty()) {
			JSB_PROCESS_LOG(Log, "[%s] %s", proc_name, line);
			// 行在此就地消费：`rd_line` 紧接着清空且只属于读取线程，回调必须立即复制。
			emit_line(line);
		}
		rd_line.clear();
	}

	virtual bool _write_stdin(const String &p_text) override {
		//NOTE 不对 `in_fd[1]` 加锁：它只在 `on_start` 里写一次、在 `on_stop` 里清一次，
		//     而调用方（编辑器工具管理器）在自己的互斥锁内串行化请求。
		if (in_fd[1] < 0) {
			return false;
		}
		const CharString utf8 = (p_text + String("\n")).utf8();
		size_t offset = 0;
		const size_t total = (size_t)utf8.length();
		while (offset < total) {
			const ssize_t written = ::write(in_fd[1], utf8.get_data() + offset, total - offset);
			if (written <= 0) {
				if (errno == EINTR) continue;
				return false;
			}
			offset += (size_t)written;
		}
		return true;
	}

	virtual bool _is_running() const override {
		if (is_closing || child_id_ < 0) {
			return false;
		}
		int status = 0;
		return ::waitpid(child_id_, &status, WNOHANG) == 0;
	}

	virtual void on_stop() override {
		// 与 Windows 实现同因：先判活（`_is_running()` 读 `is_closing`），再置标志。
		const bool was_running = _is_running();
		is_closing = true;
		if (was_running) {
			JSB_PROCESS_LOG(Verbose, "[%s] terminating...", proc_name);
			// 子进程可能已被读取线程 waitpid 回收，`kill` 失败属正常。
			::kill(child_id_, SIGKILL);
		}
		// 不在这里关 `pipefd`：宿主线程与读取线程各关一次是双重 close（fd 可能已被复用于
		// 别的对象）。`pipefd[0]` 的唯一所有者是读取线程，它在循环退出后关闭。
		if (thread.is_valid() && thread->is_started()) {
			thread->wait_to_finish();
		} else {
			// 启动失败的进程没有读取线程（见 on_start 的提前返回）⇒ 两个 fd 都还在我们手上。
			if (pipefd[0] >= 0) close(pipefd[0]);
			if (pipefd[1] >= 0) close(pipefd[1]);
			pipefd[0] = pipefd[1] = -1;
		}
		// 关掉宿主这一端的写 fd，子进程才会在 stdin 上看到 EOF。**必须在 join 之后**：
		// 读取线程要等子进程真正退出（`waitpid`）才会走到 `on_stdout_eof()`。
		if (in_fd[1] >= 0) {
			close(in_fd[1]);
			in_fd[1] = -1;
		}
		JSB_PROCESS_LOG(Log, "[%s] terminated", proc_name);
	}
};
#else
// just a null impl do nothing
class ProcessImpl : public Process {
public:
	ProcessImpl() : Process() {}

	virtual Error on_start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) override { return OK; }
	virtual bool _is_running() const override { return false; }
	virtual bool _write_stdin(const String &p_text) override { return false; }
	virtual void on_stop() override {}
};
#endif

bool Process::is_running() const {
	return _is_running();
}

bool Process::write_stdin(const String &p_text) {
	return _write_stdin(p_text);
}

void Process::set_line_callback(LineCallback p_callback) {
	std::lock_guard<std::mutex> lock(callback_mutex_);
	line_callback_ = std::move(p_callback);
}

void Process::set_eof_callback(EofCallback p_callback) {
	std::lock_guard<std::mutex> lock(callback_mutex_);
	eof_callback_ = std::move(p_callback);
}

void Process::emit_line(const String &p_line) {
	// Copy the callback out under the lock and run it unlocked: a callback must be free to call
	// `set_line_callback()` again (the tool manager re-registers on restart) without deadlocking.
	LineCallback callback;
	{
		std::lock_guard<std::mutex> lock(callback_mutex_);
		callback = line_callback_;
	}
	if (callback) {
		callback(p_line);
	}
}

void Process::emit_eof() {
	EofCallback callback;
	{
		std::lock_guard<std::mutex> lock(callback_mutex_);
		callback = eof_callback_;
	}
	if (callback) {
		callback();
	}
}

std::shared_ptr<Process> Process::create(const String &p_name, const String &p_path, const Vector<String> &p_arguments) {
	std::shared_ptr<ProcessImpl> impl = std::make_shared<ProcessImpl>();
	impl->start(p_name, p_path, p_arguments);
	return impl;
}

Process::~Process() {
}

void Process::stop() {
	// 不按 `is_running()` 提前返回：短命进程（如一次性工具）往往在持有者收尾前就自行退出，
	// 而读取 stdout 的后台线程持有 `this` 的裸指针 —— 未 join 就析构 ProcessImpl 会造成 use-after-free。
	// `on_stop()` 两个平台实现都已是幂等的（先判活、关闭后置空句柄、线程已启动才 join）。
	on_stop();
}

void Process::start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) {
	on_start(p_name, p_path, p_arguments);
}

} //namespace jsb::internal
