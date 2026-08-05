#pragma once

#include <compat/jsb_compat.h>

#include <godot_cpp/classes/resource_format_loader.hpp>

class ResourceFormatLoaderGodotJSScript : public ResourceFormatLoader {
	GDCLASS(ResourceFormatLoaderGodotJSScript, ResourceFormatLoader)

	PackedStringArray recognized_extensions_;

protected:
	static void _bind_methods() {};

public:
	ResourceFormatLoaderGodotJSScript();

	virtual bool _recognize_path(const String &p_path, const StringName &p_type) const override;
	virtual String _get_resource_type(const String &p_path) const override;
	virtual Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
	virtual PackedStringArray _get_recognized_extensions() const override;
	virtual bool _handles_type(const StringName &p_type) const override;
	virtual PackedStringArray _get_dependencies(const String &p_path, bool p_add_types) const override;
	virtual int64_t _get_resource_uid(const String &p_path) const override;
	// virtual bool has_custom_uid_support() const override;
public:
	static bool is_not_godot_resource_script(const String &p_path);
};

