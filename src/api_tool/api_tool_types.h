/************************************************************************/
/*  api_tool_types.h                                                    */
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

#pragma once

// api_tool_types.h
// All API data structure definitions, mirroring Godot extension_api.json (with docs).
// Reuses godot-cpp types directly: PropertyInfo, MethodInfo, Variant::Type, MethodFlags,
// PropertyHint, PropertyUsageFlags, GDExtensionClassMethodArgumentMetadata.
// No redundant type definitions.

#include <cstdint>
#include <functional>
#include <memory>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/api_tool_internal.h"

#define stack_alloc(type, size) (type *)alloca(sizeof(type) * (size))

namespace api_tool {

struct ApiMethodBase;
struct ApiMemberMethodBase;

namespace internal {
class ApiStoreReader;
class ApiLoader;
class ApiParser;
class ApiMethodDetailStorage;
// The store writer reads the hot fields directly (payload write/read templates
// encode by the argument's own type, so it must see the concrete member types,
// not accessor returns). External-linkage struct: a static free function cannot
// be a friend across translation units.
template <bool ReadMode>
class ApiToolPayload;
struct ApiMethodHotWriter;

// Internal-only flag bit. NOT part of the Godot method-flags contract: it is
// set at parse time when the method has no return value (see has_returns()),
// and get_flags() masks it out, so outside api_tool no one ever sees it.
// bit 256 is free: the engine uses 1/2/4/8/16/32/64/128 and the GDExtension
// mirror tops out at VIRTUAL_REQUIRED = 128.
enum MethodFlagsExt : uint32_t {
	METHOD_FLAG_NO_RETURN = 1u << 8,
};

// Byte-narrowed, load-time compressed forms of the two per-argument metadata
// enums. They exist so ApiMethodArg reads as "this holds a Variant type / an
// argument metadata" instead of a bare uint8_t - the enum types themselves are
// 4 B wide and would pad the 2-byte record to 8 (see ApiMethodArg).
using VariantType = uint8_t; // godot::Variant::Type
using ArgMeta = uint8_t; // GDExtensionClassMethodArgumentMetadata

// Compact per-argument hot record: 2 bytes instead of PropertyInfo's 48.
// MUST stay two uint8_t: sizeof(godot::Variant::Type) == 4, so a Variant::Type
// member would pad this to 8 B/argument - worse than the 2 B it replaces.
// Splitting into two per-method arrays (arg_types[] + arg_metas[]) was measured
// and is also worse: the second pointer costs +8 B on every method while 77.7%
// of the real API has at most one argument (36.5% have none).
struct ApiMethodArg {
	VariantType type; // godot::Variant::Type, narrowed to a byte
	ArgMeta meta; // GDExtensionClassMethodArgumentMetadata, narrowed to a byte
};
static_assert(sizeof(ApiMethodArg) == 2, "ApiMethodArg must stay 2 bytes");

// Lazily loaded remainder of a method record. Everything extension_api.json
// carries beyond the hot fields, faithfully preserved - except `id`, which the
// JSON does not have (so godot::MethodInfo::id was always a synthetic 0).
struct ApiMethodDetail {
	godot::PropertyInfo return_val;
	godot::LocalVector<godot::PropertyInfo> arguments;
};

// Forward declarations for the accessor signatures below. Declared here rather
// than at the top of the namespace: a forward declaration at the top SHADOWS the
// real definition in api_tool (inner-namespace unqualified lookup resolves to
// the incomplete declaration and never finds the outer definition), which broke
// the build with "undefined type ApiMethodBase".
// Sole writer of ApiMethodBase's hot fields. Both directions of the store
// (JSON parse on the editor side, file read at runtime) must populate a method
// that intentionally exposes no setters, and the internal NO_RETURN bit must be
// folded in exactly one place. The parser uses free functions, so a friend
// declaration for ApiParser alone would not cover it.
struct ApiMethodAccess {
	// p_flags is the RAW Godot flags word; the internal NO_RETURN bit is folded
	// in here (the single place) from p_has_returns.
	static void setup(ApiMethodBase &r_method, const godot::StringName &p_name, uint32_t p_hash, uint32_t p_flags, bool p_has_returns, godot::Variant::Type p_return_type, GDExtensionClassMethodArgumentMetadata p_return_meta, uint16_t p_arg_count);
	static void set_index(ApiMethodBase &r_method, uint16_t p_index);
	static void set_args(ApiMethodBase &r_method, const ApiMethodArg *p_args);
	static void set_storage(ApiMethodBase &r_method, ApiMethodDetailStorage *p_storage);
	static void set_default_count(ApiMemberMethodBase &r_method, uint16_t p_default_count);
	// Store round-trip only: the internal NO_RETURN bit has no other home, so it
	// must survive a rewrite. Never use this for the external flags contract.
	static uint32_t get_flags_raw(const ApiMethodBase &p_method);
};

} //namespace internal

using MethodHash = uint32_t;

godot::String get_variant_operator_name(godot::Variant::Operator p_op);

// ============================================================================
// Directory/file name constants
// ============================================================================

constexpr const char *DIR_BUILTIN_CLASSES = "builtin_classes";
constexpr const char *DIR_CLASSES = "classes";
constexpr const char *DIR_GLOBAL_ENUMS = "global_enums";
constexpr const char *DIR_GLOBAL_CONSTANTS = "global_constants";
constexpr const char *DIR_SINGLETONS = "singletons";
constexpr const char *DIR_NATIVE_STRUCTURES = "native_structures";
constexpr const char *DIR_COMPAT_HASHES = "compat_hashes";

constexpr const char *DIR_DOC_CLASSES = "documents/classes";
constexpr const char *DIR_DOC_BUILTIN_CLASSES = "documents/builtin_classes";
constexpr const char *DIR_DOC_UTILITY_FUNCTIONS = "documents/utility_functions";
constexpr const char *DIR_DOC_GLOBAL_ENUMS = "documents/global_enums";
constexpr const char *DIR_DOC_GLOBAL_CONSTANTS = "documents/global_constants";

constexpr const char *FILE_EXT_DATA = ".capi";
constexpr const char *FILE_EXT_DOC = ".bdoc";
constexpr const char *FILE_EXT_COMPAT = ".chash";

constexpr const char *FILE_HEADER = "header.capi";
constexpr const char *FILE_UTILITY_FUNCTIONS = "utility_functions.capi";

// ============================================================================
// Header / Metadata
// ============================================================================
enum class RealPrecision : int8_t {
	SINGLE,
	DOUBLE,
};

struct ApiHeader {
	godot::String version_status;
	godot::String version_build;
	godot::String version_full_name;

