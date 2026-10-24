/************************************************************************/
/*  jsb_engine_singleton_binding.h                                      */
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

#pragma once

#include "jsb_bridge_pch.h"

namespace jsb {

// `Engine::get_singleton(name)`（模块加载器把引擎单例暴露给 JS 的路径）会让 godot-cpp
// 给单例建 wrapper + instance binding（free_callback 在本 DLL 内）。部分单例在引擎收尾
// （本 DLL 卸载之后）才销毁，届时回调跳进已卸载代码 ⇒ 0xC0000005。
// 因此 require 到单例时调用 register：记下 instance_id 并登记进 godot-cpp 的
// `ClassDB::engine_singletons`；CORE 反初始化时调用 release：逐个校验存活后归还 binding，
// 并从 godot-cpp 的盲释放表摘除（其循环对已死对象会读悬空指针）。
void register_engine_singleton_binding(const StringName &p_name, Object *p_singleton);
void release_engine_singleton_bindings();

} // namespace jsb
