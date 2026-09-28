/************************************************************************/
/*  jsb_process.h                                                       */
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

#pragma once
#include "jsb_internal_pch.h"

#include <functional>
#include <mutex>

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal {
/**
 * A child process with a line-oriented, bidirectional pipe.
 *
 * The child's stdout (and stderr, which is redirected onto the same handle) is drained by a
 * background thread; every complete line is logged and handed to the registered line callback.
 * `write_stdin()` pushes a line into the child's stdin.
 *
 * NOTE the reader thread is a bare OS thread (see the impl), so callbacks arrive **off** the host
 * thread and the host must synchronise with `std::` primitives -- `godot::Mutex` is not usable
 * there. A callback MUST NOT call `stop()` (it would join the very thread it runs on).
 */
class Process {
public:
	using LineCallback = std::function<void(const String &)>;
	using EofCallback = std::function<void()>;

	virtual ~Process();
	static std::shared_ptr<Process> create(const String &p_name, const String &p_path, const Vector<String> &p_arguments);

	void stop();
	bool is_running() const;

	/**
	 * Write one line to the child's stdin (a trailing '\n' is appended).
	 * @return false when the pipe is no longer writable (never started, or the child is gone).
	 */
	bool write_stdin(const String &p_text);

	/** Called on the reader thread for every complete stdout line. Replaces any previous one. */
	void set_line_callback(LineCallback p_callback);

	/** Called on the reader thread once the child's stdout reached EOF (the child is gone). */
	void set_eof_callback(EofCallback p_callback);

protected:
	Process() = default;
	void start(const String &p_name, const String &p_path, const Vector<String> &p_arguments);

	virtual Error on_start(const String &p_name, const String &p_path, const Vector<String> &p_arguments) = 0;
	virtual void on_stop() = 0;
	virtual bool _is_running() const = 0;
	virtual bool _write_stdin(const String &p_text) = 0;

	/** Implementations call this on the reader thread once a complete line was assembled. */
	void emit_line(const String &p_line);
	void emit_eof();

private:
	// Guards the two callbacks only. They are invoked with the lock released, so a callback is
	// free to register another one (but must not call stop(), see the class note).
	std::mutex callback_mutex_;
	LineCallback line_callback_;
	EofCallback eof_callback_;
};
} //namespace jsb::internal
