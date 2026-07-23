#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/error_macros.hpp"
#ifdef TOOLS_ENABLED

// editor/api_tool_parser.cpp
// JSON parsing implementation (TOOLS_ENABLED only).
// Parses extension_api.json using godot-cpp PropertyInfo/MethodInfo types.
// All functions return Error with proper error messages.

#include "api_tool_parser.h"
#include "api_tool_store_writer.h"
#include "../api_tool_types.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace api_tool::internal {

// ============================================================================
// Meta string -> GDExtensionClassMethodArgumentMetadata conversion
// ============================================================================

inline GDExtensionClassMethodArgumentMetadata parse_argument_metadata(const StringName &p_meta) {
    String s(p_meta);
    if (s == "int8") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT8;
    if (s == "int16") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT16;
    if (s == "int32") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT32;
    if (s == "int64") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT64;
    if (s == "uint8") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT8;
    if (s == "uint16") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT16;
    if (s == "uint32") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT32;
    if (s == "uint64") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64;
    if (s == "float") return GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_FLOAT;
    if (s == "double") return GDEXTENSION_METHOD_ARGUMENT_METADATA_REAL_IS_DOUBLE;
    if (s == "char16") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR16;
    if (s == "char32") return GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR32;
    return GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
}

inline Variant::Type parse_variant_type(const String &p_type_name, PropertyInfo *p_propinfo = nullptr) {
    using VT = Variant::Type;
    PropertyInfo dummy;
    if (!p_propinfo) p_propinfo = &dummy;

    VT ret = VT::NIL;
    if (p_type_name == "" || p_type_name == "Nil") {
        ret = VT::NIL;
    } else if (p_type_name == "Variant")  {
        ret = VT::NIL;
        p_propinfo->usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
    } else ret =  Variant::get_type_by_name(p_type_name);

    if (ret < Variant::NIL || ret >= Variant::VARIANT_MAX) {
        if (p_type_name.ends_with("*")) {
            ret = VT::INT;
            p_propinfo->hint = PROPERTY_HINT_INT_IS_POINTER;
            p_propinfo->hint_string = p_type_name == "void*" ? String("") : p_type_name.trim_suffix("*");
        } else if (p_type_name.begins_with("enum::")) {
            ret = VT::INT;
            p_propinfo->class_name = p_type_name.trim_prefix("enum::"); // set class_name for object types
            p_propinfo->usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
        } else if (p_type_name.begins_with("bitfield::")) {
            ret = VT::INT;
            p_propinfo->class_name = p_type_name.trim_prefix("bitfield::"); // set class_name for object types
            p_propinfo->usage |= PROPERTY_USAGE_CLASS_IS_BITFIELD;
        } else if (p_type_name.begins_with("typedarray::"))  {
            ret = VT::ARRAY;
            p_propinfo->hint = PROPERTY_HINT_ARRAY_TYPE;
            p_propinfo->hint_string = p_type_name.trim_prefix("typedarray::");
        } else if (p_type_name.begins_with("typeddictionary::")) {
            ret = VT::DICTIONARY;
            const PackedStringArray splits = p_type_name.trim_prefix("typedarray::").split(";");
            p_propinfo->hint = PROPERTY_HINT_DICTIONARY_TYPE;
            p_propinfo->hint_string = p_type_name.trim_prefix("typeddictionary::");   
        } else if (p_type_name.contains(",")) {
            ret = VT::OBJECT;
            p_propinfo->hint = PROPERTY_HINT_RESOURCE_TYPE;
            p_propinfo->hint_string = p_type_name;
        } else if (ClassDB::class_exists(p_type_name)) {
            ret = VT::OBJECT;
            if (p_type_name == "Resource" || ClassDB::is_parent_class(p_type_name, "Resource")) {
                p_propinfo->hint = PROPERTY_HINT_RESOURCE_TYPE;
                p_propinfo->hint_string = p_type_name;
            } else {
                p_propinfo->class_name = p_type_name;
            }
        } else if (p_type_name == "void") {
            ret = VT::NIL;
        } else {
            CRASH_NOW_MSG("[Api Tool] unknown variant type : " + p_type_name);
        }
    }
    return ret;
}

// ============================================================================
// Operator / Constructor / Member
// ============================================================================
// Operator name string -> Variant::Operator conversion
// ============================================================================

