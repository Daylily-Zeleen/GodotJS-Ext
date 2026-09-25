/************************************************************************/
/*  jsb_shared_statics.h                                                */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 <https://github.com/godotjs/GodotJS>                 */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_bridge_pch.h"

#include <godot_cpp/templates/hash_set.hpp>

namespace jsb {
/**
 * @brief Process-wide authoritative storage for `@bind.exposed.shared()` static variables.
 *
 * A `GodotJSScript` is shared across environments, but `script_class_info_` is only a snapshot taken
 * by whichever environment loaded the module first (`jsb_script.cpp`), and every JS isolate imports
 * the module independently - so a plain JS `static` property is per-isolate by construction.
 * Members marked `@bind.exposed.shared()` therefore get their JS property replaced with an accessor
 * pair that routes through here, which gives one value for every environment plus the GDScript side.
 *
 * @note The store holds `StringName` keys, so `clear()` MUST be called from
 *       `GodotJSScriptLanguage::_finish()` - otherwise the keys outlive `StringName::cleanup()` and
 *       are reported as orphans (the same failure mode as the temporary PropertyList pool).
 *
 * @note The value lifetime is deliberately INDEPENDENT of any `GodotJSScript` object (decided
 *       2026-09-26). Tying a slot's lifetime to the script object looks attractive for GDScript
 *       parity - `GDScript` keeps `static_variables` as a plain member of the script object and
 *       drops them in `~GDScript()` - but it loses in two ways here:
 *         1. `GodotJSScript` is a plain Resource: it is destroyed as soon as its last reference
 *            goes away, so a scene unload would silently reset every `static var`. Class-level
 *            state that vanishes on a reference-count drop is much worse than state that lives a
 *            little too long, and it is unobservable to the author.
 *         2. Class parsing happens with NO `GodotJSScript` in existence - a worker isolate
 *            (`jsb_worker.cpp`, `env->load(impl->path_)`), the editor bridge
 *            (`jsb_bridge_table.cpp`) and a plain JS `import` all parse the class through
 *            `Environment::load`, where only the module id is known. A slot that had to hang off
 *            a script object would have nowhere to live in exactly those cases.
 *       Cross-environment consistency does NOT depend on the store being process-wide: it depends
 *       on every environment plus GDScript reading ONE holder, and `GodotJSScript` is itself a
 *       process-wide shared Resource (the loader returns the cached instance). The real
 *       constraint is only that a holder must be addressable by module id alone.
 *       Cost: unlike GDScript, a value survives `GodotJSScript` destruction. In practice there is
 *       one script object per path (`CACHE_MODE_IGNORE`/`IGNORE_DEEP` are downgraded to REUSE in
 *       `jsb_resource_loader.cpp`), so the divergence is barely observable.
 */
namespace SharedStatics {
/** Declares `p_name` for `p_module_id`, storing `p_initial_value` only when the entry is new.
 *  A later environment loading the same module must NOT overwrite the value already in use. */
void ensure(const StringName &p_module_id, const StringName &p_name, const Variant &p_initial_value);

/** Reads the current value. Returns false when the member was never declared. */
bool get(const StringName &p_module_id, const StringName &p_name, Variant &r_value);

/** Writes the current value. Returns false when the member was never declared. */
bool set(const StringName &p_module_id, const StringName &p_name, const Variant &p_value);

/** Rebuilds the declared name set of one module: values of surviving names are kept, names that are
 *  gone (the annotation was removed) are dropped. Used on class (re)parse, so it must be idempotent. */
void retain(const StringName &p_module_id, const HashSet<StringName> &p_names);

/** Drops every entry. See the orphan note above. */
void clear();
} // namespace SharedStatics
} // namespace jsb
