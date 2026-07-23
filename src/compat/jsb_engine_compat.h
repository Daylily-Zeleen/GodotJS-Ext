#ifndef GODOTJS_ENGINE_COMPAT_H
#define GODOTJS_ENGINE_COMPAT_H

#include "jsb_engine_version_comparison.h"
#include "../jsb.config.h"

#if defined(__GNUC__) || defined(__clang__)
#   define jsb_force_inline  __attribute__((always_inline))
#elif defined(_MSC_VER)
#   define jsb_force_inline  __forceinline
#else
#   define jsb_force_inline
#endif

#    define jsb_ext_print_rich(Content) ::godot::UtilityFunctions::print_rich(jsb_format("\u001b[90m%s\u001b[39m\n", Content))
#    define jsb_ext_print_line(Content) ::godot::UtilityFunctions::print(Content)
#    define jsb_ext_print_error(Function, File, Line, Error, EditorNotify, IsWarning) ::godot::_err_print_error(Function, File, Line, Error, EditorNotify, IsWarning)
#    define jsb_ext_type_convert(Value, Type) ::godot::UtilityFunctions::type_convert(Value, Type)
#    define jsb_ext_error_string(Error) ::godot::UtilityFunctions::error_string(Error)
#    define jsb_ext_is_cmdline_tool() false

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/spin_lock.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/rb_set.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/reg_ex.hpp>

using namespace godot;

namespace jsb::compat
{
    typedef godot::ObjectDB ObjectDB;
    typedef godot::Performance Performance;

    using ThreadID = uint64_t;
    constexpr ThreadID UNASSIGNED_THREAD_ID = 0;
    jsb_force_inline ThreadID get_thread_caller_id() { return OS::get_singleton()->get_thread_caller_id(); }
}

inline Variant EDITOR_GET(const String &p_setting)
{
    if (EditorInterface::get_singleton())
    {
        Ref settings = EditorInterface::get_singleton()->get_editor_settings();
        if (settings.is_valid() && settings->has_setting(p_setting))
        {
            return settings->get_setting(p_setting);
        }
    }
    return Variant();
}

#ifndef GLOBAL_GET
#define GLOBAL_GET(m_var) ProjectSettings::get_singleton()->get_setting_with_override(m_var)
#endif

#ifndef EDSCALE
#define EDSCALE EditorInterface::get_singleton()->get_editor_scale()
#endif

#include "jsb_engine_version_comparison.h"

#endif
