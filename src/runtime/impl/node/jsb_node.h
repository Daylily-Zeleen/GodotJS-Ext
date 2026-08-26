/************************************************************************/
/*  jsb_node.h                                                          */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Node.js (libnode) engine layer.
//
// libnode bundles V8, so the bridge compiles against the plain V8 API exactly
// like the v8 engine layer. We therefore reuse the v8 impl's helper/catch/class
// components (all pure V8 API) and only replace the process/isolate bootstrap
// with the node-specific `GlobalInitialize` / `NodeRuntime` / `NodeBridge`.

#include "jsb_node_pch.h"

// v8 engine components (pure V8 API, provided by libnode's bundled v8 headers)
#include "../v8/jsb_v8_catch.h"
#include "../v8/jsb_v8_class.h"
#include "../v8/jsb_v8_class_builder.h"
#include "../v8/jsb_v8_pch.h"
#include "jsb_node_helper.h"

#include "jsb_node_typedef.h"

#include "jsb_node_bridge.h"
#include "jsb_node_global_init.h"
#include "jsb_node_runtime.h"
