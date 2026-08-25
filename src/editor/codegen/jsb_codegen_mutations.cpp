/************************************************************************/
/*  jsb_codegen_mutations.cpp                                           */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_codegen_mutations.h"
#include "jsb_codegen_type_db.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <common/internal/jsb_naming_util.h>
#include <common/internal/jsb_settings.h>

namespace jsb {
namespace codegen {

using internal::NamingUtil;
using internal::Settings;

// ---------------------------------------------------------------------------
// Line mutators
//
// TS implementations:
//   mutate_parameter_type(name, type): replaces `name?: .+?[,)/]` with
//       `name: type` on generated lines.
//   mutate_return_type(type): replaces trailing `: X` with `: type`.
//   mutate_template(template): inserts `<template>` after the method name.
//
// The C++ versions below implement the same rewrites without regex (the lines
// are machine-generated with a known shape, so a small scanner is exact and
// much faster than std::regex).
// ---------------------------------------------------------------------------
static String escape_name(const String &p_name) {
    return p_name.replace(".", "\\.");
}

LineMutator make_mutate_parameter_type(const String &p_name, const String &p_type) {
    String name = p_name;
    if (Settings::get_camel_case_bindings_enabled()) {
        name = NamingUtil::get_parameter_name(name);
    }
    const String optional_pattern = name + "?:";
    const String plain_pattern = name + ":";

    return [name, p_type, optional_pattern, plain_pattern](const String &p_line) -> String {
        // match `name` optionally followed by `?` then `:`, preceded (ignoring
        // spaces) by `(` or `,`; replace through the end of the old type.
        int search_from = 0;
        while (true) {
            int idx = p_line.find(optional_pattern, search_from);
            if (idx < 0) {
                idx = p_line.find(plain_pattern, search_from);
            }
            if (idx < 0) {
                ERR_PRINT("Failed to mutate \"" + name + "\" parameter's type: " + p_line);
                return p_line;
            }
            // must be preceded by '(' or ',' (with optional spaces)
            int check = idx - 1;
            while (check >= 0 && p_line[check] == ' ') {
                --check;
            }
            if (check < 0 || (p_line[check] != '(' && p_line[check] != ',')) {
                search_from = idx + 1;
                continue;
            }

            // find end of the old type span: first ',', ')' or '/' (comment start)
            int type_start = idx + (p_line.find(optional_pattern, search_from) == idx ? optional_pattern.length() : plain_pattern.length());
            int end = type_start;
            while (end < p_line.length() && p_line[end] != ',' && p_line[end] != ')' && p_line[end] != '/') {
                ++end;
            }
            int type_end = end;
            while (type_end > type_start && p_line[type_end - 1] == ' ') {
                --type_end;
            }
            return p_line.substr(0, idx) + name + ": " + p_type + p_line.substr(type_end);
        }
    };
}

LineMutator make_mutate_return_type(const String &p_type) {
    return [p_type](const String &p_line) -> String {
        const int idx = p_line.rfind(": ");
        if (idx < 0) {
            ERR_PRINT("Failed to mutate return type: " + p_line);
            return p_line;
        }
        return p_line.substr(0, idx + 1) + " " + p_type;
    };
}

LineMutator make_mutate_template(const String &p_template) {
    return [p_template](const String &p_line) -> String {
        // insert `<template>` before the argument list's opening paren,
        // after any existing generic list.
        const int paren = p_line.find("(");
        if (paren < 0) {
            ERR_PRINT("Failed to mutate template: " + p_line);
            return p_line;
        }
        // find method name start (last space before paren)
        int name_start = paren - 1;
        while (name_start >= 0 && p_line[name_start] != ' ') {
            --name_start;
        }
        // existing generic suffix `<...>` directly before paren?
        int insert_at = paren;
        int scan = paren - 1;
        if (scan >= 0 && p_line[scan] == '>') {
            int depth = 0;
            while (scan >= 0) {
                const char c = p_line[scan];
                if (c == '>') {
                    ++depth;
                } else if (c == '<') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                --scan;
            }
            insert_at = scan;
        }
        return p_line.substr(0, insert_at) + "<" + p_template + ">" + p_line.substr(insert_at);
    };
}

LineMutator make_chain_mutator(Vector<LineMutator> p_mutators) {
    return [mutators = std::move(p_mutators)](const String &p_line) -> String {
        String line = p_line;
        for (const LineMutator &mutator : mutators) {
            line = mutator(line);
        }
        return line;
    };
}

// ---------------------------------------------------------------------------
// merge / lookup
// ---------------------------------------------------------------------------
TypeMutation merge_type_mutations(const TypeMutation &p_base, const TypeMutation &p_overrides) {
    TypeMutation merged = p_base;
    // TS: `{...base, ...overrides}` - every field defined on the override
    // REPLACES the base field wholesale (only property_overrides is re-merged
    // per-key). Union-by-key here would leak ancestor generics into the
    // derived class (e.g. Node's `Map` showing up on AnimationMixer).
    if (!p_overrides.generic_parameters.is_empty()) {
        merged.generic_parameters = p_overrides.generic_parameters;
    }
    if (!p_overrides.super.is_empty()) {
        merged.super = p_overrides.super;
    }
    if (!p_overrides.super_generic_arguments.is_empty()) {
        merged.super_generic_arguments = p_overrides.super_generic_arguments;
    }
    if (!p_overrides.implements.is_empty()) {
        merged.implements = p_overrides.implements;
    }
    if (!p_overrides.intro.is_empty()) {
        merged.intro = p_overrides.intro;
    }
    if (!p_overrides.prelude.is_empty()) {
        merged.prelude = p_overrides.prelude;
    }
    for (const KeyValue<String, PropertyOverride> &E : p_overrides.property_overrides) {
        PropertyOverride *existing = merged.property_overrides.getptr(E.key);
        if (existing != nullptr) {
            PropertyOverride &target = *existing;
            const PropertyOverride &source = E.value;
            if (source.is_literal || target.is_literal) {
                target = source; // literal wins/replaces
            } else {
                LineMutator chained = target.mutator;
                target.mutator = [chained, source](const String &p_line) {
                    return source.mutator(chained(p_line));
                };
            }
        } else {
            merged.property_overrides.insert(E.key, E.value);
        }
    }
    return merged;
}

TypeMutation get_type_mutation(const String &p_name, const TypeDB *p_types, bool p_apply_intrinsic) {
    // collect ancestor chain (base -> ... -> Object order reversed below)
    Vector<String> ancestor_names;
    if (p_types != nullptr) {
        const ClassDecl *cls = p_types->find_class(p_name);
        String ancestor = cls ? cls->super : String();
        while (!ancestor.is_empty()) {
            ancestor_names.push_back(ancestor);
            const ClassDecl *next = p_types->find_class(ancestor);
            ancestor = next ? next->super : String();
        }
    }

    TypeMutation mutation;
    if (p_apply_intrinsic) {
        mutation = build_intrinsic_mutation(p_name, p_types);
    }

    // apply inherited mutations from root-most to immediate parent
    for (int i = ancestor_names.size() - 1; i >= 0; --i) {
        const TypeMutation *inherited = find_inherited_mutation(ancestor_names[i]);
        if (inherited != nullptr) {
            mutation = merge_type_mutations(mutation, *inherited);
        }
    }

    const TypeMutation *own = find_direct_mutation(p_name);
    if (own != nullptr) {
        mutation = merge_type_mutations(mutation, *own);
    }

    return mutation;
}

} // namespace codegen
} // namespace jsb
