// core/api_tool_loader.cpp
// Loading + caching layer implementation (internal).
// Uses std::shared_mutex for concurrent reads, exclusive writes.
// Utility functions loaded as single batch. Supports cache invalidation callback.

#include "api_tool_loader.h"
#include "api_tool_store.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
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

LocalVector<String> ApiLoader::get_cached_names_utility() const {
    LocalVector<String> names;
    names.reserve(utility_function_cache_.items.size());
    for (const auto &item : utility_function_cache_.items) {
        names.push_back(String(item.method.name));
    }
    return names;
}

LocalVector<String> ApiLoader::get_cached_names_singleton() const {
    LocalVector<String> names;
    names.reserve(singleton_cache_.items.size());
    for (const auto &item : singleton_cache_.items) {
        names.push_back(String(item.name));
    }
    return names;
}

LocalVector<String> ApiLoader::get_cached_names_native() const {
    LocalVector<String> names;
    names.reserve(native_structure_cache_.items.size());
    for (const auto &item : native_structure_cache_.items) {
        names.push_back(item.name);
    }
    return names;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ApiLoader* singleton = nullptr;

ApiLoader::ApiLoader() {
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
        singleton->base_dir_ = ps->globalize_path(vformat("res://%sgodot/.api_dumping", use_hidden ? ".": ""));
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
    ERR_FAIL_COND_V_MSG(err, err, vformat("[API Tool] Initialize error (%s), failed to read header: %s " , UtilityFunctions::error_string(err), header_path));
    loaded_ = true;

    internal::double_precision = header_.precision == RealPrecision::DOUBLE;
    return OK;
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
        if(builtin_class_by_type_[i]){
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
    Error err = ApiStoreReader::read_utility_functions(path, funcs);
    all_utility_functions_loaded_ = true; // 防止无用的尝试加载，等待重新生成清除缓存

    ERR_FAIL_COND_MSG(err, "[API Tool] load utility functions failed: " + UtilityFunctions::error_string(err));

    for (int i = 0; i < funcs.size(); i++) {
        const StringName &name = funcs[i].method.name;
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

#ifdef TOOLS_ENABLED
// ============================================================================
// Document queries (no cache, direct file read, TOOLS_ENABLED only)
// ============================================================================

std::unique_ptr<ApiClassDocument> ApiLoader::find_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    // Try class first
    String path = base_dir_ + "/" + String(DIR_DOC_CLASSES) + "/" + String(p_name) + String(FILE_EXT_DOC);
    auto doc = std::make_unique<ApiClassDocument>();
    if (FileAccess::file_exists(path)) {
        Error err = ApiStoreReader::read_document(path, *doc);
        if (err == OK) {
            return doc;
        }
    } 
    // Try builtin class
    path = base_dir_ + "/" + String(DIR_DOC_BUILTIN_CLASSES) + "/" + String(p_name) + String(FILE_EXT_DOC);
    if (FileAccess::file_exists(path)) {
        Error err = ApiStoreReader::read_document(path, *doc);
        if (err == OK) {
            return doc;
        }
    }
    return nullptr;
#else
    return nullptr;
#endif
}

std::unique_ptr<ApiUtilityFunctionDocument> ApiLoader::find_utility_function_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    String path = base_dir_ + "/" + String(DIR_DOC_UTILITY_FUNCTIONS) + "/" + String(p_name) + String(FILE_EXT_DOC);
    auto doc = std::make_unique<ApiUtilityFunctionDocument>();
    Error err = ApiStoreReader::read_utility_function_document(path, *doc);
    if (err != OK) return nullptr;
    return doc;
#else
    return nullptr;
#endif
}

std::unique_ptr<ApiGlobalEnumDocument> ApiLoader::find_global_enum_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    String path = base_dir_ + "/" + String(DIR_DOC_GLOBAL_ENUMS) + "/" + String(p_name) + String(FILE_EXT_DOC);
    auto doc = std::make_unique<ApiGlobalEnumDocument>();
    Error err = ApiStoreReader::read_global_enum_document(path, *doc);
    if (err != OK) return nullptr;
    return doc;
#else
    return nullptr;
#endif
}

std::unique_ptr<ApiGlobalConstantDocument> ApiLoader::find_global_constant_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    String path = base_dir_ + "/" + String(DIR_DOC_GLOBAL_CONSTANTS) + "/" + String(p_name) + String(FILE_EXT_DOC);
    auto doc = std::make_unique<ApiGlobalConstantDocument>();
    Error err = ApiStoreReader::read_global_constant_document(path, *doc);
    if (err != OK) return nullptr;
    return doc;
#else
    return nullptr;
#endif
}
#endif // TOOLS_ENABLED

// ============================================================================
// Compatibility hash queries (no cache, direct file read)
// ============================================================================

LocalVector<MethodHash> ApiLoader::get_builtin_method_compatibility_hashes(Variant::Type p_type, const StringName &p_method_name) {
#ifndef DISABLE_DEPRECATED
    LocalVector<MethodHash> result;
    String path = base_dir_ + "/" + String(DIR_COMPAT_HASHES) + "/" + Variant::get_type_name(p_type) + String(FILE_EXT_COMPAT);
    if (!FileAccess::file_exists(path)) {
        return result;
    }

    ApiCompatibilityHashData data;
    Error err = ApiStoreReader::read_compatibility_hashes(path, data);
    if (err != OK) {
        return result;
    }

    for (const auto &m : data.methods) {
        if (m.method_name == p_method_name) {
            return m.hashes;
        }
    }
    return result;
#else // DISABLE_DEPRECATED
    return {};
#endif // DISABLE_DEPRECATED
}

LocalVector<MethodHash> ApiLoader::get_class_method_compatibility_hashes(const StringName &p_class_name, const StringName &p_method_name) {
#ifndef DISABLE_DEPRECATED
    LocalVector<MethodHash> result;
    String path = base_dir_ + "/" + String(DIR_COMPAT_HASHES) + "/" + String(p_class_name) + String(FILE_EXT_COMPAT);
    if (!FileAccess::file_exists(path)) {
        return result;
    }

    ApiCompatibilityHashData data;
    Error err = ApiStoreReader::read_compatibility_hashes(path, data);
    if (err != OK) {
        return result;
    }

    for (const auto &m : data.methods) {
        if (m.method_name == p_method_name) {
            return m.hashes;
        }
    }
    return result;
#else // DISABLE_DEPRECATED
    return {};
#endif // DISABLE_DEPRECATED
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
        result.insert(item.method.name);
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
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_UTILITY_FUNCTIONS))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_BUILTIN_CLASSES))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_CLASSES))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_GLOBAL_ENUMS))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_GLOBAL_CONSTANTS))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_SINGLETONS))) return false;
    if (!DirAccess::dir_exists_absolute(base_dir_.path_join(DIR_NATIVE_STRUCTURES))) return false;
    return true;
}

const String &ApiLoader::get_api_dumping_dir() {
    return base_dir_;
}

} // namespace api_tool
