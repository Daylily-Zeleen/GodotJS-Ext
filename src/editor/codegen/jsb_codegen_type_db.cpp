/************************************************************************/
/*  jsb_codegen_type_db.cpp                                             */
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

#include <runtime/internal/jsb_class_visibility.h>
#include "jsb_codegen_type_db.h"

#include "jsb_codegen_defs.h"

#include "api_tool/api_tool.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

// NamingUtil (member/parameter/class/enum name mapping)
#include <runtime/internal/jsb_format.h>
#include <runtime/internal/jsb_naming_util.h>
// VariantUtil::get_type_name (StringNames replacement aware)
#include <runtime/internal/jsb_variant_util.h>

namespace jsb {
namespace codegen {

using internal::ClassVisibility;
using internal::NamingUtil;
using internal::VariantUtil;

// Variant.Type -> GodotJS exposed type name (VariantTypeNames equivalent).
static String variant_type_to_js_name(Variant::Type p_type) {
    const String remapped = remapped_primitive_type_name(p_type);
    if (!remapped.is_empty()) {
        return remapped;
    }
    // Mirror the runtime's binding names: Array/Dictionary (and any other
    // builtin) get their exposed name through the same pure mapping the bridge
    // uses at bind time -- no dependency on StringNames replacement state.
    return internal::NamingUtil::get_class_name(Variant::get_type_name(p_type));
}

TypeDB::TypeDB() {
    load_classes();
    load_primitive_types();
    load_singletons();
    load_globals();
    load_utilities();

    // VariantNames: godot internal names and GodotJS names both map to the type
    for (int vt = 0; vt < (int)Variant::VARIANT_MAX; ++vt) {
        const Variant::Type type = (Variant::Type)vt;
        variant_names_.insert(Variant::get_type_name(type), type); // Godot internal
        variant_names_.insert(variant_type_to_js_name(type), type); // GodotJS name
        variant_type_names_.insert(type, variant_type_to_js_name(type));
    }
}

const ClassDecl *TypeDB::find_class(const String &p_name) const {
    const ClassDecl *const *it = classes.getptr(p_name);
    return it ? *it : nullptr;
}

const PrimitiveClassDecl *TypeDB::find_primitive(const String &p_name) const {
    const PrimitiveClassDecl *const *it = primitive_types.getptr(p_name);
    return it ? *it : nullptr;
}

bool TypeDB::has_variant_name(const String &p_name) const { return variant_names_.has(p_name); }

Variant::Type TypeDB::get_variant_from_name(const String &p_name) const {
    const Variant::Type *it = variant_names_.getptr(p_name);
    return it ? *it : Variant::NIL;
}

String TypeDB::get_variant_to_name(Variant::Type p_type) const {
    const String *it = variant_type_names_.getptr(p_type);
    return it ? *it : String();
}

bool TypeDB::is_valid_method_name(const String &p_name) const {
    if (!keyword_replacement(p_name).is_empty()) {
        return false;
    }
    if (p_name.contains("/") || p_name.contains(".")) {
        return false;
    }
    return true;
}

void TypeDB::build_method_decl(MethodDecl &r_decl, const MethodInfo &p_method) {
    r_decl.internal_name = p_method.name;
    r_decl.id = p_method.id;
    r_decl.name = NamingUtil::get_member_name(p_method.name);
    r_decl.hint_flags = p_method.flags;
    r_decl.is_static = (p_method.flags & METHOD_FLAG_STATIC) != 0;
    r_decl.is_const = (p_method.flags & METHOD_FLAG_CONST) != 0;
    r_decl.is_vararg = (p_method.flags & METHOD_FLAG_VARARG) != 0;

    const bool has_return_value =
            p_method.return_val.type != Variant::NIL
            || (p_method.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
    if (has_return_value) {
        PropertyInfo return_info = p_method.return_val;
        // exposed names (mirrors build_property_info(..., method=true))
        return_info.name = NamingUtil::get_parameter_name(return_info.name);
        return_info.class_name = NamingUtil::get_class_name(return_info.class_name);
        r_decl.return_ = return_info;
    }

    r_decl.args.reserve(p_method.arguments.size());
    for (const PropertyInfo &arg : p_method.arguments) {
        PropertyInfo info = arg;
        info.name = NamingUtil::get_parameter_name(info.name);
        info.class_name = NamingUtil::get_class_name(info.class_name);
        r_decl.args.push_back(info);
    }

    // default values aligned with trailing arguments
    const int argc = p_method.default_arguments.size();
    for (int index = 0; index < argc; ++index) {
        MethodDecl::DefaultValue dv;
        dv.value = p_method.default_arguments[index];
        dv.type = r_decl.args[r_decl.args.size() - (argc - index)].type;
        dv.valid = true;
        r_decl.default_arguments.push_back(dv);
    }
}

void TypeDB::load_classes() {
    LocalVector<StringName> exposed_class_list;
    ClassVisibility::get_exposed_original_class_list(exposed_class_list);

    for (const StringName &class_name : exposed_class_list) {
        const api_tool::ApiClass *api_class = api_tool::find_class(class_name);
        if (api_class == nullptr) {
            continue;
        }

        ClassDecl *decl = memnew(ClassDecl);
        owned_classes_.push_back(decl);

        decl->name = NamingUtil::get_class_name(api_class->name);
        decl->internal_name = api_class->name;
        decl->super = api_class->inherits;

#if JSB_EXCLUDE_GETSET_METHODS
        HashSet<StringName> omitted_methods;
#endif

        for (const auto &api_prop : api_class->properties) {
            PropertySetGetDecl prop;
            prop.internal_name = api_prop.property.name;
            prop.name = NamingUtil::get_member_name(api_prop.property.name);
            prop.index = api_prop.index;
            prop.setter = NamingUtil::get_member_name(api_prop.setter);
            prop.getter = NamingUtil::get_member_name(api_prop.getter);
            prop.info = api_prop.property;
            prop.info.name = NamingUtil::get_member_name(prop.info.name);
            prop.info.class_name = NamingUtil::get_class_name(prop.info.class_name);
            decl->properties.push_back(prop);

#if JSB_EXCLUDE_GETSET_METHODS
            // Mirror the old pipeline (build_class_info): only INDEX-accessed
            // properties (several properties sharing one accessor pair, e.g.
            // BaseMaterial3D flags) drop their getter/setter *methods*.
            // Regular named properties keep both the methods and the
            // `static get x()/set x()` accessor declarations.
            if (api_prop.index >= 0) {
                if (internal::VariantUtil::is_valid_name(api_prop.getter)) {
                    omitted_methods.insert(api_prop.getter);
                }
                if (internal::VariantUtil::is_valid_name(api_prop.setter)) {
                    omitted_methods.insert(api_prop.setter);
                }
            }
#endif
        }

        for (const auto &api_method : api_class->methods) {
            MethodInfo method_info = api_method.method;
            const bool is_virtual = api_method.is_virtual();

#if JSB_EXCLUDE_GETSET_METHODS
            // property accessors already emitted via PropertySetGetDecl
            if (!is_virtual && omitted_methods.has(method_info.name)) {
                continue;
            }
#endif

            MethodDecl md;
            build_method_decl(md, method_info);

            if (is_virtual) {
                decl->virtual_methods.push_back(md);
            } else {
                decl->methods.push_back(md);
            }
        }

        for (const auto &api_enum : api_class->enums) {
            EnumDecl enum_decl;
            enum_decl.name = NamingUtil::get_enum_name(api_enum.name);
            enum_decl.is_bitfield = api_enum.is_bitfield;
            for (const auto &value : api_enum.values) {
                // Old pipeline narrowed enum values through `int` + v8 Number
                // (build_enum_info); values above INT32_MAX collapsed (e.g.
                // Mesh.ARRAY_FLAG_FORMAT_VERSION_2 -> 0). Keep the narrowing
                // for byte-for-byte parity with the codegen baseline.
                const int narrowed = static_cast<int>(value.value);
                enum_decl.literals.push_back({ value.name, narrowed });
            }
            decl->enums.push_back(enum_decl);
        }

        for (const auto &constant : api_class->constants) {
            ConstantDecl constant_decl;
            constant_decl.name = NamingUtil::get_constant_name(constant.name);
            constant_decl.value = constant.value;
            decl->constants.push_back(constant_decl);
        }

        for (const auto &api_signal : api_class->signals) {
            SignalDecl signal;
            signal.internal_name = api_signal.name;
            signal.name = NamingUtil::get_member_name(api_signal.name);
            signal.method.name = api_signal.name;
            signal.method.arguments = api_signal.arguments;

            // signals are declared as `Signal<(name: T, ...) => void>`
            MethodInfo mi;
            mi.name = api_signal.name;
            mi.arguments = api_signal.arguments;

            MethodDecl md;
            md.internal_name = mi.name;
            md.name = NamingUtil::get_member_name(mi.name);
            md.args.reserve(mi.arguments.size());
            for (const PropertyInfo &arg : mi.arguments) {
                PropertyInfo info = arg;
                info.name = NamingUtil::get_parameter_name(info.name);
                info.class_name = NamingUtil::get_class_name(info.class_name);
                md.args.push_back(info);
            }
            decl->signals.push_back(signal);

            // NOTE: keep a placeholder so ordering matches the TS version when
            // emitting __RPCMap/__NameMap interfaces.
        }

        classes.insert(decl->name, decl);
    }
}

void TypeDB::load_primitive_types() {
#define JSB_CODEGEN_DEF(TypeName)                                                                    \
    {                                                                                                \
        constexpr Variant::Type type_id = Variant::TypeName;                                         \
        _load_primitive_type(Variant::get_type_name(type_id), type_id, false /* utilities_mode */);  \
    }
#define JSB_CODEGEN_DEF_UTIL(TypeName)                                                              \
    {                                                                                               \
        constexpr Variant::Type type_id = Variant::TypeName;                                        \
        _load_primitive_type(Variant::get_type_name(type_id), type_id, true /* utilities_mode */);  \
    }

    // mirror of jsb_primitive_types.def.h order + the extra String utilities entry
    static const struct {
        const char *name;
        Variant::Type type;
        bool utilities_mode;
    } kPrimitiveTypes[] = {
        { "Vector2", Variant::VECTOR2, false },
        { "Vector2i", Variant::VECTOR2I, false },
        { "Rect2", Variant::RECT2, false },
        { "Rect2i", Variant::RECT2I, false },
        { "Vector3", Variant::VECTOR3, false },
        { "Vector3i", Variant::VECTOR3I, false },
        { "Transform2D", Variant::TRANSFORM2D, false },
        { "Vector4", Variant::VECTOR4, false },
        { "Vector4i", Variant::VECTOR4I, false },
        { "Plane", Variant::PLANE, false },
        { "Quaternion", Variant::QUATERNION, false },
        { "AABB", Variant::AABB, false },
        { "Basis", Variant::BASIS, false },
        { "Transform3D", Variant::TRANSFORM3D, false },
        { "Projection", Variant::PROJECTION, false },
        { "Color", Variant::COLOR, false },
        { "NodePath", Variant::NODE_PATH, false },
        { "RID", Variant::RID, false },
        { "Callable", Variant::CALLABLE, false },
        { "Signal", Variant::SIGNAL, false },
        { "Dictionary", Variant::DICTIONARY, false },
        { "Array", Variant::ARRAY, false },
        { "PackedByteArray", Variant::PACKED_BYTE_ARRAY, false },
        { "PackedInt32Array", Variant::PACKED_INT32_ARRAY, false },
        { "PackedInt64Array", Variant::PACKED_INT64_ARRAY, false },
        { "PackedFloat32Array", Variant::PACKED_FLOAT32_ARRAY, false },
        { "PackedFloat64Array", Variant::PACKED_FLOAT64_ARRAY, false },
        { "PackedStringArray", Variant::PACKED_STRING_ARRAY, false },
        { "PackedVector2Array", Variant::PACKED_VECTOR2_ARRAY, false },
        { "PackedVector3Array", Variant::PACKED_VECTOR3_ARRAY, false },
        { "PackedVector4Array", Variant::PACKED_VECTOR4_ARRAY, false },
        { "PackedColorArray", Variant::PACKED_COLOR_ARRAY, false },
        { "String", Variant::STRING, true }, // generate_primitive_type_utilities<String>
    };

    for (const auto &entry : kPrimitiveTypes) {
        _load_primitive_type(entry.name, entry.type, entry.utilities_mode);
    }

#undef JSB_CODEGEN_DEF
#undef JSB_CODEGEN_DEF_UTIL
}

PrimitiveClassDecl *TypeDB::_load_primitive_type(const StringName &p_type_name, Variant::Type p_type, bool p_utilities_mode) {
    const api_tool::ApiBuiltinClass *builtin_class = api_tool::find_builtin_class(p_type_name);
    jsb_checkf(builtin_class, "builtin class not found: %s", String(p_type_name).utf8().get_data());

    PrimitiveClassDecl *decl = memnew(PrimitiveClassDecl);
    owned_primitives_.push_back(decl);

    decl->name = NamingUtil::get_class_name(p_type_name);
    decl->type = p_type;

    if (builtin_class->has_indexing_return_type) {
        decl->has_element_type = true;
        decl->element_type = builtin_class->indexing_type;
    }
    decl->is_keyed = builtin_class->is_keyed;

    // methods
    for (const auto &api_method : builtin_class->methods) {
        MethodInfo method_info = api_method.method;

        if (p_utilities_mode && !(method_info.flags & METHOD_FLAG_STATIC)) {
            // utility variants prepend an implicit `target` argument and are forced static
            PropertyInfo target;
            target.name = "target";
            target.type = p_type;
            method_info.arguments.insert(0, target);
            method_info.flags |= METHOD_FLAG_STATIC;
        }

        MethodDecl md;
        build_method_decl(md, method_info);
        decl->methods.push_back(md);
    }

    // enums
    for (const auto &api_enum : builtin_class->enums) {
        EnumDecl enum_decl;
        enum_decl.name = NamingUtil::get_enum_name(api_enum.name);
        enum_decl.is_bitfield = api_enum.is_bitfield;
        for (const auto &value : api_enum.values) {
            // Old pipeline narrowed enum values through `int` + v8 Number
            // (build_enum_info); values above INT32_MAX collapsed (e.g.
            // Mesh.ARRAY_FLAG_FORMAT_VERSION_2 -> 0). Keep the narrowing for
            // byte-for-byte parity with the codegen baseline.
            const int narrowed = static_cast<int>(value.value);
            enum_decl.literals.push_back({ value.name, narrowed });
        }
        decl->enums.push_back(enum_decl);
    }

    if (p_utilities_mode) {
        primitive_types.insert(decl->name, decl);
        return decl;
    }

    // constructors
    for (const auto &api_constructor : builtin_class->constructors) {
        ConstructorDecl ctor;
        for (const PropertyInfo &argument : api_constructor.arguments) {
            ctor.arguments.push_back({ NamingUtil::get_parameter_name(argument.name), argument.type });
        }
        decl->constructors.push_back(ctor);
    }

    // operators
    for (const auto &op : builtin_class->operators) {
        OperatorDecl op_decl;
        op_decl.op_name = api_tool::get_variant_operator_name(op.op);
        op_decl.return_type = op.return_type;
        op_decl.left_type = op.left_type;
        op_decl.right_type = op.right_type;
        decl->operators.push_back(op_decl);
    }

    // properties (getset)
    for (const auto &api_member : builtin_class->members) {
        PrimitiveGetSetDecl member;
        member.name = NamingUtil::get_member_name(api_member.name);
        member.type = api_member.type;
        decl->properties.push_back(member);
    }

    // constants
    for (const auto &api_constant : builtin_class->constants) {
        PrimitiveConstantDecl constant;
        constant.name = api_constant.name;
        constant.type = api_constant.type;

        switch (api_constant.type) {
            case Variant::BOOL: {
                constant.literal_is_bool = true;
                constant.literal_bool_value = (bool)(Variant)api_constant.value;
                break;
            }
            case Variant::INT: {
                constant.has_literal_value = true;
                constant.literal_value = (int64_t)(Variant)api_constant.value;
                break;
            }
            case Variant::FLOAT: {
                constant.literal_is_float = true;
                constant.literal_float_value = (double)(Variant)api_constant.value;
                break;
            }
            default:
                break;
        }
        decl->constants.push_back(constant);
    }

    primitive_types.insert(decl->name, decl);
    return decl;
}

void TypeDB::load_singletons() {
    PackedStringArray singleton_list = Engine::get_singleton()->get_singleton_list();
    for (int i = 0; i < singleton_list.size(); i++) {
        const StringName singleton_name = singleton_list[i];
        Object *singleton = Engine::get_singleton()->get_singleton(singleton_name);

        SingletonDecl *decl = memnew(SingletonDecl);
        owned_singletons_.push_back(decl);

        const api_tool::ApiSingleton *api_singleton = api_tool::find_singleton(singleton_name);
        const String class_name_str = api_singleton ? String(api_singleton->type)
                : (singleton ? String(singleton->get_class()) : String());
        decl->name = NamingUtil::get_class_name(singleton_name);
        decl->class_name = NamingUtil::get_class_name(class_name_str);
        singletons.insert(decl->name, decl);
    }
}

void TypeDB::load_globals() {
    // individual constants first (those not part of an enum), then enums,
    // mirroring `_get_global_constants`
    for (const StringName &constant_name : api_tool::list_global_constants()) {
        const api_tool::ApiConstantInfo *api_global_constant = api_tool::find_global_constant(constant_name);
        jsb_check(api_global_constant);

        GlobalConstantDecl *decl = memnew(GlobalConstantDecl);
        owned_globals_.push_back(decl);
        decl->name = NamingUtil::get_enum_value_name(constant_name);
        // Old pipeline passed the value through a v8 Number (double), rounding
        // |values| >= 2^53 (INT64_MIN/MAX). Keep the rounding for baseline parity.
        decl->value = static_cast<int64_t>(static_cast<double>(api_global_constant->value));
        globals.insert(decl->name, decl);
    }

    for (const StringName &enum_name : api_tool::list_global_enums()) {
        const api_tool::ApiEnumInfo *enum_info = api_tool::find_global_enum(enum_name);
        jsb_check(enum_info);

        GlobalConstantDecl *decl = memnew(GlobalConstantDecl);
        owned_globals_.push_back(decl);
        decl->name = NamingUtil::get_enum_name(enum_name);
        decl->is_enum = true;
        for (const auto &api_enum : enum_info->values) {
            decl->values.push_back({ NamingUtil::get_enum_value_name(api_enum.name), api_enum.value });
        }
        globals.insert(decl->name, decl);
    }
}

void TypeDB::load_utilities() {
    for (const StringName &name : api_tool::list_utility_functions()) {
        const api_tool::ApiUtilityFunction *utility_func = api_tool::find_utility_function(name);
        if (utility_func == nullptr) {
            continue;
        }

        MethodDecl *md = memnew(MethodDecl);
        owned_utilities_.push_back(md);
        build_method_decl(*md, utility_func->method);
        utilities.insert(md->name, md);
    }
}

} // namespace codegen
} // namespace jsb
/************************************************************************/
/*  jsb_codegen_type_db.cpp (part 2: typename derivation)               */
/************************************************************************/

#include "jsb_codegen_type_db.h"

#include "jsb_codegen_defs.h"

#include <godot_cpp/variant/variant.hpp>

namespace jsb {
namespace codegen {

// ---------------------------------------------------------------------------
// get_primitive_type_name_as_input
// ---------------------------------------------------------------------------
String TypeDB::primitive_type_name_as_input(Variant::Type p_type) const {
    const String *primitive_name_it = variant_type_names_.getptr(p_type);
    const String primitive_name = primitive_name_it ? *primitive_name_it : String();

    switch (p_type) {
        case Variant::PACKED_COLOR_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name(get_variant_to_name(Variant::COLOR)) });
        case Variant::PACKED_VECTOR2_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name(get_variant_to_name(Variant::VECTOR2)) });
        case Variant::PACKED_VECTOR3_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name(get_variant_to_name(Variant::VECTOR3)) });
        case Variant::PACKED_STRING_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("string") });
        case Variant::PACKED_FLOAT32_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("float32") });
        case Variant::PACKED_FLOAT64_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("float64") });
        case Variant::PACKED_INT32_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("int32") });
        case Variant::PACKED_INT64_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("int64") });
        case Variant::PACKED_BYTE_ARRAY:
            return join_type_name({ primitive_name, get_js_array_type_name("byte"), "ArrayBuffer" });
        case Variant::NODE_PATH:
            return join_type_name({ primitive_name, "string" });
        default:
            return primitive_name;
    }
}

