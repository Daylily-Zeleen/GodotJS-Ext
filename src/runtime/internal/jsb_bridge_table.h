/************************************************************************/
/*  jsb_bridge_table.h                                                  */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Runtime-side bridge table: declaration of the singleton accessor paired
// with its implementation TU (jsb_bridge_table.cpp, same directory).
//
// The cross-extension CONTRACT (function pointer typedefs + JsbBridgeTable
// layout) lives in <internal/jsb_bridge_abi.h>; this header only adds the
// runtime-private accessor. The editor must never include this file -- it
// resolves the table through EditorBridge::get_bridge() instead.

#include "jsb_bridge_abi.h"

// forward declarations only: this header is included by translation units
// that do NOT include any V8 header (e.g. the weaver), so the node console
// hook declaration below must not pull in <v8.h>. (jsb_bridge_table.cpp
// includes the full V8/node headers before this file.)
namespace v8 {
class Isolate;
class Context;
template <typename T> class Local;
}

namespace jsb {

/// Runtime-side singleton accessor (defined in jsb_bridge_table.cpp).
const JsbBridgeTable *get_bridge_table();

#if JSB_WITH_NODE
/// If the bridge console capability has been activated (i.e. some editor sink
/// was registered through `bridge_add_console_output`), install the console
/// hook on the given isolate/context. Called by NodeRuntime's constructor
/// right after its bootstrap made the node console available, so every
/// Environment created after the activation gets the wrapped console.
/// (Cannot go through `Environment::wrap` here: the Environment has not yet
/// registered itself as the isolate's embedder data at this point.)
void bridge_console_hook_ensure(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context);
#endif

} //namespace jsb