inline Variant::Operator parse_operator_name(const StringName &p_name) {
    String s(p_name);
    // comparison
    if (s == "==") return Variant::OP_EQUAL;
    if (s == "!=") return Variant::OP_NOT_EQUAL;
    if (s == "<") return Variant::OP_LESS;
    if (s == "<=") return Variant::OP_LESS_EQUAL;
    if (s == ">") return Variant::OP_GREATER;
    if (s == ">=") return Variant::OP_GREATER_EQUAL;
    // mathematic
    if (s == "+") return Variant::OP_ADD;
    if (s == "-") return Variant::OP_SUBTRACT;
    if (s == "*") return Variant::OP_MULTIPLY;
    if (s == "/") return Variant::OP_DIVIDE;
    if (s == "unary-") return Variant::OP_NEGATE;
    if (s == "unary+") return Variant::OP_POSITIVE;
    if (s == "%") return Variant::OP_MODULE;
    if (s == "**") return Variant::OP_POWER;
    // bitwise
    if (s == "<<") return Variant::OP_SHIFT_LEFT;
    if (s == ">>") return Variant::OP_SHIFT_RIGHT;
    if (s == "&") return Variant::OP_BIT_AND;
    if (s == "|") return Variant::OP_BIT_OR;
    if (s == "^") return Variant::OP_BIT_XOR;
    if (s == "~") return Variant::OP_BIT_NEGATE;
    // logic
    if (s == "and") return Variant::OP_AND;
    if (s == "or") return Variant::OP_OR;
    if (s == "xor") return Variant::OP_XOR;
    if (s == "not") return Variant::OP_NOT;
    // containment
    if (s == "in") return Variant::OP_IN;
    return Variant::OP_EQUAL; // fallback
}

// ============================================================================
// JSON helpers: safe field access (inline wrappers for readability)
// ============================================================================

static inline bool dict_has(const Dictionary &d, const String &key) {
    return d.has(key);
}

static inline Variant dict_get(const Dictionary &d, const String &key, const Variant &def = Variant()) {
    return d.has(key) ? d[key] : def;
}

static inline StringName dict_get_string_name(const Dictionary &d, const String &key) {
    CRASH_COND_MSG(!dict_has(d, key), "[Api Tool] missing key : " + key);
    return StringName(d[key]);
}

// ============================================================================
// Sub-structure parsers: JSON -> godot-cpp PropertyInfo/MethodInfo
// ============================================================================

static PropertyInfo parse_property_info(const Dictionary &d) {
    PropertyInfo pi;
    StringName type_str = dict_get_string_name(d, "type");
    pi.type = parse_variant_type(String(type_str), &pi);
    pi.name = dict_has(d, "name") ? StringName(dict_get_string_name(d, "name")) : StringName();
    // if (is_object_type(type_str) || pi.type == Variant::OBJECT) {
    //     pi.class_name = type_str.trim_prefix("enum::");
    // }
    // hint 和 usage 直接在顶层，不在嵌套的 property 对象中
    // pi.hint = dict_has(d, "hint") ? uint32_t(int32_t(d["hint"])) : PROPERTY_HINT_NONE;
    // pi.hint_string = dict_has(d, "hint_string") ? String(d["hint_string"]) : "";
    // pi.usage = dict_has(d, "usage") ? uint32_t(int64_t(d["usage"])) : PROPERTY_USAGE_DEFAULT;
    return pi;
}

