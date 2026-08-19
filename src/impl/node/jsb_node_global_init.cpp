/************************************************************************/
/*  jsb_node_global_init.cpp                                            */
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

#include "jsb_node_global_init.h"

#include <uv.h>

namespace jsb::impl {

void GlobalInitialize::init() {
	jsb_check(GlobalInitialize::platform == nullptr);

	uv_replace_allocator(
			[](size_t size) { return memalloc(size); },
			[](void *ptr, size_t size) { return memrealloc(ptr, size); },
			[](size_t count, size_t size) { return memalloc(count * size); },
			[](void *ptr) { memfree(ptr); });

	// Process-wide Node.js initialization. We skip the parts that would conflict
	// with the host engine (Godot):
	//   - kNoInitializeV8:               we call v8::V8::Initialize() ourselves below
	//   - kNoInitializeNodeV8Platform:   we create our own node::MultiIsolatePlatform
	//   - kNoInitializeCppgc:            cppgc is not used (JSB_V8_CPPGC = 0)
	//   - kNoDefaultSignalHandling:      do not install Node signal handlers (Godot owns them)
	//   - kNoStdioInitialization:        do not touch stdio/TTY state
	const std::vector<std::string> args = { "godotjs-ext", "--experimental-vm-modules" };
	// note: use the initializer_list overload; bitwise-OR of the enum values would
	// promote to int and fail to convert back to Flags (error C2665).
	node::InitializeOncePerProcess(args, {
												 node::ProcessInitializationFlags::kNoInitializeV8,
												 node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
												 node::ProcessInitializationFlags::kNoInitializeCppgc,
												 node::ProcessInitializationFlags::kNoDefaultSignalHandling,
												 node::ProcessInitializationFlags::kNoStdioInitialization,
										 });

	// the shared multi-isolate platform (thread pool of 4 workers, like gode)
	GlobalInitialize::platform = std::move(node::MultiIsolatePlatform::Create(4));
	jsb_ensure(GlobalInitialize::platform.get());

	v8::V8::InitializePlatform(GlobalInitialize::platform.get());
	v8::V8::Initialize();

	jsb_ensure(get_platform());
}

void GlobalInitialize::shutdown() {
	jsb_check(GlobalInitialize::platform);
	uv_library_shutdown();

	v8::V8::Dispose();
	v8::V8::DisposePlatform();
	node::TearDownOncePerProcess();
	GlobalInitialize::platform.reset();
}

} //namespace jsb::impl
