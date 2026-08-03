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

ClassDB::APIType parse_class_api_type(const String &p_api_type) {
    if (p_api_type == "core") return ClassDB::APIType::API_CORE;
    else if (p_api_type == "editor") return ClassDB::APIType::API_EDITOR;
    else if (p_api_type == "extension") return ClassDB::APIType::API_EXTENSION;
    else if (p_api_type == "editor_extension") return ClassDB::APIType::API_EDITOR_EXTENSION;
    else return ClassDB::APIType::API_NONE;
}

// ============================================================================
// Meta string -> GDExtensionClassMethodArgumentMetadata conversion
// ============================================================================

GDExtensionClassMethodArgumentMetadata parse_argument_metadata(const String &p_meta) {
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

Variant::Type parse_variant_type(const String &p_type_name, PropertyInfo *p_propinfo = nullptr) {
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
    p_propinfo->type = ret;
    return ret;
}

// ============================================================================
// Operator / Constructor / Member
// ============================================================================
// Operator name string -> Variant::Operator conversion
// ============================================================================

Variant::Operator parse_operator_name(const String &p_name) {
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
// Sub-structure parsers: JSON -> godot-cpp PropertyInfo/MethodInfo
// ============================================================================

static PropertyInfo parse_property_info(const Dictionary &d) {
    PropertyInfo pi;
    pi.type = parse_variant_type(d.get("type", ""), &pi);
    pi.name = d.get("name", "");
    return pi;
}

template<typename TApiMethodInfo>
static TApiMethodInfo parse_method(const Dictionary &d, ApiCompatibilityHashData *r_compat_data = nullptr) {
    TApiMethodInfo ami;
    ami.method.name = d["name"];

    // Build flags from JSON booleans
    uint32_t flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    if (d.get("is_const", false)) flags |= GDEXTENSION_METHOD_FLAG_CONST;
    if (d.get("is_vararg", false)) flags |= GDEXTENSION_METHOD_FLAG_VARARG;
    if (d.get("is_static", false)) flags |= GDEXTENSION_METHOD_FLAG_STATIC;
    if (d.get("is_virtual", false)) flags |= GDEXTENSION_METHOD_FLAG_VIRTUAL;
    if (d.get("is_required", false)) flags |= GDEXTENSION_METHOD_FLAG_VIRTUAL_REQUIRED;
    ami.method.flags = flags;

    ami.hash = MethodHash(d.get("hash", 0));

#ifndef DISABLE_DEPRECATED
    if (d.has("hash_compatibility")) {
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
    if (d.has("return_value")) {
        Dictionary rv = d["return_value"];
        ami.method.return_val = parse_property_info(rv);
        ami.method.return_val_metadata = parse_argument_metadata(d.get("meta", ""));
    } else if (d.has("return_type")) {
        // Builtin class methods use "return_type" directly (not nested "return_value")
        String ret_type = d.get("return_type", "");
        ami.method.return_val.type = parse_variant_type(ret_type, &ami.method.return_val);
        ami.method.return_val_metadata = parse_argument_metadata(d.get("meta", ""));
    }

    // arguments
    if (d.has("arguments")) {
        Array args = d["arguments"];
        ami.method.arguments.reserve(args.size());
        ami.method.arguments_metadata.reserve(args.size());
        for (int i = 0; i < args.size(); i++) {
            Dictionary ad = args[i];
            PropertyInfo pi = parse_property_info(ad);
            ami.method.arguments.push_back(pi);
            ami.method.arguments_metadata.push_back(parse_argument_metadata(ad.get("meta", "")));
            // Parse default_value for optional trailing arguments.
            // JSON stores these as string representations (e.g. "0", "true", "PackedByteArray()").
            if (ad.has("default_value")) {
                String dv_str = String(ad["default_value"]);
                // For STRING-typed args, the value IS the literal string (JSON unescaped).
                // For all other types, str_to_var converts Godot's string representation back to Variant.
                if (pi.type == Variant::STRING) {
                    ami.method.default_arguments.push_back(Variant(dv_str));
                } else {
                    ami.method.default_arguments.push_back(UtilityFunctions::str_to_var(dv_str));
                }
            }
        }
    }
    return ami;
}

static ApiEnumInfo parse_enum(const Dictionary &d) {
    ApiEnumInfo info;
    info.name = d["name"];
    info.is_bitfield = d.get("is_bitfield", false);
    if (d.has("values")) {
        Array values = d["values"];
        info.values.reserve(values.size());
        for (int i = 0; i < values.size(); i++) {
            Dictionary ev = values[i];
            ApiEnumValue v;
            v.name = ev["name"];
            v.value = ev["value"];
            info.values.push_back(v);
        }
    }
    return info;
}

static ApiSignalInfo parse_signal(const Dictionary &d) {
    ApiSignalInfo info;
    info.name = d["name"];
    if (d.has("arguments")) {
        Array args = d["arguments"];
        info.arguments.reserve(args.size());
        for (int i = 0; i < args.size(); i++) {
            info.arguments.push_back(parse_property_info(args[i]));
        }
    }
    return info;
}

static ApiPropertyInfo parse_api_property(const Dictionary &d) {
    ApiPropertyInfo info;
    info.property = parse_property_info(d);
    if (!info.property.name.is_empty()) {
        info.setter = d.get("setter", "");
        info.getter = d.get("getter", "");
        info.index = d.get("index", -1);
    }
    return info;
}

static ApiOperatorInfo parse_operator(const Dictionary &d) {
    ApiOperatorInfo info;
    String name_str = d["name"];
    info.op = parse_operator_name(name_str);
    info.return_type = parse_variant_type(d.get("return_type", ""));
    info.left_type = parse_variant_type(d.get("left_type", ""));
    info.right_type = parse_variant_type(d.get("right_type", ""));
    return info;
}

static ApiConstructorInfo parse_constructor(const Dictionary &d) {
    ApiConstructorInfo info;
    if (d.has("arguments")) {
        Array args = d["arguments"];
        info.arguments.reserve(args.size());
        for (int i = 0; i < args.size(); i++) {
            info.arguments.push_back(parse_property_info(args[i]));
        }
    }
    return info;
}

static ApiMemberInfo parse_member(const Dictionary &d) {
    ApiMemberInfo info;
    // builtin_classes members use "name" and "type" fields
    info.name = d["name"];
    info.type = parse_variant_type(d.get("type", ""));
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
    ERR_FAIL_COND_V_MSG(!p_root.has("header"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'header' section");

    Dictionary hdr = p_root["header"];
    ApiHeader header;
    header.version_major = hdr.get("version_major", 0);
    header.version_minor = hdr.get("version_minor", 0);
    header.version_patch = hdr.get("version_patch", 0);
    header.version_status = hdr.get("version_status", "");
    header.version_status = hdr.get("version_status", "");
    header.version_build = hdr.get("version_build", "");
    header.version_full_name = hdr.get("version_full_name", "");
    header.precision = hdr.get("precision", "") == "double"? RealPrecision::DOUBLE : RealPrecision::SINGLE;

    String path = p_output_dir + String("/") + String(FILE_HEADER);
    return ApiStoreWriter::write_header(path, header);
}

// ============================================================================
// Utility Functions parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_utility_functions(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!p_root.has("utility_functions"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'utility_functions' section");

    Array funcs = p_root["utility_functions"];
    LocalVector<ApiUtilityFunction> all_funcs;
    all_funcs.reserve(funcs.size());

    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_UTILITY_FUNCTIONS);

    for (int i = 0; i < funcs.size(); i++) {
        Dictionary fd = funcs[i];
        ApiUtilityFunction func;
        func.method.name = fd["name"];

        // Build flags
        if (fd.get("is_vararg", false)) func.method.flags |= GDEXTENSION_METHOD_FLAG_VARARG;

        func.hash = MethodHash(fd.get("hash", 0));
        func.category = fd.get("category", "");

        // return_type -> return_val PropertyInfo
        if (fd.has("return_type")) {
            String ret_type = fd.get("return_type", "");
            func.method.return_val.type = parse_variant_type(ret_type, &func.method.return_val);
        }

        if (fd.has("arguments")) {
            Array args = fd["arguments"];
            func.method.arguments.reserve(args.size());
            func.method.arguments_metadata.reserve(args.size());
            for (int j = 0; j < args.size(); j++) {
                Dictionary ad = args[j];
                func.method.arguments.push_back(parse_property_info(ad));
                func.method.arguments_metadata.push_back(parse_argument_metadata(ad.get("meta", "")));
            }
        }

        all_funcs.push_back(func);

        // Write document file (single pass, no separate document parsing)
        ApiUtilityFunctionDocument doc;
        doc.name = String(func.method.name);
        doc.description = fd.get("description", "");
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
    ERR_FAIL_COND_V_MSG(!p_root.has("builtin_classes"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'builtin_classes' section");

    Array classes = p_root["builtin_classes"];
    String dir = p_output_dir + String("/") + String(DIR_BUILTIN_CLASSES);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_BUILTIN_CLASSES);
    String compat_dir = p_output_dir + String("/") + String(DIR_COMPAT_HASHES);
    Error overall = OK;

    for (int i = 0; i < classes.size(); i++) {
        Dictionary cd = classes[i];
        ApiBuiltinClass bt;
        bt.type = parse_variant_type(cd["name"]);

        if (cd.has("indexing_return_type")) {
            bt.has_indexing_return_type = true;
            bt.indexing_type = parse_variant_type(cd.get("indexing_return_type", ""));
        }
        bt.is_keyed = cd.get("is_keyed", false);
        bt.has_destructor = cd.get("has_destructor", false);
        bt.has_destructor = cd.get("has_destructor", false);

        // Build document in parallel
        ApiClassDocument doc;
        doc.name = Variant::get_type_name(bt.type);
        doc.description = cd.get("description", "");
        doc.brief_description = cd.get("brief_description", "");

        // Collect compatibility hashes for this builtin class
        ApiCompatibilityHashData compat_data;

        if (cd.has("members")) {
            Array members = cd["members"];
            bt.members.reserve(members.size());
            doc.properties.reserve(members.size());
            for (int j = 0; j < members.size(); j++) {
                Dictionary md = members[j];
                bt.members.push_back(parse_member(md));
                ApiPropertyDocument pdoc;
                pdoc.name = md.get("name", "");
                pdoc.description = md.get("description", "");
                doc.properties.push_back(pdoc);
            }
        }

        if (cd.has("constants")) {
            Array constants = cd["constants"];
            bt.constants.reserve(constants.size());
            doc.constants.reserve(constants.size());
            for (int j = 0; j < constants.size(); j++) {
                Dictionary c = constants[j];
                ApiBuiltInClassConstantInfo ci;
                ci.name = c["name"];
                ci.type = parse_variant_type(c.get("type", ""));
                String value_str = c.get("value", "");
                ci.value = UtilityFunctions::str_to_var(value_str);
#           if DEBUG_ENABLED
                CRASH_COND_MSG(ci.value.get_type() != ci.type, String("Parse error, Invalid var string: ") + value_str);
                CRASH_COND_MSG(UtilityFunctions::var_to_str(ci.value) != value_str, vformat("Parse error: '%s' VS '%s'", value_str, UtilityFunctions::var_to_str(ci.value)));
#           endif // DEBUG_ENABLED
                bt.constants.push_back(ci);
                ApiConstantDocument cdoc;
                cdoc.name = ci.name;
                cdoc.description = c.get("description", "");
                doc.constants.push_back(cdoc);
            }
        }

        if (cd.has("enums")) {
            Array enums = cd["enums"];
            bt.enums.reserve(enums.size());
            for (int j = 0; j < enums.size(); j++) {
                Dictionary ed = enums[j];
                bt.enums.push_back(parse_enum(ed));
                // Build enum document
                ApiEnumDocument edoc;
                edoc.name = ed["name"];
                if (ed.has("values")) {
                    Array values = ed["values"];
                    edoc.values.reserve(values.size());
                    for (int k = 0; k < values.size(); k++) {
                        Dictionary ev = values[k];
                        ApiEnumValueDocument evdoc;
                        evdoc.name = ev["name"];
                        evdoc.description = ev.get("description", "");
                        edoc.values.push_back(evdoc);
                    }
                }
                doc.enums.push_back(edoc);
            }
        }

        if (cd.has("methods")) {
            Array methods = cd["methods"];
            bt.methods.reserve(methods.size());
            doc.methods.reserve(methods.size());
            for (int j = 0; j < methods.size(); j++) {
                Dictionary md = methods[j];
                ApiBuiltInMethod mbi = parse_method<ApiBuiltInMethod>(md, &compat_data);
                bt.methods.push_back(mbi);
                ApiMethodDocument mdoc;
                mdoc.name = md["name"];
                mdoc.description = md.get("description", "");
                doc.methods.push_back(mdoc);
            }
        }

        if (cd.has("operators")) {
            Array operators = cd["operators"];
            bt.operators.reserve(operators.size());
            doc.operators.reserve(operators.size());
            for (int j = 0; j < operators.size(); j++) {
                Dictionary od = operators[j];
                bt.operators.push_back(parse_operator(od));
                ApiOperatorDocument odoc;
                odoc.name = od["name"];
                odoc.description = od.get("description", "");
                doc.operators.push_back(odoc);
            }
        }

        if (cd.has("constructors")) {
            Array constructors = cd["constructors"];
            bt.constructors.reserve(constructors.size());
            doc.constructors.reserve(constructors.size());
            constructors.sort_custom(callable_mp_static(sort_by_key).bind("index"));
            for (int j = 0; j < constructors.size(); j++) {
                Dictionary ctor_d = constructors[j];
                bt.constructors.push_back(parse_constructor(ctor_d));
                ApiConstructorDocument cdoc;
                cdoc.description = ctor_d.get("description", "");
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
    ERR_FAIL_COND_V_MSG(!p_root.has("classes"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'classes' section");

    Array classes = p_root["classes"];
    String dir = p_output_dir + String("/") + String(DIR_CLASSES);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_CLASSES);
    String compat_dir = p_output_dir + String("/") + String(DIR_COMPAT_HASHES);
    Error overall = OK;

    for (int i = 0; i < classes.size(); i++) {
        Dictionary cd = classes[i];
        ApiClass cls;
        cls.name = cd["name"];
        cls.inherits = cd.get("inherits", "");
        cls.api_type = parse_class_api_type(cd["api_type"]);
        cls.is_refcounted = cd.get("is_refcounted", false);
        cls.is_instantiable = cd.get("is_instantiable", false);

        // Build document in parallel
        ApiClassDocument doc;
        doc.name = cls.name;
        doc.description = cd.get("description", "");
        doc.brief_description = cd.get("brief_description", "");

        // Collect compatibility hashes for this class
        ApiCompatibilityHashData compat_data;

        if (cd.has("methods")) {
            Array methods = cd["methods"];
            cls.methods.reserve(methods.size());
            doc.methods.reserve(methods.size());
            for (int j = 0; j < methods.size(); j++) {
                Dictionary md = methods[j];
                cls.methods.push_back(parse_method<ApiClassMethod>(md, &compat_data));
                ApiMethodDocument mdoc;
                mdoc.name = md["name"];
                mdoc.description = md.get("description", "");
                doc.methods.push_back(mdoc);
            }
        }

        if (cd.has("signals")) {
            Array signals = cd["signals"];
            cls.signals.reserve(signals.size());
            doc.signals.reserve(signals.size());
            for (int j = 0; j < signals.size(); j++) {
                Dictionary sd = signals[j];
                cls.signals.push_back(parse_signal(sd));
                ApiSignalDocument sdoc;
                sdoc.name = sd["name"];
                sdoc.description = sd.get("description", "");
                if (sd.has("arguments")) {
                    Array args = sd["arguments"];
                    sdoc.arguments.reserve(args.size());
                    for (int k = 0; k < args.size(); k++) {
                        sdoc.arguments.push_back(parse_property_info(args[k]));
                    }
                }
                doc.signals.push_back(sdoc);
            }
        }

        if (cd.has("properties")) {
            Array properties = cd["properties"];
            cls.properties.reserve(properties.size());
            doc.properties.reserve(properties.size());
            for (int j = 0; j < properties.size(); j++) {
                Dictionary pd = properties[j];
                cls.properties.push_back(parse_api_property(pd));
                ApiPropertyDocument pdoc;
                pdoc.name = pd["name"];
                pdoc.description = pd.get("description", "");
                doc.properties.push_back(pdoc);
            }
        }

        if (cd.has("enums")) {
            Array enums = cd["enums"];
            cls.enums.reserve(enums.size());
            doc.enums.reserve(enums.size());
            for (int j = 0; j < enums.size(); j++) {
                Dictionary ed = enums[j];
                cls.enums.push_back(parse_enum(ed));
                ApiEnumDocument edoc;
                edoc.name = ed["name"];
                if (ed.has("values")) {
                    Array values = ed["values"];
                    edoc.values.reserve(values.size());
                    for (int k = 0; k < values.size(); k++) {
                        Dictionary ev = values[k];
                        ApiEnumValueDocument evdoc;
                        evdoc.name = ev["name"];
                        evdoc.description = ev.get("description", "");
                        edoc.values.push_back(evdoc);
                    }
                }
                doc.enums.push_back(edoc);
            }
        }

        if (cd.has("constants")) {
            Array constants = cd["constants"];
            cls.constants.reserve(constants.size());
            for (int j = 0; j < constants.size(); j++) {
                Dictionary c = constants[j];
                ApiConstantInfo ci;
                ci.name = c["name"];
                ci.value = c.get("value", 0);
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
    ERR_FAIL_COND_V_MSG(!p_root.has("global_enums"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'global_enums' section");

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
        if (ed.has("values")) {
            Array values = ed["values"];
            doc.values.reserve(values.size());
            for (int k = 0; k < values.size(); k++) {
                Dictionary ev = values[k];
                ApiEnumValueDocument evdoc;
                evdoc.name = ev["name"];
                evdoc.description = ev.get("description", "");
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
    ERR_FAIL_COND_V_MSG(!p_root.has("global_constants"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'global_constants' section");

    Array constants = p_root["global_constants"];
    String dir = p_output_dir + String("/") + String(DIR_GLOBAL_CONSTANTS);
    String doc_dir = p_output_dir + String("/") + String(DIR_DOC_GLOBAL_CONSTANTS);
    Error overall = OK;

    for (int i = 0; i < constants.size(); i++) {
        Dictionary cd = constants[i];
        ApiConstantInfo info;
        info.name = cd["name"];
        info.value = cd["value"];
        info.is_bitfield = cd.get("is_bitfield", false);

        // Write main data file
        String path = dir + String("/") + String(info.name) + String(FILE_EXT_DATA);
        Error err = ApiStoreWriter::write_global_constant(path, info);
        if (err != OK) {
            ERR_PRINT("[API Tool] Failed to write constant: " + String(info.name));
            overall = err;
        }

        // Write document file
        ApiGlobalConstantDocument doc;
        doc.name = info.name;
        doc.description = cd.get("description", "");
        String doc_path = doc_dir + String("/") + String(info.name) + String(FILE_EXT_DOC);
        ApiStoreWriter::write_global_constant_document(doc_path, doc);
    }

    return overall;
}

// ============================================================================
// Singletons parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_singletons(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!p_root.has("singletons"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'singletons' section");

    Array singletons = p_root["singletons"];
    LocalVector<ApiSingleton> all_singletons;
    all_singletons.reserve(singletons.size());

    for (int i = 0; i < singletons.size(); i++) {
        Dictionary sd = singletons[i];
        ApiSingleton singleton;
        singleton.name = sd["name"];
        singleton.type = sd["type"];
        all_singletons.push_back(singleton);
    }

    String path = p_output_dir + String("/") + String(DIR_SINGLETONS) + String("/singletons") + String(FILE_EXT_DATA);
    return ApiStoreWriter::write_singletons(path, all_singletons);
}

// ============================================================================
// Native Structures parser (single file)
// ============================================================================

Error ApiParser::parse_and_write_native_structures(const Dictionary &p_root, const String &p_output_dir) {
    ERR_FAIL_COND_V_MSG(!p_root.has("native_structures"), ERR_PARSE_ERROR, "[API Tool] JSON missing 'native_structures' section");

    Array structs = p_root["native_structures"];
    LocalVector<ApiNativeStructure> all_structs;
    all_structs.reserve(structs.size());

    for (int i = 0; i < structs.size(); i++) {
        Dictionary sd = structs[i];
        ApiNativeStructure ns;
        ns.name = sd["name"];
        ns.format = sd["format"];
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

    UtilityFunctions::print("[API Tool] API data generated successfully to: " + p_output_dir);
    return OK;
}

} // namespace api_tool

#endif // TOOLS_ENABLED