	int32_t version_major = 0;
	int32_t version_minor = 0;
	int32_t version_patch = 0;

	RealPrecision precision = RealPrecision::SINGLE;
};

// ApiMethodDetailStorage lives in core/api_tool_detail_storage.h: it is an
// internal implementation detail of the store/lazy-loading machinery, not part
// of the hot layer api_tool_types exposes to its callers.

struct ApiMethodBase {
private:
	godot::StringName name_;
	const internal::ApiMethodArg *args_ = nullptr; // entity-level block, arg_count_ long
	internal::ApiMethodDetailStorage *storage_ = nullptr; // lazy host (raw: see design §6.5)
	uint32_t hash_ = 0;
	uint32_t flags_ = 0; // Godot flags | internal METHOD_FLAG_NO_RETURN
	internal::ApiMethodArg ret_ = {}; // return type + metadata, same compact record as arguments
	uint16_t arg_count_ = 0;
	uint16_t method_index_ = 0; // index inside the owning entity

	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;
	friend struct internal::ApiMethodAccess;
	friend struct internal::ApiMethodHotWriter;

protected:
	_FORCE_INLINE_ internal::ApiMethodArg arg_at(uint16_t p_index) const { return args_[p_index]; }
	_FORCE_INLINE_ internal::ApiMethodDetailStorage *get_storage() const { return storage_; }

public:
	// ---- hot accessors: inline, lock-free, no IO ----
	_FORCE_INLINE_ const godot::StringName &get_name() const { return name_; }
	_FORCE_INLINE_ MethodHash get_hash() const { return hash_; }
	// External contract: the internal NO_RETURN bit must never leak out.
	_FORCE_INLINE_ uint32_t get_flags() const { return flags_ & ~static_cast<uint32_t>(internal::METHOD_FLAG_NO_RETURN); }
	_FORCE_INLINE_ uint16_t get_argument_count() const { return arg_count_; }
	_FORCE_INLINE_ godot::Variant::Type get_argument_type(uint16_t p_index) const { return static_cast<godot::Variant::Type>(arg_at(p_index).type); }
	_FORCE_INLINE_ GDExtensionClassMethodArgumentMetadata get_argument_metadata(uint16_t p_index) const { return static_cast<GDExtensionClassMethodArgumentMetadata>(arg_at(p_index).meta); }
	_FORCE_INLINE_ const internal::ApiMethodArg *get_args() const { return args_; }
	_FORCE_INLINE_ godot::Variant::Type get_return_type() const { return static_cast<godot::Variant::Type>(ret_.type); }
	_FORCE_INLINE_ GDExtensionClassMethodArgumentMetadata get_return_metadata() const { return static_cast<GDExtensionClassMethodArgumentMetadata>(ret_.meta); }
	// Internal query: reads the RAW flags, i.e. including the NO_RETURN bit.
	_FORCE_INLINE_ bool has_returns() const { return !(flags_ & internal::METHOD_FLAG_NO_RETURN); }
	_FORCE_INLINE_ bool is_vararg() const { return flags_ & godot::METHOD_FLAG_VARARG; }
	_FORCE_INLINE_ bool is_valid() const { return storage_ != nullptr; }

