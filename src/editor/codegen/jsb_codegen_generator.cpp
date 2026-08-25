/************************************************************************/
/*  jsb_codegen_generator.cpp                                           */
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

#include "jsb_codegen_generator.h"

#include "jsb_codegen_writer.h"

#include "api_tool/api_tool.h"
#include <common/internal/jsb_class_visibility.h>
#include <api_tool/editor/api_tool_editor.h>
#include "jsb_codegen_scene_descriptors.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <common/internal/jsb_logger.h>
#include <common/internal/jsb_macros.h>
#include <common/internal/jsb_naming_util.h>
#include <common/internal/jsb_settings.h>
#include "../internal/jsb_class_visibility.h"

namespace jsb {
namespace codegen {

// defined in jsb_codegen_annotations.cpp
const HashMap<String, Dictionary> &get_annotation_types();

namespace {

constexpr int kSplitSizeLimit = 1024 * 900;
constexpr int kSplitLineLimit = 9200;

// jsb.utility_functions (GLOBAL_GET / EDITOR_GET)
struct GlobalUtilityFuncDecl {
    const char *description;
    const char *method;
};

const GlobalUtilityFuncDecl kGlobalUtilityFuncs[] = {
    { "shorthand for getting project settings", "function GLOBAL_GET(entry_path: StringName): any" },
    { "shorthand for getting editor settings\nNOTE: calling before EditorSettings created will cause null reference exception.",
            "function EDITOR_GET(entry_path: StringName): any" },
};

Dictionary make_desc(int p_type) {
    Dictionary d;
    d["type"] = p_type;
    return d;
}

Dictionary make_godot_desc(const String &p_name) {
    Dictionary d = make_desc((int)DescriptorType::Godot);
    d["name"] = p_name;
    return d;
}

Dictionary make_func_desc(const Array &p_parameters, const Dictionary &p_returns) {
    Dictionary d = make_desc((int)DescriptorType::FunctionLiteral);
    d["parameters"] = p_parameters;
    d["returns"] = p_returns;
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

} // namespace

// ---------------------------------------------------------------------------
// GodotTSDGenerator
// ---------------------------------------------------------------------------
GodotTSDGenerator::GodotTSDGenerator(const String &p_out_dir, bool p_use_project_settings)
        : out_dir_(p_out_dir), use_project_settings_(p_use_project_settings) {}

GodotTSDGenerator::~GodotTSDGenerator() {
    if (splitter_ != nullptr) {
        splitter_->close();
        memdelete(splitter_);
        splitter_ = nullptr;
    }
}

String GodotTSDGenerator::make_path(int p_index) const {
    const String filename = "godot" + itos(p_index) + ".gen.d.ts";
    if (out_dir_.is_empty()) {
        return filename;
    }
    if (out_dir_.ends_with("/")) {
        return out_dir_ + filename;
    }
    return out_dir_ + String("/") + filename;
}

FileSplitter *GodotTSDGenerator::new_splitter() {
    if (splitter_ != nullptr) {
        splitter_->close();
        memdelete(splitter_);
    }
    const String filename = make_path(split_index_++);
    splitter_ = memnew(FileSplitter(&types_, filename));
    return splitter_;
}

ModuleWriter &GodotTSDGenerator::split() {
    if (splitter_ == nullptr) {
        return *new_splitter()->get_writer();
    }
    if (splitter_->get_size() > kSplitSizeLimit || splitter_->get_lineno() > kSplitLineLimit) {
        return *new_splitter()->get_writer();
    }
    return *splitter_->get_writer();
}

void GodotTSDGenerator::cleanup() {
    // delete stale split files beyond the last written index
    while (true) {
        const String path = make_path(split_index_++);
        if (!FileAccess::file_exists(path)) {
            break;
        }
        JSB_LOG(Verbose, "delete file %s", path);
        DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(path));
    }
}

bool GodotTSDGenerator::emit(bool p_skip_static_types) {
    if (!p_skip_static_types) {
        // static types install is plugin-side (preset files); caller handles it.
    }

    emit_aliases();

    // singletons
    // NOTE: iterate in *load order* (the order the old EditorUtilityFuncs
    // getters returned), NOT alphabetically - the TS pipeline relied on JS
    // object key iteration (insertion order), and shard boundaries depend on
    // it. TypeDB's owned_* vectors preserve that order.
    {
        for (const SingletonDecl *decl : types_.ordered_singletons()) {
            ModuleWriter &cg = split();
            emit_singleton(*decl);
        }
    }

    // classes (skip those already emitted as singletons)
    {
        for (const ClassDecl *cls : types_.ordered_classes()) {
            if (types_.singletons.has(cls->name)) {
                continue;
            }
            ModuleWriter &cg = split();
            emit_godot_class(cg, *cls, false);
        }
    }

    // primitive types
    {
        for (const PrimitiveClassDecl *decl : types_.ordered_primitives()) {
            ModuleWriter &cg = split();
            emit_godot_primitive(cg, *decl);
        }
    }

    // globals
    {
        for (const GlobalConstantDecl *decl : types_.ordered_globals()) {
            ModuleWriter &cg = split();
            emit_global(*decl);
        }
    }

    // utility functions
    {
        for (const MethodDecl *decl : types_.ordered_utilities()) {
            ModuleWriter &cg = split();
            emit_utility(*decl);
        }
    }

    // jsb.utility_functions
    {
        ModuleWriter &cg = split();
        for (const GlobalUtilityFuncDecl &decl : kGlobalUtilityFuncs) {
            // TS passes the description array to DocCommentHelper.write (multi-line
            // branch: trailing double-spaces + separate " */" line).
            Vector<String> doc_lines;
            const PackedStringArray parts = String(decl.description).split("\n");
            for (const String &part : parts) {
                doc_lines.push_back(part);
            }
            DocCommentHelper::write_lines(cg, doc_lines, true);
            cg.line(decl.method);
        }
    }

    emit_ignored_interfaces();
    emit_runtime_gen();

    if (splitter_ != nullptr) {
        splitter_->close();
        memdelete(splitter_);
        splitter_ = nullptr;
    }
    cleanup();
    return true;
}

void GodotTSDGenerator::emit_aliases() {
    ModuleWriter &cg = split();
    for (int i = 0; i < kPredefinedLineCount; ++i) {
        cg.line(kPredefinedLines[i]);
    }

    if (String(kGodotAnyType) != "any") {
        String gd_variant_alias = String("type ") + kGodotAnyType + " = undefined | null";
        for (int vt = 1; vt < (int)Variant::VARIANT_MAX; ++vt) {
            const String type_name = types_.get_variant_to_name((Variant::Type)vt);
            if (type_name == kGodotAnyType || type_name == "any" || type_name.is_empty()) {
                continue;
            }
            gd_variant_alias += " | " + type_name;
        }
        cg.line(gd_variant_alias);
    }

    if (use_project_settings_) {
        cg.line("type InputActionName = ");
        IndentWriter *indent = memnew(IndentWriter(&cg));
        TypedArray<Dictionary> property_list = ProjectSettings::get_singleton()->get_property_list();
        for (int i = 0; i < property_list.size(); i++) {
            Dictionary property = property_list[i];
            String name = property["name"];
            if (!name.begins_with("input/")) {
                continue;
            }
            name = name.substr(name.find("/") + 1, name.length());
            indent->line("| \"" + name + "\"");
        }
        indent->finish();
        memdelete(indent);
    } else {
        cg.line("type InputActionName = string");
    }
}

void GodotTSDGenerator::emit_singleton(const SingletonDecl &p_singleton) {
    ModuleWriter &cg = split();
    const ClassDecl *cls = types_.find_class(p_singleton.class_name);
    if (cls != nullptr) {
        cg.line_comment_("_singleton_class_: " + p_singleton.class_name);
        emit_godot_class(cg, *cls, true);
    } else {
        cg.line_comment_("ERROR: singleton " + p_singleton.name + " without class info " + p_singleton.class_name);
    }
}

void GodotTSDGenerator::emit_godot_primitive(ModuleWriter &r_cg, const PrimitiveClassDecl &p_cls) {
    HashSet<String> ignored_consts;

    const ClassDocEntry *class_doc = types_.find_doc(p_cls.name, Variant::get_type_name(p_cls.type));
    NamespaceWriter *class_ns = r_cg.namespace_(p_cls.name, class_doc);
    for (const EnumDecl &enum_info : p_cls.enums) {
        EnumWriter *enum_cg = class_ns->enum_(enum_info.name);
        for (const Pair<String, int64_t> &literal : enum_info.literals) {
            enum_cg->element_(literal.first, literal.second);
            ignored_consts.insert(literal.first);
        }
        enum_cg->finish();
        memdelete(enum_cg);
    }
    class_ns->finish();
    memdelete(class_ns);

    const String type_name = internal::NamingUtil::get_class_name(Variant::get_type_name(p_cls.type));
    const TypeMutation mutation = get_type_mutation(type_name, &types_, /*p_apply_intrinsic=*/false);

    ClassEmitOptions options;
    options.name = type_name;
        options.original_name = Variant::get_type_name(p_cls.type);
    for (const Pair<String, GenericParameter> &gp : mutation.generic_parameters) {
        options.generic_parameters.push_back(gp);
    }
    options.super = mutation.super;
    options.super_generic_arguments = mutation.super_generic_arguments;
    options.implements = mutation.implements;
    options.intro = mutation.intro;
    options.prelude = mutation.prelude;
    options.mutation = &mutation;

    ClassWriter *class_cg = r_cg.class_(options);
    for (const PrimitiveConstantDecl &constant : p_cls.constants) {
        const String exposed = internal::NamingUtil::get_enum_value_name(constant.name);
        if (!ignored_consts.has(constant.name) && !ignored_consts.has(exposed)) {
            class_cg->primitive_constant_(constant);
        }
    }
    for (const ConstructorDecl &ctor : p_cls.constructors) {
        class_cg->constructor_(ctor);
    }
    for (const MethodDecl &method : p_cls.methods) {
        class_cg->ordinary_method_(method);
    }
    for (const OperatorDecl &op : p_cls.operators) {
        class_cg->operator_(op);
    }
    for (const PrimitiveGetSetDecl &prop : p_cls.properties) {
        class_cg->primitive_property_(prop);
    }
    class_cg->finish();
    memdelete(class_cg);
}

void GodotTSDGenerator::emit_godot_class(ModuleWriter &r_cg, const ClassDecl &p_cls, bool p_singleton_mode) {
    if (!internal::ClassVisibility::is_original_class_exposed(p_cls.internal_name)) {
        return; // skip ignored classes
    }

    HashSet<String> ignored_consts;

    const ClassDocEntry *class_doc = types_.find_doc(p_cls.name, p_cls.internal_name);
    NamespaceWriter *class_ns = r_cg.namespace_(p_cls.name, class_doc);
    for (const EnumDecl &enum_info : p_cls.enums) {
        EnumWriter *enum_cg = class_ns->enum_(enum_info.name);
        for (const Pair<String, int64_t> &literal : enum_info.literals) {
            enum_cg->element_(literal.first, literal.second);
            ignored_consts.insert(literal.first);
        }
        enum_cg->finish();
        memdelete(enum_cg);
    }
    class_ns->finish();
    memdelete(class_ns);

    const TypeMutation mutation = get_type_mutation(p_cls.name, &types_);
    String super_ = mutation.super;
    if (super_.is_empty() && types_.classes.has(p_cls.super)) {
        super_ = p_cls.super;
    }

    ClassEmitOptions options;
    options.name = p_cls.name;
        options.original_name = p_cls.internal_name;
    for (const Pair<String, GenericParameter> &gp : mutation.generic_parameters) {
        options.generic_parameters.push_back(gp);
    }
    options.super = super_;
    options.super_generic_arguments = mutation.super_generic_arguments;
    options.implements = mutation.implements;
    options.intro = mutation.intro;
    options.prelude = mutation.prelude;
    options.mutation = &mutation;
    options.singleton_mode = p_singleton_mode;

    ClassWriter *class_cg = r_cg.class_(options);

    for (const ConstantDecl &constant : p_cls.constants) {
        const String exposed = internal::NamingUtil::get_enum_value_name(constant.name);
        if (!ignored_consts.has(constant.name) && !ignored_consts.has(exposed)) {
            class_cg->constant_(constant);
        }
    }

    if (!p_singleton_mode) {
        class_cg->constructor_ex_();
    }

    HashMap<String, String> godot_name_overrides;

    for (const MethodDecl &method : p_cls.virtual_methods) {
        class_cg->virtual_method_(method);
        if (method.internal_name != method.name) {
            godot_name_overrides.insert(method.internal_name, method.name);
        }
    }
    for (const MethodDecl &method : p_cls.methods) {
        class_cg->ordinary_method_(method);
        if (method.internal_name != method.name) {
            godot_name_overrides.insert(method.internal_name, method.name);
        }
    }
    for (const PropertySetGetDecl &property : p_cls.properties) {
        class_cg->property_(property, p_singleton_mode);
        if (property.internal_name != property.name) {
            godot_name_overrides.insert(property.internal_name, property.name);
        }
    }
    for (const SignalDecl &signal : p_cls.signals) {
        class_cg->signal_(signal);
        if (signal.internal_name != signal.name) {
            godot_name_overrides.insert(signal.internal_name, signal.name);
        }
    }

    // __RPCMap interface
    {
        const String rpc_interface_name = "__RPCMap" + p_cls.name;
        const String rpc_super = p_cls.super.is_empty() ? String() : "__RPCMap" + p_cls.super;
        InterfaceWriter *rpc_writer = r_cg.interface_(rpc_interface_name, nullptr, rpc_super, Vector<String>(), nullptr);
        for (const MethodDecl &method : p_cls.rpc_methods) {
            rpc_writer->property_(method.name, "(" + types_.make_args(method) + ") => " + types_.make_return(method));
        }
        r_cg.line("/** @deprecated Internal use. Does not exist at runtime. */");
        rpc_writer->finish();
        memdelete(rpc_writer);

        PropertyWriter *rpc_map_writer = class_cg->property_("__godotRPCMap");
        rpc_map_writer->line(rpc_interface_name);
        class_cg->line("/** @deprecated Internal use. Does not exist at runtime. */");
        rpc_map_writer->finish();
        memdelete(rpc_map_writer);
    }

    // __NameMap interface
    {
        const String overrides_interface_name = "__NameMap" + p_cls.name;
        const String name_super = p_cls.super.is_empty() ? String() : "__NameMap" + p_cls.super;
        InterfaceWriter *overrides_writer = r_cg.interface_(overrides_interface_name, nullptr, name_super, Vector<String>(), nullptr);
        for (const KeyValue<String, String> &E : godot_name_overrides) {
            overrides_writer->property_(E.key, "\"" + E.value + "\"");
        }
        r_cg.line("/** @deprecated Internal use. Does not exist at runtime. */");
        overrides_writer->finish();
        memdelete(overrides_writer);

        PropertyWriter *name_map_writer = class_cg->property_("__godotNameMap");
        name_map_writer->line(overrides_interface_name);
        class_cg->line("/** @deprecated Internal use. Does not exist at runtime. */");
        name_map_writer->finish();
        memdelete(name_map_writer);
    }

    class_cg->finish();
    memdelete(class_cg);
}

void GodotTSDGenerator::emit_utility(const MethodDecl &p_utility_func) {
    ModuleWriter &cg = split();
    // global function docs come from @GlobalScope documents
    std::unique_ptr<api_tool::ApiUtilityFunctionDocument> doc =
            api_tool::find_utility_function_document(p_utility_func.name == p_utility_func.internal_name
                            ? p_utility_func.internal_name
                            : p_utility_func.internal_name);
    DocCommentHelper::write(cg, doc ? doc->description : String(), true);
    cg.utility_(p_utility_func);
}

void GodotTSDGenerator::emit_global(const GlobalConstantDecl &p_global) {
    ModuleWriter &cg = split();
    // TS: `const doc = this._types.find_doc("@GlobalScope")` - global constant
    // docs live in the GlobalScope document.
    const ClassDocEntry *doc = types_.find_doc("@GlobalScope", "@GlobalScope");

    if (!p_global.is_enum) {
        DocCommentHelper::write(cg, doc != nullptr && doc->constants.has(p_global.name) ? doc->constants[p_global.name] : String(), true);
        // v8 Number parity: format the value as a JS double
        cg.line("const " + name_string(p_global.name) + " = " + js_number_to_string((double)p_global.value));
        return;
    }

    EnumWriter *ns = cg.enum_(p_global.name);
    bool separator_line = false;
    for (const Pair<String, int64_t> &value : p_global.values) {
        DocCommentHelper::write(*ns, doc != nullptr && doc->constants.has(value.first) ? doc->constants[value.first] : String(), separator_line);
        separator_line = true;
        ns->element_(value.first, value.second);
    }
    ns->finish();
    memdelete(ns);
}

void GodotTSDGenerator::emit_ignored_interfaces() {
    {
        ModuleWriter &cg = split();
        cg.line("");
        cg.line_comment_(
                "IgnoredClasses: ignored classes that are configured by \"Project->Tools->GodotJS->Config Enabled TS "
                "Classes\" and used by other APIs.");
        InterfaceWriter *writer = cg.interface_("IgnoredClasses");
        for (const TypeDB::IgnoredClassEntry &entry : types_.ignored_classes_list) {
            writer->property_(entry.name, entry.exposed_base_class);
        }
        writer->finish();
        memdelete(writer);
    }
    {
        ModuleWriter &cg = split();
        cg.line("");
        cg.line_comment_("IgnoredClassEnums: its owner class is ignored, but the enum type is used by other APIs.");
        InterfaceWriter *writer = cg.interface_("IgnoredClassEnums");
        // NOTE: iteration order of the HashSet is unspecified; sort for determinism.
        Vector<String> names;
        for (const String &name : types_.ignored_class_enums) {
            names.push_back(name);
        }
        names.sort();
        for (const String &name : names) {
            writer->property_(name, "number");
        }
        writer->finish();
        memdelete(writer);
    }
}

void GodotTSDGenerator::emit_runtime_gen() {
    const String dir_path = out_dir_ + "/jsb.runtime.gen.d.ts";
    Ref<FileAccess> file = FileAccess::open(dir_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT("failed to open file for writing: " + dir_path);
        return;
    }

    FileWriter runtime_gen(dir_path, &types_, file);
    runtime_gen.concatenate(make_copyright_header("jsb.runtime.gen.d.ts"));
    ModuleWriter *module = memnew(ModuleWriter(&runtime_gen, "godot.annotations"));

    module->line("import * as Godot from \"godot\";");
    module->line("import * as GodotJsb from \"godot-jsb\";");

    static const HashMap<String, Dictionary> *annotation_types = nullptr;
    if (annotation_types == nullptr) {
        annotation_types = &get_annotation_types();
    }
    for (const KeyValue<String, Dictionary> &E : *annotation_types) {
        module->line("type " + internal::NamingUtil::get_class_name(E.key) + " = ");
        TypeDescriptorWriter *type_descriptor = memnew(TypeDescriptorWriter(module, true));
        type_descriptor->serialize_type_descriptor(E.value);
        type_descriptor->finish();
        memdelete(type_descriptor);
    }

    module->finish();
    memdelete(module);
    runtime_gen.finish();
    file->close();
}

// ---------------------------------------------------------------------------
// SceneTSDGenerator
// ---------------------------------------------------------------------------
SceneTSDGenerator::SceneTSDGenerator(const String &p_out_dir, const Vector<String> &p_scene_paths)
        : out_dir_(p_out_dir), scene_paths_(p_scene_paths) {}

String SceneTSDGenerator::make_scene_path(const String &p_scene_path, bool p_include_filename) const {
    String relative = p_include_filename ? p_scene_path.replace(".tscn", ".nodes.gen.ts").replace(".scn", ".nodes.gen.ts")
                                         : p_scene_path.substr(0, p_scene_path.rfind("/") + 1);
    if (relative.begins_with("res://")) {
        relative = relative.substr(6);
    } else if (relative.begins_with("res:/")) {
        relative = relative.substr(5);
    }

    if (out_dir_.is_empty()) {
        return relative;
    }
    return out_dir_.ends_with("/") ? out_dir_ + relative : out_dir_ + String("/") + relative;
}

bool SceneTSDGenerator::emit() {
    for (const String &scene_path : scene_paths_) {
        if (!emit_scene_node_types(scene_path)) {
            return false;
        }
    }
    return true;
}

bool SceneTSDGenerator::emit_scene_node_types(const String &p_scene_path) {
    // descriptor comes from GodotJSEditorHelper (C++ side)
    // NOTE: an empty dictionary is a *valid* result - a scene whose root has no
    // matching children emits `"path": {}` (see baseline Worker.nodes.gen.ts).
    // The old TS codegen only threw on `undefined`, which cannot happen through
    // this call path; helper-side load failures already log their own errors.
    Dictionary children = jsb::codegen::get_scene_nodes(p_scene_path);

    const String dir_path = make_scene_path(p_scene_path, false);
    const Error dir_error = DirAccess::make_dir_recursive_absolute(dir_path);
    if (dir_error != OK) {
        ERR_PRINT("failed to create directory (error: " + itos(dir_error) + "): " + dir_path);
    }

    const String file_path = make_scene_path(p_scene_path);
    Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT("failed to open file for writing: " + file_path);
        return false;
    }

