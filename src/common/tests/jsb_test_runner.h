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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Header-only test bootstrap shared by both suites (runtime / editor).
// Each suite calls `jsb::testing::try_run("<flag>")` from its own startup
// callback; when the flag is found in the engine cmdline args the doctest
// context runs and the process exits with doctest's exit code.
//
// Both suites keep their own DOCTEST_CONFIG_IMPLEMENT TU, so TEST_CASE
// registries stay module-local: the runtime suite only collects cases from
// src/runtime/tests/*.h, the editor suite only from src/editor/tests/*.h.

#include <cstdlib> // std::exit

#ifdef JSB_TESTS_ENABLED

#	include "doctest/doctest.h"

#	include <godot_cpp/classes/engine.hpp>
#	include <godot_cpp/classes/os.hpp>
#	include <godot_cpp/classes/scene_tree.hpp>
#	include <godot_cpp/variant/utility_functions.hpp>

namespace jsb::testing {

// Scan OS cmdline args for p_flag; on a hit run all TEST_CASEs registered in
// this module and quit with doctest's exit code (SceneTree::quit when a main
// loop exists, otherwise std::exit).
//
// While the build is a single extension library both suites share one doctest
// registry, so the requested suite is selected by the leading name tag of its
// cases ([runtime]... / [editor]...) via a -tc wildcard filter. Post P4 each
// library gets its own registry and this filter becomes redundant scaffolding.
inline void try_run(const char *p_flag) {
	bool run_tests = false;
	for (const godot::String &arg : godot::OS::get_singleton()->get_cmdline_args()) {
		if (arg == p_flag) {
			run_tests = true;
			break;
		}
	}
	if (!run_tests) {
		return;
	}

	godot::UtilityFunctions::print("[jsb] running tests via ", p_flag);
	doctest::Context context;
	const char *filter = godot::String(p_flag) == "--jsb-run-editor-tests" ? "-tc=[editor]*" : "-tc=[runtime]*";
	const char *argv[] = { "jsb", filter };
	context.applyCommandLine(2, argv);
	const int exit_code = context.run();
	if (godot::SceneTree *scene_tree = godot::Object::cast_to<godot::SceneTree>(godot::Engine::get_singleton()->get_main_loop())) {
		scene_tree->quit(exit_code);
	} else {
		std::exit(exit_code);
	}
}

} // namespace jsb::testing

#endif // JSB_TESTS_ENABLED