	// ---- cold accessors: the first call loads this entity's whole block ----
	const internal::ApiMethodDetail &get_detail() const;
	const godot::Variant *get_defaults(uint32_t &r_count) const;
};

struct ApiMemberMethodBase : public ApiMethodBase {
private:
	uint16_t default_count_ = 0; // class + builtin only; utility has none

	friend struct internal::ApiMethodAccess;

public:
	_FORCE_INLINE_ bool is_static() const { return get_flags() & godot::METHOD_FLAG_STATIC; }
	_FORCE_INLINE_ uint16_t get_default_count() const { return default_count_; }
};

struct ApiBuiltInMethod : public ApiMemberMethodBase {
private:
	mutable GDExtensionPtrBuiltInMethod func = nullptr; // lazily loaded
	godot::Variant::Type variant_type = godot::Variant::NIL; // owning builtin type (needed for base ptrcall)

	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;

	void try_load_compatible_func_ptr() const;

public:
	_FORCE_INLINE_ void set_variant_type(godot::Variant::Type p_type) { variant_type = p_type; }
	_FORCE_INLINE_ godot::Variant::Type get_variant_type() const { return variant_type; }

	_FORCE_INLINE_ GDExtensionPtrBuiltInMethod get_func_ptr() const {
		using namespace godot;
		if (unlikely(!func)) {
			func = ::godot::gdextension_interface::variant_get_ptr_builtin_method(
					(GDExtensionVariantType)variant_type,
					get_name()._native_ptr(),
					(GDExtensionInt)get_hash());

			if (unlikely(!func)) try_load_compatible_func_ptr(); // 虽然不太可能用到，保险起见

			if (func == nullptr) {
				ERR_PRINT_ONCE("Failed to load built in function: " + Variant::get_type_name(variant_type) + "::" + get_name());
				return func;
			}
		}
		return func;
	}
	_FORCE_INLINE_ void validated_call(godot::Variant *base, const godot::Variant **p_args, int p_argcount, godot::Variant *r_ret) const {
		using namespace godot;
		GDExtensionPtrBuiltInMethod func_ptr = get_func_ptr();
		ERR_FAIL_NULL_MSG(func_ptr, "Call on missing built-in function: " + Variant::get_type_name(variant_type) + "::" + get_name());

		const bool is_static_method = is_static();
		const bool has_return_value = has_returns();
		const godot::Variant::Type return_type = get_return_type();
		const GDExtensionClassMethodArgumentMetadata return_meta = get_return_metadata();

		// base
		void *base_ptr{ stack_alloc(internal::MaxSizeEncodeArgType, 1) };
		if (!is_static_method) {
			ERR_FAIL_COND_MSG(!base, "Call to non-static method without base object! (missing base argument)");
			internal::var_to_arg_ptr(*base, base_ptr, variant_type);
		}

		// arguments
		const uint32_t method_argcount = get_argument_count();
		const uint32_t missing = method_argcount > (uint32_t)p_argcount ? method_argcount - (uint32_t)p_argcount : 0;
		int argcount = MAX(p_argcount, (int)method_argcount);

		// Defaults are only fetched when the caller actually omitted arguments.
		uint32_t default_value_size = 0;
		const Variant *default_values = missing > 0 ? get_defaults(default_value_size) : nullptr;

		internal::MaxSizeEncodeArgType *var_args = stack_alloc(internal::MaxSizeEncodeArgType, argcount);
		GDExtensionTypePtr *ptr_args = stack_alloc(GDExtensionTypePtr, argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, argcount);
		const uint16_t method_argcount_u = (uint16_t)method_argcount;
		for (uint16_t i = 0; i < (uint16_t)argcount; i++) {
			// Arg
			const Variant *arg{ nullptr };
			if (i < (uint16_t)p_argcount) {
				arg = p_args[i];
			} else {
				arg = &default_values[i - (uint16_t)p_argcount + (default_value_size - missing)];
			}
			// Type
			if (i < method_argcount_u) {
				args_type[i] = get_argument_type(i);
			} else {
#if DEBUG_ENABLED
				CRASH_COND(!is_vararg());
#endif // DEBUG_ENABLED
				args_type[i] = Variant::NIL;
			}
			// Meta
			GDExtensionClassMethodArgumentMetadata meta = i < method_argcount_u ? get_argument_metadata(i) : GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

			// ArgPtr
			internal::MaxSizeEncodeArgType *arg_ptr_ = var_args + i;
			internal::var_to_arg_ptr(*arg, arg_ptr_, args_type[i], meta);

			ptr_args[i] = arg_ptr_;
		}

		void *ret_ptr = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		if (has_return_value) {
			internal::ctor_arg_ptr(ret_ptr, return_type);
			CRASH_COND_MSG(ret_ptr == nullptr, "???");
		}
		func_ptr(is_static_method ? nullptr : base_ptr, ptr_args, ret_ptr, argcount);

		if (!is_static_method) internal::dctor_arg_ptr(base_ptr, base->get_type());
		while (argcount > 0) {
			argcount--;
			internal::dctor_arg_ptr(ptr_args[argcount], args_type[argcount]);
		}
		if (has_return_value) {
			if (r_ret) internal::arg_ptr_to_var(ret_ptr, return_type, *r_ret, return_meta);

			internal::dctor_arg_ptr(ret_ptr, return_type);
		}
	};
};

struct ApiClassMethod : public ApiMemberMethodBase {
private:
	mutable GDExtensionMethodBindPtr method_bind = nullptr; // lazily loaded

	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;