template<typename TApiMethodInfo>
static TApiMethodInfo parse_method(const Dictionary &d, ApiCompatibilityHashData *r_compat_data = nullptr) {
    TApiMethodInfo ami;
    ami.method.name = dict_get_string_name(d, "name");

    // Build flags from JSON booleans
    uint32_t flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    if (dict_has(d, "is_const") && bool(d["is_const"])) flags |= GDEXTENSION_METHOD_FLAG_CONST;
    if (dict_has(d, "is_vararg") && bool(d["is_vararg"])) flags |= GDEXTENSION_METHOD_FLAG_VARARG;
    if (dict_has(d, "is_static") && bool(d["is_static"])) flags |= GDEXTENSION_METHOD_FLAG_STATIC;
    if (dict_has(d, "is_virtual") && bool(d["is_virtual"])) flags |= GDEXTENSION_METHOD_FLAG_VIRTUAL;
    if (dict_has(d, "is_required") && bool(d["is_required"])) flags |= GDEXTENSION_METHOD_FLAG_VIRTUAL_REQUIRED;
    ami.method.flags = flags;

    ami.hash = dict_has(d, "hash") ? MethodHash(d["hash"]) : 0;

#ifndef DISABLE_DEPRECATED
    if (dict_has(d, "hash_compatibility")) {
        Array compat = d["hash_compatibility"];
        if (r_compat_data) {
            ApiMethodCompatibilityHashes mch;
            mch.method_name = ami.method.name;
            mch.hashes.reserve(compat.size());
            for (int i = 0; i < compat.size(); i++) {
                mch.hashes.push_back(MethodHash(compat[i]));
            }
            r_compat_data->methods.push_back(mch);
        }
    }
#endif // DISABLE_DEPRECATED

    // return_val: builtin_classes uses "return_type", classes uses "return_value"
    if (dict_has(d, "return_value")) {
        Dictionary rv = d["return_value"];
        ami.method.return_val = parse_property_info(rv);
        if (dict_has(rv, "meta")) {
            ami.method.return_val_metadata = parse_argument_metadata(dict_get_string_name(rv, "meta"));
        }
    } else if (dict_has(d, "return_type")) {
        // Builtin class methods use "return_type" directly (not nested "return_value")
        StringName ret_type = dict_get_string_name(d, "return_type");
        ami.method.return_val.type = parse_variant_type(String(ret_type), &ami.method.return_val);
        if (dict_has(d, "meta")) {
            ami.method.return_val_metadata = parse_argument_metadata(dict_get_string_name(d, "meta"));
        }
    }

    // arguments
    if (dict_has(d, "arguments")) {
        Array args = d["arguments"];
        ami.method.arguments.reserve(args.size());
        ami.method.arguments_metadata.reserve(args.size());
        LocalVector<Variant> default_args_tmp;
        for (int i = 0; i < args.size(); i++) {
            Dictionary ad = args[i];
            PropertyInfo pi = parse_property_info(ad);
            ami.method.arguments.push_back(pi);
            if (dict_has(ad, "meta")) {
                ami.method.arguments_metadata.push_back(parse_argument_metadata(dict_get_string_name(ad, "meta")));
            } else {
                ami.method.arguments_metadata.push_back(GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE);
            }
            // Parse default_value for optional trailing arguments.
            // JSON stores these as string representations (e.g. "0", "true", "PackedByteArray()").
            if (dict_has(ad, "default_value")) {
                String dv_str = String(ad["default_value"]);
                // For STRING-typed args, the value IS the literal string (JSON unescaped).
                // For all other types, str_to_var converts Godot's string representation back to Variant.
                if (pi.type == Variant::STRING) {
                    default_args_tmp.push_back(Variant(dv_str));
                } else {
                    default_args_tmp.push_back(UtilityFunctions::str_to_var(dv_str));
                }
            }
        }
        // MethodInfo default_arguments contains only the trailing defaults.
        for (uint32_t i = 0; i < default_args_tmp.size(); i++) {
            ami.method.default_arguments.push_back(default_args_tmp[i]);
        }
    }
    return ami;
}

static ApiEnumInfo parse_enum(const Dictionary &d) {
    ApiEnumInfo info;
    info.name = dict_get_string_name(d, "name");
    info.is_bitfield = dict_has(d, "is_bitfield") ? bool(d["is_bitfield"]) : false;
    if (dict_has(d, "values")) {
        Array values = d["values"];
        info.values.reserve(values.size());
        for (int i = 0; i < values.size(); i++) {
            Dictionary ev = values[i];
            ApiEnumValue v;
            v.name = dict_get_string_name(ev, "name");
            v.value = int64_t(ev["value"]);
            info.values.push_back(v);
        }
    }
    return info;
}

static ApiSignalInfo parse_signal(const Dictionary &d) {
    ApiSignalInfo info;
    info.name = dict_get_string_name(d, "name");
    if (dict_has(d, "arguments")) {
        Array args = d["arguments"];
        info.arguments.reserve(args.size());
        for (int i = 0; i < args.size(); i++) {
            info.arguments.push_back(parse_property_info(Dictionary(args[i])));
        }
    }
    return info;
}

static ApiPropertyInfo parse_api_property(const Dictionary &d) {
    ApiPropertyInfo info;
    info.property = parse_property_info(d);
    if (!info.property.name.is_empty()) {
        info.setter = dict_has(d,"setter")? dict_get_string_name(d, "setter") :"";
        info.getter = dict_has(d,"getter")? dict_get_string_name(d, "getter") : "";
        info.index = dict_has(d, "index") ? int32_t(int64_t(d["index"])) : -1;
    }
    return info;
}

static ApiOperatorInfo parse_operator(const Dictionary &d) {
    ApiOperatorInfo info;
    String name_str = dict_get_string_name(d, "name");
    info.op = parse_operator_name(name_str);
    info.return_type = parse_variant_type(String(dict_get_string_name(d, "return_type")));
    if (dict_has(d, "left_type")) {
        info.left_type = parse_variant_type(String(dict_get_string_name(d, "left_type")));
    }
    if (dict_has(d, "right_type")) {
        info.right_type = parse_variant_type(String(dict_get_string_name(d, "right_type")));
    }
    return info;
}

static ApiConstructorInfo parse_constructor(const Dictionary &d) {
    ApiConstructorInfo info;
    if (dict_has(d, "arguments")) {
        Array args = d["arguments"];
        info.arguments.reserve(args.size());
        for (int i = 0; i < args.size(); i++) {
            info.arguments.push_back(parse_property_info(Dictionary(args[i])));
        }
    }
    return info;
}

