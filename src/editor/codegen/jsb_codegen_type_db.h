/************************************************************************/
/*  jsb_codegen_type_db.h                                               */
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

#pragma once

// jsb_codegen_type_db.h
// C++ equivalent of `TypeDB` in jsb.editor.codegen.ts: loads class info from
// api_tool (the same source EditorUtilityFuncs used to marshal into JS) and
// provides the typename derivation rules used while emitting declarations.

#include "jsb_codegen_defs.h"
#include "jsb_codegen_docs.h"

#include <cstdint>

#include "api_tool/api_tool_types.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace jsb {
namespace codegen {

using namespace godot;

// GodotJsb.editor.MethodBind equivalent. Built from godot::MethodInfo plus
// marshalled default argument values.
struct MethodDecl {
    String internal_name;
    uint32_t id = 0;
    String name;
    uint32_t hint_flags = 0;
    bool is_static = false;
    bool is_const = false;
    bool is_vararg = false;

    LocalVector<PropertyInfo> args;
    // aligned with the trailing entries of `args` (same order)
    struct DefaultValue {
        Variant::Type type = Variant::NIL;
        Variant value;
        bool valid = false; // false when the value could not be resolved (null in TS output)
    };
    LocalVector<DefaultValue> default_arguments;

    PropertyInfo return_; // .type == NIL and usage without NIL_IS_VARIANT means "no return"

    _FORCE_INLINE_ bool has_returns() const {
        return return_.type != Variant::NIL || (return_.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
    }
};

struct SignalDecl {
    String internal_name;
    String name;
    MethodInfo method;
};

struct EnumDecl {
    String name;
    // insertion ordered literals
    LocalVector<Pair<String, int64_t>> literals;
    bool is_bitfield = false;
};

struct ConstantDecl {
    String name;
    int64_t value = 0;
};

struct PropertySetGetDecl {
    String internal_name;
    String name;
    int index = -1;
    String setter;
    String getter;
    PropertyInfo info;
};

struct PrimitiveConstantDecl {
    String name;
    Variant::Type type = Variant::NIL;
    bool has_literal_value = false;
    int64_t literal_value = 0;
    double literal_float_value = 0.0;
    bool literal_is_float = false;
    bool literal_is_bool = false;
    bool literal_bool_value = false;
    bool literal_is_string = false;
    String literal_string_value;
};

struct PrimitiveGetSetDecl {
    String name;
    Variant::Type type = Variant::NIL;
};

struct ConstructorArgDecl {
    String name;
    Variant::Type type = Variant::NIL;
};

struct ConstructorDecl {
    LocalVector<ConstructorArgDecl> arguments;
};

struct OperatorDecl {
    String op_name;
    Variant::Type return_type = Variant::NIL;
    Variant::Type left_type = Variant::NIL;
    Variant::Type right_type = Variant::NIL;
};

struct ClassDecl {
    String name; // exposed (possibly camel-cased) name
    String internal_name;
    String super;

    LocalVector<PropertySetGetDecl> properties;
    LocalVector<MethodDecl> methods;
    LocalVector<MethodDecl> virtual_methods;
    LocalVector<MethodDecl> rpc_methods;
    LocalVector<EnumDecl> enums;
    LocalVector<ConstantDecl> constants;
    LocalVector<SignalDecl> signals;
};

struct PrimitiveClassDecl {
    String name;
    Variant::Type type = Variant::NIL;
    bool has_element_type = false;
    Variant::Type element_type = Variant::NIL;
    bool is_keyed = false;

    LocalVector<MethodDecl> methods;
    LocalVector<EnumDecl> enums;
    LocalVector<PrimitiveConstantDecl> constants;
    LocalVector<ConstructorDecl> constructors;
    LocalVector<OperatorDecl> operators;
    LocalVector<PrimitiveGetSetDecl> properties;
};

struct SingletonDecl {
    String name;
    String class_name;
};

struct GlobalConstantDecl {
    String name; // exposed enum/constant name
    // either a plain constant or an enum-like set of values
    bool is_enum = false;
    int64_t value = 0;
    LocalVector<Pair<String, int64_t>> values;
};

// TypeDB equivalent.
class TypeDB {
public:
    TypeDB();

