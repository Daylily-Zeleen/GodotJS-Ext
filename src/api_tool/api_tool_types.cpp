#include "api_tool_types.h"
#include <godot_cpp/templates/hash_map.hpp>

#ifndef DISABLE_DEPRECATED
#include "api_tool.h"
#endif // DISABLE_DEPRECATED

using namespace godot;

namespace api_tool {

const godot::String &get_variant_operator_name(godot::Variant::Operator p_op) {
#define __VAR_OP_TO_TEXT(op) KeyValue<Variant::Operator, String>(Variant::Operator::op, (#op))
	static const HashMap<Variant::Operator, String> search = {
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
		__VAR_OP_TO_TEXT(OP_MAX),
	};
	return search[p_op];
}

void ApiBuiltInMethod::try_load_compatible_func_ptr() const {
#ifndef DISABLE_DEPRECATED
	const StringName &method_name = method.name;
	const LocalVector<MethodHash>* compatibility_hashes = get_builtin_method_compatibility_hashes(variant_type, method_name);
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
	const LocalVector<MethodHash>* compatibility_hashes = get_class_method_compatibility_hashes(owner_class_name, method_name);
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