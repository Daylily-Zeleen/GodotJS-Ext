// api_tool.cpp
// Public implementation: delegates to loader, parser, generator.
// Cache clearing happens internally at start of generate() (req 10).

#include "api_tool.h"
#include "api_tool_types.h"
#include "core/api_tool_loader.h"

using namespace godot;

namespace api_tool {
using namespace internal;

#define CHECK_LOADER_V(ret) ERR_FAIL_NULL_V_MSG(get_loader(), ret, "Please call api_tool::initialize() first.");
#define CHECK_LOADER() ERR_FAIL_NULL_MSG(get_loader(), "Please call api_tool::initialize() first.");

// ============================================================================
// Global loader instance
// ============================================================================

static ApiLoader *get_loader() {
	static auto _dummy = [] { return memnew(ApiLoader)->initialize(); }();
	return ApiLoader::get_singleton();
}

// ============================================================================
// Core interface implementation
// ============================================================================

Error initialize() {
	if (get_loader() == nullptr) {
		memnew(ApiLoader);
	}
	return get_loader()->initialize();
}

void finalize() {
	CRASH_COND_MSG(get_loader() == nullptr, "Can't finalize Api tool again. If you need, please call api_tool::initialize() first.");
	memdelete(ApiLoader::get_singleton());
}

bool is_loaded() {
	CHECK_LOADER_V(false);
	return ApiLoader::get_singleton()->is_loaded();
}

void get_version(int32_t &r_major, int32_t &r_minor, int32_t &r_patch) {
	CHECK_LOADER();
	const ApiHeader &hdr = get_loader()->get_header();

	r_major = hdr.version_major;
	r_minor = hdr.version_minor;
	r_patch = hdr.version_patch;
}

const ApiHeader &get_header() {
	return get_loader()->get_header();
}

// ============================================================================
// Query interface implementation (delegate to loader)
// ============================================================================

const ApiUtilityFunction *find_utility_function(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_utility_function(p_name);
}

const ApiBuiltinClass *find_builtin_class(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_builtin_class(p_name);
}

const ApiBuiltinClass *find_builtin_class(Variant::Type p_type) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_builtin_class(p_type);
}

const ApiClass *find_class(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_class(p_name);
}

const ApiEnumInfo *find_global_enum(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_global_enum(p_name);
}

const ApiConstantInfo *find_global_constant(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_global_constant(p_name);
}

const ApiSingleton *find_singleton(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_singleton(p_name);
}

const ApiNativeStructure *find_native_structure(const StringName &p_name) {
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_native_structure(p_name);
}

// ============================================================================
// List interface implementation
// ============================================================================

static const HashSet<StringName> &dummy_name_list() {
	static HashSet<StringName> dummy;
	return dummy;
}

HashSet<StringName> list_utility_functions() {
	CHECK_LOADER_V({});
	return get_loader()->list_utility_functions();
}

const HashSet<StringName> &list_builtin_classes() {
	CHECK_LOADER_V(dummy_name_list());
	return get_loader()->list_builtin_classes();
}

const HashSet<StringName> &list_classes() {
	CHECK_LOADER_V(dummy_name_list());
	return get_loader()->list_classes();
}

const HashSet<StringName> &list_global_enums() {
	CHECK_LOADER_V(dummy_name_list());
	return get_loader()->list_global_enums();
}

const HashSet<StringName> &list_global_constants() {
	CHECK_LOADER_V(dummy_name_list());
	return get_loader()->list_global_constants();
}

HashSet<StringName> list_singletons() {
	CHECK_LOADER_V({});
	return get_loader()->list_singletons();
}

HashSet<StringName> list_native_structures() {
	CHECK_LOADER_V({});
	return get_loader()->list_native_structures();
}

// ============================================================================
// Existence check interface implementation
// ============================================================================

bool has_utility_function(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_utility_function(p_name);
}

bool has_builtin_class(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_builtin_class(p_name);
}

bool has_class(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_class(p_name);
}

bool has_global_enum(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_global_enum(p_name);
}

bool has_global_constant(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_global_constant(p_name);
}

bool has_singleton(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_singleton(p_name);
}

bool has_native_structure(const StringName &p_name) {
	CHECK_LOADER_V(false);
	return get_loader()->has_native_structure(p_name);
}

// ============================================================================
// Cache invalidation callback interface implementation
// ============================================================================

::CacheInvalidatedHandle register_cache_invalidated_callback(::CacheInvalidatedCallback p_callback) {
	CHECK_LOADER_V({});
	return get_loader()->register_cache_invalidated_callback(p_callback);
}

void unregister_cache_invalidated_callback(::CacheInvalidatedHandle p_handle) {
	CHECK_LOADER();
	get_loader()->unregister_cache_invalidated_callback(p_handle);
}

// ============================================================================
// Count interface implementation
// ============================================================================

int32_t get_utility_function_count() {
	CHECK_LOADER_V(0);
	return get_loader()->get_utility_function_count();
}

int32_t get_builtin_class_count() {
	CHECK_LOADER_V(0);
	return get_loader()->get_builtin_class_count();
}

int32_t get_class_count() {
	CHECK_LOADER_V(0);
	return get_loader()->get_class_count();
}

int32_t get_global_enum_count() {
	CHECK_LOADER_V(0);
	return get_loader()->get_global_enum_count();
}

int32_t get_global_constant_count() {
	CHECK_LOADER_V(0);
	return get_loader()->get_global_constant_count();
}

// ============================================================================
// Compatibility hash query interface implementation (no cache, direct file read)
// ============================================================================

const LocalVector<MethodHash> *get_builtin_method_compatibility_hashes(Variant::Type p_type, const StringName &p_name) {
#ifndef DISABLE_DEPRECATED
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_builtin_method_compatibility_hashes(p_type, p_name);
#else // DISABLE_DEPRECATED
	return nullptr;
#endif // DISABLE_DEPRECATED
}

const LocalVector<MethodHash> *get_class_method_compatibility_hashes(const StringName &p_class_name, const StringName &p_name) {
#ifndef DISABLE_DEPRECATED
	CHECK_LOADER_V(nullptr);
	return get_loader()->get_class_method_compatibility_hashes(p_class_name, p_name);
#else // DISABLE_DEPRECATED
	return nullptr;
#endif // !DISABLE_DEPRECATED
}

bool has_generated_data() {
	initialize();
	return get_loader()->has_generated_data();
}

} // namespace api_tool