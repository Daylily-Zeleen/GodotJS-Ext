/************************************************************************/
/*  jsb_v8_global_init.cpp                                              */
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

#include "jsb_v8_global_init.h"

namespace jsb::impl {
void GlobalInitialize::init() {
	jsb_check(GlobalInitialize::platform == nullptr);

#if JSB_V8_CPPGC
	GlobalInitialize::platform = std::move(td::make_unique<cppgc::DefaultPlatform>());
#else
	GlobalInitialize::platform = std::move(v8::platform::NewDefaultPlatform());
#endif

#if JSB_EXPOSE_GC_FOR_TESTING
	constexpr char exposeGcArgs[] = "--expose-gc";
	v8::V8::SetFlagsFromString(exposeGcArgs, std::size(exposeGcArgs) - 1);
#endif

#if JSB_V8_JITLESS
	constexpr char jitlessArgs[] = "--jitless";
	v8::V8::SetFlagsFromString(jitlessArgs, std::size(jitlessArgs) - 1);
#endif

#if JSB_V8_CPPGC
	v8::V8::InitializePlatform(GlobalInitialize::platform->GetV8Platform());
	cppgc::InitializeProcess(GlobalInitialize::platform->GetPageAllocator());
#else
	v8::V8::InitializePlatform(GlobalInitialize::platform.get());
#endif

	v8::V8::Initialize();

	jsb_ensure(get_platform());
}

void GlobalInitialize::shutdown() {
	jsb_check(GlobalInitialize::platform);

//NOTE never called in the current implementation
#if JSB_V8_CPPGC
	cppgc::ShutdownProcess();
#endif

	v8::V8::Dispose();
	v8::V8::DisposePlatform();

	GlobalInitialize::platform.reset();
}

} //namespace jsb::impl