// ---------------------------------------------------------------------------
// make_classname
// ---------------------------------------------------------------------------
String TypeDB::make_classname(const String &p_class_name, bool p_internal) {
    if (p_class_name.is_empty()) {
        return "any";
    }

    String class_name = p_class_name.replace("-", "");

    if (p_class_name.contains(".")) {
        const PackedStringArray layers = p_class_name.split(".", false);

        if (layers.size() == 2) {
            const String enum_name = internal::NamingUtil::get_enum_name(layers[1]);

            // nested enums in primitive types do not exist in class_info, they are manually bound.
            if (has_variant_name(layers[0])) {
                return get_variant_to_name(get_variant_from_name(layers[0])) + String(".") + enum_name;
            }

            const String exposed_class_name = internal::NamingUtil::get_class_name(layers[0]);
            if (has_variant_name(exposed_class_name)) {
                return get_variant_to_name(get_variant_from_name(exposed_class_name)) + String(".") + enum_name;
            }

            const ClassDecl *cls = find_class(exposed_class_name);
            bool enum_found = false;
            if (cls != nullptr) {
                for (const EnumDecl &decl : cls->enums) {
                    if (decl.name == enum_name) {
                        enum_found = true;
                        break;
                    }
                }
            }
            if (enum_found) {
                return exposed_class_name + String(".") + enum_name;
            }
        }
    } else {
        if (p_internal) {
            if (has_variant_name(class_name)) {
                return get_variant_to_name(get_variant_from_name(class_name));
            }
            class_name = internal::NamingUtil::get_class_name(class_name);
        }

        if (has_variant_name(class_name)) {
            return get_variant_to_name(get_variant_from_name(class_name));
        }

        if (classes.has(class_name)) {
            return class_name;
        }
        if (singletons.has(class_name)) {
            return class_name;
        }
    }

    if (globals.has(class_name)) {
        return class_name;
    }

    // Ignored classes or enums.
    const PackedStringArray layers = p_class_name.split(".");
    if (layers.size() == 2) {
        const String qualified = layers[0] + String(".") + layers[1];
        if (ignored_class_enums.has(qualified)) {
            return String("IgnoredEnums[") + qualified + "]";
        }

        const String owned_class = layers[0];
        if (!internal::ClassVisibility::is_original_class_exposed(owned_class)) {
            ignored_class_enums.insert(qualified);
            return String("IgnoredEnums[") + qualified + "]";
        }
    }

    if (ignored_classes.has(p_class_name)) {
        return String("IgnoredClasses[\"") + p_class_name + "\"]";
    }

    if (!internal::ClassVisibility::is_original_class_exposed(p_class_name)) {
        const String exposed_base_class = internal::ClassVisibility::find_exposed_base_class(p_class_name);
        if (!exposed_base_class.is_empty()) {
            ignored_classes.insert(p_class_name);
            ignored_classes_list.push_back({ p_class_name, exposed_base_class });
            return String("IgnoredClasses[\"") + p_class_name + "\"]";
        }
    }

    WARN_PRINT("undefined class " + p_class_name);
    return String("any /*") + p_class_name + "*/";
}

