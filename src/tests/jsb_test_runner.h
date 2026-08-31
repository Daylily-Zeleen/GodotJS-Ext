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

#ifdef JSB_TESTS_ENABLED

#	include "doctest/doctest.h"

#	include <godot_cpp/classes/engine.hpp>
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/scene_tree.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::tests {

static constexpr char EDITOR_TEST_FLAG[] = "EditorTest";
static constexpr char RUNTIME_TEST_FLAG[] = "RuntimeTest";

/**
Counts the test cases that passed the filters. Registered alongside the
default console reporter (`--reporters=console,counter`), so console output
is preserved while the C++ side can read the number back after `run()`.

A suite that collects zero runnable cases must fail the build: doctest
itself returns EXIT_SUCCESS for "0 cases, 0 failures", which used to let CI
pass while the entire suite was silently skipped (see the linux v8/qjs legs).
*/
class TestCountReporter final : public doctest::IReporter {
	static inline unsigned cases_ = 0;
	static inline int failed_asserts_ = 0;

public:
	explicit TestCountReporter(const doctest::ContextOptions &) {}

	void test_run_end(const doctest::TestRunStats &p_stats) override {
		cases_ = p_stats.numTestCasesPassingFilters;
		failed_asserts_ = p_stats.numAssertsFailed;
	}

	static unsigned get_case_count() { return cases_; }
	static int get_failed_assert_count() { return failed_asserts_; }

	// -- unused callbacks ----------------------------------------------------
	void report_query(const doctest::QueryData &) override {}
	void test_run_start() override {}
	void test_case_start(const doctest::TestCaseData &) override {}
	void test_case_reenter(const doctest::TestCaseData &) override {}
	void test_case_end(const doctest::CurrentTestCaseStats &) override {}
	void test_case_exception(const doctest::TestCaseException &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void log_assert(const doctest::AssertData &) override {}
	void log_message(const doctest::MessageData &) override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}
};

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

	// 执行测试。counter reporter 统计实际运行的 case 数（见 TestCountReporter 注释）。
	UtilityFunctions::print("[jsb] running tests via", p_test_type_flag);
	doctest::registerReporter<TestCountReporter>("counter", 0, true);
	const char *argv[] = { "jsb", "--reporters=console,counter" };
	doctest::Context context;
	context.applyCommandLine(2, argv);
	int exit_code = context.run();

	// CI (Linux, pipe-buffered stdout) may drop buffered doctest output on a
	// hard exit path; flush right after the run so the log always shows the
	// per-case output and the summary.
	fflush(stdout);
	fflush(stderr);

	// 0 个可运行 case 视为致命配置错误（例如整个测试 TU 未被编译进该库），
	// 不允许以 SUCCESS 退出 -- 曾经导致 CI 上 v8/qjs 的 runtime 套件被整体
	// 静默跳过却显示通过。
	const unsigned case_count = TestCountReporter::get_case_count();
	if (case_count == 0 && exit_code == 0) {
		UtilityFunctions::printerr("[jsb] test suite '", p_test_type_flag,
				"' collected 0 test cases; the suite is NOT linked into this build.");
		exit_code = EXIT_FAILURE;
	}
	UtilityFunctions::print("[jsb] test suite '", p_test_type_flag, "' ran ", (int64_t)case_count, " cases");

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
