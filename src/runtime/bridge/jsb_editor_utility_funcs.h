/************************************************************************/
/*  jsb_editor_utility_funcs.h                                          */
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
namespace jsb {
struct EditorUtilityFuncs {
	typedef void (*ExposeFunc)(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj);

	// Called by the runtime bridge module loader to populate the `jsb.editor` object.
	// Dispatches to the implementation registered by the editor library, or installs
	// editor-only stubs when no implementation is available (non-editor builds).
	static void expose(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj);

	// Registered by the editor library at load time to provide the real implementation.
	static void set_expose_impl(ExposeFunc p_func);
};
} //namespace jsb
