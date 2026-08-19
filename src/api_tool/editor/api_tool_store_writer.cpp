/************************************************************************/
/*  api_tool_store_writer.cpp                                           */
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

// editor/api_tool_store_writer.cpp
// Binary file writing implementation (editor-only, TOOLS_ENABLED).

#include "api_tool_store_writer.h"
#include "../core/api_tool_payload.h"

using namespace godot;

namespace api_tool::internal {

// ============================================================================
// PayloadWriter: builds file.
// ============================================================================
using PayloadWriter = ApiToolPayload<false>;

// ============================================================================
// Serialization helpers: structs -> PayloadWriter (v3: PropertyInfo/MethodInfo)
// ============================================================================

static void serialize_property_info(PayloadWriter &w, const PropertyInfo &pi) {
	w.write(pi.type);
	w.write(pi.name);
	w.write(pi.class_name);
	w.write(pi.hint);
	w.write(pi.hint_string);
	w.write(pi.usage);
}

static void serialize_method_info_payload(PayloadWriter &w, const MethodInfo &mi) {
	w.write(mi.name);
	w.write(mi.flags);
	w.write(mi.id);
	w.write(mi.return_val, serialize_property_info);
	w.write(mi.arguments, serialize_property_info);
	w.write(mi.default_arguments);
	w.write(mi.return_val_metadata);
	w.write(mi.arguments_metadata);
}

template <typename TApiMethodInfo>
	requires std::is_base_of_v<ApiMemberMethodBase, TApiMethodInfo>
static void serialize_api_method_info(PayloadWriter &w, const TApiMethodInfo &ami) {
	w.write(ami.method, serialize_method_info_payload);
	w.write(ami.hash);
}

static void serialize_utility_function_info(PayloadWriter &r, const ApiUtilityFunction &ami) {
	r.write(ami.method, serialize_method_info_payload);
	r.write(ami.hash);
	r.write(ami.category);
}

static void serialize_enum_value(PayloadWriter &w, const ApiEnumValue &v) {
	w.write(v.name);
	w.write(v.value);
}

static void serialize_enum_info(PayloadWriter &w, const ApiEnumInfo &v) {
	w.write(v.name);
	w.write(v.is_bitfield);
	w.write(v.values, serialize_enum_value);
}

static void serialize_constant_info(PayloadWriter &w, const ApiConstantInfo &v) {
	w.write(v.name);
	w.write(v.value);
	w.write(v.is_bitfield);
}

static void serialize_builtin_class_constant_info(PayloadWriter &w, const ApiBuiltInClassConstantInfo &v) {
	w.write(v.name);
	w.write(v.type);
	w.write(v.value);
}

static void serialize_signal_info(PayloadWriter &r, const ApiSignalInfo &v) {
	r.write(v.name);
	r.write(v.arguments, serialize_property_info);
}

static void serialize_api_property_info(PayloadWriter &r, const ApiPropertyInfo &v) {
	r.write(v.property, serialize_property_info);
	r.write(v.setter);
	r.write(v.getter);
	r.write(v.index);
}

static void serialize_operator_info(PayloadWriter &r, const ApiOperatorInfo &v) {
	r.write(v.op);
	r.write(v.return_type);
	r.write(v.left_type);
	r.write(v.right_type);
}

static void serialize_constructor_info(PayloadWriter &r, const ApiConstructorInfo &v) {
	r.write(v.arguments, serialize_property_info);
}

static void serialize_method_compat_hashes(PayloadWriter &w, const ApiMethodCompatibilityHashes &v) {
#ifndef DISABLE_DEPRECATED
	w.write(v.method_name);
	w.write(v.hashes);
#endif // DISABLE_DEPRECATED
}

static void serialize_member_info(PayloadWriter &r, const ApiMemberInfo &v) {
	r.write(v.name);
	r.write(v.type);
}

// ============================================================================
// ApiStoreWriter: Header
// ============================================================================

Error ApiStoreWriter::write_header(const String &p_path, const ApiHeader &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.version_major);
	w.write(p_data.version_minor);
	w.write(p_data.version_patch);
	w.write(p_data.version_status);
	w.write(p_data.version_build);
	w.write(p_data.version_full_name);
	w.write(p_data.precision);
	return OK;
}

// ============================================================================
// ApiStoreWriter: Utility Functions (single file, all at once)
// ============================================================================

Error ApiStoreWriter::write_utility_functions(const String &p_path, const LocalVector<ApiUtilityFunction> &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data, serialize_utility_function_info);
	return OK;
}

// ============================================================================
// ApiStoreWriter: BuiltinType
// ============================================================================

