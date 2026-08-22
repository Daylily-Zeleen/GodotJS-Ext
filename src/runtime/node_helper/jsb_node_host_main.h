/************************************************************************/
/*  jsb_node_host_main.h                                                */
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

// Entry point of the standalone node helper executable (godotjs-ext.exe /
// godotjs-ext on POSIX). This thin host is used as the execPath for
// child_process.fork() probes: the embedded node runtime reports Godot as
// process.execPath, so forking a probe must start this helper instead.
// The helper is compiled into a small executable (see SConstruct) and the
// real probe logic lives in jsb_node_probe_host.cpp inside the main DLL.
#ifdef WINDOWS_ENABLED
// wide-character console entry (wmainCRTStartup is selected automatically by
// link.exe when wmain is defined)
int wmain(int p_argc, wchar_t **p_argv);
#else
int main(int p_argc, char **p_argv);
#endif
