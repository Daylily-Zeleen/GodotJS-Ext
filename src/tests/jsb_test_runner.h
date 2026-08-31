/************************************************************************/
/*  jsb_test_runner.h                                                   */
/************************************************************************/
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Header-only test bootstrap shared by both suites (runtime / editor).
// Both suites keep their own DOCTEST_CONFIG_IMPLEMENT TU, so TEST_CASE
// registries stay module-local: the runtime suite only collects cases from
// src/runtime/tests/*.h, the editor suite only from src/editor/tests/*.h.

#include <cstdio> // fflush
#include <cstdlib> // std::exit
#include <iostream> // std::cout (doctest writes its output through it)
#include <string> // std::string (doctest --out argument lifetime)

#ifdef JSB_TESTS_ENABLED

#	include "doctest/doctest.h"

#	include <godot_cpp/classes/dir_access.hpp>
#	include <godot_cpp/classes/engine.hpp>
#	include <godot_cpp/classes/file_access.hpp>
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/scene_tree.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::tests {

static constexpr char EDITOR_TEST_FLAG[] = "EditorTest";
static constexpr char RUNTIME_TEST_FLAG[] = "RuntimeTest";

/**
Editor 与 Runtime 两个库都分别编译了自己的测试，它们不共用，状态也不直接互通。
这里使用 godot::Engine 单例来传输两个库的测试状态，确保两者都测试完成再进行退出。
*/
inline void try_run(const char *p_test_type_flag) {
	using namespace godot;
	constexpr char CMDLINE_TEST_ARG[] = "--jsb-run-tests";
	// Check cmdline flag.
	bool run_tests = false;
	for (const String &arg : OS::get_singleton()->get_cmdline_args()) {
		if (arg == CMDLINE_TEST_ARG) {
			run_tests = true;
			break;
		}
	}
	if (!run_tests) {
		return;
	}

	// 执行测试
	UtilityFunctions::print("[jsb] running tests via", p_test_type_flag);

	// doctest writes through std::cout; on CI (Linux, pipe-buffered stdout)
	// large parts of its output were silently dropped on the fast shutdown
	// path (per-case messages, the summary), which made the run unreadable
	// and previously masked the state of the suite entirely. Route doctest's
	// output into a temp file and echo it back through Godot's own print
	// (which is reliably visible in CI logs) after the run completes.
	String doctest_out_path = OS::get_singleton()->get_user_data_dir().path_join("jsb_doctest_output.log");
	{
		// --out is only honored per-suite and truncated by doctest itself on
		// open; a stale file from a previous suite must not survive.
		Ref<DirAccess> dir = DirAccess::open(OS::get_singleton()->get_user_data_dir());
		if (dir.is_valid() && dir->file_exists(doctest_out_path.get_file())) {
			dir->remove(doctest_out_path.get_file());
		}
	}
	const CharString doctest_out_path_utf8 = doctest_out_path.utf8();
	const char *argv[] = { "jsb", "--out=", nullptr };
	// doctest parses "--out=<path>" as a single argv entry.
	std::string out_arg = std::string("--out=") + doctest_out_path_utf8.get_data();
	argv[1] = out_arg.c_str();

	doctest::Context context;
	context.applyCommandLine(2, argv);
	int exit_code = context.run();
	argv[1] = nullptr; // out_arg keeps the buffer alive until here

	// Echo doctest's captured output through Godot's print (unbuffered and
	// reliably visible in CI), then remove the temp file.
	{
		Ref<FileAccess> f = FileAccess::open(doctest_out_path, FileAccess::READ);
		if (f.is_valid()) {
			while (!f->eof_reached()) {
				const String line = f->get_line();
				if (!line.is_empty() || !f->eof_reached()) {
					UtilityFunctions::print(line);
				}
			}
			f->close();
		} else {
			UtilityFunctions::printerr("[jsb] failed to read doctest output file: ", doctest_out_path);
		}
		Ref<DirAccess> dir = DirAccess::open(OS::get_singleton()->get_user_data_dir());
		if (dir.is_valid()) {
			dir->remove(doctest_out_path.get_file());
		}
	}
	// Belt and braces: flush the C/C++ standard streams as well.
	std::cout.flush();
	fflush(stdout);
	fflush(stderr);

	// 更新退出码
	const StringName TEST_RESULT_KEY = "TestResult";
	if ((int)Engine::get_singleton()->get_meta(TEST_RESULT_KEY, 0) == 0 && exit_code != 0) {
		Engine::get_singleton()->set_meta(TEST_RESULT_KEY, exit_code);
	}

	// 更新完成标志
	Engine::get_singleton()->set_meta(p_test_type_flag, true);

	for (const auto flag : { EDITOR_TEST_FLAG, RUNTIME_TEST_FLAG }) {
		if (!Engine::get_singleton()->has_meta(flag)) continue; // 跳过不存在的测试
		if ((bool)Engine::get_singleton()->get_meta(flag) == false) return; //  等待其他未完成的测试
	}

	// 所有测试都完成后取出退出码进行退出
	exit_code = Engine::get_singleton()->get_meta(TEST_RESULT_KEY, 0);
	UtilityFunctions::print("[jsb] running tests result: ", exit_code);
	if (SceneTree *scene_tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())) {
		scene_tree->quit(exit_code);
	} else {
		std::exit(exit_code);
	}
}

} //namespace jsb::tests

#endif // JSB_TESTS_ENABLED