	void try_load_compatible_method_bind() const;

public:
	_FORCE_INLINE_ bool is_virtual() const { return get_flags() & (godot::METHOD_FLAG_VIRTUAL | godot::METHOD_FLAG_VIRTUAL_REQUIRED); }
	_FORCE_INLINE_ bool is_static() const { return get_flags() & godot::METHOD_FLAG_STATIC; }
	// owning class name; lives in the shared per-entity storage (see design §6.5)
	const godot::StringName &get_owner_name() const;

	_FORCE_INLINE_ GDExtensionMethodBindPtr get_method_bind_ptr() const {
		if (unlikely(!method_bind)) {
			const godot::StringName &owner_name = get_owner_name();
			method_bind = ::godot::gdextension_interface::classdb_get_method_bind(
					owner_name._native_ptr(),
					get_name()._native_ptr(),
					(GDExtensionInt)get_hash());

			if (unlikely(!method_bind)) try_load_compatible_method_bind(); // 虽然不太可能用到，保险起见

			if (method_bind == nullptr) {
				ERR_PRINT_ONCE("Failed to load function: " + owner_name + "::" + get_name());
				return method_bind;
			}
		}
		return method_bind;
	}
	_FORCE_INLINE_ godot::Variant validated_call(godot::Object *p_object, const godot::Variant **p_args, int p_argcount, GDExtensionCallError &r_error) const {
		using namespace godot;
		Variant ret;
		const GDExtensionMethodBindPtr method_bind_ptr = get_method_bind_ptr();
		ERR_FAIL_NULL_V_MSG(method_bind_ptr, ret, "Call on missing function: " + get_owner_name() + "::" + get_name());
		// instance
		const bool is_static_method = is_static();
		ERR_FAIL_COND_V_MSG(!is_static_method && p_object == nullptr, ret, "Call to non-static method without base object! (missing base argument)");

		::godot::gdextension_interface::object_method_bind_call(
				method_bind_ptr, is_static_method ? nullptr : p_object->_owner, (const GDExtensionConstVariantPtr *)p_args, p_argcount, &ret, &r_error);

		return ret;
	};
};

// ============================================================================
// Utility Function (reuses MethodInfo)
// ============================================================================

struct ApiUtilityFunction : public ApiMethodBase {
	godot::StringName category;

private:
	mutable GDExtensionPtrUtilityFunction func = nullptr; // lazily loaded

	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;

public:
	_FORCE_INLINE_ GDExtensionPtrUtilityFunction get_func_ptr() const {
		using namespace godot;
		if (unlikely(!func)) {
			func = ::godot::gdextension_interface::variant_get_ptr_utility_function(
					get_name()._native_ptr(),
					static_cast<GDExtensionInt>(get_hash()));
			if (func == nullptr) {
				ERR_PRINT_ONCE("Failed to load utility function: " + get_name());
				return nullptr;
			}
		}
		return func;
	}
	_FORCE_INLINE_ void validated_call(godot::Variant *r_ret, const godot::Variant **p_args, int p_argcount) const {
		using namespace godot;
		const GDExtensionPtrUtilityFunction func_ptr = get_func_ptr();
		ERR_FAIL_NULL_MSG(func_ptr, "Call on missing utility function: " + get_name());

		// Utility functions never carry defaults, so the argc must match
		// exactly (this used to index default_arguments with a negative index
		// when too few args were supplied).
		const uint32_t method_argcount = get_argument_count();
		ERR_FAIL_COND_MSG(p_argcount < (int)method_argcount, "Call on utility function " + get_name() + " with too few arguments: " + itos(p_argcount) + " < " + itos((int)method_argcount));

		const bool has_return_value = has_returns();
		const godot::Variant::Type return_type = get_return_type();
		const GDExtensionClassMethodArgumentMetadata return_meta = get_return_metadata();
		int argcount = (int)method_argcount;

		internal::MaxSizeEncodeArgType *var_args = stack_alloc(internal::MaxSizeEncodeArgType, argcount);
		void **ptr_args = stack_alloc(void *, argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, argcount);
		for (uint16_t i = 0; i < (uint16_t)method_argcount; i++) {
			args_type[i] = get_argument_type(i);
			const GDExtensionClassMethodArgumentMetadata meta = get_argument_metadata(i);

			// ArgPtr
			internal::MaxSizeEncodeArgType *arg_ptr_ = var_args + i;
			internal::var_to_arg_ptr(*p_args[i], arg_ptr_, args_type[i], meta);

			ptr_args[i] = arg_ptr_;
		}

		void *ret_ptr = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		if (has_return_value) {
			internal::ctor_arg_ptr(ret_ptr, return_type);
			CRASH_COND_MSG(ret_ptr == nullptr, "???");
		}
		func_ptr(ret_ptr, ptr_args, argcount);

		while (argcount > 0) {
			argcount--;
			internal::dctor_arg_ptr(ptr_args[argcount], args_type[argcount]);
		}
		if (has_return_value) {
			if (r_ret) internal::arg_ptr_to_var(ret_ptr, return_type, *r_ret, return_meta);

			internal::dctor_arg_ptr(ret_ptr, return_type);
		}
	};
};

// ============================================================================
// Compatibility Hashes (per-class file, queried on demand)
// ============================================================================

struct ApiMethodCompatibilityHashes {
	godot::StringName method_name;
	godot::LocalVector<MethodHash> hashes;
};

struct ApiCompatibilityHashData {
	godot::LocalVector<ApiMethodCompatibilityHashes> methods;
};

// ============================================================================
// PropertyInfo wrapper (reuses godot::PropertyInfo + setter/getter + doc)
// ============================================================================

struct ApiPropertyInfo {
	godot::PropertyInfo property; // Reuse godot-cpp: type, name, class_name, hint, hint_string, usage
	godot::StringName setter;
	godot::StringName getter;
	int32_t index = -1; // Property index (for builtin classes with members)
};

// ============================================================================
// SignalInfo with PropertyInfo arguments
// ============================================================================

struct ApiSignalInfo {
	godot::LocalVector<godot::PropertyInfo> arguments;
	godot::StringName name;
};

// ============================================================================
// Enum / Constant
// ============================================================================

struct ApiEnumValue {
	godot::StringName name;
	int64_t value = 0;
};

struct ApiEnumInfo {
	godot::LocalVector<ApiEnumValue> values;
	godot::StringName name;
	bool is_bitfield = false;
};

struct ApiConstantInfo {
	godot::StringName name;
	int64_t value = 0;
	bool is_bitfield = false;
};

struct ApiBuiltInClassConstantInfo {
	godot::StringName name;
	godot::Variant::Type type;
	godot::Variant value;
};

// ============================================================================

struct ApiOperatorInfo {
	godot::Variant::Operator op = godot::Variant::OP_EQUAL;
	godot::Variant::Type return_type = godot::Variant::NIL;
	godot::Variant::Type left_type = godot::Variant::NIL;
	godot::Variant::Type right_type = godot::Variant::NIL;

private:
	GDExtensionPtrOperatorEvaluator op_evaluator;

