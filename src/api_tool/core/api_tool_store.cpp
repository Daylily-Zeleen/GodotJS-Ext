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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
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
#include "api_tool/core/api_tool_access.h"
#include "api_tool/core/api_tool_detail_storage.h"

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

// ============================================================================
// Hot method record
// ============================================================================
// Layout of one method record inside the hot section (see design.md §7):
//   name, flags(raw, incl. the internal NO_RETURN bit), hash, return_type(u8),
//   return_meta(u8), arg_count(u16), default_count(u16), then arg_count compact
//   {type(u8), meta(u8)} pairs.
// Everything else extension_api.json carried for the method lives in the cold
// detail section; the default values live in the defaults section.
//
// The compact argument records are NOT stored per method: they go into one flat
// entity-level block, so a whole entity costs a single allocation.

// Reads one method's hot fields plus its compact argument records, appending the
// arguments to p_args and reporting the count.
// Mirrors ApiStoreWriter::serialize_method_hot FIELD BY FIELD - the payload
// read/write templates encode by the argument's own type, so every field
// declared here must match the concrete type the writer uses (a missing cast
// or a wider type silently shifts the whole stream).
template <typename TApiMethodInfo>
static void deserialize_method_hot(PayloadReader &r, TApiMethodInfo &r_method, uint16_t p_index, LocalVector<internal::ApiMethodArg> &r_args, uint16_t &r_arg_count) {
	// Field order mirrors serialize_method_hot exactly: name, flags(RAW), hash,
	// ret type/meta, arg_count, default_count.
	StringName name;
	uint32_t flags = 0;
	MethodHash hash = 0;
	r.read(name);
	r.read(flags);
	r.read(hash);
	// Same compact record the hot layer uses for the return value.
	internal::ApiMethodArg ret{};
	uint16_t default_count = 0;
	r.read(ret.type);
	r.read(ret.meta);
	r.read(r_arg_count);
	r.read(default_count);

	// has_returns is derived from the RAW flags (the internal bit is already
	// folded in there by the writer; setup keeps it consistent either way).
	ApiMethodAccess::setup(r_method, name, hash, flags, !(flags & internal::METHOD_FLAG_NO_RETURN),
			(Variant::Type)ret.type, (GDExtensionClassMethodArgumentMetadata)ret.meta, r_arg_count);
	ApiMethodAccess::set_index(r_method, p_index);
	if constexpr (std::is_base_of_v<ApiMemberMethodBase, TApiMethodInfo>) {
		ApiMethodAccess::set_default_count(r_method, default_count);
	}

	for (uint16_t i = 0; i < r_arg_count; i++) {
		internal::ApiMethodArg arg{};
		r.read(arg.type);
		r.read(arg.meta);
		r_args.push_back(arg);
	}
}

// Reads the cold-section header that closes the hot section. detail_offset lands
// on the first detail record; the defaults section begins detail_size bytes
// later, so either cold section can be read without touching the other.
static bool deserialize_cold_header(PayloadReader &r, uint32_t p_hot_method_count, uint32_t &r_detail_method_count, uint64_t &r_detail_size, uint64_t &r_detail_offset, uint64_t &r_defaults_offset) {
	r.read(r_detail_method_count);
	r.read(r_detail_size);
	r_detail_offset = r.get_position();
	r_defaults_offset = r_detail_offset + r_detail_size;
	if (r_detail_method_count != p_hot_method_count) {
		ERR_PRINT(vformat("[API Tool] store corrupt: detail method count %d != hot method count %d",
				(int)r_detail_method_count, (int)p_hot_method_count));
		return false;
	}
	return true;
}

