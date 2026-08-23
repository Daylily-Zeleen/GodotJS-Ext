/************************************************************************/
/*  jsb_editor_test_main.cpp                                            */
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

#if defined(JSB_TESTS_ENABLED) && defined(TOOLS_ENABLED)

#include "doctest/doctest.h"

// Editor-suite test headers. While the build is still a single extension
// library (pre P4), both suites share the doctest implementation TU in
// src/runtime/tests/jsb_test_main.cpp and are isolated by the leading
// [runtime] / [editor] case-name tag at run time (see
// src/testing/jsb_test_runner.h). After P4 splits the libraries this file
// becomes the editor target's own DOCTEST_CONFIG_IMPLEMENT TU and the tag
// filtering turns into redundant scaffolding.
#include "tests/test_jsb_editor_skeleton.h"

#endif // JSB_TESTS_ENABLED && TOOLS_ENABLED
