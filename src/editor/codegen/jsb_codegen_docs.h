/************************************************************************/
/*  jsb_codegen_docs.h                                                  */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       */
/*  GNU Lesser General Public License for more details.                 */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// jsb_codegen_docs.h
// Class documentation cache for the C++ TS declaration generator - the
// equivalent of `TypeDB.find_doc()` in jsb.editor.codegen.ts (which called
// `jsb.editor.get_class_doc`, i.e. `_get_class_doc` in
// jsb_editor_utility_funcs.cpp). Documents are loaded lazily from api_tool
// (.bdoc sources) and cached with a negative cache, mirroring the TS
// `class_docs` map (`GodotJsb.editor.ClassDoc | false`).
//
// Member descriptions are keyed by the *exposed* names produced through
// NamingUtil (get_member_name/get_constant_name), exactly like the JS object
// keys built by `_get_class_doc`.

#include "jsb_codegen_defs.h"

#include <api_tool/editor/api_tool_doc_types.h>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb {
namespace codegen {

// Resolved document of one class (or the `@GlobalScope` pseudo-class).
// `doc.brief_description` feeds the class-level doc comment; the three maps
// feed the member doc comments. An absent entry in a map means "no doc",
// matching the TS `?.description` lookups.
struct ClassDocEntry {
	api_tool::ApiClassDocument doc;
	HashMap<String, String> constants; // exposed constant name -> description
	HashMap<String, String> methods; // exposed method name -> description
	HashMap<String, String> properties; // exposed property name -> description
};

class DocCache {
public:
	// find_doc(): returns nullptr when no document exists for the class.
	// p_original_name: the engine-side (un-renamed) class name, e.g. "Array"
	// for the exposed name "GArray". Resolves docs from api_tool.
	const ClassDocEntry *find_doc(const String &p_class_name, const String &p_original_name);

private:
	HashMap<String, ClassDocEntry *> docs_; // class name -> entry (positive cache only)
	HashSet<String> missing_docs_; // negative cache (TS: cached `false`)

	void destroy();

	// free function access (destroy_doc_cache)
	friend void destroy_doc_cache(DocCache &p_cache);
};

// releases all cached documents owned by `p_cache`
void destroy_doc_cache(DocCache &p_cache);

} // namespace codegen
} // namespace jsb
