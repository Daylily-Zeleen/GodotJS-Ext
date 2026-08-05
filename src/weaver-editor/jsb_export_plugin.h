#ifndef GODOTJS_EXPORT_PLUGIN_H
#define GODOTJS_EXPORT_PLUGIN_H

#include "jsb_editor_pch.h"
#include <godot_cpp/classes/editor_export_platform.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>

namespace jsb {
class Environment;
}

// improve the pipeline of using typescripts
class GodotJSExportPlugin : public EditorExportPlugin {
	GDCLASS(GodotJSExportPlugin, EditorExportPlugin)

protected:
	static void _bind_methods();

public:
	GodotJSExportPlugin();
	virtual String _get_name() const override;
	virtual bool _supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override;

	static const HashSet<String> &get_ignored_paths();

	virtual void _export_begin(const PackedStringArray &p_features, bool p_debug, const String &p_path, uint32_t p_flags) override;
	virtual void _export_file(const String &p_path, const String &p_type, const PackedStringArray &p_features) override;

	virtual PackedStringArray _get_export_features(const Ref<EditorExportPlatform> &p_export_platform, bool p_debug) const override;

private:
	bool export_compiled_script(const String &p_path, bool p_remap);
	bool export_module_files(const jsb::JavaScriptModule &p_module, bool p_remap);
	bool export_raw_file(const String &p_path, bool p_remap);
	void export_raw_files(const PackedStringArray &p_paths, bool p_permit_typescript);
	void get_script_resources(const String &p_dir, PackedStringArray &r_list, bool p_is_node_module = false);

	HashSet<String> exported_paths_;
	std::shared_ptr<jsb::Environment> env_;
};

#endif
