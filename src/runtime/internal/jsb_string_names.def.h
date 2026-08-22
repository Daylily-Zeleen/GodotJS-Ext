/************************************************************************/
/*  jsb_string_names.def.h                                              */
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

#ifndef DEF
#	define DEF(x)
#endif

// ONLY FREQUENTLY USED NAMES SHOULD BE LISTED HERE

// name to name
DEF(prototype)
DEF(__proto__)
DEF(constructor)
DEF(value)
DEF(id)
DEF(path)
DEF(exports)
DEF(filename)
DEF(loaded)
DEF(name)
DEF(main)
DEF(cache)
DEF(children)
DEF(type)
DEF(data)
DEF(evaluator)
DEF(_notification)
DEF(Reflect)
DEF(construct)
DEF(__jsb_type)

// class names
DEF(Object)
DEF(Node)
DEF(Variant)

// special names
DEF(free)
DEF(hint)
DEF(hint_string)
DEF(usage)
DEF(class_)

DEF(deprecated)
DEF(experimental)
DEF(help)

// special identifier for the convenience to get Variant::Type in scripts
DEF(__builtin_type__)

// godot rpc config fields
DEF(rpc_mode)
DEF(call_local)
DEF(transfer_mode)
DEF(channel)

// keyword names
DEF(default)

// worker
DEF(JSWorker)
DEF(JSWorkerParent)
DEF(ontransfer)
DEF(onmessage)
DEF(onready)
DEF(onerror)
DEF(postMessage)
DEF(transfer)
DEF(close)

#if JSB_SHADOW_REALM_ENABLED
DEF(FunctionCrossWrapper)
DEF(ObjectCrossWrapper)
DEF(JSShadowRealm)
DEF(TransferableJSShadowRealm)
DEF(has)
DEF(get)
DEF(set)
DEF(getPrototypeOf)
DEF(startupScript)
DEF(allowImportAnyModule)
#endif // JSB_SHADOW_REALM_ENABLED

// editor
#ifdef TOOLS_ENABLED
DEF(arguments)
DEF(base)
DEF(codegen)
DEF(index)
DEF(node)
DEF(properties)
DEF(resource)
DEF(GodotJSScript)
#endif

// Godot Object virtual methods
DEF(_ready)
DEF(_set)
DEF(_get)
DEF(_get_property_list)
DEF(_validate_property)
DEF(_property_can_revert)
DEF(_property_get_revert)