	friend class ApiBuiltinClass;
	void initialize(const godot::Variant::Type &p_left_type) {
		using namespace godot;
		left_type = p_left_type;
		op_evaluator = gdextension_interface::variant_get_ptr_operator_evaluator(
				(GDExtensionVariantOperator)op,
				(GDExtensionVariantType)left_type,
				(GDExtensionVariantType)right_type);
		ERR_FAIL_COND_MSG(!op_evaluator,
				vformat("Failed to load operator evaluator: %s vs %s - op code(%s)",
						Variant::get_type_name(left_type),
						Variant::get_type_name(right_type),
						get_variant_operator_name(op)));
	}

public:
	_FORCE_INLINE_ GDExtensionPtrOperatorEvaluator get_op_evaluator_ptr() const { return op_evaluator; }
	_FORCE_INLINE_ godot::Variant evaluate(const godot::Variant &p_left, const godot::Variant &p_right) const {
		using namespace godot;
		void *left = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		void *right = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_left, left, left_type);
		internal::var_to_arg_ptr(p_right, right, right_type);

		void *result = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::ctor_arg_ptr(result, return_type);

		op_evaluator(left, right, result);

		Variant ret;
		internal::arg_ptr_to_var(result, return_type, ret);

		internal::dctor_arg_ptr(left, p_left.get_type());
		internal::dctor_arg_ptr(right, p_right.get_type());
		internal::dctor_arg_ptr(result, return_type);

		return ret;
	}
};

struct ApiConstructorInfo {
	godot::LocalVector<godot::PropertyInfo> arguments;

private:
	GDExtensionPtrConstructor constructor;
	godot::Variant::Type type;