// ---------------------------------------------------------------------------
// make_typename
// ---------------------------------------------------------------------------
static const char *js_object_key_types[] = {
    "string", "byte", "int32", "int64", "float32", "float64", "uint32",
};

String TypeDB::make_typename(const PropertyInfo &p_info, bool p_used_as_input, bool p_non_nullable) {
    const String null_prefix =
            (!p_non_nullable && (p_info.type == Variant::OBJECT || (p_info.usage & PROPERTY_USAGE_STORE_IF_NULL)))
            ? String("null | ")
            : "";

    if (p_info.hint == PROPERTY_HINT_RESOURCE_TYPE) {
        // console.assert(hint_string not empty)
        const PackedStringArray names = p_info.hint_string.split(",");
        String joined;
        for (int i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += " | ";
            }
            joined += make_classname(names[i], true);
        }
        return null_prefix + joined;
    }

    // NOTE there are infos with `.class_name == bool` instead of `.type` only,
    // they will be remapped in `make_classname`
    if (p_info.class_name.is_empty() || p_info.class_name.contains(",")) {
        const String variant_type_name = p_used_as_input
                ? primitive_type_name_as_input(p_info.type)
                : get_variant_to_name(p_info.type);

        if (!variant_type_name.is_empty()) {
            if (p_info.type == Variant::ARRAY && p_info.hint == PROPERTY_HINT_ARRAY_TYPE && !p_info.hint_string.is_empty()) {
                // Handle MAKE_RESOURCE_TYPE_HINT
                const PackedStringArray components = p_info.hint_string.split(":");
                const String hint_class_name = components[components.size() - 1];
                return null_prefix + variant_type_name + "<" + make_classname(hint_class_name, true) + ">";
            }

            // PROPERTY_HINT_DICTIONARY_TYPE won't be present prior to 4.4
            if (p_info.type == Variant::DICTIONARY && p_info.hint == PROPERTY_HINT_DICTIONARY_TYPE && !p_info.hint_string.is_empty()) {
                const PackedStringArray class_names = p_info.hint_string.split(";");
                if (class_names.size() == 2) {
                    const String key_type = make_classname(class_names[0], true);
                    bool is_js_object_key = false;
                    for (const char *t : js_object_key_types) {
                        if (key_type == t) {
                            is_js_object_key = true;
                            break;
                        }
                    }
                    if (is_js_object_key) {
                        return null_prefix + variant_type_name + "<Record<" + key_type + ", "
                                + make_classname(class_names[0], true) + ">>";
                    }
                    return null_prefix + variant_type_name;
                }
            }

            return null_prefix + variant_type_name;
        }

        return "any /*unhandled: " + itos((int)p_info.type) + "*/";
    }

    return null_prefix + make_classname(p_info.class_name);
}

