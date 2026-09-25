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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

// editor/api_tool_store_writer.cpp
// Binary file writing implementation

#include "api_tool_store_writer.h"
#include "api_tool/core/api_tool_detail_storage.h"
#include "api_tool/core/api_tool_payload.h"
#include "api_tool_detail_storage_editor.h"

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

// ============================================================================
// Hot method record
// ============================================================================
// Mirrors ApiStoreReader::deserialize_method_hot (design.md §7):
//   name, flags(raw, incl. the internal NO_RETURN bit), hash, return_type(u8),
//   return_meta(u8), arg_count(u16), default_count(u16), then arg_count compact
//   {type(u8), meta(u8)} pairs.
// The full PropertyInfo detail goes to the cold detail section and the default
// values to the defaults section, both written once per entity.

// Mirrors ApiStoreReader::deserialize_method_hot FIELD BY FIELD - the payload
// write/read templates encode by the argument's own type, so every field here
// must be written with the same concrete type the reader declares (a missing
// cast or a wider type silently shifts the whole stream).
void ApiMethodHotWriter::serialize_method_hot(PayloadWriter &w, const ApiMethodBase &p_method, uint16_t p_default_count) {
	w.write(p_method.get_name()); // StringName
	w.write(p_method.flags_); // uint32_t (RAW: the internal NO_RETURN bit has no other home)
	w.write(p_method.hash_); // uint32_t
	w.write(p_method.ret_.type); // VariantType = uint8_t
	w.write(p_method.ret_.meta); // ArgMeta = uint8_t
	w.write(p_method.arg_count_); // uint16_t
	w.write(p_default_count); // uint16_t
	for (uint16_t i = 0; i < p_method.arg_count_; i++) {
		w.write(p_method.args_[i].type); // VariantType = uint8_t
		w.write(p_method.args_[i].meta); // ArgMeta = uint8_t
	}
}

// Default count of a method, taken from the entity's cold data (the hot record
// is written from the same source, so the two can never disagree).
static uint16_t cold_default_count_of(const internal::ApiMethodDetailStorage *p_storage, uint32_t p_index) {
	return p_storage != nullptr ? (uint16_t)internal::ApiMethodColdAccess::cold_default_count(*p_storage, p_index) : 0;
}

// The cold detail record: the faithful remainder of the JSON method object.
static void serialize_method_detail(PayloadWriter &w, const internal::ApiMethodDetail &p_detail) {
	w.write(p_detail.return_val, serialize_property_info);
	w.write((uint32_t)p_detail.arguments.size());
	for (uint32_t i = 0; i < p_detail.arguments.size(); i++) {
		w.write(p_detail.arguments[i], serialize_property_info);
	}
}

// Writes the cold sections of one entity plus the two words that close the hot
// section, for p_method_count methods:
//   u32 detail_method_count ; u64 detail_section_size ; [detail...]
//   u32 defaults_method_count ; u16 counts[count] ; Variant[...]
//
// The detail length is only known once the records are written, so a zero
// placeholder is emitted first and patched afterwards. That is safe here
// because FileAccessCompressed allows seeking back inside already-written data.
static void serialize_cold_sections(PayloadWriter &w, const internal::ApiMethodDetailStorage *p_storage, uint32_t p_method_count) {
	w.write(p_method_count);
	const uint64_t size_pos = w.get_position();
	w.write((uint64_t)0); // placeholder, patched once the section is written
	const uint64_t detail_start = w.get_position();
	for (uint32_t i = 0; i < p_method_count; i++) {
		if (p_storage != nullptr) {
			serialize_method_detail(w, internal::ApiMethodColdAccess::cold_details(*p_storage)[i]);
		} else {
			serialize_method_detail(w, internal::ApiMethodDetail());
		}
	}
	const uint64_t detail_end = w.get_position();
	w.seek(size_pos);
	w.write(detail_end - detail_start);
	w.seek(detail_end);

	// Defaults: per-method counts first, then the flat Variant block, so a
	// reader that only needs the counts never has to decode a single Variant.
	w.write(p_method_count);
	for (uint32_t i = 0; i < p_method_count; i++) {
		w.write((uint16_t)cold_default_count_of(p_storage, i));
	}
	for (uint32_t i = 0; i < p_method_count; i++) {
		const uint32_t n = cold_default_count_of(p_storage, i);
		const Variant *values = p_storage != nullptr ? internal::ApiMethodColdAccess::cold_defaults(*p_storage, i) : nullptr;
		for (uint32_t j = 0; j < n; j++) {
			w.write(values[j]);
		}
	}
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

Error ApiStoreWriter::write_utility_functions(const String &p_path, const LocalVector<ApiUtilityFunction> &p_data, const internal::ApiMethodDetailStorage *p_storage) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write((uint32_t)p_data.size());
	for (uint32_t i = 0; i < p_data.size(); i++) {
		// Utility functions carry no defaults, so the hot record's default
		// count is always 0 and no counts have to be tracked here.
		ApiMethodHotWriter::serialize_method_hot(w, p_data[i], 0);
		w.write(p_data[i].category);
	}
	serialize_cold_sections(w, p_storage, (uint32_t)p_data.size());
	return OK;
}

// ============================================================================
// ApiStoreWriter: BuiltinType
// ============================================================================

Error ApiStoreWriter::write_builtin_class(const String &p_path, const ApiBuiltinClass &p_data, const internal::ApiMethodDetailStorage *p_storage) {
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

	w.write((uint32_t)p_data.methods.size());
	for (uint32_t i = 0; i < p_data.methods.size(); i++) {
		ApiMethodHotWriter::serialize_method_hot(w, p_data.methods[i], (uint16_t)cold_default_count_of(p_storage, i));
	}
	w.write(p_data.operators, serialize_operator_info);
	w.write(p_data.constructors, serialize_constructor_info);

	serialize_cold_sections(w, p_storage, (uint32_t)p_data.methods.size());
	return OK;
}

// ============================================================================
// ApiStoreWriter: Class
// ============================================================================

Error ApiStoreWriter::write_class(const String &p_path, const ApiClass &p_data, const internal::ApiMethodDetailStorage *p_storage) {
	Error err{ OK };
	std::unique_ptr<PayloadWriter> w_ptr = PayloadWriter::open(p_path, err);
	if (err) return err;
	PayloadWriter &w = *w_ptr.get();

	w.write(p_data.name);
	w.write(p_data.inherits);
	w.write(p_data.api_type);
	w.write(p_data.is_refcounted);
	w.write(p_data.is_instantiable);
	w.write((uint32_t)p_data.methods.size());
	for (uint32_t i = 0; i < p_data.methods.size(); i++) {
		ApiMethodHotWriter::serialize_method_hot(w, p_data.methods[i], (uint16_t)cold_default_count_of(p_storage, i));
	}
	w.write(p_data.signals, serialize_signal_info);
	w.write(p_data.properties, serialize_api_property_info);
	w.write(p_data.enums, serialize_enum_info);
	w.write(p_data.constants, serialize_constant_info);

	serialize_cold_sections(w, p_storage, (uint32_t)p_data.methods.size());
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
