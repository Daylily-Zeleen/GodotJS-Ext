/************************************************************************/
/*  api_tool_types.cpp                                                  */
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

#include "api_tool_types.h"
#include <godot_cpp/templates/hash_map.hpp>

#ifndef DISABLE_DEPRECATED
#	include "api_tool.h"
#endif // DISABLE_DEPRECATED

using namespace godot;

namespace api_tool {

godot::String get_variant_operator_name(godot::Variant::Operator p_op) {
// Operator names as plain literals: the lookup table must NOT hold godot::String
// objects. Function-local statics holding engine-heap data are destroyed during
// DLL unload / static deinit - which can happen AFTER the engine has torn down
// its variant allocator at process exit, and the late CowData::_unref crashes.
#define __VAR_OP_TO_TEXT(op) #op
	static const char *kNames[Variant::OP_MAX] = {
		// comparison
		__VAR_OP_TO_TEXT(OP_EQUAL),
		__VAR_OP_TO_TEXT(OP_NOT_EQUAL),
		__VAR_OP_TO_TEXT(OP_LESS),
		__VAR_OP_TO_TEXT(OP_LESS_EQUAL),
		__VAR_OP_TO_TEXT(OP_GREATER),
		__VAR_OP_TO_TEXT(OP_GREATER_EQUAL),
		// mathematic
		__VAR_OP_TO_TEXT(OP_ADD),
		__VAR_OP_TO_TEXT(OP_SUBTRACT),
		__VAR_OP_TO_TEXT(OP_MULTIPLY),
		__VAR_OP_TO_TEXT(OP_DIVIDE),
		__VAR_OP_TO_TEXT(OP_NEGATE),
		__VAR_OP_TO_TEXT(OP_POSITIVE),
		__VAR_OP_TO_TEXT(OP_MODULE),
		__VAR_OP_TO_TEXT(OP_POWER),
		// bitwise
		__VAR_OP_TO_TEXT(OP_SHIFT_LEFT),
		__VAR_OP_TO_TEXT(OP_SHIFT_RIGHT),
		__VAR_OP_TO_TEXT(OP_BIT_AND),
		__VAR_OP_TO_TEXT(OP_BIT_OR),
		__VAR_OP_TO_TEXT(OP_BIT_XOR),
		__VAR_OP_TO_TEXT(OP_BIT_NEGATE),
		// logic
		__VAR_OP_TO_TEXT(OP_AND),
		__VAR_OP_TO_TEXT(OP_OR),
		__VAR_OP_TO_TEXT(OP_XOR),
		__VAR_OP_TO_TEXT(OP_NOT),
		// containment
		__VAR_OP_TO_TEXT(OP_IN),
		// no entry for OP_MAX: it is the count sentinel, not a valid operator
	};
	if (p_op < 0 || p_op >= Variant::OP_MAX) {
		// Malformed/out-of-range operator codes must never crash the
		// editor (raw array indexing is unchecked).
		ERR_PRINT(vformat("[API Tool] invalid variant operator code: %d", (int)p_op));
		return godot::String();
	}
	return godot::String(kNames[p_op]);
}

void ApiBuiltInMethod::try_load_compatible_func_ptr() const {
#ifndef DISABLE_DEPRECATED
	const StringName &method_name = method.name;
	const LocalVector<MethodHash> *compatibility_hashes = get_builtin_method_compatibility_hashes(variant_type, method_name);
	if (compatibility_hashes) {
		for (const MethodHash hash : *compatibility_hashes) {
			func = ::godot::gdextension_interface::variant_get_ptr_builtin_method(
					(GDExtensionVariantType)variant_type,
					method_name._native_ptr(),
					(GDExtensionInt)hash);
			if (func != nullptr) {
				break;
			}
		}
	}
#endif // DISABLE_DEPRECATED
}

void ApiClassMethod::try_load_compatible_method_bind() const {
#ifndef DISABLE_DEPRECATED
	const StringName &method_name = method.name;
	const LocalVector<MethodHash> *compatibility_hashes = get_class_method_compatibility_hashes(owner_class_name, method_name);
	if (compatibility_hashes) {
		for (const MethodHash hash : *compatibility_hashes) {
			method_bind = ::godot::gdextension_interface::classdb_get_method_bind(
					owner_class_name._native_ptr(),
					method_name._native_ptr(),
					(GDExtensionInt)hash);
			if (method_bind != nullptr) {
				break;
			}
		}
	}
#endif // DISABLE_DEPRECATED
}

} //namespace api_tool