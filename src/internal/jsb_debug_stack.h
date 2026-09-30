/************************************************************************/
/*  jsb_debug_stack.h                                                   */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// JavaScript 调用栈的一帧，形状与 `ScriptLanguage` 的调试 hook 所需一致
// （文件/函数/行）。刻意与引擎无关：各 `impl` 层按自家 VM 能给的东西填它，
// 桥把它交给脚本编辑器时无需知道是哪个 VM 产生的。

#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb {
struct DebugStackFrame {
	godot::String file;
	godot::String function;
	// 1 基，与调试器显示的一致。0 表示 VM 不上报该项
	// （quickjs 完全不暴露栈的列号）。
	int line = 0;
	int column = 0;
};

// 一帧快照的容器。
// 用 `LocalVector`：它是本类独占、生命周期内不共享的临时缓冲，`LocalVector` 没有
// `Vector` 那层引用计数；而 `Vector` 的拷贝虽是浅拷贝（CowData 加引用），对
// 「独占且原地改写」的用法毫无好处，只多一层间接。
// 帧数上限不用常量，而由用户设置
// `godotjs_ext/runtime/debugger/max_stack_frames` 决定
// （见 `jsb::internal::settings::project::get_debug_max_stack_frames`）。
typedef godot::LocalVector<DebugStackFrame> DebugStackFrameList;
} //namespace jsb
