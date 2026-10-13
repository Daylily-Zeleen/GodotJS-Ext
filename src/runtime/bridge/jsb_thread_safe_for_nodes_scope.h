/************************************************************************/
/*  jsb_thread_safe_for_nodes_scope.h                                   */
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

#include "jsb_bridge_pch.h"

#include <godot_cpp/classes/thread.hpp>

namespace jsb {
// 在作用域内允许当前线程访问场景树里的节点。
//
// `Node` 的 ERR_THREAD_GUARD 系列守卫读的是引擎的 thread_local 标志
// (`is_current_thread_safe_for_nodes()`)；唯一能改该标志的入口是
// `Thread::set_thread_safety_checks_enabled()`（传 false 即置位）。主线程在引擎启动时
// 已被置位，而且该接口禁止在主线程调用，所以这里跳过主线程。
//
// NOTE 引擎没有暴露该标志的读取端，取不到“进入前的值”，只能按非主线程的初值（未置位）
// 在退出时还原。全仓只有这里改这个标志。
class ThreadSafeForNodesScope {
	// 同一线程可能嵌套进入，只由最外层负责开与关
	inline static thread_local int depth_ = 0;
	bool applied_ = false;

public:
	ThreadSafeForNodesScope() {
		if (depth_++ == 0 && !::godot::Thread::is_main_thread()) {
			::godot::Thread::set_thread_safety_checks_enabled(false);
			applied_ = true;
		}
	}

	~ThreadSafeForNodesScope() {
		if (--depth_ == 0 && applied_) {
			::godot::Thread::set_thread_safety_checks_enabled(true);
		}
	}

	ThreadSafeForNodesScope(const ThreadSafeForNodesScope &) = delete;
	ThreadSafeForNodesScope &operator=(const ThreadSafeForNodesScope &) = delete;
};
} //namespace jsb
