/************************************************************************/
/*  jsb_node_probe_host.cpp                                             */
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

#include "jsb_node_pch.h"

#include "jsb_node_bridge.h"

#include <godot_cpp/core/defs.hpp>

// The native-addon probe entry exported by the main DLL.
//
// child_process.fork() probes (e.g. node-llama-cpp's gpu detection) start the
// bundled helper executable (jsb_node_host_main.cpp), which forwards straight
// here. We prepare the native-addon host (load the node.dll shim next to this
// module / promote this module's N-API symbols) and then run a real node
// process. libnode is statically linked into this DLL, so the helper
// executable itself only needs to link this one exported function.
//
// Why the explicit initialization: `node::Start(int, char**)` assumes the
// per-process state (V8 + node's MultiIsolatePlatform) is already up. Inside
// Godot that is done by GlobalInitialize::init() with kNoInitializeV8 /
// kNoInitializeNodeV8Platform (the host engine owns V8). The helper is a
// **standalone process** with no host engine, so nothing had initialized them
// and Start() dereferenced an unset platform -- an access violation writing
// NULL before any script ran, which killed the helper for EVERY invocation
// (even `--version`), making child_process.fork() unusable on the node leg.
// This libnode exports only the legacy Start(int, char**) (the modern
// Start(InitializationResult*, ...) is absent from node.h), so we do the
// initialization here and let Start() consume the prepared state.
extern "C" {

int GDE_EXPORT godotjs_node_probe_main(int p_argc, char **p_argv) {
	jsb::impl::NodeBridge::PrepareNativeAddonHost();
	// node::Start() performs the per-process initialization itself. Do NOT also
	// call InitializeOncePerProcess()/V8::Initialize() here: that runs the init
	// twice and node aborts with
	//   "Assertion failed: !init_called" / "Wrong initialization order"
	// (the kNoInitializeV8 / kNoInitializeNodeV8Platform flags exist only for the
	// embedded-in-Godot path, where the host engine owns V8 -- see
	// jsb_node_global_init.cpp; this standalone helper has no host engine, so
	// Start() must do the whole job).
	return node::Start(p_argc, p_argv);
}
} //extern "C"
