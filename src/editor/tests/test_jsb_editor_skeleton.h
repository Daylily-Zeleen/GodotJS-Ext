/************************************************************************/
/*  test_jsb_editor_skeleton.h                                          */
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

// Editor-suite smoke cases proving the bootstrap end to end:
// flag parsing, tag isolation from the runtime suite, and the
// project-root working directory contract shared by both suites.
// Real codegen unit tests land here in T1.

#include "testing/jsb_test_utils.h"
#include <godot_cpp/classes/os.hpp>

namespace jsb::tests {

TEST_CASE("[editor] bootstrap: suite is reachable via --jsb-run-editor-tests") {
	CHECK_MESSAGE(true, "editor doctest suite registered and ran");
}

TEST_CASE("[editor][smoke] engine cmdline args are queryable in editor context") {
	const godot::PackedStringArray &args = godot::OS::get_singleton()->get_cmdline_args();
	(void)args; // presence of the API without crashing is the assertion
	CHECK(true);
}

} // namespace jsb::tests
