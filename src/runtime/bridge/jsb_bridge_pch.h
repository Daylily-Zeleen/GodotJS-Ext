/************************************************************************/
/*  jsb_bridge_pch.h                                                    */
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

#include <cstdint>

#include "../jsb.config.h"
#include "../jsb.gen.h"
#include "../jsb_version.h"
#include "../compat/jsb_compat.h"
#if JSB_WITH_WEB
#	include "../impl/web/jsb_web.h"
#elif JSB_WITH_NODE
	// node mode also defines JSB_WITH_V8=1 (libnode bundles V8), keep this branch first
#	include "../impl/node/jsb_node.h"
#elif JSB_WITH_V8
#	include "../impl/v8/jsb_v8.h"
#elif JSB_WITH_QUICKJS
#	include "../impl/quickjs/jsb_quickjs.h"
#elif JSB_WITH_JAVASCRIPTCORE
#	include "../impl/jsc/jsb_jsc.h"
#else
#	error unknown javascript runtime
#endif

#include "../impl/shared/jsb_isolate_scope.h"
#include "../internal/jsb_internal.h"