// Points every method of the entity at the shared storage and at its own slice
// of the entity-level argument block. The slice offsets come straight from each
// method's own argument count, so no side table is needed.
template <typename TMethods>
static void attach_storage(TMethods &r_methods, internal::ApiMethodDetailStorage *p_storage, const internal::ApiMethodArg *p_arg_block) {
	uint32_t arg_offset = 0;
	for (uint32_t i = 0; i < r_methods.size(); i++) {
		ApiMethodAccess::set_storage(r_methods[i], p_storage);
		ApiMethodAccess::set_args(r_methods[i], p_arg_block + arg_offset);
		arg_offset += r_methods[i].get_argument_count();
	}
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

Error ApiStoreReader::read_utility_functions(const String &p_path, LocalVector<ApiUtilityFunction> &r_data, internal::ApiMethodDetailStorage *p_utility_storage) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	uint32_t method_count = 0;
	r.read(method_count);
	r_data.resize(method_count);

	// Every utility function shares one storage: they come from a single file
	// and together form the "utility_functions" entity.
	LocalVector<internal::ApiMethodArg> flat_args;
	LocalVector<uint16_t> arg_counts;
	LocalVector<uint16_t> default_counts;
	arg_counts.resize_initialized(method_count);
	default_counts.resize_initialized(method_count);
	for (uint32_t i = 0; i < method_count; i++) {
		uint16_t arg_count = 0;
		deserialize_method_hot(r, r_data[i], (uint16_t)i, flat_args, arg_count);
		r.read(r_data[i].category);
		arg_counts[i] = arg_count;
	}

	uint32_t detail_method_count = 0;
	uint64_t detail_size = 0;
	uint64_t detail_offset = 0;
	uint64_t defaults_offset = 0;
	if (!deserialize_cold_header(r, method_count, detail_method_count, detail_size, detail_offset, defaults_offset)) {
		return ERR_FILE_CORRUPT;
	}

	internal::ApiMethodArg *block = p_utility_storage->build_arg_block((uint32_t)flat_args.size());
	for (uint32_t i = 0; i < flat_args.size(); i++) block[i] = flat_args[i];
	p_utility_storage->set_hot_counts(std::move(arg_counts), std::move(default_counts));
	p_utility_storage->configure_lazy(p_path, detail_offset, detail_size, defaults_offset, method_count);
	attach_storage(r_data, p_utility_storage, block);
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

	uint32_t method_count = 0;
	r.read(method_count);
	r_data.methods.resize(method_count);
	LocalVector<internal::ApiMethodArg> flat_args;
	LocalVector<uint16_t> arg_counts;
	LocalVector<uint16_t> default_counts;
	arg_counts.resize_initialized(method_count);
	default_counts.resize_initialized(method_count);
	for (uint32_t i = 0; i < method_count; i++) {
		uint16_t arg_count = 0;
		deserialize_method_hot(r, r_data.methods[i], (uint16_t)i, flat_args, arg_count);
		r_data.methods[i].set_variant_type(r_data.type);
		arg_counts[i] = arg_count;
		default_counts[i] = r_data.methods[i].get_default_count();
	}

	r.read(r_data.operators, deserialize_operator_info);
	r.read(r_data.constructors, deserialize_constructor_info);

	uint32_t detail_method_count = 0;
	uint64_t detail_size = 0;
	uint64_t detail_offset = 0;
	uint64_t defaults_offset = 0;
	if (!deserialize_cold_header(r, method_count, detail_method_count, detail_size, detail_offset, defaults_offset)) {
		return ERR_FILE_CORRUPT;
	}

	r_data.storage_ = std::make_shared<internal::ApiMethodDetailStorage>();
	internal::ApiMethodArg *block = r_data.storage_->build_arg_block((uint32_t)flat_args.size());
	for (uint32_t i = 0; i < flat_args.size(); i++) block[i] = flat_args[i];
	r_data.storage_->set_hot_counts(std::move(arg_counts), std::move(default_counts));
	r_data.storage_->configure_lazy(p_path, detail_offset, detail_size, defaults_offset, method_count);
	attach_storage(r_data.methods, r_data.storage_.get(), block);

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

	uint32_t method_count = 0;
	r.read(method_count);
	r_data.methods.resize(method_count);
	LocalVector<internal::ApiMethodArg> flat_args;
	LocalVector<uint16_t> arg_counts;
	LocalVector<uint16_t> default_counts;
	arg_counts.resize_initialized(method_count);
	default_counts.resize_initialized(method_count);
	for (uint32_t i = 0; i < method_count; i++) {
		uint16_t arg_count = 0;
		deserialize_method_hot(r, r_data.methods[i], (uint16_t)i, flat_args, arg_count);
		arg_counts[i] = arg_count;
		default_counts[i] = r_data.methods[i].get_default_count();
	}

	r.read(r_data.signals, deserialize_signal_info);
	r.read(r_data.properties, deserialize_api_property_info);
	r.read(r_data.enums, deserialize_enum_info);
	r.read(r_data.constants, deserialize_constant_info);

	uint32_t detail_method_count = 0;
	uint64_t detail_size = 0;
	uint64_t detail_offset = 0;
	uint64_t defaults_offset = 0;
	if (!deserialize_cold_header(r, method_count, detail_method_count, detail_size, detail_offset, defaults_offset)) {
		return ERR_FILE_CORRUPT;
	}

	r_data.storage_ = std::make_shared<internal::ApiMethodDetailStorage>();
	internal::ApiMethodArg *block = r_data.storage_->build_arg_block((uint32_t)flat_args.size());
	for (uint32_t i = 0; i < flat_args.size(); i++) block[i] = flat_args[i];
	// The class name is deduplicated once per class instead of once per method.
	r_data.storage_->set_owner_name(r_data.name);
	r_data.storage_->set_hot_counts(std::move(arg_counts), std::move(default_counts));
	r_data.storage_->configure_lazy(p_path, detail_offset, detail_size, defaults_offset, method_count);
	attach_storage(r_data.methods, r_data.storage_.get(), block);
	return OK;
}