// ---------------------------------------------------------------------------
// make_arg / make_args / make_return / make_signal_type
// ---------------------------------------------------------------------------
String TypeDB::make_arg(const PropertyInfo &p_info, bool p_optional) {
    return replace_var_name(p_info.name) + (p_optional ? "?" : "") + ": " + make_typename(p_info, true, true);
}

static bool default_value_is_empty(const Variant &p_value) {
    switch (p_value.get_type()) {
        case Variant::NIL:
            return true;
        case Variant::ARRAY:
            return p_value.operator Array().is_empty();
        case Variant::DICTIONARY:
            return p_value.operator Dictionary().is_empty();
        default: {
            // packed arrays
            if (p_value.get_type() >= Variant::PACKED_BYTE_ARRAY && p_value.get_type() <= Variant::PACKED_COLOR_ARRAY) {
                // `is_empty()` is not exposed uniformly on packed arrays in godot-cpp;
                // use the string representation as a cheap emptiness probe (matches
                // `[]` / `(...)` only when empty).
                                Variant copy = p_value;
                return copy.call("is_empty");
            }
            return false;
        }
    }
}

String TypeDB::make_arg_default_value(const MethodDecl &p_method, int p_index) {
    const int def_index = p_index - ((int)p_method.args.size() - (int)p_method.default_arguments.size());

    if (def_index < 0 || def_index >= (int)p_method.default_arguments.size()) {
        return make_arg(p_method.args[p_index]);
    }

    const MethodDecl::DefaultValue &default_argument = p_method.default_arguments[def_index];
    const String arg_text = make_arg(p_method.args[p_index], true);
    const String default_text = make_literal_value(default_argument);
    return default_text.is_empty() ? arg_text : arg_text + String(" /* = ") + default_text + " */";
}

