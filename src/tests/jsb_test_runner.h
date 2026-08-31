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
#include <iostream> // std::cout
#include <sstream>
#include <string>

#ifdef JSB_TESTS_ENABLED

// The CONFIG macros must be defined before the first doctest.h inclusion in
// every TU that ends up including one. jsb_test_utils.h / jsb_test_main.cpp
// define them for their own include chains; this header may be the FIRST
// include of doctest.h in register_types.cpp / register_editor_types.cpp, so
// keep the set identical here.
#	define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#	define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#	include "doctest/doctest.h"

#	include <godot_cpp/classes/engine.hpp>
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/scene_tree.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::tests {

static constexpr char EDITOR_TEST_FLAG[] = "EditorTest";
static constexpr char RUNTIME_TEST_FLAG[] = "RuntimeTest";

/**
Console output channel for the doctest run.

doctest's own output sinks (std::cout, or a file via --out) both proved
unreliable under the test process on Linux CI: partway through the runtime
suite the underlying stream stops accepting bytes (strace showed the file's
buffered write() containing only the banner and half of the first test-case
header, while the process kept running and every later reporter write was
silently dropped). Godot's own print path is the only channel that reliably
reaches the CI log.

This reporter mirrors the default console reporter's essential output into
an in-memory buffer: the test-case header + its MESSAGE lines on first
output, failing assertions, and the final run summary. try_run() replays
the buffer through UtilityFunctions::print after context.run() returns.
*/
class BufferedReporter final : public doctest::IReporter {
	// Points at the buffer owned by try_run()'s stack frame for the duration
	// of the run. doctest constructs/destroys the reporter internally, so the
	// instance cannot own state the caller needs to read back.
	static inline std::ostringstream *s_sink = nullptr;
	static inline bool s_case_header_logged = false;

public:
	explicit BufferedReporter(const doctest::ContextOptions &) {}

	// Installs the caller-owned sink for the duration of a run.
	static void begin_run(std::ostringstream &p_sink) {
		s_sink = &p_sink;
		s_case_header_logged = false;
	}
	static void end_run() { s_sink = nullptr; }

	static std::string take_output(std::ostringstream &p_sink) { return std::move(p_sink).str(); }

	void test_case_start(const doctest::TestCaseData &) override { s_case_header_logged = false; }
	void test_case_reenter(const doctest::TestCaseData &) override { s_case_header_logged = false; }

	void log_message(const doctest::MessageData &p_msg) override {
		if (!s_sink) return;
		log_case_header(p_msg.m_file, p_msg.m_line);
		*s_sink << p_msg.m_file << "(" << p_msg.m_line << ") MESSAGE: " << p_msg.m_string << "\n";
	}

	void log_assert(const doctest::AssertData &p_assert) override {
		if (!s_sink) return;
		log_case_header(p_assert.m_file, p_assert.m_line);
		if (p_assert.m_failed) {
			*s_sink << "ERROR: " << p_assert.m_expr;
			if (p_assert.m_threw) {
				*s_sink << " threw: " << p_assert.m_exception.c_str();
			} else if (p_assert.m_decomp.c_str() && *p_assert.m_decomp.c_str()) {
				*s_sink << " values: " << p_assert.m_decomp.c_str();
			}
			*s_sink << "\n";
		}
	}

	void test_case_exception(const doctest::TestCaseException &p_exc) override {
		if (!s_sink) return;
		*s_sink << "ERROR: test case " << (p_exc.is_crash ? "CRASHED" : "THREW exception")
				<< ": " << p_exc.error_string << "\n";
	}

	void test_run_end(const doctest::TestRunStats &p_stats) override {
		if (!s_sink) return;
		*s_sink << "[doctest] test cases: " << p_stats.numTestCasesPassingFilters
				<< " | " << (p_stats.numTestCasesPassingFilters - p_stats.numTestCasesFailed)
				<< " passed | " << p_stats.numTestCasesFailed << " failed | 0 skipped\n";
		*s_sink << "[doctest] assertions: " << p_stats.numAsserts
				<< " | " << (p_stats.numAsserts - p_stats.numAssertsFailed)
				<< " passed | " << p_stats.numAssertsFailed << " failed |\n";
		*s_sink << (p_stats.numTestCasesFailed == 0 && p_stats.numAssertsFailed == 0
						? "[doctest] Status: SUCCESS!\n"
						: "[doctest] Status: FAILURE!\n");
	}

	// -- unused callbacks ----------------------------------------------------
	void report_query(const doctest::QueryData &) override {}
	void test_run_start() override {}
	void test_case_end(const doctest::CurrentTestCaseStats &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}

private:
	// The default console reporter prints the TEST_CASE header lazily, right
	// before the first output event of the case; mirror that behaviour.
	static void log_case_header(const char *p_file, int p_line) {
		if (s_case_header_logged) {
			return;
		}
		s_case_header_logged = true;
		*s_sink << "===============================================================================\n";
		*s_sink << p_file << "(" << p_line << "): TEST CASE\n";
	}
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

	// 执行测试。BufferedReporter 代替 doctest 默认 console 输出（其输出流在
	// Linux CI 上不可靠，见类注释），run() 结束后经 Godot print 回显。
	UtilityFunctions::print("[jsb] running tests via", p_test_type_flag);
	doctest::registerReporter<BufferedReporter>("buffered", 0, true);
	const char *argv[] = { "jsb", "--reporters=buffered", "--no-colors" };
	doctest::Context context;
	context.applyCommandLine(3, argv);

	std::ostringstream report_buffer;
	BufferedReporter::begin_run(report_buffer);
	int exit_code = context.run();
	BufferedReporter::end_run();

	// Echo the buffered test output through Godot's print (reliably visible
	// in CI logs), one line at a time.
	{
		std::istringstream lines(BufferedReporter::take_output(report_buffer));
		std::string line;
		while (std::getline(lines, line)) {
			UtilityFunctions::print(line.c_str());
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
