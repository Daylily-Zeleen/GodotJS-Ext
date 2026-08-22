/************************************************************************/
/*  jsb_test_main.cpp                                                   */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cstdio>

// Include all test headers to register TEST_CASE macros
#include "tests/jsb_test_helpers.h"
#include "tests/test_jsb_any_runtime.h"
#include "tests/test_jsb_path_util.h"
#include "tests/test_jsb_sarray.h"
#if JSB_SHADOW_REALM_ENABLED
#	include "tests/test_jsb_shadow_realm.h"
#endif // JSB_SHADOW_REALM_ENABLED
#include "tests/test_jsb_source_map.h"
#if JSB_WITH_QUICKJS
#	include "tests/test_jsb_quickjs_runtime.h"
#endif
#if JSB_WITH_V8
#	include "tests/test_jsb_v8_runtime.h"
#endif

// doctest will automatically collect all TEST_CASE and run them in main()
