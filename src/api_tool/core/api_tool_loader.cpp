/************************************************************************/
/*  api_tool_loader.cpp                                                 */
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

// core/api_tool_loader.cpp
// Loading + caching layer implementation (internal).
// Uses std::shared_mutex for concurrent reads, exclusive writes.
// Utility functions loaded as single batch. Supports cache invalidation callback.

#include "api_tool_loader.h"
#include "api_tool/core/api_tool_access.h"
#include "api_tool/core/api_tool_detail_storage.h"
#include "api_tool_store.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace api_tool::internal {

// ============================================================================
// Directory scan helpers (called under lock)
// ============================================================================

PackedStringArray ApiLoader::list_files_in_dir(const String &p_subdir) {
	String dir_path = base_dir_ + "/" + p_subdir;
	if (!DirAccess::dir_exists_absolute(dir_path)) {
		return PackedStringArray();
	}
	PackedStringArray files = DirAccess::get_files_at(dir_path);
	PackedStringArray result;
	for (int i = 0; i < files.size(); i++) {
		String fname = files[i];
		if (fname.ends_with(FILE_EXT_DATA)) {
			result.append(fname.left(fname.length() - static_cast<int>(strlen(FILE_EXT_DATA))));
		}
	}
	return result;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ApiLoader *ApiLoader::singleton = nullptr;

ApiLoader::ApiLoader() {
	CRASH_COND(singleton);
	singleton = this;
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	CRASH_COND(project_settings == nullptr);
	project_settings->connect("settings_changed", callable_mp_static(&ApiLoader::update_base_dir));
	ApiLoader::update_base_dir();
}

ApiLoader::~ApiLoader() {
	clear();
	singleton = nullptr;
	if (ProjectSettings *project_settings = ProjectSettings::get_singleton()) {
		project_settings->disconnect("settings_changed", callable_mp_static(&ApiLoader::update_base_dir));
	}
}

void ApiLoader::update_base_dir() {
	if (singleton) {
		ProjectSettings *ps = ProjectSettings::get_singleton();
		bool use_hidden = ps->get_setting_with_override("application/config/use_hidden_project_data_directory");
		singleton->base_dir_ = ps->globalize_path(vformat("res://%sgodot/.api_dumping", use_hidden ? "." : ""));
	}
}

// ============================================================================
// Initialize
// ============================================================================

Error ApiLoader::initialize() {
	std::unique_lock lock(mutex_);

	update_base_dir();

	loaded_ = false;

	// Try to load header
	String header_path = base_dir_ + "/" + FILE_HEADER;
	Error err = ApiStoreReader::read_header(header_path, header_);
	ERR_FAIL_COND_V_MSG(err, err, vformat("[API Tool] Initialize error (%s), failed to read header: %s ", UtilityFunctions::error_string(err), header_path));
	loaded_ = true;

	internal::double_precision = header_.precision == RealPrecision::DOUBLE;
	return OK;
}

Error ApiLoader::reload() {
	// NB: clear() and initialize() each take the mutex themselves, so this
	// wrapper must NOT hold the lock (std::mutex is not recursive; taking
	// it twice on the same thread throws std::system_error EDEADLK).
	clear();
	return initialize();
}

// ============================================================================
// Clear cache + notify callbacks
// ============================================================================

void ApiLoader::clear() {
	std::unique_lock lock(mutex_);
	utility_function_cache_.clear_data();
	all_utility_functions_loaded_ = false;
	class_cache_.clear_data();
	enum_cache_.clear_data();
	constant_cache_.clear_data();
	singleton_cache_.clear_data();
	all_singletons_loaded_ = false;
	native_structure_cache_.clear_data();
	all_native_structures_loaded_ = false;
	header_ = ApiHeader();
	loaded_ = false;

	// Clear builtin class type cache
	for (int i = 0; i < godot::Variant::VARIANT_MAX; ++i) {
		if (builtin_class_by_type_[i]) {
			memdelete(builtin_class_by_type_[i]);
			builtin_class_by_type_[i] = nullptr;
		}
	}

	// Clear name list caches
	builtin_class_names_cache_.clear();
	builtin_class_names_cached_ = false;
	class_names_cache_.clear();
	class_names_cached_ = false;
	global_enum_names_cache_.clear();
	global_enum_names_cached_ = false;
	global_constant_names_cache_.clear();
	global_constant_names_cached_ = false;

	// Clear compatibility hash caches (TypedCache handles this)
	builtin_compat_hash_cache_.clear_data();
	class_compat_hash_cache_.clear_data();

	// Copy callbacks and call them (req 5)
	std::vector<CallbackEntry> callbacks = cache_callbacks_;
	lock.unlock(); // Unlock before calling user callbacks to avoid deadlock

	for (const auto &entry : callbacks) {
		if (entry.callback) {
			entry.callback();
		}
	}
}

::CacheInvalidatedHandle ApiLoader::register_cache_invalidated_callback(::CacheInvalidatedCallback p_callback) {
	std::unique_lock lock(mutex_);
	CallbackEntry entry;
	entry.callback = p_callback;
	entry.handle = next_callback_handle_++;
	cache_callbacks_.push_back(entry);
	return entry.handle;
}

void ApiLoader::unregister_cache_invalidated_callback(::CacheInvalidatedHandle p_handle) {
	std::unique_lock lock(mutex_);
	for (auto it = cache_callbacks_.begin(); it != cache_callbacks_.end(); ++it) {
		if (it->handle == p_handle) {
			cache_callbacks_.erase(it);
			break;
		}
	}
}

bool ApiLoader::is_loaded() const {
	std::shared_lock lock(mutex_);
	return loaded_;
}

const ApiHeader &ApiLoader::get_header() const {
	return header_;
}

// ============================================================================
// Load all utility functions from single file (req 4)
// ============================================================================

void ApiLoader::ensure_all_utility_functions() {
	if (all_utility_functions_loaded_) return;

	String path = base_dir_ + "/" + FILE_UTILITY_FUNCTIONS;
	LocalVector<ApiUtilityFunction> funcs;
	utility_storage_ = std::make_shared<internal::ApiMethodDetailStorage>();
	Error err = ApiStoreReader::read_utility_functions(path, funcs, utility_storage_.get());
	all_utility_functions_loaded_ = true; // 防止无用的尝试加载，等待重新生成清除缓存

	ERR_FAIL_COND_MSG(err, "[API Tool] load utility functions failed: " + UtilityFunctions::error_string(err));

	for (int i = 0; i < funcs.size(); i++) {
		const StringName &name = funcs[i].get_name();
		if (!utility_function_cache_.name_to_index.has(name)) {
			utility_function_cache_.insert(name, funcs[i]);
		}
	}
}

// ============================================================================
// Load all singletons from single file
// ============================================================================

void ApiLoader::ensure_all_singletons() {
	if (all_singletons_loaded_) return;

	String path = base_dir_ + "/" + DIR_SINGLETONS + "/singletons" + FILE_EXT_DATA;
	LocalVector<ApiSingleton> singletons;
	Error err = ApiStoreReader::read_singletons(path, singletons);
	ERR_FAIL_COND_MSG(err, "[API Tool] load singletons failed: " + UtilityFunctions::error_string(err));

	for (int i = 0; i < singletons.size(); i++) {
		const StringName &name = singletons[i].name;
		if (!singleton_cache_.name_to_index.has(name)) {
			singleton_cache_.insert(name, singletons[i]);
		}
	}
	all_singletons_loaded_ = true;
}

// ============================================================================
// Load all native structures from single file
// ============================================================================

void ApiLoader::ensure_all_native_structures() {
	if (all_native_structures_loaded_) return;

	String path = base_dir_ + "/" + DIR_NATIVE_STRUCTURES + "/native_structures" + FILE_EXT_DATA;
	LocalVector<ApiNativeStructure> structs;
	Error err = ApiStoreReader::read_native_structures(path, structs);
	ERR_FAIL_COND_MSG(err, "[API Tool] load native structures failed: " + UtilityFunctions::error_string(err));
	for (int i = 0; i < structs.size(); i++) {
		const String &name = structs[i].name;
		StringName sn(name);
		if (!native_structure_cache_.name_to_index.has(sn)) {
			native_structure_cache_.insert(sn, structs[i]);
		}
	}
	all_native_structures_loaded_ = true;
}

// ============================================================================
// ensure_*: check cache -> load -> cache -> return
// ============================================================================

const ApiUtilityFunction *ApiLoader::ensure_utility_function(const StringName &p_name) {
	ensure_all_utility_functions();
	return utility_function_cache_.find(p_name);
}

const ApiBuiltinClass *ApiLoader::ensure_builtin_class(const Variant::Type &p_type) {
	const ApiBuiltinClass *cached = builtin_class_by_type_[p_type];
	if (cached) return cached;

	String path = base_dir_ + "/" + String(DIR_BUILTIN_CLASSES) + "/" + Variant::get_type_name(p_type) + FILE_EXT_DATA;
	ApiBuiltinClass *data = memnew(ApiBuiltinClass);
	Error err = ApiStoreReader::read_builtin_class(path, *data);
	ERR_FAIL_COND_V_MSG(err, (memdelete(data), nullptr), vformat("[API Tool] load builtin class %s failed: %s", Variant::get_type_name(p_type), UtilityFunctions::error_string(err)));

	builtin_class_by_type_[p_type] = data;
	return data;
}

const ApiClass *ApiLoader::ensure_class(const StringName &p_name) {
	const ApiClass *cached = class_cache_.find(p_name);
	if (cached) return cached;

	String path = base_dir_ + "/" + DIR_CLASSES + "/" + String(p_name) + FILE_EXT_DATA;
	ApiClass data;
	Error err = ApiStoreReader::read_class(path, data);
	ERR_FAIL_COND_V_MSG(err, nullptr, vformat("[API Tool] load class %s failed: %s", p_name, UtilityFunctions::error_string(err)));

	if (p_name == Object::get_class_static()) {
		// 补充 GDExtension 未暴露的 FLAG_OBJECT_CORE 虚函数。
		const auto is_exists = [&](const StringName &p_name) {
			for (const auto &m : data.methods) {
				if (m.get_name() == p_name) return true; // Found
			}
			return false;
		};

		// Injected methods exist in no store file, so their cold detail (and the
		// compact argument block) is built here and kept in memory; the storage
		// appends it after the file records.
		LocalVector<ApiClassMethod> injected;
		LocalVector<internal::ApiMethodDetail> injected_details;
		LocalVector<internal::ApiMethodArg> injected_args;
		for (const Dictionary &mdict : ClassDB::class_get_method_list(Object::get_class_static())) {
			uint32_t flags = mdict.get("flags", 0);
			if ((flags & METHOD_FLAG_VIRTUAL) && (flags & METHOD_FLAG_OBJECT_CORE)) {
				if (is_exists(mdict["name"])) continue;

				const MethodInfo minfo = MethodInfo::from_dict(mdict);

				internal::ApiMethodDetail detail;
				detail.return_val = minfo.return_val;
				detail.arguments = minfo.arguments;
				for (uint32_t a = 0; a < detail.arguments.size(); a++) {
					const PropertyInfo &pi = detail.arguments[a];
					internal::ApiMethodArg arg{};
					arg.type = (VariantType)pi.type;
					arg.meta = (ArgMeta)(a < minfo.arguments_metadata.size() ? minfo.arguments_metadata[a] : GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE);
					injected_args.push_back(arg);
				}

				const bool has_return_value = internal::has_returns(minfo);
				ApiClassMethod method_data;
				// godot::MethodInfo carries no hash, so these synthesised methods keep
				// hash 0 exactly as the previous implementation did (the old code copied
				// the MethodInfo and left ApiMethodBase::hash at its default).
				ApiMethodAccess::setup(method_data, minfo.name, (MethodHash)0, minfo.flags, has_return_value, minfo.return_val.type, minfo.return_val_metadata, (uint16_t)minfo.arguments.size());
				ApiMethodAccess::set_default_count(method_data, (uint16_t)minfo.default_arguments.size());
				injected_details.push_back(detail);
				injected.push_back(method_data);
			}
		}
		if (injected.size() > 0) {
			// The owner storage is created by read_class; the injected tail is
			// appended to it so all methods share one index space.
			if (data.storage_ == nullptr) data.storage_ = std::make_shared<internal::ApiMethodDetailStorage>();
			for (uint32_t i = 0; i < injected_details.size(); i++) {
				data.storage_->push_injected(injected_details[i], nullptr, 0);
			}
			internal::ApiMethodArg *injected_block = data.storage_->build_injected_arg_block((uint32_t)injected_args.size());
			for (uint32_t i = 0; i < injected_args.size(); i++) injected_block[i] = injected_args[i];

			const uint32_t base_index = (uint32_t)data.methods.size();
			uint32_t arg_offset = 0;
			for (uint32_t i = 0; i < injected.size(); i++) {
				ApiMethodAccess::set_index(injected[i], (uint16_t)(base_index + i));
				ApiMethodAccess::set_storage(injected[i], data.storage_.get());
				ApiMethodAccess::set_args(injected[i], injected_block + arg_offset);
				arg_offset += injected[i].get_argument_count();
				data.methods.push_back(injected[i]);
			}
		}
	}

	class_cache_.insert(p_name, data);
	return class_cache_.find(p_name);
}

const ApiEnumInfo *ApiLoader::ensure_global_enum(const StringName &p_name) {
	const ApiEnumInfo *cached = enum_cache_.find(p_name);
	if (cached) return cached;

	String path = base_dir_ + "/" + DIR_GLOBAL_ENUMS + "/" + String(p_name) + FILE_EXT_DATA;
	ApiEnumInfo data;
	Error err = ApiStoreReader::read_global_enum(path, data);
	ERR_FAIL_COND_V_MSG(err, nullptr, vformat("[API Tool] load global enum %s failed: %s", p_name, UtilityFunctions::error_string(err)));

	enum_cache_.insert(p_name, data);
	return enum_cache_.find(p_name);
}

const ApiConstantInfo *ApiLoader::ensure_global_constant(const StringName &p_name) {
	const ApiConstantInfo *cached = constant_cache_.find(p_name);
	if (cached) return cached;

	String path = base_dir_ + "/" + DIR_GLOBAL_CONSTANTS + "/" + String(p_name) + FILE_EXT_DATA;
	ApiConstantInfo data;
	Error err = ApiStoreReader::read_global_constant(path, data);
	ERR_FAIL_COND_V_MSG(err, nullptr, vformat("[API Tool] load global constant %s failed: %s", p_name, UtilityFunctions::error_string(err)));

	constant_cache_.insert(p_name, data);
	return constant_cache_.find(p_name);
}

const ApiSingleton *ApiLoader::ensure_singleton(const StringName &p_name) {
	ensure_all_singletons();
	return singleton_cache_.find(p_name);
}

const ApiNativeStructure *ApiLoader::ensure_native_structure(const StringName &p_name) {
	ensure_all_native_structures();
	return native_structure_cache_.find(p_name);
}

// ============================================================================
// Compatibility hash queries (cached, thread-safe)
// ============================================================================

const LocalVector<MethodHash> *ApiLoader::get_builtin_method_compatibility_hashes(Variant::Type p_type, const StringName &p_method_name) {
#ifndef DISABLE_DEPRECATED
	// Fast path: shared lock read
	{
		std::shared_lock lock(mutex_);
		const ApiCompatibilityHashData *data = builtin_compat_hash_cache_.find(p_type);
		if (data) {
			for (const auto &m : data->methods) {
				if (m.method_name == p_method_name) return &m.hashes;
			}
			return nullptr;
		}
	}
	// Slow path: exclusive lock + load
	{
		std::unique_lock lock(mutex_);
		// Double-check after acquiring exclusive lock
		const ApiCompatibilityHashData *data = builtin_compat_hash_cache_.find(p_type);
		if (data) {
			for (const auto &m : data->methods) {
				if (m.method_name == p_method_name) return &m.hashes;
			}
			return nullptr;
		}

		// Load from file
		String type_name = Variant::get_type_name(p_type);
		String path = base_dir_ + "/" + String(DIR_COMPAT_HASHES) + "/" + type_name + String(FILE_EXT_COMPAT);
		if (!FileAccess::file_exists(path)) {
			return nullptr; // No file, no negative cache (TypedCache doesn't support it)
		}

		ApiCompatibilityHashData loaded_data;
		Error err = ApiStoreReader::read_compatibility_hashes(path, loaded_data);
		if (err != OK) {
			return nullptr;
		}

		builtin_compat_hash_cache_.insert(p_type, loaded_data);

		for (const auto &m : loaded_data.methods) {
			if (m.method_name == p_method_name) return &m.hashes;
		}
		return nullptr;
	}
#else
	return nullptr;
#endif
}

const LocalVector<MethodHash> *ApiLoader::get_class_method_compatibility_hashes(const StringName &p_class_name, const StringName &p_method_name) {
#ifndef DISABLE_DEPRECATED
	// Fast path: shared lock read
	{
		std::shared_lock lock(mutex_);
		const ApiCompatibilityHashData *data = class_compat_hash_cache_.find(p_class_name);
		if (data) {
			for (const auto &m : data->methods) {
				if (m.method_name == p_method_name) return &m.hashes;
			}
			return nullptr;
		}
	}
	// Slow path: exclusive lock + load
	{
		std::unique_lock lock(mutex_);
		// Double-check after acquiring exclusive lock
		const ApiCompatibilityHashData *data = class_compat_hash_cache_.find(p_class_name);
		if (data) {
			for (const auto &m : data->methods) {
				if (m.method_name == p_method_name) return &m.hashes;
			}
			return nullptr;
		}

		// Load from file
		String path = base_dir_ + "/" + String(DIR_COMPAT_HASHES) + "/" + p_class_name + String(FILE_EXT_COMPAT);
		if (!FileAccess::file_exists(path)) {
			return nullptr; // No file, no negative cache (TypedCache doesn't support it)
		}

		ApiCompatibilityHashData loaded_data;
		Error err = ApiStoreReader::read_compatibility_hashes(path, loaded_data);
		if (err != OK) {
			return nullptr;
		}

		class_compat_hash_cache_.insert(p_class_name, loaded_data);

		for (const auto &m : loaded_data.methods) {
			if (m.method_name == p_method_name) return &m.hashes;
		}
		return nullptr;
	}
#else
	return nullptr;
#endif
}

// ============================================================================
// Name list cache population (caller must hold lock)
// ============================================================================

void ApiLoader::ensure_builtin_class_names() {
	if (!builtin_class_names_cached_) {
		PackedStringArray files = list_files_in_dir(String(DIR_BUILTIN_CLASSES));
		for (int i = 0; i < files.size(); i++) {
			builtin_class_names_cache_.insert(files[i]);
		}
		builtin_class_names_cached_ = true;
	}
}

void ApiLoader::ensure_class_names() {
	if (!class_names_cached_) {
		PackedStringArray files = list_files_in_dir(String(DIR_CLASSES));
		for (int i = 0; i < files.size(); i++) {
			class_names_cache_.insert(files[i]);
		}
		class_names_cached_ = true;
	}
}

void ApiLoader::ensure_global_enum_names() {
	if (!global_enum_names_cached_) {
		PackedStringArray files = list_files_in_dir(DIR_GLOBAL_ENUMS);
		for (int i = 0; i < files.size(); i++) {
			global_enum_names_cache_.insert(files[i]);
		}
		global_enum_names_cached_ = true;
	}
}

void ApiLoader::ensure_global_constant_names() {
	if (!global_constant_names_cached_) {
		PackedStringArray files = list_files_in_dir(DIR_GLOBAL_CONSTANTS);
		for (int i = 0; i < files.size(); i++) {
			global_constant_names_cache_.insert(files[i]);
		}
		global_constant_names_cached_ = true;
	}
}

// ============================================================================
// Public query interfaces (shared_lock for reads, unique_lock for cache misses)
// ============================================================================

const ApiUtilityFunction *ApiLoader::get_utility_function(const StringName &p_name) {
	// First try shared lock (concurrent read)
	{
		std::shared_lock lock(mutex_);
		const ApiUtilityFunction *result = utility_function_cache_.find(p_name);
		if (result) return result;
		if (all_utility_functions_loaded_) return nullptr;
	}
	// Need exclusive lock to load
	std::unique_lock lock(mutex_);
	return ensure_utility_function(p_name);
}

const ApiBuiltinClass *ApiLoader::get_builtin_class(const StringName &p_name) {
	const Variant::Type type = Variant::get_type_by_name(p_name);

	CRASH_COND_MSG(type < 0 || type >= godot::Variant::VARIANT_MAX, "[API Tool] get_builtin_class failed: invalid type name: " + p_name);
	ERR_FAIL_COND_V_MSG(type < 0 || type >= godot::Variant::VARIANT_MAX, nullptr, "[API Tool] get_builtin_class failed: invalid type name: " + p_name);
	return get_builtin_class(type);
}

const ApiBuiltinClass *ApiLoader::get_builtin_class(Variant::Type p_type) {
	std::shared_lock lock(mutex_);
	if (builtin_class_by_type_[p_type] == nullptr) {
		lock.unlock();
		std::unique_lock write_lock(mutex_);
		// Double-check after acquiring exclusive lock
		if (builtin_class_by_type_[p_type] == nullptr) {
			ensure_builtin_class(p_type);
		}
	}
	return builtin_class_by_type_[p_type];
}

// ============================================================================
// Existence check interfaces (O(1) via HashSet)
// ============================================================================

bool ApiLoader::has_utility_function(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_all_utility_functions();
	return utility_function_cache_.name_to_index.has(p_name);
}

bool ApiLoader::has_builtin_class(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_builtin_class_names();
	return builtin_class_names_cache_.has(p_name);
}

bool ApiLoader::has_class(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_class_names();
	return class_names_cache_.has(p_name);
}

bool ApiLoader::has_global_enum(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_global_enum_names();
	return global_enum_names_cache_.has(p_name);
}

bool ApiLoader::has_global_constant(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_global_constant_names();
	return global_constant_names_cache_.has(p_name);
}

bool ApiLoader::has_singleton(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_all_singletons();
	return singleton_cache_.name_to_index.has(p_name);
}

bool ApiLoader::has_native_structure(const StringName &p_name) {
	std::shared_lock lock(mutex_);
	ensure_all_native_structures();
	return native_structure_cache_.name_to_index.has(p_name);
}

const ApiClass *ApiLoader::get_class(const StringName &p_name) {
	{
		std::shared_lock lock(mutex_);
		const ApiClass *result = class_cache_.find(p_name);
		if (result) return result;
	}
	std::unique_lock lock(mutex_);
	return ensure_class(p_name);
}

const ApiEnumInfo *ApiLoader::get_global_enum(const StringName &p_name) {
	{
		std::shared_lock lock(mutex_);
		const ApiEnumInfo *result = enum_cache_.find(p_name);
		if (result) return result;
	}
	std::unique_lock lock(mutex_);
	return ensure_global_enum(p_name);
}

const ApiConstantInfo *ApiLoader::get_global_constant(const StringName &p_name) {
	{
		std::shared_lock lock(mutex_);
		const ApiConstantInfo *result = constant_cache_.find(p_name);
		if (result) return result;
	}
	std::unique_lock lock(mutex_);
	return ensure_global_constant(p_name);
}

const ApiSingleton *ApiLoader::get_singleton(const StringName &p_name) {
	{
		std::shared_lock lock(mutex_);
		const ApiSingleton *result = singleton_cache_.find(p_name);
		if (result) return result;
		if (all_singletons_loaded_) return nullptr;
	}
	std::unique_lock lock(mutex_);
	return ensure_singleton(p_name);
}

const ApiNativeStructure *ApiLoader::get_native_structure(const StringName &p_name) {
	{
		std::shared_lock lock(mutex_);
		const ApiNativeStructure *result = native_structure_cache_.find(p_name);
		if (result) return result;
		if (all_native_structures_loaded_) return nullptr;
	}
	std::unique_lock lock(mutex_);
	return ensure_native_structure(p_name);
}

// ============================================================================
// List interfaces (shared_lock for concurrent reads, O(1) lookup via HashSet)
// ============================================================================

godot::HashSet<godot::StringName> ApiLoader::list_utility_functions() {
	std::shared_lock lock(mutex_);
	ensure_all_utility_functions();
	HashSet<StringName> result;
	for (const auto &item : utility_function_cache_.items) {
		result.insert(item.get_name());
	}
	return result;
}

const godot::HashSet<godot::StringName> &ApiLoader::list_builtin_classes() {
	std::shared_lock lock(mutex_);
	ensure_builtin_class_names();
	return builtin_class_names_cache_;
}

const godot::HashSet<godot::StringName> &ApiLoader::list_classes() {
	std::shared_lock lock(mutex_);
	ensure_class_names();
	return class_names_cache_;
}

const godot::HashSet<godot::StringName> &ApiLoader::list_global_enums() {
	std::shared_lock lock(mutex_);
	ensure_global_enum_names();
	return global_enum_names_cache_;
}

const godot::HashSet<godot::StringName> &ApiLoader::list_global_constants() {
	std::shared_lock lock(mutex_);
	ensure_global_constant_names();
	return global_constant_names_cache_;
}

godot::HashSet<godot::StringName> ApiLoader::list_singletons() {
	std::shared_lock lock(mutex_);
	ensure_all_singletons();
	HashSet<StringName> result;
	for (const auto &item : singleton_cache_.items) {
		result.insert(item.name);
	}
	return result;
}

godot::HashSet<godot::StringName> ApiLoader::list_native_structures() {
	std::shared_lock lock(mutex_);
	ensure_all_native_structures();
	HashSet<StringName> result;
	for (const auto &item : native_structure_cache_.items) {
		result.insert(item.name);
	}
	return result;
}

// ============================================================================
// Count queries (no lock needed for directory scans)
// ============================================================================

int32_t ApiLoader::get_utility_function_count() {
	std::shared_lock lock(mutex_);
	ensure_all_utility_functions();
	return utility_function_cache_.size();
}

int32_t ApiLoader::get_builtin_class_count() {
	std::shared_lock lock(mutex_);
	return list_files_in_dir(String(DIR_BUILTIN_CLASSES)).size();
}

int32_t ApiLoader::get_class_count() {
	std::shared_lock lock(mutex_);
	return list_files_in_dir(DIR_CLASSES).size();
}

int32_t ApiLoader::get_global_enum_count() {
	std::shared_lock lock(mutex_);
	return list_files_in_dir(DIR_GLOBAL_ENUMS).size();
}

int32_t ApiLoader::get_global_constant_count() {
	std::shared_lock lock(mutex_);
	return list_files_in_dir(DIR_GLOBAL_CONSTANTS).size();
}

bool ApiLoader::has_generated_data() const {
	if (!DirAccess::dir_exists_absolute(base_dir_)) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_BUILTIN_CLASSES))) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_CLASSES))) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_GLOBAL_ENUMS))) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_GLOBAL_CONSTANTS))) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_SINGLETONS))) return false;
	if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_NATIVE_STRUCTURES))) return false;
	if (!FileAccess::file_exists(base_dir_.path_join(FILE_UTILITY_FUNCTIONS))) return false;
	if (!FileAccess::file_exists(base_dir_.path_join(FILE_HEADER))) return false;
	return true;
}

const String &ApiLoader::get_api_dumping_dir() {
	return base_dir_;
}

} //namespace api_tool::internal
