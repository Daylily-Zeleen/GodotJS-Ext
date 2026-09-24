/************************************************************************/
/*  jsb_v8_pch.h                                                        */
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

// JSB_WITH_* come from jsb.gen.h, so the config has to be pulled in before the
// conditional includes below.
#include "../../internal/jsb_macros.h"

#include <libplatform/libplatform.h>
// libnode is built with --without-inspector, so the V8 it bundles ships a reduced
// SDK with neither <v8-inspector.h> nor <v8-version-string.h> installed. Gate each
// header on exactly the condition under which its consumer is compiled, so this
// can never drift from the code that needs it:
//   - <v8-inspector.h>: only jsb_debugger.cpp uses the inspector API, and its whole
//     body sits behind this same `JSB_WITH_DEBUGGER && JSB_WITH_LWS && JSB_WITH_V8`.
//     lws is off in node mode, so the debugger is never compiled there.
//   - <v8-version-string.h>: only jsb_v8_typedef.h (a v8-only header) uses
//     V8_VERSION_STRING; node takes its version from node_version.h instead.
#if JSB_WITH_DEBUGGER && JSB_WITH_LWS && JSB_WITH_V8
#	include <v8-inspector.h>
#endif
#if !JSB_WITH_NODE
#	include <v8-version-string.h>
#endif
#include <v8-persistent-handle.h>
#include <v8.h>

#if JSB_V8_CPPGC
#	include <cppgc/default-platform.h>
#	include <v8-cppgc.h>
#endif

#include <internal/jsb_logger.h>

#include <internal/jsb_custom_field.h>
#include <internal/jsb_statistics.h>
