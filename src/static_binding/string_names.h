/************************************************************************/
/*  string_names.h                                                      */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Lazy StringName resolution over the generated string table.          */
/*  Hard rule (AGENTS.md): no global-scope godot-cpp type instances --   */
/*  the table is wrapped in a function-local static and resolved on      */
/*  first access (after GDExtension init).                               */
/************************************************************************/

#pragma once

#include <cstdint>

namespace godot {
class StringName;
}

namespace jsb::static_binding {

class StringNames {
public:
    // Thread-safe. On first access resolves the entire generated string
    // table into StringNames; subsequent lookups are lock-free O(1).
    static const godot::StringName &get(uint32_t p_id);

    static uint32_t count();
};

} // namespace jsb::static_binding
