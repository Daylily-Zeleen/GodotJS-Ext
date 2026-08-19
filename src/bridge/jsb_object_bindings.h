/************************************************************************/
/*  jsb_object_bindings.h                                               */
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
#include "jsb_class_info.h"

namespace jsb {
class Environment;

struct ObjectReflectBindingUtil {
	static NativeClassInfoPtr reflect_bind(Environment *p_env, const godot::StringName &p_class_name, NativeClassID *r_class_id);

	static void _godot_object_free(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_object_method(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_object_get2(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_object_set2(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_object_signal_get(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_object_cached_export_update(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _godot_utility_func(const v8::FunctionCallbackInfo<v8::Value> &info);
};
} //namespace jsb
