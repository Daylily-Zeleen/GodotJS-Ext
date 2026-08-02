#pragma once

// core/api_tool_loader.h
// Loading + caching layer (internal).
// Lazily loads API data from disk on demand, caches for subsequent queries.
// Uses std::shared_mutex (RWLock) for concurrent reads, exclusive writes.
// Utility functions are loaded as a single batch from one file.
// Supports cache invalidation callback for external notification.

#include "../api_tool_types.h"
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <deque>
#include <memory>
#include <shared_mutex>

namespace api_tool::internal {

class ApiLoader {
public:
    ApiLoader();
    ~ApiLoader();

    godot::Error initialize();

    // Clear all cached data. Calls cache invalidation callbacks.
    void clear();

    bool is_loaded() const;
    const ApiHeader &get_header() const;

    // ---- Name-based queries (returns nullptr if not found) ----
    // Pointers valid until clear() is called.

    const ApiUtilityFunction *get_utility_function(const godot::StringName &p_name);
    const ApiBuiltinClass *get_builtin_class(const godot::StringName &p_name);
    const ApiBuiltinClass *get_builtin_class(godot::Variant::Type p_type); // O(1) via fixed-size array
    const ApiClass *get_class(const godot::StringName &p_name);
    const ApiEnumInfo *get_global_enum(const godot::StringName &p_name);
    const ApiConstantInfo *get_global_constant(const godot::StringName &p_name);
    const ApiSingleton *get_singleton(const godot::StringName &p_name);
    const ApiNativeStructure *get_native_structure(const godot::StringName &p_name);

#ifdef TOOLS_ENABLED
    // ---- Document queries (no cache, direct file read, TOOLS_ENABLED only) ----
    // Returns std::unique_ptr<T> (caller owns). Returns nullptr if file missing/corrupted.
    std::unique_ptr<ApiClassDocument> find_document(const godot::StringName &p_name);
    std::unique_ptr<ApiUtilityFunctionDocument> find_utility_function_document(const godot::StringName &p_name);
    std::unique_ptr<ApiGlobalEnumDocument> find_global_enum_document(const godot::StringName &p_name);
    std::unique_ptr<ApiGlobalConstantDocument> find_global_constant_document(const godot::StringName &p_name);
#endif // TOOLS_ENABLED

    // ---- Compatibility hash queries (cached, thread-safe) ----
    const godot::LocalVector<MethodHash>* get_builtin_method_compatibility_hashes(godot::Variant::Type p_type, const godot::StringName &p_method_name);
    const godot::LocalVector<MethodHash>* get_class_method_compatibility_hashes(const godot::StringName &p_class_name, const godot::StringName &p_method_name);

    // ---- List all names (O(1) lookup via HashSet) ----

    godot::HashSet<godot::StringName> list_utility_functions();
    const godot::HashSet<godot::StringName> &list_builtin_classes();
    const godot::HashSet<godot::StringName> &list_classes();
    const godot::HashSet<godot::StringName> &list_global_enums();
    const godot::HashSet<godot::StringName> &list_global_constants();
    godot::HashSet<godot::StringName> list_singletons();
    godot::HashSet<godot::StringName> list_native_structures();

    // ---- Existence checks (O(1) via cache) ----

    bool has_utility_function(const godot::StringName &p_name);
    bool has_builtin_class(const godot::StringName &p_name);
    bool has_class(const godot::StringName &p_name);
    bool has_global_enum(const godot::StringName &p_name);
    bool has_global_constant(const godot::StringName &p_name);
    bool has_singleton(const godot::StringName &p_name);
    bool has_native_structure(const godot::StringName &p_name);

    // ---- Cache invalidation callbacks ----

    ::CacheInvalidatedHandle register_cache_invalidated_callback(::CacheInvalidatedCallback p_callback);
    void unregister_cache_invalidated_callback(::CacheInvalidatedHandle p_handle);

    // ---- Count queries (no lock for simple file counts) ----

    int32_t get_utility_function_count();
    int32_t get_builtin_class_count();
    int32_t get_class_count();
    int32_t get_global_enum_count();
    int32_t get_global_constant_count();

    bool has_generated_data() const;

    const godot::String &get_api_dumping_dir();

    static ApiLoader *get_singleton() { return singleton; }

private:
    static ApiLoader *singleton;

