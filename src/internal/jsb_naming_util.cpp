/************************************************************************/
/*  jsb_naming_util.cpp                                                 */
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

#include "jsb_internal.h"
#include "jsb_macros.h"

// GDExtension: provide local character conversion functions
#include <cctype>
_FORCE_INLINE_ char32_t _find_upper(char32_t p_char) { return (char32_t)std::toupper((int)p_char); }
_FORCE_INLINE_ char32_t _find_lower(char32_t p_char) { return (char32_t)std::tolower((int)p_char); }

// Logic is largely derived from mono/utils/naming_utils.cpp so that our naming conventions remain similar to .NET.
namespace jsb::internal {
_FORCE_INLINE_ static const HashMap<String, String> &_get_pascal_case_name_overrides() {
	static const HashMap<String, String> table = {
		{ "BitMap", "Bitmap" },
		{ "JSONRPC", "JsonRpc" },
		{ "Object", "GObject" },
		{ "Dictionary", "GDictionary" },
		{ "Error", "GError" },
		{ "Array", "GArray" },
		{ "OpenXRIPBinding", "OpenXRIPBinding" },
		{ "OpenXRIPBindingModifier", "OpenXRIPBindingModifier" },
		{ "SkeletonModification2DCCDIK", "SkeletonModification2DCcdik" },
		{ "SkeletonModification2DFABRIK", "SkeletonModification2DFabrik" },
		{ "SkeletonModification3DCCDIK", "SkeletonModification3DCcdik" },
		{ "SkeletonModification3DFABRIK", "SkeletonModification3DFabrik" },
	};
	return table;
}

// Hardcoded collection of PascalCase part conversions.
_FORCE_INLINE_ static const HashMap<String, String> &_get_pascal_case_part_overrides() {
	static const HashMap<String, String> table = {
		{ "AA", "AA" }, // Anti Aliasing
		{ "AO", "AO" }, // Ambient Occlusion
		{ "FILENAME", "FileName" },
		{ "FADEIN", "FadeIn" },
		{ "FADEOUT", "FadeOut" },
		{ "FX", "FX" },
		{ "GI", "GI" }, // Global Illumination
		{ "GZIP", "GZip" },
		{ "HBOX", "HBox" }, // Horizontal Box
		{ "ID", "Id" },
		{ "IO", "IO" }, // Input/Output
		{ "IP", "IP" }, // Internet Protocol
		{ "IV", "IV" }, // Initialization Vector
		{ "JS", "JS" }, // JavaScript
		{ "MACOS", "MacOS" },
		{ "NODEPATH", "NodePath" },
		{ "SPIRV", "SpirV" },
		{ "STDIN", "StdIn" },
		{ "STDOUT", "StdOut" },
		{ "USERNAME", "UserName" },
		{ "UV", "UV" },
		{ "UV2", "UV2" },
		{ "VBOX", "VBox" }, // Vertical Box
		{ "WHITESPACE", "WhiteSpace" },
		{ "WM", "WM" },
		{ "XR", "XR" },
		{ "XRAPI", "XRApi" },
	};
	return table;
}

String _get_pascal_case_part_override(String p_part, bool p_input_is_upper = true) {
	if (!p_input_is_upper) {
		for (int i = 0; i < p_part.length(); i++) {
			p_part[i] = _find_upper(p_part[i]);
		}
	}

	const String *result = _get_pascal_case_part_overrides().getptr(p_part);
	if (result) {
		return *result;
	}

	return String();
}

Vector<String> _split_pascal_case(const String &p_identifier) {
	Vector<String> parts;
	int current_part_start = 0;
	bool prev_was_upper = is_ascii_upper_case(p_identifier[0]);
	for (int i = 1; i < p_identifier.length(); i++) {
		if (prev_was_upper) {
			if (is_digit(p_identifier[i]) || is_ascii_lower_case(p_identifier[i])) {
				if (!is_digit(p_identifier[i])) {
					// These conditions only apply when the separator is not a digit.
					if (i - current_part_start == 1) {
						// Upper character was only the beginning of a word.
						prev_was_upper = false;
						continue;
					}

					if (i != p_identifier.length()) {
						// If this is not the last character, the last uppercase
						// character is the start of the next word.
						i--;
					}
				}

				if (i - current_part_start > 0) {
					parts.append(p_identifier.substr(current_part_start, i - current_part_start));
					current_part_start = i;
					prev_was_upper = false;
				}
			}
		} else {
			if (is_digit(p_identifier[i]) || is_ascii_upper_case(p_identifier[i])) {
				parts.append(p_identifier.substr(current_part_start, i - current_part_start));
				current_part_start = i;
				prev_was_upper = true;
			}
		}
	}

	// Add the rest of the identifier as the last part.
	if (current_part_start != p_identifier.length()) {
		parts.append(p_identifier.substr(current_part_start));
	}

	return parts;
}

String NamingUtil::pascal_to_pascal_case(const String &p_identifier) {
	if (p_identifier.length() == 0) {
		return p_identifier;
	}

	if (p_identifier.length() <= 2) {
		return p_identifier.to_upper();
	}

	const String *result = _get_pascal_case_name_overrides().getptr(p_identifier);
	if (result) {
		// Use hardcoded value for the identifier.
		return *result;
	}

	Vector<String> parts = _split_pascal_case(p_identifier);

	String ret;

	for (String &part : parts) {
		String part_override = _get_pascal_case_part_override(part);
		if (!part_override.is_empty()) {
			// Use hardcoded value for part.
			ret += part_override;
			continue;
		}

		if (part.length() <= 2 && part.to_upper() == part) {
			// Acronym of length 1 or 2.
			for (int j = 0; j < part.length(); j++) {
				part[j] = _find_upper(part[j]);
			}
			ret += part;
			continue;
		}

		part[0] = _find_upper(part[0]);
		for (int i = 1; i < part.length(); i++) {
			if (is_digit(part[i - 1])) {
				// Use uppercase after digits.
				part[i] = _find_upper(part[i]);
				continue;
			}

			part[i] = _find_lower(part[i]);
		}
		ret += part;
	}

	return ret;
}

String NamingUtil::snake_to_pascal_case(const String &p_identifier, bool p_input_is_upper) {
	String ret;
	PackedStringArray parts = p_identifier.split("_", true);

	for (int i = 0; i < parts.size(); i++) {
		String part = parts[i];
		String part_override = _get_pascal_case_part_override(part, p_input_is_upper);

		if (!part_override.is_empty()) {
			// Use hardcoded value for part.
			ret += part_override;
			continue;
		}

		if (!part.is_empty()) {
			part[0] = _find_upper(part[0]);

			for (int j = 1; j < part.length(); j++) {
				if (is_digit(part[j - 1])) {
					// Use uppercase after digits.
					part[j] = _find_upper(part[j]);
					continue;
				}

				if (p_input_is_upper) {
					part[j] = _find_lower(part[j]);
				}
			}
			ret += part;
		} else {
			if (i == 0 || i == (parts.size() - 1)) {
				// Preserve underscores at the beginning and end
				ret += "_";
			} else {
				// Preserve contiguous underscores
				if (parts[i - 1].length()) {
					ret += "__";
				} else {
					ret += "_";
				}
			}
		}
	}

	return ret;
}

String NamingUtil::snake_to_camel_case(const String &p_identifier, bool p_input_is_upper) {
	String ret;
	PackedStringArray parts = p_identifier.split("_", true);

	for (int i = 0; i < parts.size(); i++) {
		String part = parts[i];

		String part_override = _get_pascal_case_part_override(part, p_input_is_upper);
		if (!part_override.is_empty()) {
			// Use hardcoded value for part.
			if (i == 0 || (i == 1 && parts[0].is_empty())) {
				part_override[i] = _find_lower(part_override[i]);
			}
			ret += part_override;
			continue;
		}

		if (!part.is_empty()) {
			if (i == 0 || (i == 1 && parts[0].is_empty())) {
				part[i] = _find_lower(part[i]);
			} else {
				part[0] = _find_upper(part[0]);
			}
			for (int j = 1; j < part.length(); j++) {
				if (is_digit(part[j - 1])) {
					// Use uppercase after digits.
					part[j] = _find_upper(part[j]);
					continue;
				}

				if (p_input_is_upper) {
					part[j] = _find_lower(part[j]);
				}
			}
			ret += part;
		} else {
			if (i == 0 || i == (parts.size() - 1)) {
				// Preserve underscores at the beginning and end
				ret += "_";
			} else {
				// Preserve contiguous underscores
				if (parts[i - 1].length()) {
					ret += "__";
				} else {
					ret += "_";
				}
			}
		}
	}

	return ret;
}

} //namespace jsb::internal
