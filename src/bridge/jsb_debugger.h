/************************************************************************/
/*  jsb_debugger.h                                                      */
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

#pragma once
#include "jsb_bridge_pch.h"

#if JSB_WITH_DEBUGGER
namespace jsb {
class Environment;

class JavaScriptDebugger {
public:
	JavaScriptDebugger();
	~JavaScriptDebugger();

	void init(v8::Isolate *p_isolate, uint16_t p_port);
	void update();
	void drop();
	bool is_initialized() const;

protected:
	void on_context_created(const v8::Local<v8::Context> &p_context);
	void on_context_destroyed(const v8::Local<v8::Context> &p_context);

	class JavaScriptDebuggerImpl *impl;

	friend class Environment;
};
} //namespace jsb
#endif // JSB_WITH_DEBUGGER

