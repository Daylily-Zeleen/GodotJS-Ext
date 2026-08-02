// api_tool.cpp
// Public implementation: delegates to loader, parser, generator.
// Cache clearing happens internally at start of generate() (req 10).

#include "api_tool.h"
#include "api_tool_types.h"
#include "core/api_tool_loader.h"
#include "editor/api_tool_generator.h"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef TOOLS_ENABLED
#include <godot_cpp/classes/engine.hpp>
#endif

using namespace godot;

namespace api_tool {
using namespace internal;

#define CHECK_LOADER_V(ret) ERR_FAIL_NULL_V_MSG(get_loader(), ret, "Please call api_tool::initialize() first.");
#define CHECK_LOADER() ERR_FAIL_NULL_MSG(get_loader(), "Please call api_tool::initialize() first.");

// ============================================================================
// Global loader instance
// ============================================================================

static ApiLoader *get_loader() {
    static auto _dummy = []{ return memnew(ApiLoader)->initialize(); }();
    return ApiLoader::get_singleton();
}

// ============================================================================
// Core interface implementation
// ============================================================================

Error initialize() {
    if (get_loader() == nullptr) { memnew(ApiLoader); }
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
// Document query interface implementation (delegate to loader, no cache)
// ============================================================================

std::unique_ptr<ApiClassDocument> find_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    CHECK_LOADER_V(nullptr);
    return get_loader()->find_document(p_name);
#else // !TOOLS_ENABLED
    return nullptr;
#endif // TOOLS_ENABLED
}

std::unique_ptr<ApiClassDocument> find_document(const Variant::Type &p_type) {
    return find_document(Variant::get_type_name(p_type));
}

std::unique_ptr<ApiUtilityFunctionDocument> find_utility_function_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    CHECK_LOADER_V(nullptr);
    return get_loader()->find_utility_function_document(p_name);
#else // !TOOLS_ENABLED
    return nullptr;
#endif // TOOLS_ENABLED
}

std::unique_ptr<ApiGlobalEnumDocument> find_global_enum_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    CHECK_LOADER_V(nullptr);
    return get_loader()->find_global_enum_document(p_name);
#else // !TOOLS_ENABLED
    return nullptr;
#endif // TOOLS_ENABLED
}

std::unique_ptr<ApiGlobalConstantDocument> find_global_constant_document(const StringName &p_name) {
#ifdef TOOLS_ENABLED
    CHECK_LOADER_V(nullptr);
    return get_loader()->find_global_constant_document(p_name);
#else // !TOOLS_ENABLED
    return nullptr;
#endif // TOOLS_ENABLED
}

// ============================================================================
// List interface implementation
// ============================================================================