static ApiMemberInfo parse_member(const Dictionary &d) {
    ApiMemberInfo info;
    // builtin_classes members use "name" and "type" fields
    info.name = dict_get_string_name(d, "name");
    info.type = parse_variant_type(String(dict_get_string_name(d, "type")));
    return info;
}

// ============================================================================
// Directory preparation
// ============================================================================

Error ApiParser::prepare_output_dirs(const String &p_output_dir) {
    DirAccess::make_dir_recursive_absolute(p_output_dir);

    const char *subdirs[] = {
        DIR_UTILITY_FUNCTIONS,
        DIR_BUILTIN_CLASSES,
        DIR_CLASSES,
        DIR_GLOBAL_ENUMS,
        DIR_GLOBAL_CONSTANTS,
        DIR_SINGLETONS,
        DIR_NATIVE_STRUCTURES,
        // Document subdirectories (sharded by entity type)
        DIR_DOC_CLASSES,
        DIR_DOC_BUILTIN_CLASSES,
        DIR_DOC_UTILITY_FUNCTIONS,
        DIR_DOC_GLOBAL_ENUMS,
        DIR_DOC_GLOBAL_CONSTANTS,
#ifndef DISABLE_DEPRECATED
        // Compatibility hashes subdirectory
        DIR_COMPAT_HASHES,
#endif // DISABLE_DEPRECATED
    };
    for (const char *subdir : subdirs) {
        String path = p_output_dir + String("/") + subdir;
        if (DirAccess::dir_exists_absolute(path)) {
            PackedStringArray files = DirAccess::get_files_at(path);
            for (int i = 0; i < files.size(); i++) {
                DirAccess::remove_absolute(path + String("/") + files[i]);
            }
        }
        DirAccess::make_dir_recursive_absolute(path);
    }

    return OK;
}

// ============================================================================
// Header parser
// ============================================================================

Error ApiParser::parse_and_write_header(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "header"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'header' section");

    Dictionary hdr = p_root["header"];
    ApiHeader header;
    header.version_major = int32_t(dict_get(hdr, "version_major", 0));
    header.version_minor = int32_t(dict_get(hdr, "version_minor", 0));
    header.version_patch = int32_t(dict_get(hdr, "version_patch", 0));
    header.version_status = String(dict_get(hdr, "version_status", String()));
    header.version_build = String(dict_get(hdr, "version_build", String()));
    header.version_full_name = String(dict_get(hdr, "version_full_name", String()));
    header.precision = String(dict_get(hdr, "precision", String())) == "double"? RealPrecision::DOUBLE : RealPrecision::SINGLE;

    String path = p_output_dir + String("/") + String(FILE_HEADER);
    return ApiStoreWriter::write_header(path, header);
}

// ============================================================================
// Utility Functions parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_utility_functions(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "utility_functions"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'utility_functions' section");

    Array funcs = p_root["utility_functions"];
    LocalVector<ApiUtilityFunction> all_funcs;
    all_funcs.reserve(funcs.size());

    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_UTILITY_FUNCTIONS);

    for (int i = 0; i < funcs.size(); i++) {
        Dictionary fd = funcs[i];
        ApiUtilityFunction func;
        func.method.name = dict_get_string_name(fd, "name");

        // Build flags
        uint32_t flags = GDEXTENSION_METHOD_FLAG_NORMAL;
        if (dict_has(fd, "is_vararg") && bool(fd["is_vararg"])) flags |= GDEXTENSION_METHOD_FLAG_VARARG;
        func.method.flags = flags;

        func.hash = dict_has(fd, "hash") ? MethodHash(fd["hash"]) : 0;
        func.category = dict_get_string_name(fd, "category");

        // return_type -> return_val PropertyInfo
        if (dict_has(fd, "return_type")) {
            StringName ret_type = dict_get_string_name(fd, "return_type");
            func.method.return_val.type = parse_variant_type(String(ret_type), &func.method.return_val);
            // if (is_object_type(ret_type)) {
            //     func.method.return_val.class_name = ret_type.trim_prefix("enum::");
            // }
        }

        if (dict_has(fd, "arguments")) {
            Array args = fd["arguments"];
            func.method.arguments.reserve(args.size());
            func.method.arguments_metadata.reserve(args.size());
            for (int j = 0; j < args.size(); j++) {
                Dictionary ad = args[j];
                func.method.arguments.push_back(parse_property_info(ad));
                if (dict_has(ad, "meta")) {
                    func.method.arguments_metadata.push_back(parse_argument_metadata(dict_get_string_name(ad, "meta")));
                } else {
                    func.method.arguments_metadata.push_back(GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE);
                }
            }
        }

        all_funcs.push_back(func);

        // Write document file (single pass, no separate document parsing)
        ApiUtilityFunctionDocument doc;
        doc.name = String(func.method.name);
        if (dict_has(fd, "description")) {
            doc.description = String(fd["description"]);
        }
        String doc_path = doc_dir + String("/") + String(func.method.name) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_utility_function_document(doc_path, doc);
    }

    String path = p_output_dir + String("/") + String(FILE_UTILITY_FUNCTIONS);
    return ApiStoreWriter::write_utility_functions(path, all_funcs);
}