String TypeDB::make_args(const MethodDecl &p_method) {
    const char *varargs = "...varargs: any[]";
    if (p_method.args.size() == 0) {
        return p_method.is_vararg ? varargs : "";
    }
    String args;
    for (int i = 0; i < (int)p_method.args.size(); ++i) {
        if (i > 0) {
            args += ", ";
        }
        args += make_arg_default_value(p_method, i);
    }
    if (p_method.is_vararg) {
        args += ", ";
        args += varargs;
    }
    return args;
}

String TypeDB::make_return(const MethodDecl &p_method) {
    if (p_method.has_returns()) {
        return make_typename(p_method.return_, false, p_method.name.begins_with("create"));
    }
    return "void";
}

String TypeDB::make_signal_type(const SignalDecl &p_signal) {
    String args;
    for (int i = 0; i < (int)p_signal.method.arguments.size(); ++i) {
        const PropertyInfo &arg = p_signal.method.arguments[i];
        if (i > 0) {
            args += ", ";
        }
        PropertyInfo info = arg;
        info.name = internal::NamingUtil::get_parameter_name(info.name);
        info.class_name = internal::NamingUtil::get_class_name(info.class_name);
        args += info.name + String(": ") + make_typename(info, false, true);
    }
    if (p_signal.method.flags & METHOD_FLAG_VARARG) {
        if (!args.is_empty()) {
            args += ", ";
        }
        args += "...varargs: any[]";
    }
    return "Signal<(" + args + ") => void>";
}