static const HashSet<StringName> &dummy_name_list() {
    static HashSet<StringName> dummy; return dummy;
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

const LocalVector<MethodHash>* get_builtin_method_compatibility_hashes(Variant::Type p_type, const StringName &p_name) {
#ifndef DISABLE_DEPRECATED
    CHECK_LOADER_V(nullptr);
    return get_loader()->get_builtin_method_compatibility_hashes(p_type, p_name);
#else // DISABLE_DEPRECATED
    return nullptr;
#endif// DISABLE_DEPRECATED

}

const LocalVector<MethodHash>* get_class_method_compatibility_hashes(const StringName &p_class_name, const StringName &p_name) {
#ifndef DISABLE_DEPRECATED
    CHECK_LOADER_V(nullptr);
    return get_loader()->get_class_method_compatibility_hashes(p_class_name, p_name);
#else // DISABLE_DEPRECATED
    return nullptr;
#endif // !DISABLE_DEPRECATED
}

// ============================================================================
// Generate interface implementation (only TOOLS_ENABLED)
// Cache is invalidated internally at start of generate() (req 10)
// ============================================================================

#ifdef TOOLS_ENABLED
void full_generate_and_reboot() {
    const String project_dir = ProjectSettings::get_singleton()->globalize_path("res://");
    const String godot_executable_path = OS::get_singleton()->get_executable_path();
    const String api_file_path = project_dir.path_join("extension_api.json");
    const int my_pid = OS::get_singleton()->get_process_id();

    const String api_file_bak = api_file_path + String(".bak");
    if (FileAccess::file_exists(api_file_path)) {
        DirAccess::rename_absolute(api_file_path, api_file_bak);
    }

    const String dump_cmd = vformat("\"%s\" --headless --path \"%s\" --dump-extension-api-with-docs",
        godot_executable_path, project_dir);
    const String reboot_cmd = vformat("\"%s\" --editor --path \"%s\" --godotjs-api-generate \"%s\"",
        godot_executable_path, project_dir, api_file_path);
    // Use platform-appropriate command separator: & on Windows, ; on Unix
    const String separator = OS::get_singleton()->has_feature("windows") ? String(" & ") : String(" ; ");
    const String chain = dump_cmd + separator + reboot_cmd;

    // Write a temporary shell script that polls until the current editor process
    // fully exits, then runs the dump + reboot chain. This ensures no file/resource
    // conflicts between the shutting-down editor and the headless dump.
    String script_path;
    String shell_program;
    PackedStringArray shell_args;

    if (OS::get_singleton()->has_feature("windows")) {
        script_path = project_dir.path_join(".godot/.reboot_chain.bat");
        Ref<FileAccess> f = FileAccess::open(script_path, FileAccess::WRITE);
        ERR_FAIL_NULL_MSG(f, "[API Tool] Failed to create reboot script");
        f->store_string("@echo off\r\n");
        f->store_string("echo Waiting for editor to exit...\r\n");
        f->store_string(vformat(":wait\r\n"));
        f->store_string(vformat("tasklist /fi \"PID eq %d\" 2>nul | findstr /i \"%d\" >nul\r\n", my_pid, my_pid));
        f->store_string("if %errorlevel%==0 (timeout /t 1 /nobreak >nul & goto wait)\r\n");
        f->store_string("echo Editor exited. Starting dump and reboot...\r\n");
        f->store_string(chain + String("\r\n"));
        f->store_string("echo Reboot chain completed. Press any key to close this window...\r\n");
        f->store_string("pause\r\n");
        f->close();
        shell_program = "cmd";
        shell_args.push_back("/c");
        shell_args.push_back(script_path);
    } else if (OS::get_singleton()->has_feature("linux") ||
               OS::get_singleton()->has_feature("macos") ||
               OS::get_singleton()->has_feature("android")) {
        script_path = project_dir.path_join(".godot/.reboot_chain.sh");
        Ref<FileAccess> f = FileAccess::open(script_path, FileAccess::WRITE);
        ERR_FAIL_NULL_MSG(f, "[API Tool] Failed to create reboot script");
        f->store_string("#!/bin/sh\n");
        f->store_string("echo 'Waiting for editor to exit...'\n");
        f->store_string(vformat("while kill -0 %d 2>/dev/null; do sleep 1; done\n", my_pid));
        f->store_string("echo 'Editor exited. Starting dump and reboot...'\n");
        f->store_string(chain + String("\n"));
        f->store_string("echo 'Reboot chain completed. Press Enter to exit...'\n");
        f->store_string("read -p ''\n");
        f->close();
        shell_program = "/bin/sh";
        shell_args.push_back(script_path);
    } else {
        UtilityFunctions::push_warning("[API Tool] Reboot chain not supported on OS. Please restart editor manually.");
        return;
    }

    UtilityFunctions::print("[API Tool] Launching reboot chain: wait for exit -> dump API -> relaunch editor...");
    int pid = OS::get_singleton()->create_process(shell_program, shell_args, true);
    if (pid < 0) {
        UtilityFunctions::push_error("[API Tool] Failed to launch reboot chain script");
        return;
    }

    // Step 1: Close current editor. The script will poll until we fully exit before proceeding.
    Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())->quit();
}


Error generate_api_tool_data(const String &p_extension_api_json_path) {
    initialize();
    ApiLoader &loader = *get_loader();
    String out_dir = loader.get_api_dumping_dir();

    // Clear cache before generation (req 10)
    loader.clear();

    // Use generator to launch subprocess and parse
    Error err = ApiGenerator::generate(p_extension_api_json_path, out_dir);
    if (err != OK) {
        return err;
    }

    // Delete generated file and restore backup if exists.
    if (FileAccess::file_exists(p_extension_api_json_path)) {
        DirAccess::remove_absolute(p_extension_api_json_path);
    }

    String backup_path = p_extension_api_json_path + String(".bak");
    if (FileAccess::file_exists(backup_path)) {
        DirAccess::rename_absolute(backup_path, p_extension_api_json_path);
    }

    return loader.initialize();
}
#endif // TOOLS_ENABLED


bool has_generated_data() {
    initialize();
    return get_loader()->has_generated_data();
}


} // namespace api_tool