    HashMap<String, ClassDecl *> classes;
    HashMap<String, PrimitiveClassDecl *> primitive_types;
    HashMap<String, SingletonDecl *> singletons;
    HashMap<String, GlobalConstantDecl *> globals;
    HashMap<String, MethodDecl *> utilities;

    HashSet<String> ignored_classes; // name -> nothing (base recorded separately below)
    struct IgnoredClassEntry {
        String name;
        String exposed_base_class;
    };
    LocalVector<IgnoredClassEntry> ignored_classes_list;
    HashSet<String> ignored_class_enums;

    const ClassDecl *find_class(const String &p_name) const;
    const PrimitiveClassDecl *find_primitive(const String &p_name) const;

    // TS: `find_doc()` - lazily loads and caches the class document (nullptr when none).
    const ClassDocEntry *find_doc(const String &p_class_name, const String &p_original_name) { return docs_.find_doc(p_class_name, p_original_name); }
    bool has_variant_name(const String &p_name) const;
    Variant::Type get_variant_from_name(const String &p_name) const;
    String get_variant_to_name(Variant::Type p_type) const;

    bool is_primitive_type(const String &p_name) const { return primitive_types.has(p_name); }
    bool is_valid_method_name(const String &p_name) const;

    String make_classname(const String &p_class_name, bool p_internal = false);
    String make_typename(const PropertyInfo &p_info, bool p_used_as_input, bool p_non_nullable);
    String make_arg(const PropertyInfo &p_info, bool p_optional = false);
    String make_arg_default_value(const MethodDecl &p_method, int p_index);
    String make_args(const MethodDecl &p_method);
    String make_return(const MethodDecl &p_method);
    String make_signal_type(const SignalDecl &p_signal);

    // public wrapper of primitive_type_name_as_input for writers
    String primitive_type_name_as_input_public(Variant::Type p_type) const {
        return primitive_type_name_as_input(p_type);
    }

    // ordered (load-order) views for the generator emit loops - the TS pipeline
    // relied on JS object insertion order, so shard boundaries depend on it.
    const Vector<ClassDecl *> &ordered_classes() const { return owned_classes_; }
    const Vector<PrimitiveClassDecl *> &ordered_primitives() const { return owned_primitives_; }
    const Vector<SingletonDecl *> &ordered_singletons() const { return owned_singletons_; }
    const Vector<GlobalConstantDecl *> &ordered_globals() const { return owned_globals_; }
    const Vector<MethodDecl *> &ordered_utilities() const { return owned_utilities_; }

    // replace_var_name()
    static String replace_var_name(const String &p_name) {
        const String rep = keyword_replacement(p_name);
        return rep.is_empty() ? p_name : rep;
    }

    // make_literal_value(): returns an empty string when no literal can be emitted
    String make_literal_value(const MethodDecl::DefaultValue &p_value);

private:
    void load_classes();
    void load_primitive_types();
    void load_singletons();
    void load_globals();
    void load_utilities();

    void build_method_decl(MethodDecl &r_decl, const MethodInfo &p_method);
    PrimitiveClassDecl *_load_primitive_type(const StringName &p_type_name, Variant::Type p_type, bool p_utilities_mode);

    // get_primitive_type_name_as_input()
    String primitive_type_name_as_input(Variant::Type p_type) const;

    // VariantNames: both godot-internal names and GodotJS names -> type
    HashMap<String, Variant::Type> variant_names_;
    HashMap<Variant::Type, String> variant_type_names_;

    // memory ownership for the decls
    Vector<ClassDecl *> owned_classes_;
    Vector<PrimitiveClassDecl *> owned_primitives_;
    Vector<SingletonDecl *> owned_singletons_;
    Vector<GlobalConstantDecl *> owned_globals_;
    Vector<MethodDecl *> owned_utilities_;

    // TS: `class_docs` map - lazily populated class document cache
    DocCache docs_;

    friend void destroy_typedb(TypeDB &p_db);
};

} // namespace codegen
} // namespace jsb
