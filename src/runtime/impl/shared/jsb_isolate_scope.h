/************************************************************************/
/*  jsb_isolate_scope.h                                                 */
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

#include "../../jsb.config.h"

// two-level indirection so __LINE__ is expanded before token pasting
#define JSB_CONCAT2_(a, b) a##b
#define JSB_CONCAT_(a, b) JSB_CONCAT2_(a, b)
#define JSB_LINE_NAME_(prefix) JSB_CONCAT_(prefix, __LINE__)

#if JSB_USE_V8_LOCKER_PER_ISOLATE_SCOPE && JSB_WITH_V8
#	include <v8-locker.h>
#	define JSB_ISOLATE_SCOPE(isolate) \
		v8::Locker JSB_LINE_NAME_(jsb_locker_)(isolate); \
		v8::Isolate::Scope JSB_LINE_NAME_(jsb_isolate_scope_)(isolate)
#else
#	define JSB_ISOLATE_SCOPE(isolate) v8::Isolate::Scope JSB_LINE_NAME_(jsb_isolate_scope_)(isolate)
#endif