// ============================================================================
// ApiStoreReader: Global Enum / Constant / Singletons / Native Structures
// ============================================================================

Error ApiStoreReader::read_global_enum(const String &p_path, ApiEnumInfo &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, deserialize_enum_info);
	return OK;
}

Error ApiStoreReader::read_global_constant(const String &p_path, ApiConstantInfo &r_data) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return err;
	PayloadReader &r = *r_ptr.get();

	r.read(r_data, deserialize_constant_info);
	return OK;
}

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

// ============================================================================
// ApiStoreReader: lazily loaded cold sections
// ============================================================================

bool ApiStoreReader::read_method_details(const String &p_path, uint64_t p_offset, uint64_t p_detail_size, uint32_t p_method_count, const LocalVector<uint16_t> &p_hot_arg_counts, LocalVector<internal::ApiMethodDetail> &r_details) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return false;
	PayloadReader &r = *r_ptr.get();

	r.seek(p_offset);
	r_details.resize(p_method_count);
	for (uint32_t i = 0; i < p_method_count; i++) {
		internal::ApiMethodDetail &detail = r_details[i];
		r.read(detail.return_val, deserialize_property_info);
		uint32_t arg_count = 0;
		r.read(arg_count);
		if (i < p_hot_arg_counts.size() && arg_count != p_hot_arg_counts[i]) {
			ERR_PRINT(vformat("[API Tool] store corrupt: detail[%d] carries %d arguments, hot record says %d (AC7)",
					(int)i, (int)arg_count, (int)p_hot_arg_counts[i]));
			return false;
		}
		detail.arguments.resize(arg_count);
		for (uint32_t a = 0; a < arg_count; a++) {
			r.read(detail.arguments[a], deserialize_property_info);
		}
	}
	// AC7: the detail section is self-delimiting, so the bytes actually consumed
	// must match the length the hot section announced.
	const uint64_t consumed = r.get_position() - p_offset;
	if (consumed != p_detail_size) {
		ERR_PRINT(vformat("[API Tool] store corrupt: detail section consumed %d bytes, header says %d (AC7)",
				(int)consumed, (int)p_detail_size));
		return false;
	}
	return true;
}

bool ApiStoreReader::read_method_defaults(const String &p_path, uint64_t p_offset, uint32_t p_method_count, const LocalVector<uint16_t> &p_hot_default_counts, LocalVector<Variant> &r_values, LocalVector<uint32_t> &r_offsets) {
	Error err{ OK };
	std::unique_ptr<PayloadReader> r_ptr = PayloadReader::open(p_path, err);
	if (err) return false;
	PayloadReader &r = *r_ptr.get();

	r.seek(p_offset);
	uint32_t count = 0;
	r.read(count);
	if (count != p_method_count) {
		ERR_PRINT(vformat("[API Tool] store corrupt: defaults section covers %d methods, expected %d (AC7)", (int)count, (int)p_method_count));
		return false;
	}
	LocalVector<uint16_t> counts;
	// uint16_t is trivially constructible, so resize() leaves this uninitialised
	// (local_vector.hpp) - each slot is read into below, so it must be zeroed.
	counts.resize_initialized(count);
	uint32_t total = 0;
	for (uint32_t i = 0; i < count; i++) {
		r.read(counts[i]);
		if (i < p_hot_default_counts.size() && counts[i] != p_hot_default_counts[i]) {
			ERR_PRINT(vformat("[API Tool] store corrupt: defaults[%d] has %d values, hot record says %d (AC7)",
					(int)i, (int)counts[i], (int)p_hot_default_counts[i]));
			return false;
		}
		total += counts[i];
	}
	r_values.resize(total);
	for (uint32_t i = 0; i < total; i++) {
		r.read(r_values[i]);
	}
	r_offsets.resize(count + 1);
	uint32_t acc = 0;
	for (uint32_t i = 0; i < count; i++) {
		r_offsets[i] = acc;
		acc += counts[i];
	}
	r_offsets[count] = acc;
	return true;
}

} //namespace api_tool::internal