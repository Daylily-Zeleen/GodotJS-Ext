/************************************************************************/
/*  jsb_node_host_main.cpp                                              */
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

// Standalone node helper executable entry.
//
// GodotJS-Ext embeds libnode inside the main GDExtension DLL, so when user
// code calls child_process.fork() the embedded runtime reports the Godot
// executable as process.execPath — forking would spawn Godot itself. To make
// fork() work (node-llama-cpp and other native addons use it to probe the
// host), we ship a tiny standalone helper executable. It links the main DLL
// (which exports godotjs_node_probe_main) and hands control to node::Start
// after preparing the native-addon host (loading node.dll / promoting N-API
// symbols). See jsb_node_probe_host.cpp for the probe entry.
#include "jsb_node_host_main.h"

#ifdef WINDOWS_ENABLED
#	include <windows.h>

#	include <string>
#	include <vector>

// implemented by the main godotjs-ext DLL (jsb_node_probe_host.cpp)
extern "C" __declspec(dllimport) int godotjs_node_probe_main(int p_argc, char **p_argv);

static std::string wide_to_utf8(const wchar_t *p_value) {
	if (p_value == nullptr) {
		return {};
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, p_value, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, p_value, -1, out.data(), size, nullptr, nullptr);
	return out;
}

int wmain(int p_argc, wchar_t **p_argv) {
	// convert the wide argv to UTF-8 (node expects char* argv)
	std::vector<std::string> utf8_args;
	utf8_args.reserve(static_cast<size_t>(p_argc));
	std::vector<char *> argv;
	argv.reserve(static_cast<size_t>(p_argc) + 1);
	for (int i = 0; i < p_argc; i++) {
		utf8_args.push_back(wide_to_utf8(p_argv[i]));
		argv.push_back(utf8_args.back().data());
	}
	argv.push_back(nullptr);
	return godotjs_node_probe_main(p_argc, argv.data());
}
#else
extern "C" int godotjs_node_probe_main(int p_argc, char **p_argv);

int main(int p_argc, char **p_argv) {
	return godotjs_node_probe_main(p_argc, p_argv);
}
#endif
