/************************************************************************/
/*  string_names.cpp                                                    */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
#include "string_names.h"

#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/mutex.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "gen/string_names.gen.h"

namespace jsb::static_binding {
namespace {

class StringNameTable {
public:
    const godot::StringName &get(uint32_t p_id) {
        if (unlikely(!resolved_)) {
            resolve_locked();
        }
        return names_[p_id];
    }

private:
    // First call happens at class-wrapper build time, i.e. after GDExtension
    // CORE init; constructing StringNames there is safe. Double-checked lock
    // keeps later lookups lock-free.
    void resolve_locked() {
        godot::MutexLock lock(mutex_);
        if (resolved_) {
            return;
        }
        const uint32_t count = gen::k_string_count;
        names_.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            names_[i] = godot::StringName(gen::k_strings[i]);
        }
        resolved_ = true;
    }

    godot::Mutex mutex_;
    godot::LocalVector<godot::StringName> names_;
    bool resolved_ = false;
};

// Function-local static: deferred construction per AGENTS.md hard rule.
StringNameTable &table() {
    static StringNameTable t;
    return t;
}

const godot::StringName &nil() {
    static godot::StringName nil_name;
    return nil_name;
}

} // namespace

const godot::StringName &StringNames::get(uint32_t p_id) {
    if (p_id >= gen::k_string_count) {
        ERR_PRINT_ONCE("static_binding::StringNames: id out of range");
        return nil();
    }
    return table().get(p_id);
}

uint32_t StringNames::count() {
    return gen::k_string_count;
}

} // namespace jsb::static_binding