    FileWriter file_writer(file_path, &types_, file);
    file_writer.concatenate(make_copyright_header(file_path.get_file()));
    ModuleWriter *module = memnew(ModuleWriter(&file_writer, "godot"));
    InterfaceWriter *scene_nodes = memnew(InterfaceWriter(module, "SceneNodes"));

    String scene_key = p_scene_path;
    if (scene_key.begins_with("res://")) {
        scene_key = scene_key.substr(6);
    }
    PropertyWriter *scene_property = memnew(PropertyWriter(scene_nodes, scene_key));

    Array child_keys = children.keys();
    PropertyWriter *child_writer = scene_property; // children serialized as nested object
    ObjectWriter *obj = memnew(ObjectWriter(scene_property));
    for (int i = 0; i < child_keys.size(); ++i) {
        const String key = child_keys[i];
        const Dictionary value = children[key];
        if (value.is_empty()) {
            continue;
        }
        PropertyWriter *property = memnew(PropertyWriter(obj, key));
        TypeDescriptorWriter descriptor(property, true);
        descriptor.serialize_type_descriptor(value);
        descriptor.finish();
        // TS calls property.finish() - without it the buffered content never
        // flushes into the parent object writer (empty `{}` output).
        property->finish();
        memdelete(property);
    }
    obj->finish();
    memdelete(obj);