    // Stable-pointer cache: deque stores data, HashMap stores name->index.
    // Pointers into deque are stable after push_back (deque block structure).
    template<typename CacheKey, typename T>
    struct TypedCache {
        std::deque<T> items;
        godot::HashMap<CacheKey, int32_t> name_to_index;

        const T *find(const CacheKey &p_name) const {
            auto it = name_to_index.find(p_name);
            if (it == name_to_index.end()) return nullptr;
            return &items[it->value];
        }

        void insert(const CacheKey &p_name, const T &p_item) {
            int32_t idx = static_cast<int32_t>(items.size());
            items.push_back(p_item);
            name_to_index[p_name] = idx;
        }

        void clear_data() {
            items.clear();
            name_to_index.clear();
        }

        int32_t size() const {
            return static_cast<int32_t>(items.size());
        }
    };

    static void update_base_dir();

    // Ensure-load: check cache -> load from file -> cache -> return pointer.
    const ApiUtilityFunction *ensure_utility_function(const godot::StringName &p_name);
    const ApiBuiltinClass *ensure_builtin_class(const godot::Variant::Type &p_type);
    const ApiClass *ensure_class(const godot::StringName &p_name);
    const ApiEnumInfo *ensure_global_enum(const godot::StringName &p_name);
    const ApiConstantInfo *ensure_global_constant(const godot::StringName &p_name);
    const ApiSingleton *ensure_singleton(const godot::StringName &p_name);
    const ApiNativeStructure *ensure_native_structure(const godot::StringName &p_name);

    // Load all utility functions from single file (req 4).
    void ensure_all_utility_functions();
    // Load all singletons from single file.
    void ensure_all_singletons();
    // Load all native structures from single file.
    void ensure_all_native_structures();

    // Directory scan helpers (no lock - caller must hold lock).
    godot::PackedStringArray list_files_in_dir(const godot::String &p_subdir);

    // Ensure name list caches are populated.
    void ensure_builtin_class_names();
    void ensure_class_names();
    void ensure_global_enum_names();
    void ensure_global_constant_names();

    godot::String base_dir_;
    ApiHeader header_;
    bool loaded_ = false;

    // Cache invalidation callbacks (handle-based)
    struct CallbackEntry {
        ::CacheInvalidatedHandle handle;
        CacheInvalidatedCallback callback;
    };
    std::vector<CallbackEntry> cache_callbacks_;
    ::CacheInvalidatedHandle next_callback_handle_ = 1;

    // Caches
    TypedCache<godot::StringName, ApiUtilityFunction> utility_function_cache_;
    bool all_utility_functions_loaded_ = false;

    // Fixed-size array for O(1) builtin class lookup by Variant::Type
    // Indexed by Variant::Type (0..VARIANT_MAX-1), nullptr if not loaded/not a builtin type
    ApiBuiltinClass *builtin_class_by_type_[godot::Variant::VARIANT_MAX] = {nullptr};

    TypedCache<godot::StringName, ApiClass> class_cache_;
    TypedCache<godot::StringName, ApiEnumInfo> enum_cache_;
    TypedCache<godot::StringName, ApiConstantInfo> constant_cache_;

    TypedCache<godot::StringName, ApiSingleton> singleton_cache_;
    bool all_singletons_loaded_ = false;

    TypedCache<godot::StringName, ApiNativeStructure> native_structure_cache_;
    bool all_native_structures_loaded_ = false;

    // Name list caches (populated on first list_* call, cleared on clear())
    godot::HashSet<godot::StringName> builtin_class_names_cache_;
    bool builtin_class_names_cached_ = false;
    godot::HashSet<godot::StringName> class_names_cache_;
    bool class_names_cached_ = false;
    godot::HashSet<godot::StringName> global_enum_names_cache_;
    bool global_enum_names_cached_ = false;
    godot::HashSet<godot::StringName> global_constant_names_cache_;
    bool global_constant_names_cached_ = false;

    // Compatibility hash caches (per-type/class, keyed by Variant::Type or StringName)
    // Uses TypedCache for stable-pointer pattern (deque + HashMap).
    TypedCache<godot::Variant::Type, ApiCompatibilityHashData> builtin_compat_hash_cache_;
    TypedCache<godot::StringName, ApiCompatibilityHashData> class_compat_hash_cache_;

    // RWLock: shared_mutex for concurrent reads, exclusive writes (req 3)
    mutable std::shared_mutex mutex_;
};

} // namespace api_tool