// ============================================================================
// Builtin Types parser
// ============================================================================
static bool sort_by_key(const Dictionary&a, const Dictionary&b, const String&key){return a[key] < b[key];};

Error ApiParser::parse_and_write_builtin_classes(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "builtin_classes"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'builtin_classes' section");

    Array classes = p_root["builtin_classes"];
    String dir = p_output_dir + String("/") + String(DIR_BUILTIN_CLASSES);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_BUILTIN_CLASSES);
    String compat_dir = p_output_dir + String("/") + String(DIR_COMPAT_HASHES);
    Error overall = OK;

    for (int i = 0; i < classes.size(); i++) {
        Dictionary cd = classes[i];
        ApiBuiltinClass bt;
        bt.type = parse_variant_type(dict_get_string_name(cd, "name"));

        if (dict_has(cd, "indexing_return_type")) {
            bt.has_indexing_return_type = true;
            bt.indexing_type = parse_variant_type(String(dict_get_string_name(cd, "indexing_return_type")));
        }
        bt.is_keyed = dict_has(cd, "is_keyed") ? bool(cd["is_keyed"]) : false;
        bt.has_destructor = dict_has(cd, "has_destructor") ? bool(cd["has_destructor"]) : false;

        // Build document in parallel
        ApiClassDocument doc;
        doc.name = Variant::get_type_name(bt.type);
        if (dict_has(cd, "description")) {
            doc.description = String(cd["description"]);
        }
        if (dict_has(cd, "brief_description")) {
            doc.brief_description = String(cd["brief_description"]);
        }

        // Collect compatibility hashes for this builtin class
        ApiCompatibilityHashData compat_data;

        if (dict_has(cd, "members")) {
            Array members = cd["members"];
            bt.members.reserve(members.size());
            doc.properties.reserve(members.size());
            for (int j = 0; j < members.size(); j++) {
                Dictionary md = members[j];
                bt.members.push_back(parse_member(md));
                ApiPropertyDocument pdoc;
                pdoc.name = String(dict_get_string_name(md, "name"));
                if (dict_has(md, "description")) {
                    pdoc.description = String(md["description"]);
                }
                doc.properties.push_back(pdoc);
            }
        }

        if (dict_has(cd, "constants")) {
            Array constants = cd["constants"];
            bt.constants.reserve(constants.size());
            doc.constants.reserve(constants.size());
            for (int j = 0; j < constants.size(); j++) {
                Dictionary c = constants[j];
                ApiBuiltInClassConstantInfo ci;
                ci.name = dict_get_string_name(c, "name");
                String type_str = dict_get_string_name(c, "type");
                ci.type = parse_variant_type(type_str);
                String value_str = dict_has(c, "value") ? String(c["value"]) : "";
                ci.value = UtilityFunctions::str_to_var(value_str);
                CRASH_COND_MSG(ci.value.get_type() != ci.type, String("Parse error, Invalid var string: ") + value_str);
                CRASH_COND_MSG(UtilityFunctions::var_to_str(ci.value) != value_str, vformat("Parse error: '%s' VS '%s'", value_str, UtilityFunctions::var_to_str(ci.value)));
                bt.constants.push_back(ci);
                ApiConstantDocument cdoc;
                cdoc.name = String(ci.name);
                if (dict_has(c, "description")) {
                    cdoc.description = String(c["description"]);
                }
                doc.constants.push_back(cdoc);
            }
        }

        if (dict_has(cd, "enums")) {
            Array enums = cd["enums"];
            bt.enums.reserve(enums.size());
            for (int j = 0; j < enums.size(); j++) {
                Dictionary ed = enums[j];
                bt.enums.push_back(parse_enum(ed));
                // Build enum document
                ApiEnumDocument edoc;
                edoc.name = String(dict_get_string_name(ed, "name"));
                if (dict_has(ed, "values")) {
                    Array values = ed["values"];
                    edoc.values.reserve(values.size());
                    for (int k = 0; k < values.size(); k++) {
                        Dictionary ev = values[k];
                        ApiEnumValueDocument evdoc;
                        evdoc.name = String(dict_get_string_name(ev, "name"));
                        if (dict_has(ev, "description")) {
                            evdoc.description = String(ev["description"]);
                        }
                        edoc.values.push_back(evdoc);
                    }
                }
                doc.enums.push_back(edoc);
            }
        }

        if (dict_has(cd, "methods")) {
            Array methods = cd["methods"];
            bt.methods.reserve(methods.size());
            doc.methods.reserve(methods.size());
            for (int j = 0; j < methods.size(); j++) {
                Dictionary md = methods[j];
                ApiBuiltInMethod mbi = parse_method<ApiBuiltInMethod>(md, &compat_data);
                bt.methods.push_back(mbi);
                ApiMethodDocument mdoc;
                mdoc.name = String(dict_get_string_name(md, "name"));
                if (dict_has(md, "description")) {
                    mdoc.description = String(md["description"]);
                }
                doc.methods.push_back(mdoc);
            }
        }

        if (dict_has(cd, "operators")) {
            Array operators = cd["operators"];
            bt.operators.reserve(operators.size());
            doc.operators.reserve(operators.size());
            for (int j = 0; j < operators.size(); j++) {
                Dictionary od = operators[j];
                bt.operators.push_back(parse_operator(od));
                ApiOperatorDocument odoc;
                odoc.name = String(dict_get_string_name(od, "name"));
                if (dict_has(od, "description")) {
                    odoc.description = String(od["description"]);
                }
                doc.operators.push_back(odoc);
            }
        }

        if (dict_has(cd, "constructors")) {
            Array constructors = cd["constructors"];
            bt.constructors.reserve(constructors.size());
            doc.constructors.reserve(constructors.size());
            constructors.sort_custom(callable_mp_static(sort_by_key).bind("index"));
            for (int j = 0; j < constructors.size(); j++) {
                Dictionary ctor_d = constructors[j];
                bt.constructors.push_back(parse_constructor(ctor_d));
                ApiConstructorDocument cdoc;
                if (dict_has(ctor_d, "description")) {
                    cdoc.description = String(ctor_d["description"]);
                }
                doc.constructors.push_back(cdoc);
            }
        }

        // Write main data file
        String path = dir + String("/") + Variant::get_type_name(bt.type) + String(FILE_EXT_DATA);
        Error err = ApiStoreWriter::write_builtin_class(path, bt);
        if (err != OK) {
            ERR_PRINT("[API Tool] Failed to write builtin type: " + Variant::get_type_name(bt.type));
            overall = err;
        }

#ifndef DISABLE_DEPRECATED
        // Write compatibility hashes file (only if data exists)
        if (compat_data.methods.size() > 0) {
            String compat_path = compat_dir + String("/") + Variant::get_type_name(bt.type) + String(FILE_EXT_COMPAT);
            ApiStoreWriter::write_compatibility_hashes(compat_path, compat_data);
        }
#endif // DISABLE_DEPRECATED

        // Write document file
        String doc_path = doc_dir + String("/") + Variant::get_type_name(bt.type) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_document(doc_path, doc);
    }

    return overall;
}