	friend class ApiBuiltinClass;
	void initialize(const godot::Variant::Type &p_type, const int32_t p_index) {
		using namespace godot;
		type = p_type;
		constructor = gdextension_interface::variant_get_ptr_constructor((GDExtensionVariantType)p_type, (int32_t)p_index);
		ERR_FAIL_COND_MSG(!constructor, vformat("Can't load %s constructor (index: %s)", Variant::get_type_name(p_type), p_index));
	}

public:
	_FORCE_INLINE_ GDExtensionPtrConstructor get_constructor_ptr() const { return constructor; }

	_FORCE_INLINE_ godot::Variant validated_construct(const godot::Variant **p_args, int p_argcount) const {
		using namespace godot;

		ERR_FAIL_COND_V(p_argcount != arguments.size(), {});

		// arguments
		internal::MaxSizeEncodeArgType *var_args = stack_alloc(internal::MaxSizeEncodeArgType, p_argcount);
		GDExtensionTypePtr *ptr_args = stack_alloc(GDExtensionTypePtr, p_argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, p_argcount);
		for (int i = 0; i < p_argcount; i++) {
			const Variant *arg_ptr = p_args[i];
			internal::var_to_arg_ptr(*arg_ptr, var_args + i, arguments[i].type);
			ptr_args[i] = var_args + i;
			args_type[i] = arg_ptr->get_type();
		}

		void *ret_ptr = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		constructor(ret_ptr, ptr_args);

		Variant ret;
		internal::arg_ptr_to_var(ret_ptr, type, ret);

		while (p_argcount > 0) {
			p_argcount--;
			internal::dctor_arg_ptr(ptr_args[p_argcount], args_type[p_argcount]);
		}
		internal::dctor_arg_ptr(ret_ptr, type);
		return ret;
	}
};

struct ApiMemberInfo {
	godot::StringName name;
	godot::Variant::Type type = godot::Variant::NIL;

private:
	mutable GDExtensionPtrSetter setter_func = nullptr; // Private member, loaded lazily
	mutable GDExtensionPtrGetter getter_func = nullptr; // Private member, loaded lazily

	friend class ApiBuiltinClass;
	void initialize(const godot::Variant::Type &p_type) {
		using namespace godot;
		setter_func = gdextension_interface::variant_get_ptr_setter((GDExtensionVariantType)p_type, name._native_ptr());
		if (!setter_func) WARN_PRINT("Failed to load setter: " + Variant::get_type_name(p_type) + "::" + name);
		getter_func = gdextension_interface::variant_get_ptr_getter((GDExtensionVariantType)p_type, name._native_ptr());
		if (!setter_func) WARN_PRINT("Failed to load Getter: " + Variant::get_type_name(p_type) + "::" + name);
	}

public:
	_FORCE_INLINE_ GDExtensionPtrSetter get_setter_ptr() const { return setter_func; }
	_FORCE_INLINE_ GDExtensionPtrGetter get_getter_ptr() const { return getter_func; }

	_FORCE_INLINE_ void setter_validated_call(godot::Variant &p_base, const godot::Variant &p_value) const {
		using namespace godot;
		ERR_FAIL_NULL(setter_func);
		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		void *value = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base);
		internal::var_to_arg_ptr(p_value, value, type);
		setter_func(base, value);

		internal::dctor_arg_ptr(base, p_base.get_type());
		internal::dctor_arg_ptr(value, p_value.get_type());
	}
	_FORCE_INLINE_ void getter_validated_call(const godot::Variant &p_base, godot::Variant &r_value) const {
		using namespace godot;
		ERR_FAIL_NULL(getter_func);
		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		void *value = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base);
		internal::ctor_arg_ptr(value, type);
		getter_func(base, value);
		internal::arg_ptr_to_var(value, type, r_value);

		internal::dctor_arg_ptr(base, p_base.get_type());
		internal::dctor_arg_ptr(value, type);
	}
};

// ============================================================================
// Builtin Class
// ============================================================================

struct ApiBuiltinClass {
	godot::LocalVector<ApiMemberInfo> members;
	godot::LocalVector<ApiBuiltInClassConstantInfo> constants;
	godot::LocalVector<ApiEnumInfo> enums;
	godot::LocalVector<ApiBuiltInMethod> methods;
	godot::LocalVector<ApiOperatorInfo> operators;
	godot::LocalVector<ApiConstructorInfo> constructors; // 解析时已按 index 排序

	godot::Variant::Type type = godot::Variant::NIL;
	godot::Variant::Type indexing_type = godot::Variant::NIL;
	bool has_indexing_return_type = false;
	bool is_keyed = false;
	bool has_destructor = false;

private:
	mutable GDExtensionPtrIndexedGetter indexed_getter{ nullptr };
	mutable GDExtensionPtrIndexedSetter indexed_setter{ nullptr };

