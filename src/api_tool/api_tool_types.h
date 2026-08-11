#pragma once

// api_tool_types.h
// All API data structure definitions, mirroring Godot extension_api.json (with docs).
// Reuses godot-cpp types directly: PropertyInfo, MethodInfo, Variant::Type, MethodFlags,
// PropertyHint, PropertyUsageFlags, GDExtensionClassMethodArgumentMetadata.
// No redundant type definitions.

#include <functional>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/api_tool_internal.h"

#define stack_alloc(type, size) (type *)alloca(sizeof(type) * (size))

namespace api_tool {
namespace internal {
class ApiStoreReader;
}

using MethodHash = uint32_t;

const godot::String &get_variant_operator_name(godot::Variant::Operator p_op);

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

// ============================================================================
// MethodInfo wrapper (reuses godot::MethodInfo + JSON-specific fields)
// ============================================================================

struct ApiMethodBase {
	godot::MethodInfo method;
	MethodHash hash = 0;

public:
	_FORCE_INLINE_ bool is_vararg() const { return method.flags & godot::METHOD_FLAG_VARARG; }
	_FORCE_INLINE_ bool has_returns() const { return internal::has_returns(method); }
};

struct ApiMemberMethodBase : public ApiMethodBase {
	_FORCE_INLINE_ bool is_static() const { return method.flags & godot::METHOD_FLAG_STATIC; }
};

struct ApiBuiltInMethod : public ApiMemberMethodBase {
private:
	mutable GDExtensionPtrBuiltInMethod func; // = nullptr; // Private member, loaded lazily
	godot::Variant::Type variant_type; // = godot::Variant::NIL; // Store type for lazy loading

	mutable bool is_static_ = false;
	mutable bool is_vararg_ = false;
	mutable bool has_returns_ = false;

	friend class internal::ApiStoreReader;

	void try_load_compatible_func_ptr() const;

public:
	_FORCE_INLINE_ GDExtensionPtrBuiltInMethod get_func_ptr() const {
		using namespace godot;
		if (unlikely(!func)) {
			func = ::godot::gdextension_interface::variant_get_ptr_builtin_method(
					(GDExtensionVariantType)variant_type,
					method.name._native_ptr(),
					(GDExtensionInt)hash);

			if (unlikely(!func)) try_load_compatible_func_ptr(); // 虽然不太可能用到，保险起见

			if (func == nullptr) {
				ERR_PRINT_ONCE("Failed to load built in function: " + Variant::get_type_name(variant_type) + "::" + method.name);
				return func;
			}
			is_static_ = is_static();
			has_returns_ = has_returns();
			is_vararg_ = is_vararg();
		}
		return func;
	}
	_FORCE_INLINE_ void validated_call(godot::Variant *base, const godot::Variant **p_args, int p_argcount, godot::Variant *r_ret) const {
		using namespace godot;
		GDExtensionPtrBuiltInMethod func_ptr = get_func_ptr();
		ERR_FAIL_NULL_MSG(func_ptr, "Call on missing built-in function: " + Variant::get_type_name(variant_type) + "::" + method.name);

		// base
		void *base_ptr{ stack_alloc(Variant, 1) };
		if (!is_static_) {
			ERR_FAIL_COND_MSG(!base, "Call to non-static method without base object! (missing base argument)");
			internal::var_to_arg_ptr(*base, base_ptr, variant_type);
		}

		// arguments
		const auto &arguments = method.arguments;
		const auto &meta_list = method.arguments_metadata;
		const auto &default_values = method.default_arguments;
		const uint32_t default_value_size = default_values.size();
		const uint32_t method_argcount = arguments.size();
		const uint32_t missing = method_argcount - (uint32_t)p_argcount;
		int argcount = MAX(p_argcount, method_argcount);

		Variant *var_args = stack_alloc(Variant, argcount);
		GDExtensionTypePtr *ptr_args = stack_alloc(GDExtensionTypePtr, argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, argcount);
		for (int i = 0; i < argcount; i++) {
			// Arg
			const Variant *arg{ nullptr };
			if (i < p_argcount) {
				arg = p_args[i];
			} else {
				arg = &default_values[i - p_argcount + (default_value_size - missing)];
			}
			// Type
			if (i < arguments.size()) {
				args_type[i] = arguments[i].type;
			} else {
				CRASH_COND(is_vararg_);
				args_type[i] = Variant::NIL;
			}
			// Meta
			GDExtensionClassMethodArgumentMetadata meta = i < meta_list.size() ? meta_list[i] : GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

			// ArgPtr
			Variant *arg_ptr_ = var_args + i;
			internal::var_to_arg_ptr(*arg, arg_ptr_, args_type[i], meta);

			ptr_args[i] = arg_ptr_;
		}

		void *ret_ptr = stack_alloc(Variant, 1);
		if (has_returns_) {
			internal::ctor_arg_ptr(ret_ptr, method.return_val.type);
			CRASH_COND_MSG(ret_ptr == nullptr, "???");
		}
		func_ptr(is_static_ ? nullptr : base_ptr, ptr_args, ret_ptr, argcount);

		if (!is_static_) internal::dctor_arg_ptr(base_ptr, base->get_type());
		while (argcount > 0) {
			argcount--;
			internal::dctor_arg_ptr(ptr_args[argcount], args_type[argcount]);
		}
		if (has_returns_) {
			if (r_ret) internal::arg_ptr_to_var(ret_ptr, method.return_val.type, *r_ret, method.return_val_metadata);

			internal::dctor_arg_ptr(ret_ptr, method.return_val.type);
		}
	};
};

struct ApiClassMethod : public ApiMemberMethodBase {
private:
	mutable GDExtensionMethodBindPtr method_bind = nullptr;
	godot::StringName owner_class_name;

