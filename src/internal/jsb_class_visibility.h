/************************************************************************/
/*  jsb_class_visibility.h                                              */
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

// Class exposure queries: whether an original (engine-side) class name is
// usable from JavaScript. These are binding-availability semantics -- they
// consult ClassDB, the omitted-classes table, project settings (ignored
// classes) and the api store -- deliberately kept out of NamingUtil so that
// the latter stays a pure string-transform utility.

#include "compat/jsb_compat.h"
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal {
class ClassVisibility {
public:
	// Classes hardcoded as not usable from JavaScript regardless of ignored-classes settings.
	static const HashSet<StringName> &get_omitted_original_classes();

	static void get_exposed_original_class_list(LocalVector<StringName> &r_list, bool p_exclude_ignored_classes = true);

	/// Inject the editor-owned ignored-classes list. Called by the editor
	/// extension at startup; shared code never reads editor settings directly.
	static void set_ignored_classes(const PackedStringArray &p_ignored_classes);

	static bool is_original_class_exposed(const StringName &p_original_name, const PackedStringArray &p_ignored_classes = {});

	// Nearest exposed ancestor of an unexposed class (empty if none).
	static StringName find_exposed_base_class(const StringName &p_unexposed_original_class);
};
} //namespace jsb::internal
