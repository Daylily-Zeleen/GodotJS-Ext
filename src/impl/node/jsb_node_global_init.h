/************************************************************************/
/*  jsb_node_global_init.h                                              */
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
// Process-wide initialization of the embedded Node.js runtime (libnode).
// Mirrors `jsb::impl::GlobalInitialize` of the v8 engine layer, but additionally
// runs `node::InitializeOncePerProcess` and creates the `node::MultiIsolatePlatform`.
//
// Node embeds V8, so in node mode this is the *only* place where `v8::V8::Initialize()`
// is called; the v8 engine layer's `GlobalInitialize` is not compiled in.
struct GlobalInitialize {
	// the per-process multi-isolate platform owned by the single GlobalInitialize instance
	static inline std::unique_ptr<node::MultiIsolatePlatform> platform{};

public:
	// the per-process multi-isolate platform (shared by all NodeRuntime instances)
	static _FORCE_INLINE_ node::MultiIsolatePlatform *get_platform() {
		jsb_ensure(platform);
		return platform.get();
	}

	static void init();
	static void shutdown();
};

} //namespace jsb::impl