// ============================================================================
// Classes parser
// ============================================================================

Error ApiParser::parse_and_write_classes(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "classes"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'classes' section");

    Array classes = p_root["classes"];
    String dir = p_output_dir + String("/") + String(DIR_CLASSES);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_CLASSES);
    String compat_dir = p_output_dir + String("/") + String(DIR_COMPAT_HASHES);
    Error overall = OK;

    for (int i = 0; i < classes.size(); i++) {
        Dictionary cd = classes[i];
        ApiClass cls;
        cls.name = dict_get_string_name(cd, "name");
        cls.inherits = dict_has(cd, "inherits") ? dict_get_string_name(cd, "inherits") : "";
        cls.api_type = dict_get_string_name(cd, "api_type");
        cls.is_refcounted = dict_has(cd, "is_refcounted") ? bool(cd["is_refcounted"]) : false;
        cls.is_instantiable = dict_has(cd, "is_instantiable") ? bool(cd["is_instantiable"]) : true;

        // Build document in parallel
        ApiClassDocument doc;
        doc.name = String(cls.name);
        if (dict_has(cd, "description")) {
            doc.description = String(cd["description"]);
        }
        if (dict_has(cd, "brief_description")) {
            doc.brief_description = String(cd["brief_description"]);
        }

        // Collect compatibility hashes for this class
        ApiCompatibilityHashData compat_data;

        if (dict_has(cd, "methods")) {
            Array methods = cd["methods"];
            cls.methods.reserve(methods.size());
            doc.methods.reserve(methods.size());
            for (int j = 0; j < methods.size(); j++) {
                Dictionary md = methods[j];
                cls.methods.push_back(parse_method<ApiClassMethod>(md, &compat_data));
                ApiMethodDocument mdoc;
                mdoc.name = String(dict_get_string_name(md, "name"));
                if (dict_has(md, "description")) {
                    mdoc.description = String(md["description"]);
                }
                doc.methods.push_back(mdoc);
            }
        }

        if (dict_has(cd, "signals")) {
            Array signals = cd["signals"];
            cls.signals.reserve(signals.size());
            doc.signals.reserve(signals.size());
            for (int j = 0; j < signals.size(); j++) {
                Dictionary sd = signals[j];
                cls.signals.push_back(parse_signal(sd));
                ApiSignalDocument sdoc;
                sdoc.name = String(dict_get_string_name(sd, "name"));
                if (dict_has(sd, "description")) {
                    sdoc.description = String(sd["description"]);
                }
                if (dict_has(sd, "arguments")) {
                    Array args = sd["arguments"];
                    sdoc.arguments.reserve(args.size());
                    for (int k = 0; k < args.size(); k++) {
                        sdoc.arguments.push_back(parse_property_info(Dictionary(args[k])));
                    }
                }
                doc.signals.push_back(sdoc);
            }
        }

        if (dict_has(cd, "properties")) {
            Array properties = cd["properties"];
            cls.properties.reserve(properties.size());
            doc.properties.reserve(properties.size());
            for (int j = 0; j < properties.size(); j++) {
                Dictionary pd = properties[j];
                cls.properties.push_back(parse_api_property(pd));
                ApiPropertyDocument pdoc;
                pdoc.name = String(dict_get_string_name(pd, "name"));
                if (dict_has(pd, "description")) {
                    pdoc.description = String(pd["description"]);
                }
                doc.properties.push_back(pdoc);
            }
        }

        if (dict_has(cd, "enums")) {
            Array enums = cd["enums"];
            cls.enums.reserve(enums.size());
            doc.enums.reserve(enums.size());
            for (int j = 0; j < enums.size(); j++) {
                Dictionary ed = enums[j];
                cls.enums.push_back(parse_enum(ed));
                ApiEnumDocument edoc;
                edoc.name = String(dict_get_string_name(ed, "name"));
                if (dict_has(ed, "values")) {
                    Array values = ed["values"];
                    edoc.values.reserve(values.size());
                    for (int k = 0; k < values.size(); k++) {
                        Dictionary ev = values[k];
                        ApiEnumValueDocument evdoc;
                        evdoc.name = String(dict_get_string_name(ev, "name"));
                        if (dict_has(ev, "description")) {
                            evdoc.description = String(ev["description"]);
                        }
                        edoc.values.push_back(evdoc);
                    }
                }
                doc.enums.push_back(edoc);
            }
        }

        if (dict_has(cd, "constants")) {
            Array constants = cd["constants"];
            cls.constants.reserve(constants.size());
            for (int j = 0; j < constants.size(); j++) {
                Dictionary c = constants[j];
                ApiConstantInfo ci;
                ci.name = dict_get_string_name(c, "name");
                ci.value = dict_has(c, "value") ? int64_t(c["value"]) : 0;
                cls.constants.push_back(ci);
            }
        }

        // Write main data file
        String path = dir + String("/") + String(cls.name) + String(FILE_EXT_DATA);
        Error err = ApiStoreWriter::write_class(path, cls);
        if (err != OK) {
            ERR_PRINT("[API Tool] Failed to write class: " + String(cls.name));
            overall = err;
        }

#ifndef DISABLE_DEPRECATED
        // Write compatibility hashes file (only if data exists)
        if (compat_data.methods.size() > 0) {
            String compat_path = compat_dir + String("/") + String(cls.name) + String(FILE_EXT_COMPAT);
            ApiStoreWriter::write_compatibility_hashes(compat_path, compat_data);
        }
#endif // DISABLE_DEPRECATED

        // Write document file
        String doc_path = doc_dir + String("/") + String(cls.name) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_document(doc_path, doc);
    }

    return overall;
}

