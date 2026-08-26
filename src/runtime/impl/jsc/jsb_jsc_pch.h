/************************************************************************/
/*  jsb_jsc_pch.h                                                       */
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

#include "../../internal/jsb_internal.h"
#include "../../jsb.gen.h"
#include "../../impl/shared/jsb_custom_field.h"
#include "../../impl/shared/jsb_statistics.h"

#include "compat/ring_buffer.h"

//TODO WARNING: ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY.
#if !defined(API_AVAILABLE) && !defined(MACOS_ENABLED) && !defined(IOS_ENABLED)
#	define API_AVAILABLE(...)
#endif

#include <JavaScriptCore/JavaScriptCore.h>

//NOTE the header file for WeakRef is private in webkit, we copy it here. hope it's a viable plan :)
#include "JSWeakPrivate.h"

// Apple headers define `nil` as a macro; this leaks into C++ code and can break
// identifiers named `nil` in non-ObjC translation units.
#ifdef nil
#	undef nil
#endif

#include <cstdint>
#include <memory>

#define JSB_JSC_LOG(Severity, Format, ...) JSB_LOG_IMPL(jsc, Severity, Format, ##__VA_ARGS__)
