/************************************************************************/
/*  jsb_naming_util.h                                                   */
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

#include "jsb_settings.h"
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal {
class NamingUtil {
public:
	static String pascal_to_pascal_case(const String &p_identifier);

	static String snake_to_pascal_case(const String &p_identifier, bool p_input_is_upper = false);

	static String snake_to_camel_case(const String &p_identifier, bool p_input_is_upper = false);

	static void get_exposed_original_class_list(LocalVector<StringName> &r_list, bool p_exclude_ignored_classes = true);

	static bool is_original_class_exposed(const StringName &p_original_name, const PackedStringArray &p_ignored_classes = {});

	static StringName find_exposed_base_class(const StringName &p_unexposed_original_class);

	// Classes hardcoded as not usable from JavaScript regardless of ignored-classes settings.
	static const HashSet<StringName> &get_omitted_original_classes();

	static String get_class_name(const String &p_original_name) {
		if (Settings::get_camel_case_bindings_enabled()) {
			return pascal_to_pascal_case(p_original_name);
		}

		if (p_original_name == Variant::get_type_name(Variant::DICTIONARY)) {
			return "GDictionary";
		}

		if (p_original_name == Variant::get_type_name(Variant::ARRAY)) {
			return "GArray";
		}

		return p_original_name;
	}

	static String get_constant_name(const String &p_original_name) {
		return p_original_name;
	}

	static String get_enum_name(const String &p_original_name) {
		if (Settings::get_camel_case_bindings_enabled()) {
			return pascal_to_pascal_case(p_original_name);
		}

		return p_original_name;
	}

	static String get_enum_value_name(const String &p_original_value_name) {
		if (Settings::get_camel_case_bindings_enabled()) {
			return snake_to_pascal_case(p_original_value_name, true);
		}

		return p_original_value_name;
	}

	static String get_member_name(const String &p_original_name) {
		if (Settings::get_camel_case_bindings_enabled()) {
			return snake_to_camel_case(p_original_name);
		}

		return p_original_name;
	}

	static String get_parameter_name(const String &p_original_name) {
		if (Settings::get_camel_case_bindings_enabled()) {
			return snake_to_camel_case(p_original_name);
		}

		return p_original_name;
	}

	static String validate_ascii_identifier(const String &p_original) {
		if (p_original.is_empty()) {
			return "_"; // Empty string is not a valid identifier.
		}

		String result;
		if (is_digit(p_original[0])) {
			result = "_" + p_original;
		} else {
			result = p_original;
		}

		int len = result.length();
		char32_t *buffer = result.ptrw();
		for (int i = 0; i < len; i++) {
			if (!is_ascii_identifier_char(buffer[i])) {
				buffer[i] = '_';
			}
		}

		return result;
	}
};
} //namespace jsb::internal

