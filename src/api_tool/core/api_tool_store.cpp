/************************************************************************/
/*  api_tool_store.cpp                                                  */
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

// core/api_tool_store.cpp
// Binary file reading implementation (runtime, core/).

#include "api_tool_store.h"
#include "api_tool_payload.h"

using namespace godot;

namespace api_tool::internal {

// ============================================================================
// PayloadReader: reads from a file.
// ============================================================================
using PayloadReader = ApiToolPayload<true>;

// ============================================================================
// Deserialization helpers
// ============================================================================

static void deserialize_property_info(PayloadReader &r, PropertyInfo &pi) {
	r.read(pi.type);
	r.read(pi.name);
	r.read(pi.class_name);
	r.read(pi.hint);
	r.read(pi.hint_string);
	r.read(pi.usage);
}

static void deserialize_method_info_core(PayloadReader &r, MethodInfo &mi) {
	r.read(mi.name);
	r.read(mi.flags);
	r.read(mi.id);
	r.read(mi.return_val, deserialize_property_info);
	r.read(mi.arguments, deserialize_property_info);
	r.read(mi.default_arguments);
	r.read(mi.return_val_metadata);
	r.read(mi.arguments_metadata);
}

template <typename TApiMethodInfo>
	requires std::is_base_of_v<ApiMemberMethodBase, TApiMethodInfo>
static void deserialize_api_method_info(PayloadReader &r, TApiMethodInfo &ami) {
	r.read(ami.method, deserialize_method_info_core);
	r.read(ami.hash);
}

static void deserialize_utility_function_info(PayloadReader &r, ApiUtilityFunction &ami) {
	r.read(ami.method, deserialize_method_info_core);
	r.read(ami.hash);
	r.read(ami.category);
}

static void deserialize_enum_value(PayloadReader &r, ApiEnumValue &v) {
	r.read(v.name);
	r.read(v.value);
}

static void deserialize_enum_info(PayloadReader &r, ApiEnumInfo &v) {
	r.read(v.name);
	r.read(v.is_bitfield);
	r.read(v.values, deserialize_enum_value);
}

static void deserialize_constant_info(PayloadReader &r, ApiConstantInfo &v) {
	r.read(v.name);
	r.read(v.value);
	r.read(v.is_bitfield);
}

static void deserialize_builtin_class_constant_info(PayloadReader &r, ApiBuiltInClassConstantInfo &v) {
	r.read(v.name);
	r.read(v.type);
	r.read(v.value);
}

static void deserialize_signal_info(PayloadReader &r, ApiSignalInfo &v) {
	r.read(v.name);
	r.read(v.arguments, deserialize_property_info);
}

static void deserialize_api_property_info(PayloadReader &r, ApiPropertyInfo &v) {
	r.read(v.property, deserialize_property_info);
	r.read(v.setter);
	r.read(v.getter);
	r.read(v.index);
}

static void deserialize_operator_info(PayloadReader &r, ApiOperatorInfo &v) {
	r.read(v.op);
	r.read(v.return_type);
	r.read(v.left_type);
	r.read(v.right_type);
}

static void deserialize_constructor_info(PayloadReader &r, ApiConstructorInfo &v) {
	r.read(v.arguments, deserialize_property_info);
}

static void deserialize_method_compat_hashes(PayloadReader &r, ApiMethodCompatibilityHashes &v) {
#ifndef DISABLE_DEPRECATED
	r.read(v.method_name);
	r.read(v.hashes);
#endif // DISABLE_DEPRECATED
}

static void deserialize_member_info(PayloadReader &r, ApiMemberInfo &v) {
	r.read(v.name);
	r.read(v.type);
}

// ============================================================================
// ApiStoreReader: Header
// ============================================================================

Error ApiStoreReader::read_header(const String &p_path, ApiHeader &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.version_major);
	r.read(r_data.version_minor);
	r.read(r_data.version_patch);
	r.read(r_data.version_status);
	r.read(r_data.version_build);
	r.read(r_data.version_full_name);
	r.read(r_data.precision);
	return OK;
}

// ============================================================================
// ApiStoreReader: Utility Functions (single file)
// ============================================================================

Error ApiStoreReader::read_utility_functions(const String &p_path, LocalVector<ApiUtilityFunction> &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, deserialize_utility_function_info);
	return OK;
}

// ============================================================================
// ApiStoreReader: BuiltinClass
// ============================================================================

Error ApiStoreReader::read_builtin_class(const String &p_path, ApiBuiltinClass &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.type);
	r.read(r_data.has_indexing_return_type);
	if (r_data.has_indexing_return_type) {
		r.read(r_data.indexing_type);
	}
	r.read(r_data.is_keyed);
	r.read(r_data.has_destructor);
	r.read(r_data.members, deserialize_member_info);
	r.read(r_data.constants, deserialize_builtin_class_constant_info);
	r.read(r_data.enums, deserialize_enum_info);

	r.read(r_data.methods, deserialize_api_method_info<ApiBuiltInMethod>);
	r.read(r_data.operators, deserialize_operator_info);
	r.read(r_data.constructors, deserialize_constructor_info);

	// Special handle for initialization.
	for (auto &m : r_data.methods) {
		m.variant_type = r_data.type;
	}
	r_data.initialize();
	return OK;
}

// ============================================================================
// ApiStoreReader: Class
// ============================================================================

Error ApiStoreReader::read_class(const String &p_path, ApiClass &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.name);
	r.read(r_data.inherits);
	r.read(r_data.api_type);
	r.read(r_data.is_refcounted);
	r.read(r_data.is_instantiable);
	r.read(r_data.methods, deserialize_api_method_info<ApiClassMethod>);
	r.read(r_data.signals, deserialize_signal_info);
	r.read(r_data.properties, deserialize_api_property_info);
	r.read(r_data.enums, deserialize_enum_info);
	r.read(r_data.constants, deserialize_constant_info);

	// Special handle for initialization.
	for (auto &m : r_data.methods) {
		m.owner_class_name = r_data.name;
	}
	return OK;
}

// ============================================================================
// ApiStoreReader: Global Enum
// ============================================================================

Error ApiStoreReader::read_global_enum(const String &p_path, ApiEnumInfo &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, deserialize_enum_info);
	return OK;
}

// ============================================================================
// ApiStoreReader: Global Constant
// ============================================================================

Error ApiStoreReader::read_global_constant(const String &p_path, ApiConstantInfo &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, deserialize_constant_info);
	return OK;
}

// ============================================================================
// ApiStoreReader: Singletons (single file)
// ============================================================================

Error ApiStoreReader::read_singletons(const String &p_path, LocalVector<ApiSingleton> &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, [](PayloadReader &r, ApiSingleton &s) {
		r.read(s.name);
		r.read(s.type);
	});
	return OK;
}

// ============================================================================
// ApiStoreReader: Native Structures (single file)
// ============================================================================

Error ApiStoreReader::read_native_structures(const String &p_path, LocalVector<ApiNativeStructure> &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, [](PayloadReader &r, ApiNativeStructure &ns) {
		r.read(ns.name);
		r.read(ns.format); // TODO: 解析后调整反序列化
	});
	return OK;
}

// ============================================================================
// ApiStoreReader: Compatibility Hashes (per-class file)
// ============================================================================

Error ApiStoreReader::read_compatibility_hashes(const godot::String &p_path, ApiCompatibilityHashData &r_data) {
#ifndef DISABLE_DEPRECATED
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data.methods, deserialize_method_compat_hashes);
#endif // DISABLE_DEPRECATED
	return OK;
}

} //namespace api_tool::internal