// ============================================================================
// Global Enums parser
// ============================================================================

Error ApiParser::parse_and_write_global_enums(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "global_enums"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'global_enums' section");

    Array enums = p_root["global_enums"];
    String dir = p_output_dir + String("/") + String(DIR_GLOBAL_ENUMS);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_GLOBAL_ENUMS);
    Error overall = OK;

    for (int i = 0; i < enums.size(); i++) {
        Dictionary ed = enums[i];
        ApiEnumInfo info = parse_enum(Dictionary(ed));

        // Write main data file
        String path = dir + String("/") + String(info.name) + String(FILE_EXT_DATA);
        Error err = ApiStoreWriter::write_global_enum(path, info);
        if (err != OK) {
            ERR_PRINT("[API Tool] Failed to write enum: " + String(info.name));
            overall = err;
        }

        // Write document file
        ApiGlobalEnumDocument doc;
        doc.name = String(info.name);
        if (dict_has(ed, "values")) {
            Array values = ed["values"];
            doc.values.reserve(values.size());
            for (int k = 0; k < values.size(); k++) {
                Dictionary ev = values[k];
                ApiEnumValueDocument evdoc;
                evdoc.name = String(dict_get_string_name(ev, "name"));
                if (dict_has(ev, "description")) {
                    evdoc.description = String(ev["description"]);
                }
                doc.values.push_back(evdoc);
            }
        }
        String doc_path = doc_dir + String("/") + String(info.name) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_global_enum_document(doc_path, doc);
    }

    return overall;
}

