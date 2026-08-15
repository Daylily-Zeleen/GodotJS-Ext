#pragma once

#ifndef TOOLS_ENABLED
#	error "CAN NOT COMPILE WITHOUT TOOLS_ENABLED, PLEASE CHECK IT'S NOT UNEXPECTEDLY INCLUDED."
#endif

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>

namespace godot {
Ref<EditorSettings> get_editor_settings();

#ifndef EDSCALE
#	define EDSCALE EditorInterface::get_singleton()->get_editor_scale()
#endif

Variant EDITOR_GET(const String &p_setting);
Variant _EDITOR_DEF(const String &p_setting, const Variant &p_default, bool p_restart_if_changed = false, bool p_basic = false);
} //namespace godot