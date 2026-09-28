/************************************************************************/
/*  jsb_node_console_hook.h                                             */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Node console hook (node engine layer only, guarded by JSB_WITH_NODE).
//
// The node bootstrap installs its own `console` object on the global; its
// output goes straight to the node stdout and never reaches the
// jsb::internal::IConsoleOutput sinks. This hook wraps the 9 console methods so
// every call is mirrored into the sinks and then forwarded to the original
// node implementation.
//
// Activation is a process-wide one-shot keyed on "a console sink exists":
// the IConsoleOutput constructor arms it (jsb_console_output.cpp), so products
// that never construct a sink -- every template product -- never install it.
// NodeRuntime's constructor installs it on each newly created Environment
// afterwards. The hook is never uninstalled.
//
// The declarations below deliberately avoid <v8.h>: this header is included by
// jsb_console_output.cpp, which must not pull the V8 SDK into the shared layer.

namespace v8 {
class Isolate;
class Context;
template <typename T>
class Local;
} //namespace v8

namespace jsb::impl {

/// Arm the hook process-wide (idempotent) and install it on every live
/// Environment, then keep it armed for every Environment created afterwards.
/// Safe to call from any thread; a no-op once armed.
void console_hook_arm();

/// Install the wrapped console methods on one isolate/context when the hook has
/// been armed, otherwise return immediately. Called by NodeRuntime's
/// constructor right after its bootstrap made the console available on that
/// context. The caller must already hold the isolate scope AND a HandleScope
/// (`p_context` is a Local handle).
void console_hook_ensure(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context);

/// Drop the per-isolate hook state (the timer tag table) before the isolate is
/// disposed. Called by NodeRuntime's destructor.
void console_hook_drop_isolate(v8::Isolate *p_isolate);

} //namespace jsb::impl
