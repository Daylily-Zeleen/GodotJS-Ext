/************************************************************************/
/*  jsb_codegen_generator.h                                             */
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

// jsb_codegen_generator.h
// C++ port of TSDCodeGen / SceneTSDCodeGen / ResourceTSDCodeGen.
//
// All generators are synchronous (the TS versions were async only to yield to
// the editor main loop for progress reporting); callers run them on a worker
// or accept a blocking editor frame.

#include "jsb_codegen_defs.h"
#include "jsb_codegen_type_db.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace jsb {
namespace codegen {

class FileSplitter;
class ModuleWriter;

// TSDCodeGen: emits typings/godotN.gen.d.ts + typings/jsb.runtime.gen.d.ts
class GodotTSDGenerator {
public:
    // `p_out_dir` is res://-relative ("" means relative to project root).
    GodotTSDGenerator(const String &p_out_dir, bool p_use_project_settings);
    ~GodotTSDGenerator();

    // returns false on error (errors are logged)
    bool emit(bool p_skip_static_types = true);

private:
    String make_path(int p_index) const;
    FileSplitter *new_splitter();
    ModuleWriter &split();
    void cleanup();

    void emit_aliases();
    void emit_singleton(const SingletonDecl &p_singleton);
    void emit_godot_primitive(ModuleWriter &r_cg, const PrimitiveClassDecl &p_cls);
    void emit_godot_class(ModuleWriter &r_cg, const ClassDecl &p_cls, bool p_singleton_mode);
    void emit_utility(const MethodDecl &p_utility_func);
    void emit_global(const GlobalConstantDecl &p_global);
    void emit_runtime_gen();
    void emit_ignored_interfaces();

    int split_index_ = 0;
    String out_dir_;
    bool use_project_settings_;
    TypeDB types_;

    FileSplitter *splitter_ = nullptr;
};

// SceneTSDCodeGen: emits <scene>.nodes.gen.ts next to autogen path
class SceneTSDGenerator {
public:
    SceneTSDGenerator(const String &p_out_dir, const Vector<String> &p_scene_paths);

    bool emit();

private:
    String make_scene_path(const String &p_scene_path, bool p_include_filename = true) const;
    bool emit_scene_node_types(const String &p_scene_path);

    String out_dir_;
    Vector<String> scene_paths_;
    TypeDB types_;
};

// ResourceTSDCodeGen: emits <resource>.gen.ts
class ResourceTSDGenerator {
public:
    ResourceTSDGenerator(const String &p_out_dir, const Vector<String> &p_resource_paths);

    bool emit();

private:
    String make_resource_path(const String &p_resource_path, bool p_include_filename = true) const;
    bool emit_resource_type(const String &p_resource_path);
    struct ScriptRpcInfo {
        bool valid = false;
        String class_name;
        PackedStringArray methods;
    };
    ScriptRpcInfo get_script_rpc_info(const String &p_resource_path) const;

    String out_dir_;
    Vector<String> resource_paths_;
    PackedStringArray script_extensions_;
    TypeDB types_;
};

// convenience: full generation chain used by --generate-types
// (static types install stays in the plugin; this covers godot d.ts + scenes + resources)
bool generate_all_types(const String &p_out_dir, bool p_use_project_settings,
        const Vector<String> &p_scene_paths, const Vector<String> &p_resource_paths);

} // namespace codegen
} // namespace jsb