// ============================================================================
// Global Constants parser
// ============================================================================

Error ApiParser::parse_and_write_global_constants(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "global_constants"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'global_constants' section");

    Array constants = p_root["global_constants"];
    String dir = p_output_dir + String("/") + String(DIR_GLOBAL_CONSTANTS);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_GLOBAL_CONSTANTS);
    Error overall = OK;

    for (int i = 0; i < constants.size(); i++) {
        Dictionary cd = constants[i];
        ApiConstantInfo info;
        info.name = dict_get_string_name(cd, "name");
        info.value = int64_t(cd["value"]);
        info.is_bitfield = dict_has(cd, "is_bitfield") ? bool(cd["is_bitfield"]) : false;

        // Write main data file
        String path = dir + String("/") + String(info.name) + String(FILE_EXT_DATA);
        Error err = ApiStoreWriter::write_global_constant(path, info);
        if (err != OK) {
            ERR_PRINT("[API Tool] Failed to write constant: " + String(info.name));
            overall = err;
        }

        // Write document file
        ApiGlobalConstantDocument doc;
        doc.name = String(info.name);
        if (dict_has(cd, "description")) {
            doc.description = String(cd["description"]);
        }
        String doc_path = doc_dir + String("/") + String(info.name) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_global_constant_document(doc_path, doc);
    }

    return overall;
}

// ============================================================================
// Singletons parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_singletons(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "singletons"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'singletons' section");

    Array singletons = p_root["singletons"];
    LocalVector<ApiSingleton> all_singletons;
    all_singletons.reserve(singletons.size());

    for (int i = 0; i < singletons.size(); i++) {
        Dictionary sd = singletons[i];
        ApiSingleton singleton;
        singleton.name = dict_get_string_name(sd, "name");
        singleton.type = dict_get_string_name(sd, "type");
        singleton.user_created = dict_has(sd, "user_created") ? bool(sd["user_created"]) : false;
        singleton.editor_only = dict_has(sd, "editor_only") ? bool(sd["editor_only"]) : false;
        all_singletons.push_back(singleton);
    }

    String path = p_output_dir + String("/") + String(DIR_SINGLETONS) + String("/singletons") + String(FILE_EXT_DATA);
    return ApiStoreWriter::write_singletons(path, all_singletons);
}

// ============================================================================
// Native Structures parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_native_structures(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!dict_has(p_root, "native_structures"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'native_structures' section");

    Array structs = p_root["native_structures"];
    LocalVector<ApiNativeStructure> all_structs;
    all_structs.reserve(structs.size());

    for (int i = 0; i < structs.size(); i++) {
        Dictionary sd = structs[i];
        ApiNativeStructure ns;
        ns.name = String(sd["name"]);
        ns.format = String(sd["format"]);
        all_structs.push_back(ns);
    }

    String path = p_output_dir + String("/") + String(DIR_NATIVE_STRUCTURES) + String("/native_structures") + String(FILE_EXT_DATA);
    return ApiStoreWriter::write_native_structures(path, all_structs);
}

// ============================================================================
// Main entry point
// ============================================================================

Error ApiParser::generate(const Dictionary &p_json_root, const String &p_output_dir) {
    Error err = prepare_output_dirs(p_output_dir);
    ERR_FAIL_COND_V_MSG(err, err, "[API Tool] " + UtilityFunctions::error_string(err) + ": Failed to prepare output directories");

    err = parse_and_write_header(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_utility_functions(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_builtin_classes(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_classes(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_global_enums(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_global_constants(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_singletons(p_json_root, p_output_dir);
    if (err != OK) return err;

    err = parse_and_write_native_structures(p_json_root, p_output_dir);
    if (err != OK) return err;

    UtilityFunctions::print("[API Tool] API dump generated successfully to: " + p_output_dir);
    return OK;
}

} // namespace api_tool

#endif // TOOLS_ENABLED