#!/usr/bin/env python3
"""
Generate API extension files from extension_api.json:
- utility_functions_ext.gen.h/cpp
- variant_builtin_ext.gen.h/cpp
- core_constants.gen.h/cpp
"""

import os
import sys
import io
import json

def write_file(filename, ostream):
    ostream.seek(0)
    content = ostream.read()
    if os.path.exists(filename):
        with open(filename, "rt", encoding="utf-8") as input:
            if input.read() == content:
                print("generate {}: no diff".format(filename))
                return
    with open(filename, "wt", encoding="utf-8") as output:
        print("generating {}".format(filename))
        output.write(content)

def get_output_dir():
    """Get the output directory for generated files"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(os.path.dirname(script_dir))
    return os.path.join(project_root, "GodotJS", "gen")

def get_variant_type_str(variant_type):
    """Convert variant type string to Variant::Type enum value"""
    tmap = {
        "void": "NIL", "bool": "BOOL", "int": "INT", "float": "FLOAT",
        "String": "STRING", "Vector2": "VECTOR2", "Vector2i": "VECTOR2I",
        "Rect2": "RECT2", "Vector3": "VECTOR3", "Vector3i": "VECTOR3I",
        "Vector4": "VECTOR4", "Vector4i": "VECTOR4I",
        "Transform2D": "TRANSFORM2D", "Plane": "PLANE",
        "Quaternion": "QUATERNION", "AABB": "AABB", "Basis": "BASIS",
        "Transform3D": "TRANSFORM3D", "Projection": "PROJECTION",
        "Color": "COLOR", "StringName": "STRING_NAME", "NodePath": "NODE_PATH",
        "RID": "RID", "Object": "OBJECT", "callable": "CALLABLE",
        "Signal": "SIGNAL", "Dictionary": "DICTIONARY", "Array": "ARRAY",
        "PackedByteArray": "PACKED_BYTE_ARRAY", "PackedInt32Array": "PACKED_INT32_ARRAY",
        "PackedInt64Array": "PACKED_INT64_ARRAY", "PackedFloat32Array": "PACKED_FLOAT32_ARRAY",
        "PackedFloat64Array": "PACKED_FLOAT64_ARRAY", "PackedStringArray": "PACKED_STRING_ARRAY",
        "PackedVector2Array": "PACKED_VECTOR2_ARRAY", "PackedVector3Array": "PACKED_VECTOR3_ARRAY",
        "PackedColorArray": "PACKED_COLOR_ARRAY", "PackedVector4Array": "PACKED_VECTOR4_ARRAY",
        "Variant": "NIL",
    }
    if variant_type.startswith("enum::"):
        return "INT"
    return tmap.get(variant_type, "NIL")

def generate_utility_functions_ext(api_path):
    """Generate utility_functions_ext.gen.h/cpp"""
    print("Generating utility_functions_ext.gen.h/cpp from {}...".format(api_path))

    with open(api_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    utility_functions = data.get('utility_functions', [])
    func_count = len(utility_functions)
    output_dir = get_output_dir()

    # --- Header ---
    uf_h = io.StringIO()
    uf_h.write('''/**
 * @file utility_functions_ext.gen.h
 * @brief Auto-generated list of Godot utility functions from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#ifndef GODOTJS_UTILITY_FUNCTIONS_EXT_GEN_H
#define GODOTJS_UTILITY_FUNCTIONS_EXT_GEN_H

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/list.hpp>

namespace godot {

// Validated function pointer types (same as in jsb_compat.h, forward-declared here to avoid heavy includes)
using ValidatedUtilityFunction = void (*)(Variant *r_ret, const Variant **p_args, int p_argcount);

/**
 * @brief VariantExt - wraps Variant utility function info
 */
namespace VariantExt {

// Utility functions
int get_utility_function_count();
bool has_utility_function(const StringName& p_name);
MethodInfo get_utility_function_info(const StringName& p_name);
int get_utility_function_argument_count(const StringName& p_name);
Variant::Type get_utility_function_argument_type(const StringName& p_name, int p_arg);
String get_utility_function_argument_name(const StringName& p_name, int p_arg);
bool has_utility_function_return_value(const StringName& p_name);
Variant::Type get_utility_function_return_type(const StringName& p_name);
bool is_utility_function_vararg(const StringName& p_name);
void get_utility_function_list(List<StringName>* r_functions);
ValidatedUtilityFunction get_validated_utility_function(const StringName& p_name);

} // namespace VariantExt

} // namespace godot

#endif // GODOTJS_UTILITY_FUNCTIONS_EXT_GEN_H
''')
    write_file(os.path.join(output_dir, 'utility_functions_ext.gen.h'), uf_h)

    # --- Implementation ---
    uf_cpp = io.StringIO()
    uf_cpp.write('''/**
 * @file utility_functions_ext.gen.cpp
 * @brief Auto-generated list of Godot utility functions from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#include "utility_functions_ext.gen.h"

namespace godot {

static MethodInfo _make_uf(const StringName& p_name, Variant::Type p_ret, uint32_t p_ret_usage, uint32_t p_flags, const LocalVector<PropertyInfo>& p_args, uint32_t p_hash) {
    MethodInfo mi;
    mi.name = p_name;
    mi.return_val = PropertyInfo(p_ret, StringName(""), PROPERTY_HINT_NONE, "", p_ret_usage);
    mi.flags = p_flags;
    mi.arguments = p_args;
    mi.id = p_hash;
    return mi;
}

static const MethodInfo& _uf_data(int& r_count) {
    static const MethodInfo s_data[] = {
''')

    for uf in utility_functions:
        name = uf["name"]
        ret_raw = uf.get("return_type", "void")
        ret_type = get_variant_type_str(ret_raw)
        is_vararg = uf.get("is_vararg", False)
        args = uf.get("arguments", [])
        flags = "GDEXTENSION_METHOD_FLAG_STATIC"
        if is_vararg:
            flags += " | GDEXTENSION_METHOD_FLAG_VARARG"
        hash_val = uf.get("hash", 0)
        
        # Determine return value usage flags
        if ret_raw == "Variant":
            ret_usage = "PROPERTY_USAGE_NIL_IS_VARIANT"
        else:
            ret_usage = "PROPERTY_USAGE_NONE"
        
        if args:
            arg_inits = []
            for a in args:
                arg_type_str = get_variant_type_str(a["type"])
                if a["type"] == "Variant":
                    arg_usage = "PROPERTY_USAGE_NIL_IS_VARIANT"
                else:
                    arg_usage = "PROPERTY_USAGE_NONE"
                arg_inits.append(
                    "PropertyInfo(Variant::" + arg_type_str + ", StringName(\"" + a["name"] + "\"), PROPERTY_HINT_NONE, \"\", " + arg_usage + ")"
                )
            arg_list = "{ " + ", ".join(arg_inits) + " }"
        else:
            arg_list = "{}"
        
        line = "        _make_uf(StringName(\"" + name + "\"), Variant::" + ret_type + ", " + ret_usage + ", " + flags + ", " + arg_list + ", " + str(hash_val) + "),\n"
        uf_cpp.write(line)

    uf_cpp.write("    };\n")
    uf_cpp.write("    static const int s_count = " + str(func_count) + ";\n")
    uf_cpp.write("    r_count = s_count;\n")
    uf_cpp.write("    return s_data[0];\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("static int _uf_find(const StringName& p_name) {\n")
    uf_cpp.write("    int n = 0;\n")
    uf_cpp.write("    const MethodInfo& d = _uf_data(n);\n")
    uf_cpp.write("    for (int i = 0; i < n; ++i) {\n")
    uf_cpp.write("        if ((&d)[i].name == p_name) return i;\n")
    uf_cpp.write("    }\n")
    uf_cpp.write("    return -1;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("static const MethodInfo* _uf_get(const StringName& p_name) {\n")
    uf_cpp.write("    int i = _uf_find(p_name);\n")
    uf_cpp.write("    if (i < 0) return nullptr;\n")
    uf_cpp.write("    int n = 0;\n")
    uf_cpp.write("    return &(&_uf_data(n))[i];\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("int VariantExt::get_utility_function_count() {\n")
    uf_cpp.write("    int n = 0;\n")
    uf_cpp.write("    _uf_data(n);\n")
    uf_cpp.write("    return n;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("bool VariantExt::has_utility_function(const StringName& p_name) {\n")
    uf_cpp.write("    return _uf_find(p_name) >= 0;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("void VariantExt::get_utility_function_list(List<StringName>* r) {\n")
    uf_cpp.write("    int n = 0;\n")
    uf_cpp.write("    const MethodInfo& d = _uf_data(n);\n")
    uf_cpp.write("    for (int i = 0; i < n; ++i) r->push_back((&d)[i].name);\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("int VariantExt::get_utility_function_argument_count(const StringName& p_name) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    return f ? (int)f->arguments.size() : 0;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("bool VariantExt::is_utility_function_vararg(const StringName& p_name) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    return f ? (f->flags & GDEXTENSION_METHOD_FLAG_VARARG) != 0 : false;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("Variant::Type VariantExt::get_utility_function_return_type(const StringName& p_name) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    return f ? f->return_val.type : Variant::NIL;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("bool VariantExt::has_utility_function_return_value(const StringName& p_name) {\n")
    uf_cpp.write("    return get_utility_function_return_type(p_name) != Variant::NIL;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("MethodInfo VariantExt::get_utility_function_info(const StringName& p_name) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    return f ? *f : MethodInfo();\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("Variant::Type VariantExt::get_utility_function_argument_type(const StringName& p_name, int p_arg) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    if (!f || p_arg < 0 || (uint32_t)p_arg >= f->arguments.size()) return Variant::NIL;\n")
    uf_cpp.write("    return f->arguments[p_arg].type;\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("String VariantExt::get_utility_function_argument_name(const StringName& p_name, int p_arg) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    if (!f || p_arg < 0 || (uint32_t)p_arg >= f->arguments.size()) return String();\n")
    uf_cpp.write("    return String(f->arguments[p_arg].name);\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("ValidatedUtilityFunction VariantExt::get_validated_utility_function(const StringName& p_name) {\n")
    uf_cpp.write("    const MethodInfo* f = _uf_get(p_name);\n")
    uf_cpp.write("    if (!f) return nullptr;\n")
    uf_cpp.write("    GDExtensionPtrUtilityFunction ptr_func = ::godot::gdextension_interface::variant_get_ptr_utility_function(f->name._native_ptr(), (GDExtensionInt)f->id);\n")
    uf_cpp.write("    if (!ptr_func) return nullptr;\n")
    uf_cpp.write("    return reinterpret_cast<ValidatedUtilityFunction>(ptr_func);\n")
    uf_cpp.write("}\n\n")

    uf_cpp.write("} // namespace godot\n")
    write_file(os.path.join(output_dir, 'utility_functions_ext.gen.cpp'), uf_cpp)
    print("  - Generated {} utility functions".format(func_count))

def generate_variant_builtin_ext(api_path):
    """Generate variant_builtin_ext.gen.h/cpp"""
    print("Generating variant_builtin_ext.gen.h/cpp from {}...".format(api_path))

    with open(api_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    builtin_classes = data.get('builtin_classes', [])
    output_dir = get_output_dir()

    # --- Header ---
    vb_h = io.StringIO()
    vb_h.write('''/**
 * @file variant_builtin_ext.gen.h
 * @brief Auto-generated Variant builtin reflection info from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#ifndef GODOTJS_VARIANT_BUILTIN_EXT_GEN_H
#define GODOTJS_VARIANT_BUILTIN_EXT_GEN_H

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/templates/list.hpp>

#include <gdextension_interface.h>

namespace godot {

// Validated function pointer types (same as in jsb_compat.h, defined here to avoid heavy includes)
using ValidatedConstructor = void (*)(Variant *r_base, const Variant **p_args);
using ValidatedSetter = void (*)(Variant *base, const Variant *value);
using ValidatedGetter = void (*)(const Variant *base, Variant *value);
using ValidatedBuiltInMethod = void (*)(Variant *base, const Variant **p_args, int p_argcount, Variant *r_ret);

/**
 * @brief VariantExt - wraps Variant builtin reflection info
 */
namespace VariantExt {

// Constructors
int get_constructor_count(Variant::Type p_type);
ValidatedConstructor get_validated_constructor(Variant::Type p_type, int p_index);
int get_constructor_argument_count(Variant::Type p_type, int p_constructor_index);
Variant::Type get_constructor_argument_type(Variant::Type p_type, int p_constructor_index, int p_argument_index);
String get_constructor_argument_name(Variant::Type p_type, int p_constructor, int p_argument);

// Properties (Members)
void get_member_list(Variant::Type p_type, List<StringName>* r_list);
Variant::Type get_member_type(Variant::Type p_type, const StringName& p_member);
ValidatedSetter get_member_validated_setter(Variant::Type p_type, const StringName& p_member);
ValidatedGetter get_member_validated_getter(Variant::Type p_type, const StringName& p_member);
GDExtensionPtrGetter get_member_ptr_getter(Variant::Type p_type, const StringName& p_member);
GDExtensionPtrSetter get_member_ptr_setter(Variant::Type p_type, const StringName& p_member);

// Indexing / Keyed
bool has_indexing(Variant::Type p_type);
bool is_keyed(Variant::Type p_type);
Variant::Type get_indexed_element_type(Variant::Type p_type);

// Builtin Methods
void get_builtin_method_list(Variant::Type p_type, List<StringName>* r_list);
int get_builtin_method_argument_count(Variant::Type p_type, const StringName& p_method);
bool has_builtin_method_return_value(Variant::Type p_type, const StringName& p_method);
Variant::Type get_builtin_method_return_type(Variant::Type p_type, const StringName& p_method);
GDExtensionPtrBuiltInMethod get_ptr_builtin_method(Variant::Type p_type, const StringName& p_method);
ValidatedBuiltInMethod get_validated_builtin_method(Variant::Type p_type, const StringName& p_method);
bool is_builtin_method_static(Variant::Type p_type, const StringName& p_method);
Variant::Type get_builtin_method_argument_type(Variant::Type p_type, const StringName& p_method, int p_argument_index);
String get_builtin_method_argument_name(Variant::Type p_type, const StringName& p_method, int p_argument);
Vector<Variant> get_builtin_method_default_arguments(Variant::Type p_type, const StringName& p_method);
bool is_builtin_method_vararg(Variant::Type p_type, const StringName& p_method);
bool is_builtin_method_const(Variant::Type p_type, const StringName& p_method);

// Enums
void get_enums_for_type(Variant::Type p_type, List<StringName>* r_list);
void get_enumerations_for_enum(Variant::Type p_type, const StringName& p_enum, List<StringName>* r_list);
int get_enum_value(Variant::Type p_type, const StringName& p_enum, const StringName& p_enumeration, bool* r_valid = nullptr);

// Constants
int get_constants_count_for_type(Variant::Type p_type);
void get_constants_for_type(Variant::Type p_type, List<StringName>* r_constants);
bool has_constant(Variant::Type p_type, const StringName& p_name);
Variant get_constant_value(Variant::Type p_type, const StringName& p_name, bool* r_valid = nullptr);

} // namespace VariantExt

} // namespace godot

#endif // GODOTJS_VARIANT_BUILTIN_EXT_GEN_H
''')
    write_file(os.path.join(output_dir, 'variant_builtin_ext.gen.h'), vb_h)

    # --- Implementation ---
    vb_cpp = io.StringIO()
    vb_cpp.write('''/**
 * @file variant_builtin_ext.gen.cpp
 * @brief Auto-generated Variant builtin reflection info from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#include "variant_builtin_ext.gen.h"
#include <gdextension_interface.h>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/vector4i.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rect2i.hpp>

namespace godot {

typedef struct { const char* name; int variant_type; GDExtensionPtrGetter getter; GDExtensionPtrSetter setter; } MemberEntry;
typedef struct { const char* name; GDExtensionPtrBuiltInMethod method; bool is_static; bool is_const; int return_type; bool has_return_value; bool is_vararg; int arg_count; int default_arg_count; int first_default_arg; } MethodEntry;
typedef struct { const char* name; int64_t value; } ValueEntry;
typedef struct { const char* name; const char* type_str; const char* value_str; } ConstantEntry;

typedef struct {
    const char* arg_names[8];
    int arg_types[8];
    int arg_count;
} ConstructorInfo;

typedef struct {
    const char* arg_names[8];
    int arg_types[8];
    int default_arg_types[8];
    int default_arg_int_values[8];
    double default_arg_float_values[8];
    bool default_arg_is_numeric[8];
    int arg_count;
    int default_arg_count;
} MethodInfoExt;

// Enum storage
struct EnumValueInfo { const char* name; int64_t value; };
struct EnumInfo { const char* name; bool is_bitfield; int value_count; const EnumValueInfo* values; };

static const GDExtensionVariantFromTypeConstructorFunc* _builtin_constructors[Variant::VARIANT_MAX] = {};
static const MemberEntry* _builtin_members[Variant::VARIANT_MAX] = {};
static const MethodEntry* _builtin_methods[Variant::VARIANT_MAX] = {};
static const ValueEntry* _builtin_values[Variant::VARIANT_MAX] = {};
static const ConstantEntry* _builtin_constants[Variant::VARIANT_MAX] = {};
static const ConstructorInfo* _builtin_constructor_infos[Variant::VARIANT_MAX] = {};
static const MethodInfoExt* _builtin_method_infos_ext[Variant::VARIANT_MAX] = {};
static const EnumInfo* _builtin_enums[Variant::VARIANT_MAX] = {};
static int _builtin_constructor_counts[Variant::VARIANT_MAX] = {};
static int _builtin_member_counts[Variant::VARIANT_MAX] = {};
static int _builtin_method_counts[Variant::VARIANT_MAX] = {};
static int _builtin_value_counts[Variant::VARIANT_MAX] = {};
static int _builtin_constant_counts[Variant::VARIANT_MAX] = {};
static int _builtin_enum_counts[Variant::VARIANT_MAX] = {};
static Variant::Type _builtin_indexing_element_type[Variant::VARIANT_MAX] = {};
static bool _initialized = false;

''')

    # Generate per-type lazy-init functions
    for idx, cls in enumerate(builtin_classes):
        class_name = cls.get("name", "")
        lc = class_name.lower()

        # Constructors
        constructors = cls.get("constructors", [])
        vb_cpp.write("static const GDExtensionVariantFromTypeConstructorFunc* _get_%s_constructors(int& r_count) {\n" % lc)
        if constructors:
            vb_cpp.write("    static GDExtensionVariantFromTypeConstructorFunc s_data[] = {\n")
            for i in range(len(constructors)):
                vb_cpp.write("        (GDExtensionVariantFromTypeConstructorFunc)godot::gdextension_interface::variant_get_ptr_constructor((GDExtensionVariantType)%d, %d),\n" % (idx, i))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(constructors))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Constructor info (argument names and types)
        vb_cpp.write("static const ConstructorInfo* _get_%s_constructor_infos(int& r_count) {\n" % lc)
        if constructors:
            vb_cpp.write("    static const ConstructorInfo s_data[] = {\n")
            for ctor in constructors:
                args = ctor.get("arguments", [])
                if args:
                    arg_names = ', '.join(['"%s"' % a.get("name", "") for a in args])
                    arg_types = ', '.join(['Variant::%s' % get_variant_type_str(a.get("type", "Variant")) for a in args])
                    vb_cpp.write("        { {%s}, {%s}, %d },\n" % (arg_names, arg_types, len(args)))
                else:
                    vb_cpp.write("        { {nullptr}, {0}, 0 },\n")
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(constructors))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Members
        members = cls.get("members", [])
        vb_cpp.write("static const MemberEntry* _get_%s_members(int& r_count) {\n" % lc)
        if members:
            vb_cpp.write("    static const MemberEntry s_data[] = {\n")
            for member in members:
                member_name = member.get("name", "")
                member_type = get_variant_type_str(member.get("type", "Variant"))
                vb_cpp.write("        { \"%s\", Variant::%s, (GDExtensionPtrGetter)godot::gdextension_interface::variant_get_ptr_getter((GDExtensionVariantType)%d, StringName(\"%s\")._native_ptr()), (GDExtensionPtrSetter)godot::gdextension_interface::variant_get_ptr_setter((GDExtensionVariantType)%d, StringName(\"%s\")._native_ptr()) },\n" % (member_name, member_type, idx, member_name, idx, member_name))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(members))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Methods
        methods = cls.get("methods", [])
        vb_cpp.write("static const MethodEntry* _get_%s_methods(int& r_count) {\n" % lc)
        if methods:
            vb_cpp.write("    static const MethodEntry s_data[] = {\n")
            for method in methods:
                method_name = method.get("name", "")
                is_static = method.get("is_static", False)
                is_const = method.get("is_const", False)
                ret_type = method.get("return_type", "")
                has_ret = ret_type is not None and ret_type != ""
                ret_type_enum = get_variant_type_str(ret_type) if has_ret else "NIL"
                is_vararg = method.get("is_vararg", False)
                args = method.get("arguments", [])
                arg_count = len(args)
                default_args = method.get("default_arguments", [])
                default_arg_count = len(default_args)
                first_default = arg_count - default_arg_count if default_arg_count > 0 else -1
                vb_cpp.write("        { \"%s\", (GDExtensionPtrBuiltInMethod)godot::gdextension_interface::variant_get_ptr_builtin_method((GDExtensionVariantType)%d, StringName(\"%s\")._native_ptr(), %d), %s, %s, Variant::%s, %s, %s, %d, %d, %d },\n" % (method_name, idx, method_name, method.get("hash", 0), str(is_static).lower(), str(is_const).lower(), ret_type_enum, str(has_ret).lower(), str(is_vararg).lower(), arg_count, default_arg_count, first_default))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(methods))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Method info ext (argument names and types)
        vb_cpp.write("static const MethodInfoExt* _get_%s_method_infos_ext(int& r_count) {\n" % lc)
        if methods:
            vb_cpp.write("    static const MethodInfoExt s_data[] = {\n")
            for method in methods:
                args = method.get("arguments", [])
                if args:
                    arg_names = ', '.join(['"%s"' % a.get("name", "") for a in args])
                    arg_types = ', '.join(['Variant::%s' % get_variant_type_str(a.get("type", "Variant")) for a in args])
                    vb_cpp.write("        { {%s}, {%s}, {}, {}, {}, {}, %d, 0 },\n" % (arg_names, arg_types, len(args)))
                else:
                    vb_cpp.write("        { {nullptr}, {0}, {}, {}, {}, {}, 0, 0 },\n")
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(methods))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Values
        values = cls.get("values", [])
        vb_cpp.write("static const ValueEntry* _get_%s_values(int& r_count) {\n" % lc)
        if values:
            vb_cpp.write("    static const ValueEntry s_data[] = {\n")
            for val in values:
                value_name = val.get("name", "")
                value_val = val.get("value", 0)
                vb_cpp.write("        { \"%s\", (int64_t)%d },\n" % (value_name, int(value_val) if isinstance(value_val, (int, float)) else 0))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(values))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Constants
        constants = cls.get("constants", [])
        vb_cpp.write("static const ConstantEntry* _get_%s_constants(int& r_count) {\n" % lc)
        if constants:
            vb_cpp.write("    static const ConstantEntry s_data[] = {\n")
            for const in constants:
                const_name = const.get("name", "")
                const_type = const.get("type", "")
                const_value = const.get("value", "")
                # Escape backslashes and quotes in the value string
                const_value_escaped = const_value.replace("\\", "\\\\").replace('"', '\\"')
                vb_cpp.write('        { "%s", "%s", "%s" },\n' % (const_name, const_type, const_value_escaped))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(constants))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

        # Enums
        enums = cls.get("enums", [])
        vb_cpp.write("static const EnumInfo* _get_%s_enums(int& r_count) {\n" % lc)
        if enums:
            # Generate enum value arrays
            for enum_def in enums:
                enum_name = enum_def.get("name", "")
                enum_lc = enum_name.lower()
                values = enum_def.get("values", [])
                vb_cpp.write("    static const EnumValueInfo _%s_%s_values[] = {\n" % (lc, enum_lc))
                for val in values:
                    val_name = val.get("name", "")
                    val_val = val.get("value", 0)
                    vb_cpp.write('        { "%s", (int64_t)%d },\n' % (val_name, int(val_val)))
                vb_cpp.write("    };\n")

            vb_cpp.write("    static const EnumInfo s_data[] = {\n")
            for enum_def in enums:
                enum_name = enum_def.get("name", "")
                enum_lc = enum_name.lower()
                is_bitfield = enum_def.get("is_bitfield", False)
                val_count = len(enum_def.get("values", []))
                vb_cpp.write('        { "%s", %s, %d, _%s_%s_values },\n' % (enum_name, str(is_bitfield).lower(), val_count, lc, enum_lc))
            vb_cpp.write("    };\n")
            vb_cpp.write("    r_count = %d;\n" % len(enums))
            vb_cpp.write("    return s_data;\n")
        else:
            vb_cpp.write("    r_count = 0;\n    return nullptr;\n")
        vb_cpp.write("}\n\n")

    # Generate Variant construction helper from value string
    vb_cpp.write('''static Variant _make_variant_from_value_str(const char* p_type, const char* p_value) {
    String type_str(p_type);
    String val_str(p_value);

    // Simple scalar types
    if (type_str == "int" || type_str == "") {
        return Variant(val_str.to_int());
    }
    if (type_str == "float") {
        return Variant(val_str.to_float());
    }
    if (type_str == "bool") {
        return Variant(val_str == "true");
    }
    if (type_str == "String") {
        return Variant(val_str);
    }

    // Parse constructor-style values: TypeName(arg1, arg2, ...)
    // Extract arguments from the parentheses
    int paren_start = val_str.find("(");
    int paren_end = val_str.rfind(")");
    if (paren_start < 0 || paren_end < 0 || paren_end <= paren_start) {
        return Variant();
    }
    String args_str = val_str.substr(paren_start + 1, paren_end - paren_start - 1).strip_edges();

    // Split by comma
    Vector<String> arg_strs;
    if (!args_str.is_empty()) {
        int depth = 0;
        int last = 0;
        for (int i = 0; i < args_str.length(); i++) {
            char32_t c = args_str[i];
            if (c == '(' || c == '[') depth++;
            else if (c == ')' || c == ']') depth--;
            else if (c == ',' && depth == 0) {
                arg_strs.push_back(args_str.substr(last, i - last).strip_edges());
                last = i + 1;
            }
        }
        arg_strs.push_back(args_str.substr(last).strip_edges());
    }

    auto parse_float = [&](int idx) -> double {
        if (idx < arg_strs.size()) {
            return arg_strs[idx].to_float();
        }
        return 0.0;
    };
    auto parse_int = [&](int idx) -> int64_t {
        if (idx < arg_strs.size()) {
            return arg_strs[idx].to_int();
        }
        return 0;
    };

    // Vector2(x, y)
    if (type_str == "Vector2") {
        return Variant(Vector2(parse_float(0), parse_float(1)));
    }
    if (type_str == "Vector2i") {
        return Variant(Vector2i(parse_int(0), parse_int(1)));
    }
    if (type_str == "Vector3") {
        return Variant(Vector3(parse_float(0), parse_float(1), parse_float(2)));
    }
    if (type_str == "Vector3i") {
        return Variant(Vector3i(parse_int(0), parse_int(1), parse_int(2)));
    }
    if (type_str == "Vector4") {
        return Variant(Vector4(parse_float(0), parse_float(1), parse_float(2), parse_float(3)));
    }
    if (type_str == "Vector4i") {
        return Variant(Vector4i(parse_int(0), parse_int(1), parse_int(2), parse_int(3)));
    }
    if (type_str == "Color") {
        return Variant(Color(parse_float(0), parse_float(1), parse_float(2), parse_float(3)));
    }
    if (type_str == "Plane") {
        return Variant(Plane(parse_float(0), parse_float(1), parse_float(2), parse_float(3)));
    }
    if (type_str == "Quaternion") {
        return Variant(Quaternion(parse_float(0), parse_float(1), parse_float(2), parse_float(3)));
    }
    if (type_str == "Transform2D") {
        return Variant(Transform2D(parse_float(0), parse_float(1), parse_float(2), parse_float(3), parse_float(4), parse_float(5)));
    }
    if (type_str == "Basis") {
        return Variant(Basis(parse_float(0), parse_float(1), parse_float(2),
                             parse_float(3), parse_float(4), parse_float(5),
                             parse_float(6), parse_float(7), parse_float(8)));
    }
    if (type_str == "Transform3D") {
        return Variant(Transform3D(parse_float(0), parse_float(1), parse_float(2),
                                    parse_float(3), parse_float(4), parse_float(5),
                                    parse_float(6), parse_float(7), parse_float(8),
                                    parse_float(9), parse_float(10), parse_float(11)));
    }
    if (type_str == "AABB") {
        return Variant(AABB(Vector3(parse_float(0), parse_float(1), parse_float(2)),
                            Vector3(parse_float(3), parse_float(4), parse_float(5))));
    }
    if (type_str == "Rect2") {
        return Variant(Rect2(parse_float(0), parse_float(1), parse_float(2), parse_float(3)));
    }
    if (type_str == "Rect2i") {
        return Variant(Rect2i(parse_int(0), parse_int(1), parse_int(2), parse_int(3)));
    }

    return Variant();
}

''')

    # Generate init function
    vb_cpp.write("static void _ensure_initialized() {\n")
    vb_cpp.write("    if (_initialized) return;\n")
    vb_cpp.write("    _initialized = true;\n")
    for idx, cls in enumerate(builtin_classes):
        lc = cls.get("name", "").lower()
        vb_cpp.write("    _builtin_constructors[%d] = _get_%s_constructors(_builtin_constructor_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_constructor_infos[%d] = _get_%s_constructor_infos(_builtin_constructor_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_members[%d] = _get_%s_members(_builtin_member_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_methods[%d] = _get_%s_methods(_builtin_method_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_method_infos_ext[%d] = _get_%s_method_infos_ext(_builtin_method_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_values[%d] = _get_%s_values(_builtin_value_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_constants[%d] = _get_%s_constants(_builtin_constant_counts[%d]);\n" % (idx, lc, idx))
        vb_cpp.write("    _builtin_enums[%d] = _get_%s_enums(_builtin_enum_counts[%d]);\n" % (idx, lc, idx))
        irt = cls.get("indexing_return_type")
        if irt:
            vb_cpp.write("    _builtin_indexing_element_type[%d] = Variant::%s;\n" % (idx, get_variant_type_str(irt)))
    vb_cpp.write("}\n\n")

    # Accessor functions
    vb_cpp.write('''// Constructor functions
int VariantExt::get_constructor_count(Variant::Type p_type) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constructors[p_type]) {
        return _builtin_constructor_counts[p_type];
    }
    return 0;
}

ValidatedConstructor VariantExt::get_validated_constructor(Variant::Type p_type, int p_index) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constructors[p_type]) {
        GDExtensionVariantFromTypeConstructorFunc ptr_func = _builtin_constructors[p_type][p_index];
        if (!ptr_func) return nullptr;
        return reinterpret_cast<ValidatedConstructor>(ptr_func);
    }
    return nullptr;
}

int VariantExt::get_constructor_argument_count(Variant::Type p_type, int p_constructor_index) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constructor_infos[p_type]) {
        if (p_constructor_index >= 0 && p_constructor_index < _builtin_constructor_counts[p_type]) {
            return _builtin_constructor_infos[p_type][p_constructor_index].arg_count;
        }
    }
    return 0;
}

Variant::Type VariantExt::get_constructor_argument_type(Variant::Type p_type, int p_constructor_index, int p_argument_index) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constructor_infos[p_type]) {
        if (p_constructor_index >= 0 && p_constructor_index < _builtin_constructor_counts[p_type]) {
            const ConstructorInfo& info = _builtin_constructor_infos[p_type][p_constructor_index];
            if (p_argument_index >= 0 && p_argument_index < info.arg_count) {
                return (Variant::Type)info.arg_types[p_argument_index];
            }
        }
    }
    return Variant::NIL;
}

String VariantExt::get_constructor_argument_name(Variant::Type p_type, int p_constructor, int p_argument) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constructor_infos[p_type]) {
        if (p_constructor >= 0 && p_constructor < _builtin_constructor_counts[p_type]) {
            const ConstructorInfo& info = _builtin_constructor_infos[p_type][p_constructor];
            if (p_argument >= 0 && p_argument < info.arg_count) {
                return String(info.arg_names[p_argument]);
            }
        }
    }
    return String();
}

// Member functions
void VariantExt::get_member_list(Variant::Type p_type, List<StringName>* r_list) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_members[p_type]) {
        int count = _builtin_member_counts[p_type];
        for (int i = 0; i < count; i++) {
            r_list->push_back(StringName(_builtin_members[p_type][i].name));
        }
    }
}

Variant::Type VariantExt::get_member_type(Variant::Type p_type, const StringName& p_member) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_members[p_type]) {
        int count = _builtin_member_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_members[p_type][i].name) == p_member) {
                return (Variant::Type)_builtin_members[p_type][i].variant_type;
            }
        }
    }
    return Variant::NIL;
}

ValidatedSetter VariantExt::get_member_validated_setter(Variant::Type p_type, const StringName& p_member) {
    GDExtensionPtrSetter ptr = get_member_ptr_setter(p_type, p_member);
    return ptr ? reinterpret_cast<ValidatedSetter>(ptr) : nullptr;
}

ValidatedGetter VariantExt::get_member_validated_getter(Variant::Type p_type, const StringName& p_member) {
    GDExtensionPtrGetter ptr = get_member_ptr_getter(p_type, p_member);
    return ptr ? reinterpret_cast<ValidatedGetter>(ptr) : nullptr;
}

GDExtensionPtrGetter VariantExt::get_member_ptr_getter(Variant::Type p_type, const StringName& p_member) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_members[p_type]) {
        int count = _builtin_member_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_members[p_type][i].name) == p_member) {
                return _builtin_members[p_type][i].getter;
            }
        }
    }
    return nullptr;
}

GDExtensionPtrSetter VariantExt::get_member_ptr_setter(Variant::Type p_type, const StringName& p_member) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_members[p_type]) {
        int count = _builtin_member_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_members[p_type][i].name) == p_member) {
                return _builtin_members[p_type][i].setter;
            }
        }
    }
    return nullptr;
}

// Indexing functions
bool VariantExt::has_indexing(Variant::Type p_type) {
    return godot::gdextension_interface::variant_get_ptr_indexed_getter((GDExtensionVariantType)p_type) != nullptr;
}

bool VariantExt::is_keyed(Variant::Type p_type) {
    return godot::gdextension_interface::variant_get_ptr_keyed_getter((GDExtensionVariantType)p_type) != nullptr;
}

Variant::Type VariantExt::get_indexed_element_type(Variant::Type p_type) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX) {
        return _builtin_indexing_element_type[p_type];
    }
    return Variant::NIL;
}

// Builtin method functions
void VariantExt::get_builtin_method_list(Variant::Type p_type, List<StringName>* r_list) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            r_list->push_back(StringName(_builtin_methods[p_type][i].name));
        }
    }
}

int VariantExt::get_builtin_method_argument_count(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].arg_count;
            }
        }
    }
    return 0;
}

bool VariantExt::has_builtin_method_return_value(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].has_return_value;
            }
        }
    }
    return false;
}

Variant::Type VariantExt::get_builtin_method_return_type(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return (Variant::Type)_builtin_methods[p_type][i].return_type;
            }
        }
    }
    return Variant::NIL;
}

GDExtensionPtrBuiltInMethod VariantExt::get_ptr_builtin_method(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].method;
            }
        }
    }
    return nullptr;
}

ValidatedBuiltInMethod VariantExt::get_validated_builtin_method(Variant::Type p_type, const StringName& p_method) {
    void* ptr = get_ptr_builtin_method(p_type, p_method);
    return ptr ? reinterpret_cast<ValidatedBuiltInMethod>(ptr) : nullptr;
}

bool VariantExt::is_builtin_method_static(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].is_static;
            }
        }
    }
    return false;
}

Variant::Type VariantExt::get_builtin_method_argument_type(Variant::Type p_type, const StringName& p_method, int p_argument_index) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type] && _builtin_method_infos_ext[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                if (i < count && p_argument_index >= 0 && p_argument_index < _builtin_method_infos_ext[p_type][i].arg_count) {
                    return (Variant::Type)_builtin_method_infos_ext[p_type][i].arg_types[p_argument_index];
                }
                break;
            }
        }
    }
    return Variant::NIL;
}

String VariantExt::get_builtin_method_argument_name(Variant::Type p_type, const StringName& p_method, int p_argument) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type] && _builtin_method_infos_ext[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                if (i < count && p_argument >= 0 && p_argument < _builtin_method_infos_ext[p_type][i].arg_count) {
                    return String(_builtin_method_infos_ext[p_type][i].arg_names[p_argument]);
                }
                break;
            }
        }
    }
    return String();
}

Vector<Variant> VariantExt::get_builtin_method_default_arguments(Variant::Type p_type, const StringName& p_method) {
    (void)p_type;
    (void)p_method;
    return Vector<Variant>(); // TODO: implement
}

bool VariantExt::is_builtin_method_vararg(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].is_vararg;
            }
        }
    }
    return false;
}

bool VariantExt::is_builtin_method_const(Variant::Type p_type, const StringName& p_method) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_methods[p_type]) {
        int count = _builtin_method_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_methods[p_type][i].name) == p_method) {
                return _builtin_methods[p_type][i].is_const;
            }
        }
    }
    return false;
}

// Enum functions
void VariantExt::get_enums_for_type(Variant::Type p_type, List<StringName>* r_list) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_enums[p_type]) {
        int count = _builtin_enum_counts[p_type];
        for (int i = 0; i < count; i++) {
            r_list->push_back(StringName(_builtin_enums[p_type][i].name));
        }
    }
}

void VariantExt::get_enumerations_for_enum(Variant::Type p_type, const StringName& p_enum, List<StringName>* r_list) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_enums[p_type]) {
        int count = _builtin_enum_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_enums[p_type][i].name) == p_enum) {
                for (int j = 0; j < _builtin_enums[p_type][i].value_count; j++) {
                    r_list->push_back(StringName(_builtin_enums[p_type][i].values[j].name));
                }
                break;
            }
        }
    }
}

int VariantExt::get_enum_value(Variant::Type p_type, const StringName& p_enum, const StringName& p_enumeration, bool* r_valid) {
    _ensure_initialized();
    if (r_valid) *r_valid = false;
    if (p_type < Variant::VARIANT_MAX && _builtin_enums[p_type]) {
        int count = _builtin_enum_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_enums[p_type][i].name) == p_enum) {
                for (int j = 0; j < _builtin_enums[p_type][i].value_count; j++) {
                    if (StringName(_builtin_enums[p_type][i].values[j].name) == p_enumeration) {
                        if (r_valid) *r_valid = true;
                        return (int)_builtin_enums[p_type][i].values[j].value;
                    }
                }
                break;
            }
        }
    }
    return 0;
}

// Constant functions
int VariantExt::get_constants_count_for_type(Variant::Type p_type) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constants[p_type]) {
        return _builtin_constant_counts[p_type];
    }
    return 0;
}

void VariantExt::get_constants_for_type(Variant::Type p_type, List<StringName>* r_constants) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constants[p_type]) {
        int count = _builtin_constant_counts[p_type];
        for (int i = 0; i < count; i++) {
            r_constants->push_back(StringName(_builtin_constants[p_type][i].name));
        }
    }
}

bool VariantExt::has_constant(Variant::Type p_type, const StringName& p_name) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constants[p_type]) {
        int count = _builtin_constant_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_constants[p_type][i].name) == p_name) {
                return true;
            }
        }
    }
    return false;
}

Variant VariantExt::get_constant_value(Variant::Type p_type, const StringName& p_name, bool* r_valid) {
    _ensure_initialized();
    if (p_type < Variant::VARIANT_MAX && _builtin_constants[p_type]) {
        int count = _builtin_constant_counts[p_type];
        for (int i = 0; i < count; i++) {
            if (StringName(_builtin_constants[p_type][i].name) == p_name) {
                if (r_valid) *r_valid = true;
                return _make_variant_from_value_str(
                    _builtin_constants[p_type][i].type_str,
                    _builtin_constants[p_type][i].value_str);
            }
        }
    }
    if (r_valid) *r_valid = false;
    return Variant();
}

} // namespace godot
''')
    write_file(os.path.join(output_dir, 'variant_builtin_ext.gen.cpp'), vb_cpp)
    print("  - Generated variant builtin reflection info")

def generate_core_constants(api_path):
    """Generate core_constants.gen.h/cpp"""
    print("Generating core_constants.gen.h/cpp from {}...".format(api_path))

    with open(api_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    global_constants = data.get('global_constants', [])
    global_enums = data.get('global_enums', [])
    output_dir = get_output_dir()

    all_constants = []
    for c in global_constants:
        all_constants.append({'name': c['name'], 'value': c['value'], 'is_enum': False, 'is_bitfield': False, 'enum_name': ''})
    for enum_def in global_enums:
        enum_name = enum_def['name']
        is_bitfield = enum_def.get('is_bitfield', False)
        for value in enum_def.get('values', []):
            all_constants.append({'name': value['name'], 'value': value['value'], 'is_enum': True, 'is_bitfield': is_bitfield, 'enum_name': enum_name})

    # --- Header ---
    cc_h = io.StringIO()
    cc_h.write('''/**
 * @file core_constants.gen.h
 * @brief Auto-generated list of Godot global constants and enums from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#ifndef GODOTJS_CORE_CONSTANTS_GEN_H
#define GODOTJS_CORE_CONSTANTS_GEN_H

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/list.hpp>

namespace godot {

class CoreConstants {
public:
    static int get_global_constant_count();
    static StringName get_global_constant_enum(int p_idx);
    static bool is_global_constant_bitfield(int p_idx);
    static bool get_ignore_value_in_docs(int p_idx);
    static const char* get_global_constant_name(int p_idx);
    static int64_t get_global_constant_value(int p_idx);
    static bool is_global_constant(const StringName& p_name);
    static int get_global_constant_index(const StringName& p_name);
    static bool is_global_enum(const StringName& p_enum);
    static void get_enum_values(const StringName& p_enum, HashMap<StringName, int64_t>* r_values);
#ifdef TOOLS_ENABLED
    static void get_global_enums(List<StringName>* r_values);
#endif
};

} // namespace godot

#endif // GODOTJS_CORE_CONSTANTS_GEN_H
''')
    write_file(os.path.join(output_dir, 'core_constants.gen.h'), cc_h)

    # --- Implementation ---
    cc_cpp = io.StringIO()
    cc_cpp.write('''/**
 * @file core_constants.gen.cpp
 * @brief Auto-generated list of Godot global constants and enums from extension_api.json
 * @warning This file is auto-generated. Do not edit manually.
 */

#include "core_constants.gen.h"

namespace godot {

struct _CCEntry {
    const char* name;
    int64_t value;
    bool is_enum;
    bool is_bitfield;
    const char* enum_name;
};

static const _CCEntry& _cc_data(int& r_count) {
    static const _CCEntry s_data[] = {
''')

    for c in all_constants:
        val = c['value']
        if isinstance(val, (int, float)):
            val_s = str(int(val))
        else:
            val_s = "0"
        line = "        { \"" + c['name'] + "\", (int64_t)" + val_s + ", " + str(c['is_enum']).lower() + ", " + str(c['is_bitfield']).lower() + ", \"" + c['enum_name'] + "\" },\n"
        cc_cpp.write(line)

    cc_cpp.write("    };\n")
    cc_cpp.write("    static const int s_count = " + str(len(all_constants)) + ";\n")
    cc_cpp.write("    r_count = s_count;\n")
    cc_cpp.write("    return s_data[0];\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("int CoreConstants::get_global_constant_count() {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    _cc_data(n);\n")
    cc_cpp.write("    return n;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("const char* CoreConstants::get_global_constant_name(int p_idx) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    if (p_idx < 0 || p_idx >= n) return \"\";\n")
    cc_cpp.write("    return (&d)[p_idx].name;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("int64_t CoreConstants::get_global_constant_value(int p_idx) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    if (p_idx < 0 || p_idx >= n) return 0;\n")
    cc_cpp.write("    return (&d)[p_idx].value;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("StringName CoreConstants::get_global_constant_enum(int p_idx) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    if (p_idx < 0 || p_idx >= n) return StringName();\n")
    cc_cpp.write("    return StringName((&d)[p_idx].name);\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("bool CoreConstants::is_global_constant_bitfield(int p_idx) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    if (p_idx < 0 || p_idx >= n) return false;\n")
    cc_cpp.write("    return (&d)[p_idx].is_bitfield;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("bool CoreConstants::get_ignore_value_in_docs(int p_idx) {\n")
    cc_cpp.write("    (void)p_idx;\n")
    cc_cpp.write("    return false;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("bool CoreConstants::is_global_constant(const StringName& p_name) {\n")
    cc_cpp.write("    return get_global_constant_index(p_name) >= 0;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("int CoreConstants::get_global_constant_index(const StringName& p_name) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    for (int i = 0; i < n; ++i) {\n")
    cc_cpp.write("        if (StringName((&d)[i].name) == p_name) return i;\n")
    cc_cpp.write("    }\n")
    cc_cpp.write("    return -1;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("bool CoreConstants::is_global_enum(const StringName& p_enum) {\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    for (int i = 0; i < n; ++i) {\n")
    cc_cpp.write("        if ((&d)[i].is_enum && StringName((&d)[i].enum_name) == p_enum) return true;\n")
    cc_cpp.write("    }\n")
    cc_cpp.write("    return false;\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("void CoreConstants::get_enum_values(const StringName& p_enum, HashMap<StringName, int64_t>* r_values) {\n")
    cc_cpp.write("    if (!r_values) return;\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    for (int i = 0; i < n; ++i) {\n")
    cc_cpp.write("        if ((&d)[i].is_enum && StringName((&d)[i].enum_name) == p_enum) {\n")
    cc_cpp.write("            (*r_values)[StringName((&d)[i].name)] = (&d)[i].value;\n")
    cc_cpp.write("        }\n")
    cc_cpp.write("    }\n")
    cc_cpp.write("}\n\n")

    cc_cpp.write("#ifdef TOOLS_ENABLED\n")
    cc_cpp.write("void CoreConstants::get_global_enums(List<StringName>* r_values) {\n")
    cc_cpp.write("    if (!r_values) return;\n")
    cc_cpp.write("    int n = 0;\n")
    cc_cpp.write("    const _CCEntry& d = _cc_data(n);\n")
    cc_cpp.write("    for (int i = 0; i < n; ++i) {\n")
    cc_cpp.write("        if ((&d)[i].is_enum) {\n")
    cc_cpp.write("            r_values->push_back(StringName((&d)[i].enum_name));\n")
    cc_cpp.write("        }\n")
    cc_cpp.write("    }\n")
    cc_cpp.write("}\n")
    cc_cpp.write("#endif\n\n")

    cc_cpp.write("} // namespace godot\n")
    write_file(os.path.join(output_dir, 'core_constants.gen.cpp'), cc_cpp)
    print("  - Generated {} constants/enums".format(len(all_constants)))

def main():
    # Determine API path
    api_path = os.environ.get("GODOT_EXTENSION_API_PATH")
    if not api_path:
        # Default to godot-cpp/gdextension/extension_api.json relative to script location
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(os.path.dirname(script_dir))
        api_path = os.path.join(project_root, "godot-cpp", "gdextension", "extension_api.json")
    
    if not os.path.exists(api_path):
        print("Error: extension_api.json not found at {}".format(api_path))
        sys.exit(1)

    generate_utility_functions_ext(api_path)
    generate_variant_builtin_ext(api_path)
    generate_core_constants(api_path)
    print("Done!")

if __name__ == "__main__":
    main()