    scene_property->finish();
    memdelete(scene_property);
    scene_nodes->finish();
    memdelete(scene_nodes);
    module->finish();
    memdelete(module);
    file_writer.finish();
    file->close();
    return true;
}

// ---------------------------------------------------------------------------
// ResourceTSDGenerator
// ---------------------------------------------------------------------------
ResourceTSDGenerator::ResourceTSDGenerator(const String &p_out_dir, const Vector<String> &p_resource_paths)
        : out_dir_(p_out_dir), resource_paths_(p_resource_paths) {
    script_extensions_ = ResourceLoader::get_singleton()->get_recognized_extensions_for_type("Script");
}

String ResourceTSDGenerator::make_resource_path(const String &p_resource_path, bool p_include_filename) const {
    String relative = p_include_filename ? p_resource_path + String(".gen.ts")
                                         : p_resource_path.substr(0, p_resource_path.rfind("/") + 1);
    if (relative.begins_with("res://")) {
        relative = relative.substr(6);
    } else if (relative.begins_with("res:/")) {
        relative = relative.substr(5);
    }

    if (out_dir_.is_empty()) {
        return relative;
    }
    return out_dir_.ends_with("/") ? out_dir_ + relative : out_dir_ + String("/") + relative;
}

bool ResourceTSDGenerator::emit() {
    for (const String &resource_path : resource_paths_) {
        if (!emit_resource_type(resource_path)) {
            return false;
        }
    }
    return true;
}

ResourceTSDGenerator::ScriptRpcInfo ResourceTSDGenerator::get_script_rpc_info(const String &p_resource_path) const {
    ScriptRpcInfo info;
    const int last_dot = p_resource_path.rfind(".");
    const String extension = last_dot >= 0 ? p_resource_path.substr(last_dot + 1) : String();

    bool is_script = false;
    for (int i = 0; i < script_extensions_.size(); ++i) {
        if (script_extensions_[i] == extension) {
            is_script = true;
            break;
        }
    }
    if (!is_script) {
        return info;
    }

    Ref<Resource> resource = ResourceLoader::get_singleton()->load(p_resource_path, "", ResourceLoader::CACHE_MODE_REUSE);
    Ref<Script> script = resource;
    if (script.is_null()) {
        return info;
    }

    info.class_name = script->get_global_name();
    Dictionary rpc_config = script->get_rpc_config();
    Array keys = rpc_config.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const String method_name = keys[i];
        if (!method_name.is_empty()) {
            info.methods.push_back(method_name);
        }
    }
    info.methods.sort();
    info.valid = info.methods.size() > 0;
    return info;
}