// ---------------------------------------------------------------------------
// make_literal_value
// ---------------------------------------------------------------------------
static String format_double(double p_value) {
    // integral doubles print without a decimal point (matches JS number formatting)
    if (p_value == (double)(int64_t)p_value) {
        return itos((int64_t)p_value);
    }
    return String::num(p_value, 12);
}

String TypeDB::make_literal_value(const MethodDecl::DefaultValue &p_value) {
    const Variant &value = p_value.value;
    const String &type_name = get_variant_to_name(p_value.type);

    switch (p_value.type) {
        case Variant::BOOL:
            return value.get_type() == Variant::NIL ? "false" : ((bool)value ? "true" : "false");
        case Variant::FLOAT:
        case Variant::INT:
            if (value.get_type() == Variant::NIL) {
                return "0";
            }
            return p_value.type == Variant::INT ? itos((int64_t)value) : format_double((double)value);
        case Variant::STRING:
        case Variant::STRING_NAME:
            return value.get_type() == Variant::NIL ? "''" : "'" + String(value) + "'";
        case Variant::NODE_PATH:
            return value.get_type() == Variant::NIL ? "''" : "'" + String(NodePath(value)) + "'";
        case Variant::ARRAY:
        case Variant::OBJECT:
            return String();
        case Variant::NIL:
            return "{}";
        case Variant::CALLABLE:
        case Variant::RID:
            return "new " + type_name + "()";
        default:
            break;
    }

    // vector2-ish
    if (p_value.type == Variant::VECTOR2 || p_value.type == Variant::VECTOR2I) {
        if (value.get_type() == Variant::NIL) {
            return "new " + type_name + "()";
        }
        const double x = value.get("x"), y = value.get("y");
        if (x == y) {
            if (x == 0) return type_name + String(".ZERO");
            if (x == 1) return type_name + String(".ONE");
        }
        return "new " + type_name + "(" + format_double(x) + ", " + format_double(y) + ")";
    }

    // vector3-ish
    if (p_value.type == Variant::VECTOR3 || p_value.type == Variant::VECTOR3I) {
        if (value.get_type() == Variant::NIL) {
            return "new " + type_name + "()";
        }
        const double x = value.get("x"), y = value.get("y"), z = value.get("z");
        if ((x == y) == z) { // mirrors TS `(x == y) == z`
            if (x == 0) return type_name + String(".ZERO");
            if (x == 1) return type_name + String(".ONE");
        }
        return "new " + type_name + "(" + format_double(x) + ", " + format_double(y) + ", " + format_double(z) + ")";
    }

    if (p_value.type == Variant::COLOR) {
        if (value.get_type() == Variant::NIL) {
            return "new " + type_name + "()";
        }
        return "new " + type_name + "(" + format_double(value.get("r")) + ", " + format_double(value.get("g")) + ", "
                + format_double(value.get("b")) + ", " + format_double(value.get("a")) + ")";
    }

    if (p_value.type == Variant::RECT2 || p_value.type == Variant::RECT2I) {
        if (value.get_type() == Variant::NIL) {
            return "new " + type_name + "()";
        }
        const Variant position = value.get("position");
        const Variant size = value.get("size");
        return "new " + type_name + "(" + format_double(position.get("x")) + ", " + format_double(position.get("y"))
                + ", " + format_double(size.get("x")) + ", " + format_double(size.get("y")) + ")";
    }

    if (p_value.type >= Variant::PACKED_BYTE_ARRAY && p_value.type <= Variant::PACKED_COLOR_ARRAY) {
        if (default_value_is_empty(value)) {
            return "[]";
        }
    }

    if (p_value.type == Variant::DICTIONARY && default_value_is_empty(value)) {
        return "new " + type_name + "()";
    }

    // NOTE hope all default values for Transform2D/Transform3D are IDENTITY
    if (p_value.type == Variant::TRANSFORM2D || p_value.type == Variant::TRANSFORM3D) {
        return "new " + type_name + "()";
    }

    return String();
}

void destroy_typedb(TypeDB &p_db) {
    // decls are owned by the TypeDB and must be freed explicitly (mirrors the
    // TS objects being garbage-collected).
    for (ClassDecl *decl : p_db.owned_classes_) {
        memdelete(decl);
    }
    p_db.owned_classes_.clear();
    p_db.classes.clear();

    for (PrimitiveClassDecl *decl : p_db.owned_primitives_) {
        memdelete(decl);
    }
    p_db.owned_primitives_.clear();
    p_db.primitive_types.clear();

    for (SingletonDecl *decl : p_db.owned_singletons_) {
        memdelete(decl);
    }
    p_db.owned_singletons_.clear();
    p_db.singletons.clear();

    for (GlobalConstantDecl *decl : p_db.owned_globals_) {
        memdelete(decl);
    }
    p_db.owned_globals_.clear();
    p_db.globals.clear();

    for (MethodDecl *decl : p_db.owned_utilities_) {
        memdelete(decl);
    }
    p_db.owned_utilities_.clear();
    p_db.utilities.clear();

    destroy_doc_cache(p_db.docs_);
}

} // namespace codegen
} // namespace jsb
