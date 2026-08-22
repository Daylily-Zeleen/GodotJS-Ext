/************************************************************************/
/*  jsb_monitor.h                                                       */
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

#include <runtime/compat/jsb_compat.h>
#include <runtime/bridge/jsb_statistics.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/variant.hpp>

#define JSB_DECLARE_MONITOR(MonitorName) static Variant get_value_##MonitorName()

class GodotJSMonitor {
private:
	static jsb::Statistics stats_;
	static uint64_t last_flush_tick_;

protected:
	static void flush();

	JSB_DECLARE_MONITOR(objects);
	JSB_DECLARE_MONITOR(native_classes);
	JSB_DECLARE_MONITOR(script_classes);
	JSB_DECLARE_MONITOR(cached_string_names);
	JSB_DECLARE_MONITOR(persistent_objects);
	JSB_DECLARE_MONITOR(allocated_variants);

#if JSB_WITH_V8
	JSB_DECLARE_MONITOR(heap_size);
#elif JSB_WITH_QUICKJS
	JSB_DECLARE_MONITOR(memory_used_size);
#endif

public:
	static void register_monitors();
	static void unregister_monitors();
};