	mutable GDExtensionPtrKeyedGetter keyed_getter{ nullptr };
	mutable GDExtensionPtrKeyedSetter keyed_setter{ nullptr };

	// Shared host of this entity's hot argument block + lazily loaded detail.
	// Held by shared_ptr so ApiBuiltinClass stays copyable while the methods'
	// raw `storage_` pointers remain valid (see design.md §6.5).
	std::shared_ptr<internal::ApiMethodDetailStorage> storage_;

	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;
	void initialize() {
		for (auto &member : members) member.initialize(type);
		for (uint32_t idx = 0; idx < constructors.size(); ++idx) constructors[idx].initialize(type, idx);
		for (auto &op : operators) op.initialize(type);
	}

public:
	_FORCE_INLINE_ void indexed_getter_validated_call(godot::Variant &p_base, int64_t p_index, godot::Variant &r_value) const {
		using namespace godot;
		if (unlikely(!indexed_getter)) {
			indexed_getter = ::godot::gdextension_interface::variant_get_ptr_indexed_getter(
					(GDExtensionVariantType)type);
			if (!indexed_getter) {
				ERR_PRINT_ONCE("Failed to load indexed getter for type  " + Variant::get_type_name(type));
				return;
			}
		}

		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		void *value = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base, type);
		internal::ctor_arg_ptr(value, indexing_type);
		indexed_getter(base, (GDExtensionInt)p_index, value);
		internal::arg_ptr_to_var(value, indexing_type, r_value);

		internal::dctor_arg_ptr(base, type);
		internal::dctor_arg_ptr(value, indexing_type);
	}
	_FORCE_INLINE_ void indexed_setter_validated_call(const godot::Variant &p_base, int64_t p_index, godot::Variant &p_value) const {
		using namespace godot;
		if (unlikely(!indexed_setter)) {
			indexed_setter = ::godot::gdextension_interface::variant_get_ptr_indexed_setter(
					(GDExtensionVariantType)type);
			if (!indexed_setter) {
				ERR_PRINT_ONCE("Failed to load indexed setter for type " + Variant::get_type_name(type));
				return;
			}
		}

		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		void *value = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base, type);
		internal::var_to_arg_ptr(p_value, value, indexing_type);
		indexed_setter(base, (GDExtensionInt)p_index, value);

		internal::dctor_arg_ptr(base, type);
		internal::dctor_arg_ptr(value, indexing_type);
	}

	// 只有 Dictionary 有效
	_FORCE_INLINE_ void keyed_getter_validated_call(godot::Variant &p_base, const godot::Variant &p_key, godot::Variant &r_value) const {
		using namespace godot;
		if (unlikely(!keyed_getter)) {
			keyed_getter = ::godot::gdextension_interface::variant_get_ptr_keyed_getter(
					(GDExtensionVariantType)type);
			if (!keyed_getter) {
				ERR_PRINT_ONCE("Failed to load keyed getter for type  " + Variant::get_type_name(type));
				return;
			}
		}

		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base, type);
		keyed_getter(base, &p_key, &r_value);
	}
	// 只有 Dictionary 有效
	_FORCE_INLINE_ void keyed_setter_validated_call(const godot::Variant &p_base, const godot::Variant &p_key, godot::Variant &p_value) const {
		using namespace godot;
		if (unlikely(!keyed_setter)) {
			keyed_setter = ::godot::gdextension_interface::variant_get_ptr_keyed_setter(
					(GDExtensionVariantType)type);
			if (!keyed_setter) {
				ERR_PRINT_ONCE("Failed to load indexed getter for type " + Variant::get_type_name(type));
				return;
			}
		}

		void *base = stack_alloc(internal::MaxSizeEncodeArgType, 1);
		internal::var_to_arg_ptr(p_base, base, type);
		keyed_setter(base, &p_key, &p_value);
	}
};

// ============================================================================
// Class
// ============================================================================

struct ApiClass {
	godot::LocalVector<ApiClassMethod> methods; // TODO: 拆分出虚函数（纯定义，没有hash）
	godot::LocalVector<ApiSignalInfo> signals;
	godot::LocalVector<ApiPropertyInfo> properties;
	godot::LocalVector<ApiEnumInfo> enums;
	godot::LocalVector<ApiConstantInfo> constants;
	godot::StringName name;
	godot::StringName inherits;
	godot::ClassDB::APIType api_type;
	bool is_refcounted = false;
	bool is_instantiable = true;

private:
	// Shared host of this class's hot argument block + lazily loaded detail.
	// shared_ptr keeps ApiClass copyable (TypedCache::insert copies it) while the
	// methods' raw `storage_` pointers stay valid (see design.md §6.5).
	std::shared_ptr<internal::ApiMethodDetailStorage> storage_;

public:
	// Host of this class's hot argument block and lazily loaded cold data. The
	// store reader creates it; the parser creates it so the writer can read the
	// detail/defaults back out of the same place.
	_FORCE_INLINE_ const std::shared_ptr<internal::ApiMethodDetailStorage> &get_storage() const { return storage_; }
	_FORCE_INLINE_ void set_storage(std::shared_ptr<internal::ApiMethodDetailStorage> p_storage) { storage_ = std::move(p_storage); }

private:
	friend class internal::ApiStoreReader;
	friend class internal::ApiLoader;
	friend class internal::ApiParser;
};