	mutable bool is_static_ = false;
	mutable bool is_vararg_ = false;
	mutable bool has_returns_ = false;

	friend class internal::ApiStoreReader;

	void try_load_compatible_method_bind() const;

public:
	_FORCE_INLINE_ bool is_virtual() const { return method.flags & (godot::METHOD_FLAG_VIRTUAL | godot::METHOD_FLAG_VIRTUAL_REQUIRED); }
	_FORCE_INLINE_ GDExtensionMethodBindPtr get_method_bind_ptr() const {
		if (unlikely(!method_bind)) {
			method_bind = ::godot::gdextension_interface::classdb_get_method_bind(
					owner_class_name._native_ptr(),
					method.name._native_ptr(),
					(GDExtensionInt)hash);

			if (unlikely(!method_bind)) try_load_compatible_method_bind(); // 虽然不太可能用到，保险起见

			if (method_bind == nullptr) {
				ERR_PRINT_ONCE("Failed to load function: " + owner_class_name + "::" + method.name);
				return method_bind;
			}
			is_static_ = is_static();
			has_returns_ = has_returns();
			is_vararg_ = is_vararg();
		}
		return method_bind;
	}
	_FORCE_INLINE_ godot::Variant validated_call(godot::Object *p_object, const godot::Variant **p_args, int p_argcount, GDExtensionCallError &r_error) const {
		using namespace godot;
		Variant ret;
		const GDExtensionMethodBindPtr method_bind_ptr = get_method_bind_ptr();
		ERR_FAIL_NULL_V_MSG(method_bind_ptr, ret, "Call on missing function: " + owner_class_name + "::" + method.name);
		// instance
		ERR_FAIL_COND_V_MSG(!is_static_ && p_object == nullptr, ret, "Call to non-static method without base object! (missing base argument)");

		::godot::gdextension_interface::object_method_bind_call(
				method_bind_ptr, is_static_ ? nullptr : p_object->_owner, (const GDExtensionConstVariantPtr *)p_args, p_argcount, &ret, &r_error);

		return ret;
	};
};

// ============================================================================
// Utility Function (reuses MethodInfo)
// ============================================================================

struct ApiUtilityFunction : public ApiMethodBase {
	godot::StringName category;

private:
	mutable GDExtensionPtrUtilityFunction func = nullptr; // Private member, loaded lazily
	mutable bool has_returns_ = false;
	mutable bool is_vararg_ = false;

public:
	_FORCE_INLINE_ GDExtensionPtrUtilityFunction get_func_ptr() const {
		using namespace godot;
		if (unlikely(!func)) {
			func = ::godot::gdextension_interface::variant_get_ptr_utility_function(
					method.name._native_ptr(),
					static_cast<GDExtensionInt>(hash));
			if (func == nullptr) {
				ERR_PRINT_ONCE("Failed to load utility function: " + method.name);
				return nullptr;
			}
			has_returns_ = has_returns();
			is_vararg_ = is_vararg();
		}
		return func;
	}
	_FORCE_INLINE_ void validated_call(godot::Variant *r_ret, const godot::Variant **p_args, int p_argcount) const {
		using namespace godot;
		const GDExtensionPtrUtilityFunction func_ptr = get_func_ptr();
		ERR_FAIL_NULL_MSG(func_ptr, "Call on missing utility function: " + method.name);

		const auto &arguments = method.arguments;
		const auto &meta_list = method.arguments_metadata;
		const auto &default_values = method.default_arguments;
		const uint32_t default_value_size = default_values.size();
		const uint32_t method_argcount = arguments.size();
		const uint32_t missing = method_argcount - (uint32_t)p_argcount;
		int argcount = MAX(p_argcount, method_argcount);

		Variant *var_args = stack_alloc(Variant, argcount);
		void **ptr_args = stack_alloc(void *, argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, argcount);
		for (int i = 0; i < argcount; i++) {
			// Arg
			const Variant *arg{ nullptr };
			if (i < p_argcount) {
				arg = p_args[i];
			} else {
				arg = &default_values[i - p_argcount + (default_value_size - missing)];
			}
			// Type
			if (i < arguments.size()) {
				args_type[i] = arguments[i].type;
			} else {
				CRASH_COND(is_vararg_);
				args_type[i] = Variant::NIL;
			}
			// Meta
			GDExtensionClassMethodArgumentMetadata meta = i < meta_list.size() ? meta_list[i] : GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

			// ArgPtr
			Variant *arg_ptr_ = var_args + i;
			internal::var_to_arg_ptr(*arg, arg_ptr_, args_type[i], meta);

			ptr_args[i] = arg_ptr_;
		}

		void *ret_ptr = stack_alloc(Variant, 1);
		if (has_returns_) {
			internal::ctor_arg_ptr(ret_ptr, method.return_val.type);
			CRASH_COND_MSG(ret_ptr == nullptr, "???");
		}
		func_ptr(ret_ptr, ptr_args, argcount);

		while (argcount > 0) {
			argcount--;
			internal::dctor_arg_ptr(ptr_args[argcount], args_type[argcount]);
		}
		if (has_returns_) {
			if (r_ret) internal::arg_ptr_to_var(ret_ptr, method.return_val.type, *r_ret, method.return_val_metadata);

			internal::dctor_arg_ptr(ret_ptr, method.return_val.type);
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
		void *left = stack_alloc(Variant, 1);
		void *right = stack_alloc(Variant, 1);
		internal::var_to_arg_ptr(p_left, left, left_type);
		internal::var_to_arg_ptr(p_right, right, right_type);

		void *result = stack_alloc(Variant, 1);
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
		Variant *var_args = stack_alloc(Variant, p_argcount);
		GDExtensionTypePtr *ptr_args = stack_alloc(GDExtensionTypePtr, p_argcount);
		Variant::Type *args_type = stack_alloc(Variant::Type, p_argcount);
		for (int i = 0; i < p_argcount; i++) {
			const Variant *arg_ptr = p_args[i];
			internal::var_to_arg_ptr(*arg_ptr, var_args + i, arguments[i].type);
			ptr_args[i] = var_args + i;
			args_type[i] = arg_ptr->get_type();
		}

		void *ret_ptr = stack_alloc(Variant, 1);
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
		void *base = stack_alloc(Variant, 1);
		void *value = stack_alloc(Variant, 1);
		internal::var_to_arg_ptr(p_base, base);
		internal::var_to_arg_ptr(p_value, value, type);
		setter_func(base, value);

		internal::dctor_arg_ptr(base, p_base.get_type());
		internal::dctor_arg_ptr(value, p_value.get_type());
	}
	_FORCE_INLINE_ void getter_validated_call(const godot::Variant &p_base, godot::Variant &r_value) const {
		using namespace godot;
		ERR_FAIL_NULL(getter_func);
		void *base = stack_alloc(Variant, 1);
		void *value = stack_alloc(Variant, 1);
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

	friend class internal::ApiStoreReader;
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

		void *base = stack_alloc(Variant, 1);
		void *value = stack_alloc(Variant, 1);
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

		void *base = stack_alloc(Variant, 1);
		void *value = stack_alloc(Variant, 1);
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

		void *base = stack_alloc(Variant, 1);
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

		void *base = stack_alloc(Variant, 1);
		internal::var_to_arg_ptr(p_base, base, type);
		keyed_setter(base, &p_key, &p_value);
	}
};

// ============================================================================
// Class
// ============================================================================

struct ApiClass {
	godot::LocalVector<ApiClassMethod> methods;
	godot::LocalVector<ApiSignalInfo> signals;
	godot::LocalVector<ApiPropertyInfo> properties;
	godot::LocalVector<ApiEnumInfo> enums;
	godot::LocalVector<ApiConstantInfo> constants;
	godot::StringName name;
	godot::StringName inherits;
	godot::ClassDB::APIType api_type;
	bool is_refcounted = false;
	bool is_instantiable = true;
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
// Cache invalidation callback types (global scope for cross-namespace use)
// ============================================================================

// Cache invalidation callback type (global scope for cross-namespace use)
using CacheInvalidatedCallback = std::function<void()>;

// Cache invalidation handle type (global scope for cross-namespace use)
using CacheInvalidatedHandle = int32_t;
