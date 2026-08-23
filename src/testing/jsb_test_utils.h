/************************************************************************/
/*  jsb_test_utils.h                                                    */
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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Engine-independent test utilities shared by both suites (runtime / editor).
// Engine-dependent fixtures stay in each suite's own helpers:
//   - runtime suite: src/runtime/tests/jsb_test_helpers.h
//   - editor suite:  src/editor/tests/ (added alongside codegen unit tests in T1)

#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include "doctest/doctest.h"

#include <chrono>
#include <thread>

namespace jsb::tests {

#define JSB_TESTS_CONCAT_(a, b) a##b
#define JSB_TESTS_CONCAT(a, b) JSB_TESTS_CONCAT_(a, b)

// RAII scope entry for cases executing inside a JS environment; expands to a
// uniquely named V8ContextScope. V8ContextScope itself stays per-suite
// (runtime: src/runtime/tests/jsb_test_helpers.h) because it depends on the
// JS engine impl; the macro only requires it to be visible at the use site.
#define JSB_TESTS_EXECUTION_SCOPE(env) \
	const ::jsb::tests::V8ContextScope JSB_TESTS_CONCAT(unique_, __COUNTER__)(env)

// Lightweight elapsed-time guard for timing-sensitive cases.
struct ScopedTimer {
	explicit ScopedTimer() : start_(std::chrono::steady_clock::now()) {}

	[[nodiscard]] long long elapsed_ms() const {
		const auto now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
	}

private:
	std::chrono::steady_clock::time_point start_;
};

} // namespace jsb::tests
