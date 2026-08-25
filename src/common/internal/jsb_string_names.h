/************************************************************************/
/*  jsb_string_names.h                                                  */
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

#include <godot_cpp/variant/packed_string_array.hpp>
#include "jsb_internal_pch.h"
#include "jsb_macros.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/string_name.hpp>

#define jsb_string_name(name) ::jsb::internal::StringNames::get_singleton().sn_##name
#define jsb_literal(name) (sizeof(::jsb::internal::StringNames::sn_##name) == sizeof(StringName), #name)

class GodotJSScriptLanguage;

namespace jsb::internal {
class StringNames {
private:
	friend class ::GodotJSScriptLanguage;

	static JSB_RUNTIME_API StringNames *singleton_;

	static void create();
	static void free();

	// we need to ignore some names used in godot (such as XXX.name) to avoid conflicts in javascript.
	// for instance, the GodotJS script name is determined with the `name` property of a javascript class.
	HashSet<StringName> ignored_;

	// replace confusing names (such as Dictionary/Array)
	HashMap<StringName, StringName> replacements_; // original => modified (Array => GArray)
	HashMap<StringName, StringName> replacements_inv_; // modified => original (GArray => Array)

	StringNames();

public:
	// non-inline: see note on GodotJSScriptLanguage::get_singleton. The editor
	// library must obtain this through a cross-DLL function call rather than an
	// inlined read of the exported `singleton_` data symbol.
	static StringNames &get_singleton();

	_FORCE_INLINE_ bool is_ignored(const StringName &p_name) const { return ignored_.has(p_name); }

	_FORCE_INLINE_ bool is_replaced_name(const StringName &p_name) const { return replacements_.has(p_name); }

	_FORCE_INLINE_ StringName get_replaced_name(const StringName &p_name) const {
		if (const StringName *ptr = replacements_.getptr(p_name)) return *ptr;
		return p_name;
	}

	_FORCE_INLINE_ StringName get_original_name(const StringName &p_name) const {
		if (const StringName *ptr = replacements_inv_.getptr(p_name)) return *ptr;
		return p_name;
	}

	void add_replacement(const StringName &name, const StringName &replacement) {
		replacements_.insert(name, replacement);
		replacements_inv_.insert(replacement, name);
	}


	StringName sn_godot_typeloader;
	StringName sn_godot_postbind;

#pragma push_macro("DEF")
#undef DEF
#define DEF(KeyName) StringName sn_##KeyName;
#include "jsb_string_names.def.h"
#pragma pop_macro("DEF")
};
} //namespace jsb::internal