// ============================================================================
// Singleton / Native Structure
// ============================================================================

struct ApiSingleton {
	godot::StringName name;
	godot::StringName type;
};

struct ApiNativeStructure {
	godot::String name;
	godot::String format;
};

} // namespace api_tool

// ============================================================================
// Hot-layer layout contract
// ============================================================================
// The sizes below are the measured basis of the memory budget (prd.md AC2:
// class methods 3,475,812 B -> 1,110,366 B). They are asserted, not merely
// documented, so silent layout drift (a new hot field, an accidental
// godot::MethodInfo member coming back) fails the build instead of quietly
// reintroducing the per-method cost this task removed.
//
// If a deliberate change moves a size, update both the assert and the numbers
// in design.md §11 / .trellis/tasks/.../report.md.
//
// Sizes are ABI-specific, so only the ones that are genuinely portable are
// asserted as equalities everywhere:
//   - MSVC does not reuse base-class tail padding, so `default_count_` pushes
//     ApiMemberMethodBase to 48 there;
//   - the Itanium ABI (GCC/Clang on Linux, macOS, Android, iOS) does reuse it,
//     giving 40 -- and 48 / 56 for the two subclasses;
//   - 32-bit targets (wasm32, android armv7) halve the pointers: 28 / 28 / 32 /
//     36.
// An exact equality on the non-MSVC ABIs therefore rejects perfectly correct
// builds. Keep the measured MSVC numbers exact (that is the ABI they were
// measured on, and the one the memory budget in design.md §11 is derived from)
// and enforce a bound elsewhere: it still fails the build when a member creeps
// back -- the godot::MethodInfo this task removed was 120 B plus three heap
// arrays, far past any bound below.
static_assert(sizeof(api_tool::internal::ApiMethodArg) == 2, "ApiMethodArg must stay 2 bytes");

#if defined(_MSC_VER)
static_assert(sizeof(api_tool::ApiMethodBase) == 40, "ApiMethodBase hot layout drifted");
static_assert(sizeof(api_tool::ApiMemberMethodBase) == 48, "ApiMemberMethodBase hot layout drifted");
static_assert(sizeof(api_tool::ApiClassMethod) == 56, "ApiClassMethod hot layout drifted");
static_assert(sizeof(api_tool::ApiBuiltInMethod) == 64, "ApiBuiltInMethod hot layout drifted");
static_assert(sizeof(api_tool::ApiUtilityFunction) == 56, "ApiUtilityFunction hot layout drifted");
static_assert(sizeof(api_tool::internal::ApiMethodDetail) == 64, "ApiMethodDetail cold layout drifted");
#else
// Measured here: 40 / 40 / 48 / 56 / 56 on the 64-bit Itanium ABIs (Linux,
// macOS) and 28 / 28 / 32 / 36 / 36 on 32-bit (wasm32, android armv7). The
// bounds are deliberately generous -- they exist to catch a heavyweight member
// creeping back, not to pin an exact byte count off-MSVC -- but they are far
// below what re-adding godot::MethodInfo (120 B plus three heap arrays) costs.
static_assert(sizeof(api_tool::ApiMethodBase) <= 56, "ApiMethodBase hot layout drifted");
static_assert(sizeof(api_tool::ApiMemberMethodBase) <= 64, "ApiMemberMethodBase hot layout drifted");
static_assert(sizeof(api_tool::ApiClassMethod) <= 72, "ApiClassMethod hot layout drifted");
static_assert(sizeof(api_tool::ApiBuiltInMethod) <= 80, "ApiBuiltInMethod hot layout drifted");
static_assert(sizeof(api_tool::ApiUtilityFunction) <= 72, "ApiUtilityFunction hot layout drifted");
static_assert(sizeof(api_tool::internal::ApiMethodDetail) <= 128, "ApiMethodDetail cold layout drifted");
#endif

// ============================================================================
// Cache invalidation callback types (global scope for cross-namespace use)
// ============================================================================

// Cache invalidation callback type (global scope for cross-namespace use)
using CacheInvalidatedCallback = std::function<void()>;

// Cache invalidation handle type (global scope for cross-namespace use)
using CacheInvalidatedHandle = int32_t;
