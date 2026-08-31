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

#include <cstdlib> // std::exit
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

namespace detail {

/**
Replacement for doctest's default console reporter.

doctest's default output channel is unusable here on g++/Linux: its integer
formatting (ostream's num_put/locale machinery) silently fails inside the
runtime GDExtension, so every line containing a number (case headers, the
run summary) is dropped mid-write — on stdout, in a --out file, everywhere.
UtilityFunctions::print, in contrast, is proven to always reach the CI log.

This reporter mirrors the default console reporter's essential output —
lazily-printed case headers, MESSAGEs, failed assertions, exceptions, and
the run summary — but formats every number with std::to_string and emits
each line straight through UtilityFunctions::print.
*/
class ConsoleReporter final : public doctest::IReporter {
	static inline bool s_case_header_logged = false;

public:
	explicit ConsoleReporter(const doctest::ContextOptions &) {}

	void test_case_start(const doctest::TestCaseData &) override { s_case_header_logged = false; }
	void test_case_reenter(const doctest::TestCaseData &) override { s_case_header_logged = false; }

	void log_message(const doctest::MessageData &p_msg) override {
		log_case_header(p_msg.m_file, p_msg.m_line);
		std::ostringstream ss;
		ss << p_msg.m_file << "(" << std::to_string(p_msg.m_line).c_str() << ") MESSAGE: " << p_msg.m_string << "\n";
		print_line(ss.str().c_str());
	}

	void log_assert(const doctest::AssertData &p_assert) override {
		log_case_header(p_assert.m_file, p_assert.m_line);
		if (p_assert.m_failed) {
			std::ostringstream ss;
			ss << "ERROR: " << p_assert.m_expr;
			if (p_assert.m_threw) {
				ss << " threw: " << p_assert.m_exception.c_str();
			} else if (p_assert.m_decomp.c_str() && *p_assert.m_decomp.c_str()) {
				ss << " values: " << p_assert.m_decomp.c_str();
			}
			ss << "\n";
			print_line(ss.str().c_str());
		}
	}

	void test_case_exception(const doctest::TestCaseException &p_exc) override {
		std::ostringstream ss;
		ss << "ERROR: test case " << (p_exc.is_crash ? "CRASHED" : "THREW exception")
				<< ": " << p_exc.error_string << "\n";
		print_line(ss.str().c_str());
	}

	void test_run_end(const doctest::TestRunStats &p_stats) override {
		std::ostringstream ss;
		ss << "[doctest] test cases: " << std::to_string((int64_t)p_stats.numTestCasesPassingFilters).c_str()
				<< " | " << std::to_string((int64_t)(p_stats.numTestCasesPassingFilters - p_stats.numTestCasesFailed)).c_str()
				<< " passed | " << std::to_string((int64_t)p_stats.numTestCasesFailed).c_str() << " failed | 0 skipped\n"
				<< "[doctest] assertions: " << std::to_string((int64_t)p_stats.numAsserts).c_str()
				<< " | " << std::to_string((int64_t)(p_stats.numAsserts - p_stats.numAssertsFailed)).c_str()
				<< " passed | " << std::to_string((int64_t)p_stats.numAssertsFailed).c_str() << " failed |\n"
				<< (p_stats.numTestCasesFailed == 0 && p_stats.numAssertsFailed == 0
							? "[doctest] Status: SUCCESS!\n"
							: "[doctest] Status: FAILURE!\n");
		print_line(ss.str().c_str());
	}

	// -- unused callbacks ----------------------------------------------------
	void report_query(const doctest::QueryData &) override {}
	void test_run_start() override {}
	void test_case_end(const doctest::CurrentTestCaseStats &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}

private:
	static void print_line(const char *p_line) {
		godot::UtilityFunctions::print(p_line);
	}

	// The default console reporter prints the TEST_CASE header lazily, right
	// before the first output event of the case; mirror that behaviour.
	static void log_case_header(const char *p_file, int p_line) {
		if (s_case_header_logged) {
			return;
		}
		s_case_header_logged = true;
		godot::UtilityFunctions::print(
				"===============================================================================");
		std::ostringstream ss;
		ss << p_file << "(" << std::to_string((int64_t)p_line).c_str() << "): TEST CASE";
		godot::UtilityFunctions::print(ss.str().c_str());
	}
};

} //namespace detail

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

	// 执行测试。detail::ConsoleReporter 代替 doctest 默认 console 输出：
	// 后者的整数格式化在 g++/Linux 的 runtime DLL 内静默失败导致输出断流
	// （见 detail::ConsoleReporter 注释），这里改经 Godot print 落到日志。
	UtilityFunctions::print("[jsb] running tests via", p_test_type_flag);
	doctest::registerReporter<detail::ConsoleReporter>("direct", 0, true);
	const char *argv[] = { "jsb", "--reporters=direct", "--no-colors" };
	doctest::Context context;
	context.applyCommandLine(2, argv);
	int exit_code = context.run();

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
