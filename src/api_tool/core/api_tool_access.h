/************************************************************************/
/*  api_tool_access.h                                                   */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// core/api_tool_access.h
// Sole writer of ApiMethodBase's hot fields. Both directions of the store (JSON
// parse on the editor side, file read at runtime) must populate a method that
// intentionally exposes no setters, and the internal NO_RETURN bit must be
// folded in exactly one place. The parser uses free functions, so a friend
// declaration for ApiParser alone would not cover it.
//
// Declared here (api_tool_types.h keeps only the forward declaration), matching
// ApiStoreReader / ApiLoader in core/ and ApiMethodHotWriter in editor/: those
// accessors are friend-declared in api_tool_types.h and defined in the
// subdirectory that owns them. The friend declaration is independent of where
// the definition lives, so the hot types are unchanged.

#include "api_tool/api_tool_types.h"

namespace api_tool::internal {

struct ApiMethodAccess {
	// p_flags is the RAW Godot flags word; the internal NO_RETURN bit is folded
	// in here (the single place) from p_has_returns.
	static void setup(ApiMethodBase &r_method, const godot::StringName &p_name, uint32_t p_hash, uint32_t p_flags, bool p_has_returns, godot::Variant::Type p_return_type, GDExtensionClassMethodArgumentMetadata p_return_meta, uint16_t p_arg_count);
	static void set_index(ApiMethodBase &r_method, uint16_t p_index);
	static void set_args(ApiMethodBase &r_method, const ApiMethodArg *p_args);
	static void set_storage(ApiMethodBase &r_method, ApiMethodDetailStorage *p_storage);
	static void set_default_count(ApiMemberMethodBase &r_method, uint16_t p_default_count);
	// Store round-trip only: the internal NO_RETURN bit has no other home, so it
	// must survive a rewrite. Never use this for the external flags contract.
	static uint32_t get_flags_raw(const ApiMethodBase &p_method);
};

} //namespace api_tool::internal
