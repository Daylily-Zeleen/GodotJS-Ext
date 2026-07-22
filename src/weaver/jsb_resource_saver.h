#ifndef GODOTJS_RESOURCE_SAVER_H
#define GODOTJS_RESOURCE_SAVER_H

#include "../compat/jsb_compat.h"

#include <godot_cpp/classes/resource_format_saver.hpp>

class ResourceFormatSaverGodotJSScript : public ResourceFormatSaver
{
    GDCLASS(ResourceFormatSaverGodotJSScript, ResourceFormatSaver)

protected:
    static void _bind_methods() {};

private:
    bool add_uid_to_source(String &p_r_source, const String &p_path, int64_t p_uid = -1) const;

public:
    virtual Error _save(const Ref<Resource>& p_resource, const String& p_path, uint32_t p_flags) override;
    virtual PackedStringArray _get_recognized_extensions(const Ref<Resource>& p_resource) const override;
    virtual bool _recognize(const Ref<Resource>& p_resource) const override;
	virtual Error _set_uid(const String& p_path, int64_t p_uid) override;
};

#endif