Error ApiStoreWriter::write_builtin_class(const String &p_path, const ApiBuiltinClass &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.type);
	w.write(p_data.has_indexing_return_type);
	if (p_data.has_indexing_return_type) {
		w.write(p_data.indexing_type);
	}
	w.write(p_data.is_keyed);
	w.write(p_data.has_destructor);
	w.write(p_data.members, serialize_member_info);
	w.write(p_data.constants, serialize_builtin_class_constant_info);
	w.write(p_data.enums, serialize_enum_info);

	w.write(p_data.methods, serialize_api_method_info<ApiBuiltInMethod>);
	w.write(p_data.operators, serialize_operator_info);
	w.write(p_data.constructors, serialize_constructor_info);

	return OK;
}

// ============================================================================
// ApiStoreWriter: Class
// ============================================================================

Error ApiStoreWriter::write_class(const String &p_path, const ApiClass &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.inherits);
	w.write(p_data.api_type);
	w.write(p_data.is_refcounted);
	w.write(p_data.is_instantiable);
	w.write(p_data.methods, serialize_api_method_info<ApiClassMethod>);
	w.write(p_data.signals, serialize_signal_info);
	w.write(p_data.properties, serialize_api_property_info);
	w.write(p_data.enums, serialize_enum_info);
	w.write(p_data.constants, serialize_constant_info);
	return OK;
}

// ============================================================================
// ApiStoreWriter: Global Enum
// ============================================================================

Error ApiStoreWriter::write_global_enum(const String &p_path, const ApiEnumInfo &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data, serialize_enum_info);
	return OK;
}

// ============================================================================
// ApiStoreWriter: Global Constant
// ============================================================================

Error ApiStoreWriter::write_global_constant(const String &p_path, const ApiConstantInfo &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data, serialize_constant_info);
	return OK;
}

// ============================================================================
// ApiStoreWriter: Singletons (all in one file)
// ============================================================================

Error ApiStoreWriter::write_singletons(const String &p_path, const LocalVector<ApiSingleton> &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data, [](PayloadWriter &w, const ApiSingleton &s) {
		w.write(s.name);
		w.write(s.type);
	});
	return OK;
}

// ============================================================================
// ApiStoreWriter: Native Structures (all in one file)
// ============================================================================

Error ApiStoreWriter::write_native_structures(const String &p_path, const LocalVector<ApiNativeStructure> &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data, [](PayloadWriter &w, const ApiNativeStructure &s) {
		w.write(s.name);
		w.write(s.format);
	});
	return OK;
}

// ============================================================================
// ApiStoreWriter: Compatibility Hashes (per-class file)
// ============================================================================

Error ApiStoreWriter::write_compatibility_hashes(const String &p_path, const ApiCompatibilityHashData &p_data) {
#ifndef DISABLE_DEPRECATED
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.methods, serialize_method_compat_hashes);
#endif // DISABLE_DEPRECATED
	return OK;
}

// ============================================================================
// ApiStoreWriter: Class Document
// ============================================================================

static void serialize_method_document(PayloadWriter &w, const ApiMethodDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_signal_document(PayloadWriter &w, const ApiSignalDocument &d) {
	w.write(d.name);
	w.write(d.description);
	w.write(d.arguments, serialize_property_info);
}

static void serialize_property_document(PayloadWriter &w, const ApiPropertyDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_member_document(PayloadWriter &w, const ApiMemberDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_constant_document(PayloadWriter &w, const ApiConstantDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_enum_value_document(PayloadWriter &w, const ApiEnumValueDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_enum_document(PayloadWriter &w, const ApiEnumDocument &d) {
	w.write(d.name);
	w.write(d.values, serialize_enum_value_document);
}

static void serialize_operator_document(PayloadWriter &w, const ApiOperatorDocument &d) {
	w.write(d.name);
	w.write(d.description);
}

static void serialize_constructor_document(PayloadWriter &w, const ApiConstructorDocument &d) {
	w.write(d.description);
}

Error ApiStoreWriter::write_document(const String &p_path, const ApiClassDocument &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.brief_description);
	w.write(p_data.description);
	w.write(p_data.methods, serialize_method_document);
	w.write(p_data.signals, serialize_signal_document);
	w.write(p_data.properties, serialize_property_document);
	w.write(p_data.enums, serialize_enum_document);
	w.write(p_data.constants, serialize_constant_document);
	w.write(p_data.operators, serialize_operator_document);
	w.write(p_data.constructors, serialize_constructor_document);
	return OK;
}

Error ApiStoreWriter::write_utility_function_document(const String &p_path, const ApiUtilityFunctionDocument &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.description);
	return OK;
}

Error ApiStoreWriter::write_global_enum_document(const String &p_path, const ApiGlobalEnumDocument &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.values, serialize_enum_value_document);
	return OK;
}

Error ApiStoreWriter::write_global_constant_document(const String &p_path, const ApiGlobalConstantDocument &p_data) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.description);
	return OK;
}

} //namespace api_tool::internal
