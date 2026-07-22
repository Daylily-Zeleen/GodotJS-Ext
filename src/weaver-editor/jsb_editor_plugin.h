#ifndef GODOTJS_EDITOR_PLUGINS_H
#define GODOTJS_EDITOR_PLUGINS_H

#include "jsb_editor_pch.h"

#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/editor_file_system_directory.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>

#include <functional>

namespace jsb::weaver
{
    enum ECategoryHint : uint16_t
    {
        CH_JAVASCRIPT = 1 << 0,
        CH_TYPESCRIPT = 1 << 1,
        CH_MISC = 1 << 2,
        CH_GDIGNORE = 1 << 3,
        CH_NODE_MODULES = 1 << 4,

        // only write file if not existed since some files would be modified in projects, such as package.json, by users
        CH_CREATE_ONLY = 1 << 5,
        CH_REPLACE_VARS = 1 << 6,

        CH_D_TS = 1 << 7,
        CH_OPTIONAL = 1 << 8,

        // obsolete files which should be deleted
        CH_OBSOLETE = 1 << 9,
    };

    struct InstallFileInfo
    {
        String source_name;
        String target_dir;

        __underlying_type(ECategoryHint) hint;
    };

}

class InstallGodotJSPresetConfirmationDialog : public ConfirmationDialog
{

public:
    Vector<jsb::weaver::InstallFileInfo> pending_installs_;
    std::function<void(bool)> pending_installs_callback_;
};

// essential editor utilities for GodotJS, such as menu entries in the editor (Install Presets, Generate d.ts)
class GodotJSEditorPlugin : public EditorPlugin
{
    GDCLASS(GodotJSEditorPlugin, EditorPlugin)

private:
    Ref<class GodotJSExportPlugin> export_plugin_;
    Vector<jsb::weaver::InstallFileInfo> install_files_;
    InstallGodotJSPresetConfirmationDialog* confirm_dialog_;

    std::shared_ptr<jsb::internal::Process> tsc_;

    void _generate_api_tool_data();
    void _on_generate_api_tool_data_confirmed(class ConfirmationDialog* p_dialog);

    void _on_scene_saved(const String& p_path);
    void _on_resource_saved(const Ref<Resource>& p_resource);
    void _generate_imported_resource_dts(const PackedStringArray& p_resources);
    void _generate_types_from_cmdline();

	static void _on_generate_completed(const v8::FunctionCallbackInfo<v8::Value>& info);

    static bool _is_path_matchn(const PackedStringArray& p_wildcards, const String& p_path);
    static Vector<String> _filter_resource_paths(const PackedStringArray& p_exclude_wildcards, const PackedStringArray& p_include_wildcards, const Vector<String>& p_paths);

protected:
    static void _bind_methods();

    void _notification(int p_what);
    void _on_menu_pressed(int p_what);
    void _on_confirm_overwrite();

    // Add install file info.
    // Crash if the given info is invalid, ensure to update the preset list in C++ code after it changed in SCsub.
    void add_install_file(jsb::weaver::InstallFileInfo&& p_install_file);

    static String mutate_types(const String& p_content);
    static Error apply_file(const jsb::weaver::InstallFileInfo& p_file);
    static bool install_files(const Vector<jsb::weaver::InstallFileInfo>& p_files);
    static Vector<jsb::weaver::InstallFileInfo> filter_files(const Vector<jsb::weaver::InstallFileInfo>& p_files, int p_hint);
    static bool delete_file(const String& p_file);
    static void get_all_scenes(EditorFileSystemDirectory* p_dir, Vector<String>& r_list);
    static void get_all_resources(EditorFileSystemDirectory* p_dir, Vector<String>& r_list);
    static void generate_scene_nodes_types(std::function<void(bool)> complete, const Vector<String>& p_paths); // TODO: Vector<String> 改为 PackedStringArray
    static void generate_resource_types(std::function<void(bool)> complete, const Vector<String>& p_paths); // TODO: Vector<String> 改为 PackedStringArray

public:
    GodotJSEditorPlugin();
    virtual ~GodotJSEditorPlugin() override;

    void start_tsc_watch();
    bool is_tsc_watching();
    void kill_tsc();

    void remove_obsolete_files();
    bool verify_ts_project() const;
    void _ignore_node_modules();

    // not really a singleton, but always get from `EditorNode` which assumed unique
    static GodotJSEditorPlugin* get_singleton();

    static void generate_types(std::function<void(bool)> complete = {}, bool skip_static_types = false);
    static void try_install_project_files(std::function<void(bool)> complete = {}, bool force = false);
    static void cleanup_invalid_files(std::function<void(bool)> complete = {});
    static void install_static_types(std::function<void(bool)> complete = {});

    static void generate_all_scene_nodes_types();
    static void generate_all_resource_types();
    static void ignore_node_modules();
    static void collect_invalid_files(Vector<String>& r_invalid_files);
    static void collect_invalid_files(const String& p_path, Vector<String>& r_invalid_files);
    static void install_project_files(std::function<void(bool)> complete, const Vector<jsb::weaver::InstallFileInfo>& p_files);
    static bool is_preset_source_valid(const String& p_filename) { return get_preset_source(p_filename).is_valid(); }
    static jsb::internal::PresetSource get_preset_source(const String& p_filename);

    static void ensure_tsc_installed();

    /**
     * return true if everything is identical to the expected version.
     * otherwise return false with changed files in `r_modified`.
     */
    static bool verify_files(const Vector<jsb::weaver::InstallFileInfo>& p_files, bool p_verify_content, Vector<jsb::weaver::InstallFileInfo>* r_modified);
    static bool verify_file(const jsb::weaver::InstallFileInfo& p_file, bool p_verify_content);

    static void on_successfully_installed();

    static void load_editor_entry_module();

private:
    // Editor Progress Helpers. In c++, use EditorProgress directly.
    static void _add_progress_task(const String& p_task_name, int total_steps);
    static void _update_progress_task(const String& p_task_name, const String& p_state, int p_step);
    static void _finish_progress_task(const String& p_task_name);
};

#endif