bool ResourceTSDGenerator::emit_resource_type(const String &p_resource_path) {
    Dictionary descriptor = jsb::codegen::get_resource_type_descriptor(p_resource_path);
    if (descriptor.is_empty()) {
        ERR_PRINT("resource type unavailable: " + p_resource_path);
        return false;
    }

    const String dir_path = make_resource_path(p_resource_path, false);
    const Error dir_error = DirAccess::make_dir_recursive_absolute(dir_path);
    if (dir_error != OK) {
        ERR_PRINT("failed to create directory (error: " + itos(dir_error) + "): " + dir_path);
    }

    const String file_path = make_resource_path(p_resource_path);
    Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT("failed to open file for writing: " + file_path);
        return false;
    }

    FileWriter file_writer(file_path, &types_, file);
    file_writer.concatenate(make_copyright_header(file_path.get_file()));
    ModuleWriter *module = memnew(ModuleWriter(&file_writer, "godot"));
    InterfaceWriter *resource_types = memnew(InterfaceWriter(module, "ResourceTypes"));
    PropertyWriter *resource_property = memnew(PropertyWriter(resource_types, p_resource_path));

    TypeDescriptorWriter type_descriptor(resource_property, true);
    type_descriptor.serialize_type_descriptor(descriptor);
    type_descriptor.finish();

    resource_property->finish();
    memdelete(resource_property);
    resource_types->finish();
    memdelete(resource_types);

    // script RPC entries
    {
        const ScriptRpcInfo rpc_info = get_script_rpc_info(p_resource_path);
        if (rpc_info.valid) {
            module->add_import(rpc_info.class_name, p_resource_path);
            // resolve imported name (first-come naming in FileWriter)
            const HashMap<String, String> &imports = file_writer.imports_for(p_resource_path);
            String imported_class_name = rpc_info.class_name;
            if (imports.has("default")) {
                imported_class_name = imports["default"];
            }

            InterfaceWriter *rpc_entries = memnew(InterfaceWriter(module, "GodotUserRPCEntries"));
            PropertyWriter *entry_property = memnew(PropertyWriter(rpc_entries, p_resource_path));
            ObjectWriter *entry_writer = memnew(ObjectWriter(entry_property));
            entry_writer->property_("type", imported_class_name);

            PropertyWriter *rpc_map_property = memnew(PropertyWriter(entry_writer, "procedures"));
            ObjectWriter *rpc_map_writer = memnew(ObjectWriter(rpc_map_property));
            for (const String &method_name : rpc_info.methods) {
                rpc_map_writer->property_(method_name,
                        imported_class_name + "[\"" + method_name.replace("\"", "\\\"") + "\"]");
            }
            rpc_map_writer->finish();
            memdelete(rpc_map_writer);
            rpc_map_property->finish();
            memdelete(rpc_map_property);

            entry_writer->finish();
            memdelete(entry_writer);
            entry_property->finish();
            memdelete(entry_property);
            rpc_entries->finish();
            memdelete(rpc_entries);
        }
    }

    module->finish();
    memdelete(module);
    file_writer.finish();
    file->close();
    return true;
}

// ---------------------------------------------------------------------------
// convenience chain
// ---------------------------------------------------------------------------
bool generate_all_types(const String &p_out_dir, bool p_use_project_settings,
        const Vector<String> &p_scene_paths, const Vector<String> &p_resource_paths) {
    GodotTSDGenerator godot_gen(p_out_dir, p_use_project_settings);
    if (!godot_gen.emit(true)) {
        return false;
    }
    if (!p_scene_paths.is_empty()) {
        SceneTSDGenerator scene_gen(p_out_dir, p_scene_paths);
        if (!scene_gen.emit()) {
            return false;
        }
    }
    if (!p_resource_paths.is_empty()) {
        ResourceTSDGenerator resource_gen(p_out_dir, p_resource_paths);
        if (!resource_gen.emit()) {
            return false;
        }
    }
    return true;
}

} // namespace codegen
} // namespace jsb
