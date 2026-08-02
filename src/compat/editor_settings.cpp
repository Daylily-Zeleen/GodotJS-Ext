#include "editor_settings.h"

namespace godot {

EditorSettings* get_editor_settings()
{
	if (EditorInterface* ei = EditorInterface::get_singleton())
	{
		return EditorInterface::get_singleton()->get_editor_settings().ptr();
	}
    return nullptr;
}

Variant EDITOR_GET(const String &p_setting)
{
    if (EditorSettings *settings = get_editor_settings())
    {
        if (settings && settings->has_setting(p_setting))
        {
            return settings->get_setting(p_setting);
        }
    }
    return Variant();
}

Variant _EDITOR_DEF(const String &p_setting, const Variant &p_default, bool p_restart_if_changed, bool p_basic) {
    EditorSettings *editor_settings = get_editor_settings();
	ERR_FAIL_NULL_V_MSG(editor_settings, p_default, "EditorSettings not instantiated yet.");

	Variant ret = p_default;
	if (editor_settings->has_setting(p_setting)) {
		ret = EDITOR_GET(p_setting);
	} else {
        bool blocking_signals = editor_settings;
        editor_settings->set_block_signals(true);
		editor_settings->set_setting(p_setting, p_default);
        editor_settings->set_block_signals(blocking_signals);
	}
	// editor_settings->set_restart_if_changed(p_setting, p_restart_if_changed); // TODO: EditorSettings 未暴露该接口
	// editor_settings->set_basic(p_setting, p_basic);

    editor_settings->set_initial_value(p_setting, p_default, false);
	return ret;
}

}