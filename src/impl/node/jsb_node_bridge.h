/************************************************************************/
/*  jsb_node_bridge.h                                                   */
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

#include "jsb_node_pch.h"

namespace jsb::impl {
// The 'godot' linked binding exposed to the node runtime. It provides the
// low-level helpers the bootstrap script uses to wire res:// aware `fs`
// and `process.dlopen` for native `.node` addons.
struct NodeBridge {
	// prepare the host process for loading native .node addons:
	// - Windows: preload the node.dll shim next to this module (the addon's
	//   napi_* imports are forwarded through the shim)
	// - Linux/macOS/Android: promote this module's N-API symbols to global
	//   visibility (libnode is statically linked, not globally visible by default)
	static void PrepareNativeAddonHost();

	// register the 'godot' linked binding on the node environment.
	// After this call, `internalBinding('godot')` (and `require('godot')` after
	// the bootstrap patches the module loader) returns an object with
	// `fs_readFile`, `fs_stat` and `preload_dlls`.
	static void AddGodotLinkedBinding(node::Environment *p_env);
};

} //namespace jsb::impl
