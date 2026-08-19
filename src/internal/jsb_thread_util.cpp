/************************************************************************/
/*  jsb_thread_util.cpp                                                 */
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

#include "jsb_thread_util.h"
#include "jsb_macros.h"

#include <godot_cpp/classes/os.hpp>

#ifdef WINDOWS_ENABLED
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace jsb::internal {

#ifdef WINDOWS_ENABLED
namespace {
typedef HRESULT(WINAPI *SetThreadDescriptionFunc)(HANDLE hThread, PCWSTR lpThreadDescription);

SetThreadDescriptionFunc GetSetThreadDescriptionFunc() {
	if (const HMODULE module = GetModuleHandle(TEXT("kernel32.dll"))) {
		return (SetThreadDescriptionFunc)GetProcAddress(module, "SetThreadDescription");
	}
	return nullptr;
}
} //namespace
#endif

void ThreadUtil::set_name(const String &p_name) {
#ifdef WINDOWS_ENABLED
	static const SetThreadDescriptionFunc func = GetSetThreadDescriptionFunc();
	if (func) {
		const Char16String str16 = p_name.utf16();
		const HRESULT res = func(::GetCurrentThread(), (PCWSTR)str16.get_data());
		if (SUCCEEDED(res)) {
			return;
		}
	}
#endif
	godot::OS::get_singleton()->set_thread_name(p_name);
}

} //namespace jsb::internal
