#pragma once

#include <godot_cpp/classes/project_settings.hpp>

namespace godot {

#ifndef GLOBAL_GET
#define GLOBAL_GET(m_var) ProjectSettings::get_singleton()->get_setting_with_override(m_var)
#endif

Variant _GLOBAL_DEF(const String &p_var, const Variant &p_default, bool p_restart_if_changed = false, bool p_ignore_value_in_docs = false, bool p_basic = false, bool p_internal = false);
Variant _GLOBAL_DEF(const PropertyInfo &p_info, const Variant &p_default, bool p_restart_if_changed = false, bool p_ignore_value_in_docs = false, bool p_basic = false, bool p_internal = false);

} //namespace godot