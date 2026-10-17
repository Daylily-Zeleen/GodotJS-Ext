/************************************************************************/
/*  jsb_web_broker.h                                                    */
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
#include "jsb_web_pch.h"

namespace v8 {
class Isolate;
}

namespace jsb::impl {
// a helper class to break header cyclic dependencies
class Broker {
public:
	static void SetWeakCallback(v8::Isolate *isolate, HandleID value, void *parameter, void *callback);

	static JSRuntime get_engine(v8::Isolate *isolate);

	// web：一个 pthread 只能访问自己的 JS global，句柄槽存在该 realm 的 JS 堆里，
	// 因此只能由属主线程释放。跨线程的 `Global::Reset()` 经这里把释放投递回属主线程。
	// （放在 Broker 是为了避开 `Global` 头文件里 `Isolate` 是不完整类型的限制。）
	static bool owns_current_thread(v8::Isolate *isolate);
	static void proxy_release_handle(v8::Isolate *isolate, HandleID value, bool weak);
};
} //namespace jsb::impl
