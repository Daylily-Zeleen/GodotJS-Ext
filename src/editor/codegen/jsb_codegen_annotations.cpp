/************************************************************************/
/*  jsb_codegen_annotations.cpp                                         */
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

// Port of the `annotation_types` table from jsb.editor.codegen.ts (the static
// descriptor dictionaries serialized into jsb.runtime.gen.d.ts). The emitted
// text only depends on the descriptor structure, so the port keeps the same
// Dictionary literals.

#include "jsb_codegen_defs.h"

#include <common/internal/jsb_naming_util.h>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb {
namespace codegen {

namespace {

using internal::NamingUtil;

Dictionary make_desc(int p_type) {
    Dictionary d;
    d["type"] = p_type;
    return d;
}

Dictionary make_godot(const String &p_name) {
    Dictionary d = make_desc((int)DescriptorType::Godot);
    d["name"] = p_name;
    return d;
}

Dictionary make_godot_args(const String &p_name, const Array &p_args) {
    Dictionary d = make_godot(p_name);
    d["arguments"] = p_args;
    return d;
}

Dictionary make_param(const String &p_name, const Dictionary &p_type, bool p_optional = false) {
    Dictionary d;
    d["name"] = p_name;
    d["type"] = p_type;
    if (p_optional) {
        d["optional"] = true;
    }
    return d;
}

Dictionary make_func(const Array &p_params, const Dictionary &p_returns) {
    Dictionary d = make_desc((int)DescriptorType::FunctionLiteral);
    d["parameters"] = p_params;
    d["returns"] = p_returns;
    return d;
}

Dictionary make_func_generics(const Array &p_generics, const Array &p_params, const Dictionary &p_returns) {
    Dictionary d = make_func(p_params, p_returns);
    d["generics"] = p_generics;
    return d;
}

Dictionary make_generic(const String &p_name, const Dictionary &p_extends) {
    Dictionary d;
    d["name"] = p_name;
    d["extends"] = p_extends;
    return d;
}

Dictionary make_union(const Array &p_types) {
    Dictionary d = make_desc((int)DescriptorType::Union);
    d["types"] = p_types;
    return d;
}

Dictionary make_intersection(const Array &p_types) {
    Dictionary d = make_desc((int)DescriptorType::Intersection);
    d["types"] = p_types;
    return d;
}

Dictionary make_string_literal(const String &p_value) {
    Dictionary d = make_desc((int)DescriptorType::StringLiteral);
    d["value"] = p_value;
    return d;
}

Dictionary make_object(const Dictionary &p_properties) {
    Dictionary d = make_desc((int)DescriptorType::ObjectLiteral);
    d["properties"] = p_properties;
    return d;
}

Dictionary make_optional(const Dictionary &p_desc) {
    Dictionary d = p_desc;
    d["optional"] = true;
    return d;
}

String member(const char *p_name) { return NamingUtil::get_member_name(p_name); }
String cls(const char *p_name) { return NamingUtil::get_class_name(p_name); }

Dictionary build_class_binder() {
    // decorator function returning a decorator, intersected with sub-decorators
    const Dictionary object_constructor = make_godot("GObjectConstructor");
    const Dictionary class_decorator_context = make_godot("ClassDecoratorContext");

    // top: () => (target, context) => void
    Array top_params;
    top_params.push_back(make_param("target", object_constructor));
    top_params.push_back(make_param("context", class_decorator_context));
    Dictionary top = make_func(Array(),
            make_func(top_params, make_godot("void")));

    // properties of the intersection object
    Dictionary properties;

    // tool
    {
        Array params;
        params.push_back(make_param("target", object_constructor));
        params.push_back(make_param("_context", class_decorator_context));
        properties[member("tool")] = make_func(Array(), make_func(params, make_godot("void")));
    }

    // icon
    {
        // TS: icon(path: string): (target: GObjectConstructor, _context: ClassDecoratorContext) => void
        Array params;
        params.push_back(make_param("path", make_godot("string")));

        Array inner_params;
        inner_params.push_back(make_param("target", object_constructor));
        inner_params.push_back(make_param("_context", class_decorator_context));

        properties[member("icon")] = make_func(params, make_func(inner_params, make_godot("void")));
    }

    // export
    {
        // main callable
        Array main_params;
        main_params.push_back(make_param("type", make_godot("Godot.Variant.Type")));
        main_params.push_back(make_param("options", make_godot("ExportOptions"), true));
        Dictionary main_func = make_func(main_params, make_godot("ClassMemberDecorator"));

        Dictionary export_props;

        // multiline
        export_props[member("multiline")] = make_func(Array(), make_godot("ClassMemberDecorator"));

        // range
        {
            Array range_params;
            range_params.push_back(make_param("min", make_godot("number")));
            range_params.push_back(make_param("max", make_godot("number")));
            range_params.push_back(make_param("step", make_godot("number")));
            range_params.push_back(make_param("...extra_hints", make_godot("ExportRangeExtraHint[]")));
            export_props[member("range")] = make_func(range_params, make_godot("ClassMemberDecorator"));
        }

        // range_int
        {
            Array range_params;
            range_params.push_back(make_param("min", make_godot("number")));
            range_params.push_back(make_param("max", make_godot("number")));
            range_params.push_back(make_param("step", make_godot("number")));
            range_params.push_back(make_param("...extra_hints", make_godot("ExportRangeExtraHint[]")));
            export_props[member("range_int")] = make_func(range_params, make_godot("ClassMemberDecorator"));
        }

        // file / dir
        {
            Array filter_params;
            filter_params.push_back(make_param("filter", make_godot("string")));
            export_props[member("file")] = make_func(filter_params, make_godot("ClassMemberDecorator"));
            export_props[member("dir")] = make_func(filter_params, make_godot("ClassMemberDecorator"));
        }

        // global_file / global_dir
        {
            Array filter_params;
            filter_params.push_back(make_param("filter", make_godot("string")));
            export_props[member("global_file")] = make_func(filter_params, make_godot("ClassMemberDecorator"));
            export_props[member("global_dir")] = make_func(filter_params, make_godot("ClassMemberDecorator"));
        }

        // exp_easing
        {
            Array easing_types;
            easing_types.push_back(make_string_literal(""));
            easing_types.push_back(make_string_literal("attenuation"));
            easing_types.push_back(make_string_literal("positive_only"));
            easing_types.push_back(make_string_literal("attenuation,positive_only"));

            Array easing_params;
            easing_params.push_back(make_param("hint", make_union(easing_types), true));
            export_props[member("exp_easing")] = make_func(easing_params, make_godot("ClassMemberDecorator"));
        }

        // array
        {
            Array array_params;
            array_params.push_back(make_param("clazz", make_godot("ClassSpecifier")));
            export_props[member("array")] = make_func(array_params, make_godot("ClassMemberDecorator"));
        }

        // dictionary
        {
            Array dict_params;
            dict_params.push_back(make_param("key_class", make_godot("VariantConstructor")));
            dict_params.push_back(make_param("value_class", make_godot("VariantConstructor")));
            export_props[member("dictionary")] = make_func(dict_params, make_godot("ClassMemberDecorator"));
        }

        // object
        {
            Array generics;
            generics.push_back(make_generic("Constructor", make_godot("GObjectConstructor")));

            Array object_params;
            object_params.push_back(make_param("clazz", make_godot("Constructor")));

            Array instance_args;
            instance_args.push_back(make_godot("unknown"));
            {
                Array union_types;
                union_types.push_back(make_godot("null"));
                Array instance_type_args;
                instance_type_args.push_back(make_godot("Constructor"));
                union_types.push_back(make_godot_args("InstanceType", instance_type_args));
                instance_args.push_back(make_union(union_types));
            }
            Array ret_args;
            ret_args.push_back(make_godot_args("ClassValueMemberDecoratorContext", instance_args));
            export_props[member("object")] = make_func_generics(generics, object_params,
                    make_godot_args("ClassMemberDecorator", ret_args));
        }

        // enum / flags
        {
            Array record_args;
            record_args.push_back(make_godot("string"));
            {
                Array union_types;
                union_types.push_back(make_godot("string"));
                union_types.push_back(make_godot("number"));
                record_args.push_back(make_union(union_types));
            }

            Array enum_params;
            enum_params.push_back(make_param("enum_type", make_godot_args("Record", record_args)));
            export_props[member("enum")] = make_func(enum_params, make_godot("ClassMemberDecorator"));
            export_props[member("flags")] = make_func(enum_params, make_godot("ClassMemberDecorator"));
        }

        // cache
        {
            Array cache_ret_args;
            {
                Array cache_union;
                const String object_name = "Godot." + cls("Object");
                {
                    Array accessor_args;
                    accessor_args.push_back(make_godot(object_name));
                    cache_union.push_back(make_godot_args("ClassAccessorDecoratorContext", accessor_args));
                }
                {
                    Array setter_args;
                    setter_args.push_back(make_godot(object_name));
                    cache_union.push_back(make_godot_args("ClassSetterDecoratorContext", setter_args));
                }
                cache_ret_args.push_back(make_union(cache_union));
            }
            export_props[member("cache")] = make_func(Array(), make_godot_args("ClassMemberDecorator", cache_ret_args));
        }

        Dictionary export_object = make_object(export_props);
        Array export_types;
        export_types.push_back(main_func);
        export_types.push_back(export_object);
        properties[member("export")] = make_intersection(export_types);
    }

    // signal
    {
        const String object_name = "Godot." + cls("Object");

        Array context_union;
        {
            Array accessor_args;
            accessor_args.push_back(make_godot(object_name));
            accessor_args.push_back(make_godot("Godot.Signal"));
            context_union.push_back(make_godot_args("ClassAccessorDecoratorContext", accessor_args));
        }
        {
            Array getter_args;
            getter_args.push_back(make_godot(object_name));
            getter_args.push_back(make_godot("Godot.Signal"));
            context_union.push_back(make_godot_args("ClassGetterDecoratorContext", getter_args));
        }
        {
            Array field_args;
            field_args.push_back(make_godot(object_name));
            field_args.push_back(make_godot("Godot.Signal"));
            context_union.push_back(make_godot_args("ClassFieldDecoratorContext", field_args));
        }

        Array generics;
        generics.push_back(make_generic("Context", make_union(context_union)));

        Array signal_params;
        signal_params.push_back(make_param("_target", make_godot("unknown")));
        signal_params.push_back(make_param("context", make_godot("Context")));

        Array signal_ret_args;
        signal_ret_args.push_back(make_godot("Context"));
        // TS: signal(): <Context extends ...>(_target: unknown, context: Context)
        //     => ClassMemberDecoratorReturn<Context> - the generics live on the
        // INNER function, the outer one takes no parameters.
        properties[member("signal")] = make_func(Array(),
                make_func_generics(generics, signal_params, make_godot_args("ClassMemberDecoratorReturn", signal_ret_args)));
    }

    // rpc
    {
        Array rpc_params;
        rpc_params.push_back(make_param("config", make_godot(cls("RPCConfig")), true));

        Array rpc_ret_params;
        rpc_ret_params.push_back(make_param("_target", make_godot("Function")));
        {
            Array context_union;
            context_union.push_back(make_godot("string"));
            context_union.push_back(make_godot("ClassMethodDecoratorContext"));
            rpc_ret_params.push_back(make_param("context", make_union(context_union)));
        }
        // TS inner function has no explicit returns -> writer emits implicit "void"
        properties[member("rpc")] = make_func(rpc_params, make_func(rpc_ret_params, make_godot("void")));
    }

    // onready
    {
        Array onready_params;
        {
            Array evaluator_union;
            evaluator_union.push_back(make_godot("string"));
            evaluator_union.push_back(make_godot("GodotJsb.internal.OnReadyEvaluatorFunc"));
            onready_params.push_back(make_param("evaluator", make_union(evaluator_union)));
        }

        Array onready_ret_params;
        onready_ret_params.push_back(make_param("_target", make_godot("undefined")));
        {
            Array context_union;
            context_union.push_back(make_godot("string"));
            context_union.push_back(make_godot("ClassMethodDecoratorContext"));
            onready_ret_params.push_back(make_param("context", make_union(context_union)));
        }
        // TS inner function has no explicit returns -> writer emits implicit "void"
        properties[member("onready")] = make_func(onready_params, make_func(onready_ret_params, make_godot("void")));
    }

    // deprecated / experimental / help (same shape)
    const char *decorator_names[] = { "deprecated", "experimental", "help" };
    for (const char *name : decorator_names) {
        Array msg_params;
        msg_params.push_back(make_param("message", make_godot("string"), true));

        Array decorator_args;
        {
            Array decorator_union;
            Array class_ctx_args;
            class_ctx_args.push_back(make_godot("GObjectConstructor"));
            decorator_union.push_back(make_godot_args("ClassDecoratorContext", class_ctx_args));
            Array member_ctx_args;
            member_ctx_args.push_back(make_godot("GObjectConstructor"));
            decorator_union.push_back(make_godot_args("ClassValueMemberDecoratorContext", member_ctx_args));
            decorator_args.push_back(make_union(decorator_union));
        }
        properties[member(name)] = make_func(msg_params, make_godot_args("Decorator", decorator_args));
    }

    // top-level: () => decorator-func & { ... }
    Array top_types;
    top_types.push_back(top);
    top_types.push_back(make_object(properties));
    return make_intersection(top_types);
}

Dictionary build_export_options() {
    Dictionary properties;
    {
        Dictionary p = make_godot("any");
        p["optional"] = true;
        properties[member("class")] = p;
    }
    {
        Dictionary p = make_godot("Godot.PropertyHint");
        p["optional"] = true;
        properties[member("hint")] = p;
    }
    {
        Dictionary p = make_godot("string");
        p["optional"] = true;
        properties[member("hint_string")] = p;
    }
    {
        Dictionary p = make_godot("Godot.PropertyUsageFlags");
        p["optional"] = true;
        properties[member("usage")] = p;
    }
    return make_object(properties);
}

Dictionary build_rpc_config() {
    Dictionary properties;
    {
        Dictionary p = make_godot("Godot." + cls("MultiplayerAPI") + "." + NamingUtil::get_enum_name("RPCMode"));
        p["optional"] = true;
        properties[member("mode")] = p;
    }
    {
        Array sync_types;
        sync_types.push_back(make_string_literal("call_remote"));
        sync_types.push_back(make_string_literal("call_local"));
        Dictionary p = make_union(sync_types);
        p["optional"] = true;
        properties[member("sync")] = p;
    }
    {
        Dictionary p = make_godot("Godot.MultiplayerPeer.TransferMode");
        p["optional"] = true;
        properties[member("transfer_mode")] = p;
    }
    {
        Dictionary p = make_godot("number");
        p["optional"] = true;
        properties[member("transfer_channel")] = p;
    }
    return make_object(properties);
}

HashMap<String, Dictionary> *s_annotation_types = nullptr;

} // namespace

const HashMap<String, Dictionary> &get_annotation_types() {
    if (s_annotation_types == nullptr) {
        HashMap<String, Dictionary> *map = new HashMap<String, Dictionary>();
        map->insert("ClassBinder", build_class_binder());
        map->insert("ExportOptions", build_export_options());
        map->insert("RPCConfig", build_rpc_config());
        s_annotation_types = map;
    }
    return *s_annotation_types;
}

} // namespace codegen
} // namespace jsb
