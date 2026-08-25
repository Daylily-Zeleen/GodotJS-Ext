/************************************************************************/
/*  jsb_codegen_tables.cpp                                              */
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

// Port of the TypeMutations / InheritedTypeMutations literal tables from
// jsb.editor.codegen.ts (verbatim content, mutators implemented with the
// scanners from jsb_codegen_mutations.cpp).

#include "jsb_codegen_mutations.h"
#include "jsb_codegen_type_db.h"

#include <initializer_list>
#include <utility>

#include <common/internal/jsb_naming_util.h>
#include <common/internal/jsb_settings.h>

namespace jsb {
namespace codegen {

using internal::NamingUtil;

namespace {

GenericParameter gp(const String &p_extends, const String &p_default) {
    GenericParameter p;
    p.extends_ = p_extends;
    p.default_ = p_default;
    return p;
}

PropertyOverride lit(std::initializer_list<String> p_lines) {
    PropertyOverride o;
    o.is_literal = true;
    for (const String &line : p_lines) {
        o.literal_lines.push_back(line);
    }
    return o;
}

PropertyOverride mut(LineMutator p_mutator) {
    PropertyOverride o;
    o.is_literal = false;
    o.mutator = std::move(p_mutator);
    return o;
}

PropertyOverride chain(Vector<LineMutator> p_mutators) {
    return mut(make_chain_mutator(std::move(p_mutators)));
}

ImplementsEntry impl_entry(const String &p_type, Vector<String> p_args = {}) {
    ImplementsEntry e;
    e.type = p_type;
    e.generic_arguments = std::move(p_args);
    return e;
}

// names.get_member / get_class / get_parameter shorthands (identity when camel
// case bindings are off; the tables are built lazily for that reason)
String m(const char *p_name) { return NamingUtil::get_member_name(p_name); }
String c(const char *p_name) { return NamingUtil::get_class_name(p_name); }
String p(const char *p_name) { return NamingUtil::get_parameter_name(p_name); }

void add_override(TypeMutation &r, const char *p_key, PropertyOverride p_override) {
    r.property_overrides.insert(p_key, std::move(p_override));
}

void build_animation_library(TypeMutation &t) {
    t.prelude = { "namespace __PathMappableDummyKeys { const AnimationLibrary: unique symbol }" };
    t.generic_parameters.push_back({ "AnimationName", gp("string", "string") });
    {
        ImplementsEntry e;
        e.type = "PathMappable";
        e.generic_arguments = { "typeof __PathMappableDummyKeys.AnimationLibrary",
            "Record<AnimationName, Animation>" };
        t.implements.push_back(e);
    }
    t.intro = { "[__PathMappableDummyKeys.AnimationLibrary]: Record<AnimationName, Animation>" };
    add_override(t, "add_animation", mut(make_mutate_parameter_type("name", "AnimationName")));
    add_override(t, "remove_animation", mut(make_mutate_parameter_type("name", "AnimationName")));
    add_override(t, "rename_animation",
            chain({ make_mutate_parameter_type("name", "AnimationName"), make_mutate_parameter_type("newname", "AnimationName") }));
    add_override(t, "has_animation",
            chain({ make_mutate_parameter_type("name", "AnimationName") }));
    add_override(t, "get_animation", mut(make_mutate_parameter_type("name", "AnimationName")));
    add_override(t, "animation_added", lit({ "readonly " + m("animation_added") + ": Signal<(name: StringName) => void>" }));
    add_override(t, "animation_removed", lit({ "readonly " + m("animation_removed") + ": Signal<(name: StringName) => void>" }));
    add_override(t, "animation_renamed",
            lit({ "readonly " + m("animation_renamed") + ": Signal<(name: StringName, " + p("to_name") + ") => void>" }));
    add_override(t, "animation_changed", lit({ "readonly " + m("animation_changed") + ": Signal<(name: StringName) => void>" }));
}

void build_animation_mixer(TypeMutation &t) {
    t.prelude = { "namespace __PathMappableDummyKeys { const AnimationMixer: unique symbol }" };
    t.generic_parameters.push_back({ "NodeMap", gp("NodePathMap", "any") });
    t.generic_parameters.push_back({ "LibraryMap", gp("AnimationMixerPathMap", "any") });
    t.super_generic_arguments = { "NodeMap" };
    {
        ImplementsEntry e;
        e.type = "PathMappable";
        e.generic_arguments = { "typeof __PathMappableDummyKeys.AnimationMixer", "LibraryMap" };
        t.implements.push_back(e);
    }
    t.intro = { "[__PathMappableDummyKeys.AnimationMixer]: LibraryMap" };
    const String static_mixer_path = "StaticAnimationMixerPath<LibraryMap>";
    add_override(t, "add_animation_library",
            chain({ make_mutate_template("Name extends keyof LibraryMap"),
                    make_mutate_parameter_type("name", "Name"),
                    make_mutate_parameter_type("library", "LibraryMap[Name]") }));
    add_override(t, "remove_animation_library",
            chain({ make_mutate_template("Name extends keyof LibraryMap"), make_mutate_parameter_type("name", "Name") }));
    add_override(t, "rename_animation_library",
            chain({
                    make_mutate_template("FromName extends keyof LibraryMap, ToName extends ExtractValueKeys<LibraryMap, LibraryMap[FromName]>"),
                    make_mutate_parameter_type("name", "FromName"),
                    make_mutate_parameter_type("newname", "ToName"),
            }));
    add_override(t, "has_animation_library",
            chain({ make_mutate_template("Name extends keyof LibraryMap"), make_mutate_parameter_type("name", "Name") }));
    add_override(t, "get_animation_library",
            chain({ make_mutate_template("Name extends keyof LibraryMap"),
                    make_mutate_parameter_type("name", "Name"),
                    make_mutate_return_type("LibraryMap[Name]") }));
    add_override(t, "get_animation_library_list",
            mut(make_mutate_return_type("keyof LibraryMap extends GAny ? GArray<keyof LibraryMap> : GArray")));
    add_override(t, "has_animation",
            chain({ make_mutate_template("Name extends " + static_mixer_path), make_mutate_parameter_type("name", "Name") }));
    add_override(t, "get_animation",
            chain({ make_mutate_template("Name extends " + static_mixer_path),
                    make_mutate_parameter_type("name", "Name"),
                    make_mutate_return_type("ResolveAnimationMixerPath<LibraryMap, Name>") }));
}

void build_animation_player(TypeMutation &t) {
    const String path = "StaticAnimationMixerPath<LibraryMap>";
    add_override(t, "animation_set_next",
            chain({ make_mutate_parameter_type("animation_from", path), make_mutate_parameter_type("animation_to", path) }));
    add_override(t, "animation_get_next",
            chain({ make_mutate_parameter_type("animation_from", path), make_mutate_return_type(path) }));
    add_override(t, "set_blend_time",
            chain({ make_mutate_parameter_type("animation_from", path), make_mutate_parameter_type("animation_to", path) }));
    add_override(t, "get_blend_time",
            chain({ make_mutate_parameter_type("animation_from", path), make_mutate_parameter_type("animation_to", path) }));
    add_override(t, "play", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "play_section", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "play_backwards", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "play_section_with_markers_backwards", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "play_section_backwards", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "play_with_capture", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "queue", mut(make_mutate_parameter_type("name", path)));
    add_override(t, "current_animation",
            lit({ "get " + m("current_animation") + "(): " + path,
                    "set " + m("current_animation") + "(value: " + path + ")" }));
    add_override(t, "assigned_animation",
            lit({ "get " + m("assigned_animation") + "(): " + path,
                    "set " + m("assigned_animation") + "(value: " + path + ")" }));
    add_override(t, "autoplay", lit({ "get autoplay(): " + path, "set autoplay(value: " + path + ")" }));
    add_override(t, "current_animation_changed",
            lit({ "readonly " + m("current_animation_changed") + ": Signal<(name: " + path + ") => void>" }));
    add_override(t, "animation_changed",
            lit({ "readonly " + m("current_animation_changed") + ": Signal<(" + p("old_name") + ": " + path + ", "
                            + p("new_name") + ": " + path + ") => void>" }));
}

void build_callable(TypeMutation &t) {
    t.intro = {
        "/**",
        " * Create godot Callable without a bound object.",
        " */",
        "static create<F extends Function>(fn: F): Callable<F>",
        "/**",
        " * Create godot Callable with a bound object `self`.",
        " */",
        "static create<S extends Object, F extends (this: S, ...args: any[]) => any>(self: S, fn: F): Callable<F>",
    };
    t.generic_parameters.push_back({ "T", gp("Function", "Function") });
    add_override(t, "bind",
            chain({ make_mutate_template("A extends any[]"),
                    make_mutate_parameter_type("...varargs", "A"),
                    make_mutate_return_type("Callable<BindRight<T, A>>") }));
    add_override(t, "call", lit({ "call: T" }));
}

void build_camera_feed(TypeMutation &t) {
    t.prelude = {
        "namespace CameraFeed {",
        "    type FeedFormat = GDictionary<{",
        "        width: int64",
        "        height: int64",
        "        format: string",
        "        frame_numerator?: int64",
        "        frame_denominator?: int64",
        "        pixel_format?: uint32",
        "    }>",
        "}",
        "",
    };
    add_override(t, "formats",
            lit({ "get formats(): GArray<CameraFeed.FeedFormat>", "set formats(value: GArray<CameraFeed.FeedFormat>)" }));
}

void build_editor_undo_redo_manager(TypeMutation &t) {
    const String object_class = c("Object");
    add_override(t, "add_do_method",
            lit({ m("add_do_method") + "<T extends " + object_class + ", M extends GodotNames<T>>(object: T, method: M, ...args: ResolveGodotNameParameters<T, M>): void" }));
    add_override(t, "add_undo_method",
            lit({ m("add_undo_method") + "<T extends " + object_class + ", M extends GodotNames<T>>(object: T, method: M, ...args: ResolveGodotNameParameters<T, M>): void" }));
    add_override(t, "add_do_property",
            lit({ m("add_do_property") + "<T extends " + object_class + ", P extends GodotNames<T>>(object: T, property: P, value: ResolveGodotNameValue<T, P>): void" }));
    add_override(t, "add_undo_property",
            lit({ m("add_undo_property") + "<T extends " + object_class + ", P extends GodotNames<T>>(object: T, property: P, value: ResolveGodotNameValue<T, P>): void" }));
}

void append_garray_overrides(TypeMutation &t) {
    const String elem = "GArrayElement<T>";
    add_override(t, "set", lit({ "set<I extends int64>(index: I, value: GArrayElement<T, I>): void" }));
    const char *param_value_keys[] = { "push_back", "push_front", "append", "insert", "fill", "erase", "count", "has" };
    for (const char *key : param_value_keys) {
        add_override(t, key, mut(make_mutate_parameter_type("value", elem)));
    }
    add_override(t, "bsearch", mut(make_mutate_parameter_type("value", elem)));
    add_override(t, "bsearch_custom",
            chain({ make_mutate_parameter_type("value", elem),
                    make_mutate_parameter_type("func", "Callable<(a: GArrayElement<T>, b: GArrayElement<T>) => boolean>") }));
    add_override(t, "find", mut(make_mutate_parameter_type("what", elem)));
    add_override(t, "rfind", mut(make_mutate_parameter_type("what", elem)));
    add_override(t, "get", lit({ "get<I extends int64>(index: I): GArrayElement<T, I>" }));
    const char *ret_elem_keys[] = { "front", "back", "pick_random", "pop_back", "pop_front", "pop_at", "min", "max" };
    for (const char *key : ret_elem_keys) {
        add_override(t, key, mut(make_mutate_return_type(elem)));
    }
    add_override(t, "sort_custom",
            mut(make_mutate_parameter_type("func", "Callable<(a: GArrayElement<T>, b: GArrayElement<T>) => boolean>")));
    add_override(t, "all", mut(make_mutate_parameter_type("callable", "Callable<(value: GArrayElement<T>) => boolean>")));
    add_override(t, "any", mut(make_mutate_parameter_type("callable", "Callable<(value: GArrayElement<T>) => boolean>")));
    add_override(t, "filter",
            chain({ make_mutate_parameter_type("callable", "Callable<(value: GArrayElement<T>) => boolean>"),
                    make_mutate_return_type("GArray<GArrayElement<T>>") }));
    add_override(t, "map",
            chain({ make_mutate_parameter_type("callable", "Callable<(value: GArrayElement<T>) => U>"),
                    make_mutate_return_type("GArray<U>"),
                    make_mutate_template("U extends GAny") }));
    add_override(t, "append_array", mut(make_mutate_parameter_type("array", "GArray<GArrayElement<T>>")));
    add_override(t, "duplicate", mut(make_mutate_return_type("this")));
    add_override(t, "slice", mut(make_mutate_return_type("GArray<GArrayElement<T>>")));
}

void build_garray(TypeMutation &t) {
    t.generic_parameters.push_back({ "T", gp(String(kGodotAnyType) + " | " + kGodotAnyType + "[]",
                                             String(kGodotAnyType) + " | " + kGodotAnyType + "[]") });
    t.intro = {
        "/** Builder function that returns a GArray populated with elements from a JS array. */",
        "static create<A extends any[]>(elements: A): GValueWrap<A>",
        "static create<A extends GArray<any>(",
        "    elements: A extends GArray<infer T>",
        "        ? [T] extends [any[]]",
        "            ? { [I in keyof T]: GDataStructureCreateValue<T[I]> }",
        "            : Array<GDataStructureCreateValue<T>>",
        "        : never",
        "): GValueWrap<A>",
        "static create<E extends GAny>(elements: Array<GDataStructureCreateValue<E>>): GArray<E>",
        "[Symbol.iterator](): IteratorObject<GArrayElement<T>>",
        "/** Returns a Proxy that targets this GArray but behaves similar to a JavaScript array. */",
        "proxy<Write extends boolean = false>(): Write extends true ? GArrayProxy<GArrayElement<T>> : GArrayReadProxy<GArrayElement<T>>",
        "",
        m("set_indexed") + "<I extends int64>(index: I, value: GArrayElement<T, I>): void",
        m("get_indexed") + "<I extends int64>(index: I): GArrayElement<T, I>",
        "/** [jsb utility method] Converts a GArray to a JavaScript T[]. */",
        m("to_array") + "(): T[]",
    };
    // NOTE line 3 of intro in TS is `static create<A extends GArray<any>>(` — keep verbatim
    t.intro.write[2] = "static create<A extends GArray<any>>(";
    append_garray_overrides(t);
}

void append_gdictionary_overrides(TypeMutation &t) {
    add_override(t, "assign", mut(make_mutate_parameter_type("dictionary", "T")));
    add_override(t, "merge", mut(make_mutate_parameter_type("dictionary", "T")));
    add_override(t, "merged",
            chain({ make_mutate_parameter_type("dictionary", "GDictionary<U>"),
                    make_mutate_return_type("GDictionary<T & U>"),
                    make_mutate_template("U") }));
    add_override(t, "has", mut(make_mutate_parameter_type("key", "keyof T")));
    add_override(t, "has_all",
            mut(make_mutate_parameter_type("keys", "keyof T extends GAny ? GArray<keyof T> : GArray")));
    add_override(t, "find_key",
            chain({ make_mutate_parameter_type("value", "T[keyof T]"), make_mutate_return_type("keyof T") }));
    add_override(t, "erase", mut(make_mutate_parameter_type("key", "keyof T")));
    add_override(t, "keys", mut(make_mutate_return_type("keyof T extends GAny ? GArray<keyof T> : GArray")));
    add_override(t, "values",
            mut(make_mutate_return_type(
                    "UndefinedToNull<T[keyof T]> extends GAny ? GArray<UndefinedToNull<T[keyof T]>> : GArray")));
    add_override(t, "duplicate", mut(make_mutate_return_type("GDictionary<T>")));
    add_override(t, "get",
            chain({ make_mutate_parameter_type("key", "K"),
                    make_mutate_return_type("UndefinedToNull<T[K]>"),
                    make_mutate_template("K extends keyof T") }));
    add_override(t, "get_or_add",
            chain({ make_mutate_parameter_type("key", "K"),
                    make_mutate_return_type("UndefinedToNull<T[K]>"),
                    make_mutate_parameter_type("default_", "T[K]"),
                    make_mutate_template("K extends keyof T") }));
    add_override(t, "set",
            chain({ make_mutate_parameter_type("key", "K"),
                    make_mutate_parameter_type("value", "T[K]"),
                    make_mutate_template("K extends keyof T") }));
}

void build_gdictionary(TypeMutation &t) {
    t.prelude = {
        "type GArrayCreateSource<T> = ReadonlyArray<T> | {",
        "    [Symbol.iterator](): IteratorObject<GDataStructureCreateValue<T>>;",
        "    [K: number]: GDataStructureCreateValue<T>;",
        "}",
        "type GDataStructureCreateValue<V> = V | (",
        "     V extends GArray<infer T>",
        " ? [T] extends [any[]]",
        "     ? GArrayCreateSource<{ [I in keyof T]: GDataStructureCreateValue<T[I]> }>",
        "     : GArrayCreateSource<GDataStructureCreateValue<T>>",
        " : V extends GDictionary<infer T>",
        "     ? { [K in keyof T]: GDataStructureCreateValue<T[K]> }",
        "     : never",
        "     )",
    };
    t.generic_parameters.push_back({ "T", gp("", "Record<any, any>") });
    t.intro = {
        "/** Builder function that returns a GDictionary with properties populated from a source JS object. */",
        "static create<V extends { [key: number | string]: GWrappableValue }>(properties: V): GValueWrap<V>",
        "static create<V extends GDictionary<any>>(properties: V extends GDictionary<infer T> ? { [K in keyof T]: GDataStructureCreateValue<T[K]> } : never): V",
        "[Symbol.iterator](): IteratorObject<{ key: any, value: any }>",
        "/** Returns a Proxy that targets this GDictionary but behaves similar to a regular JavaScript object. Values are exposed as enumerable properties, so Object.keys(), Object.entries() etc. will work. */",
        "proxy<Write extends boolean = false>(): Write extends true ? GDictionaryProxy<T> : GDictionaryReadProxy<T>",
        "",
        m("set_keyed") + "<K extends keyof T>(key: K, value: T[K]): void",
        m("get_keyed") + "<K extends keyof T>(key: K): UndefinedToNull<T[K]>",
    };
    append_gdictionary_overrides(t);
}

void build_input(TypeMutation &t) {
    add_override(t, "is_action_pressed", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "is_action_just_pressed", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "is_action_just_released", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "get_action_strength", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "get_action_raw_strength", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "get_axis",
            chain({ make_mutate_parameter_type("negative_action", "InputActionName"),
                    make_mutate_parameter_type("positive_action", "InputActionName") }));
    add_override(t, "get_vector",
            chain({ make_mutate_parameter_type("negative_x", "InputActionName"),
                    make_mutate_parameter_type("positive_x", "InputActionName"),
                    make_mutate_parameter_type("negative_y", "InputActionName"),
                    make_mutate_parameter_type("positive_y", "InputActionName") }));
    add_override(t, "action_press", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "action_release", mut(make_mutate_parameter_type("action", "InputActionName")));
}

void build_input_event(TypeMutation &t) {
    add_override(t, "is_action", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "is_action_pressed", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "is_action_released", mut(make_mutate_parameter_type("action", "InputActionName")));
    add_override(t, "get_action_strength", mut(make_mutate_parameter_type("action", "InputActionName")));
}

void build_node(TypeMutation &t) {
    t.prelude = { "namespace __PathMappableDummyKeys { const Node: unique symbol }" };
    t.intro = { "[__PathMappableDummyKeys.Node]: Map" };
    t.generic_parameters.push_back({ "Map", gp("NodePathMap", "any") });
    {
        ImplementsEntry e;
        e.type = "PathMappable";
        e.generic_arguments = { "typeof __PathMappableDummyKeys.Node", "Map" };
        t.implements.push_back(e);
    }
    add_override(t, "add_child", mut(make_mutate_parameter_type("node", "NodePathMapChild<Map>")));
    add_override(t, "get_child", mut(make_mutate_return_type("NodePathMapChild<Map>")));
    add_override(t, "get_children", mut(make_mutate_return_type("GArray<NodePathMapChild<Map>>")));
    add_override(t, "get_node",
            lit({ m("get_node") + "<Path extends StaticNodePath<Map>, Default = never>(path: Path): ResolveNodePath<Map, Path, Default>" }));
    add_override(t, "get_node_or_null",
            lit({
                    m("get_node_or_null")
                            + "<Path extends StaticNodePath<Map, undefined | Node>, Default = null>(path: Path): null | "
                              "ResolveNodePath<Map, Path, Default, undefined | Node>",
                    m("get_node_or_null") + "(path: NodePath | string): null | Node",
            }));
    add_override(t, "get_tree", mut(make_mutate_return_type("SceneTree")));
    add_override(t, "move_child", mut(make_mutate_parameter_type(p("child_node"), "NodePathMapChild<Map>")));
    add_override(t, "remove_child", mut(make_mutate_parameter_type("node", "NodePathMapChild<Map>")));
    add_override(t, "validate_property", mut(make_mutate_parameter_type("property", "GDictionary<PropertyInfo>")));
    const String object_class = c("Object");
    add_override(t, "rpc",
            lit({ m("rpc") + "<Method extends GodotRPCNames<this>>(method: Method, ...varargs: ResolveGodotRPCParameters<this, Method>): Error" }));
    add_override(t, "rpc_id",
            lit({ m("rpc_id")
                            + "<Method extends GodotRPCNames<this>>(" + p("peer_id") + ": int64, method: Method, ...varargs: "
                              "ResolveGodotRPCParameters<this, Method>): Error" }));
}

void build_object(TypeMutation &t) {
    add_override(t, "call",
            lit({ "call<M extends GodotNames<this>>(method: M, ...args: ResolveGodotNameParameters<this, NoInfer<M>>): "
                  "ResolveGodotReturnType<this, NoInfer<M>>" }));
    add_override(t, "call_deferred",
            lit({ m("call_deferred")
                            + "<M extends GodotNames<this>>(method: M, ...args: ResolveGodotNameParameters<this, NoInfer<M>>): void" }));
    add_override(t, "set_deferred",
            lit({ m("set_deferred")
                            + "<P extends GodotNames<this>>(property: P, value: ResolveGodotNameValue<this,  NoInfer<P>>): void" }));
    add_override(t, "callv",
            lit({ "callv<M extends GodotNames<this>>(method: M, argArray: GArray<ResolveGodotNameParameters<this, NoInfer<M>>>): "
                  "ResolveGodotReturnType<this, NoInfer<M>>" }));
    add_override(t, "get_property_list", mut(make_mutate_return_type("GArray<GDictionary<PropertyInfo>>")));
    add_override(t, "get_method_list", mut(make_mutate_return_type("GArray<GDictionary<MethodInfo>>")));
    add_override(t, "get_script", mut(make_mutate_return_type("null | Script")));
    add_override(t, "set_script", mut(make_mutate_parameter_type("script", "null | Script")));
    add_override(t, "get_incoming_connections", mut(make_mutate_return_type("GArray<GDictionary<SignalConnection>>")));
    add_override(t, "get_signal_connection_list", mut(make_mutate_return_type("GArray<GDictionary<SignalConnection>>")));
    add_override(t, "get_signal_list", mut(make_mutate_return_type("GDictionary<Record<string, GDictionary<MethodInfo>>>")));
    add_override(t, "_get_property_list", mut(make_mutate_return_type("GArray<GDictionary<PropertyInfo>>")));
    add_override(t, "_validate_property", mut(make_mutate_parameter_type("property", "GDictionary<PropertyInfo>")));
}

void build_class_db(TypeMutation &t) {
    add_override(t, "class_get_method_list", mut(make_mutate_return_type("GArray<GDictionary<MethodInfo>>")));
    add_override(t, "class_get_property_list", mut(make_mutate_return_type("GArray<GDictionary<PropertyInfo>>")));
    add_override(t, "class_get_signal_list", mut(make_mutate_return_type("GArray<GDictionary<MethodInfo>>")));
}

void build_packed_to_array(TypeMutation &t, const char *p_js_type, const char *p_element_type, const char *p_class_name,
        bool p_is_byte_buffer) {
    if (p_is_byte_buffer) {
        t.intro = {
            "/** [jsb utility method] Converts a PackedByteArray to a JavaScript ArrayBuffer. */",
            m("to_array_buffer") + "(): ArrayBuffer",
            "/** [jsb utility method] Converts a PackedByteArray to a JavaScript int8[]. */",
            m("to_array") + "(): int8[]",
        };
        return;
    }
    // TS: the doc text names the concrete PackedXxxArray; the declared element
    // type inside godot.d.ts drops the `godot.` namespace prefix.
    t.intro = {
        "/** [jsb utility method] Converts a " + String(p_class_name) + " to a JavaScript " + String(p_js_type)
                + "[]. */",
        m("to_array") + "(): " + String(p_element_type) + "[]",
    };
}

void build_packed_scene(TypeMutation &t) {
    t.generic_parameters.push_back({ "T", gp("Node", "Node") });
    add_override(t, "pack", mut(make_mutate_parameter_type("path", "T")));
    add_override(t, "instantiate", mut(make_mutate_return_type("T")));
}

void build_resource(TypeMutation &t) {
    add_override(t, "duplicate", mut(make_mutate_return_type("this")));
}

void build_resource_loader(TypeMutation &t) {
    add_override(t, "load",
            lit({
                    "static load<Path extends keyof ResourceTypes>(path: Path, " + p("type_hint")
                            + "?: string /* = \"\" */, " + p("cache_mode") + "?: ResourceLoader.CacheMode /* = 1 */): ResourceTypes[Path]",
                    "static load(path: string, " + p("type_hint") + "?: string /* = \"\" */, " + p("cache_mode")
                            + "?: ResourceLoader.CacheMode /* = 1 */): Resource",
            }));
    add_override(t, "load_threaded_get",
            lit({
                    "static " + m("load_threaded_get") + "<Path extends keyof ResourceTypes>(path: Path): ResourceTypes[Path]",
                    "static " + m("load_threaded_get") + "(path: string): Resource",
            }));
}

void build_signal(TypeMutation &t) {
    t.intro = { m("as_promise")
                        + "(): Parameters<T> extends [] ? Promise<void> : Parameters<T> extends [infer R] ? Promise<R> : "
                          "Promise<Parameters<T>>" };
    t.generic_parameters.push_back({ "T", gp("(...args: any[]) => void", "(...args: any[]) => void") });
    add_override(t, "connect", mut(make_mutate_parameter_type("callable", "Callable<T>")));
    add_override(t, "disconnect", mut(make_mutate_parameter_type("callable", "Callable<T>")));
    add_override(t, "is_connected", mut(make_mutate_parameter_type("callable", "Callable<T>")));
    add_override(t, "emit", lit({ "emit: T" }));
}

void build_undo_redo(TypeMutation &t) {
    const String object_class = c("Object");
    add_override(t, "add_do_property",
            lit({ m("add_do_property") + "<T extends " + object_class + ", P extends GodotNames<T>>(object: T, property: P, value: ResolveGodotNameValue<T, P>): void" }));
    add_override(t, "add_undo_property",
            lit({ m("add_undo_property") + "<T extends " + object_class + ", P extends GodotNames<T>>(object: T, property: P, value: ResolveGodotNameValue<T, P>): void" }));
}

const HashMap<String, TypeMutation> *s_direct_table = nullptr;

void build_direct_table() {
    HashMap<String, TypeMutation> *table = new HashMap<String, TypeMutation>();
    TypeMutation t;

    t = TypeMutation();
    build_animation_library(t);
    table->insert("AnimationLibrary", t);

    t = TypeMutation();
    build_animation_mixer(t);
    table->insert("AnimationMixer", t);

    t = TypeMutation();
    build_animation_player(t);
    table->insert("AnimationPlayer", t);

    t = TypeMutation();
    build_callable(t);
    table->insert("Callable", t);

    t = TypeMutation();
    build_camera_feed(t);
    table->insert("CameraFeed", t);

    t = TypeMutation();
    build_editor_undo_redo_manager(t);
    table->insert("EditorUndoRedoManager", t);

    t = TypeMutation();
    build_garray(t);
    table->insert("GArray", t);

    t = TypeMutation();
    build_gdictionary(t);
    table->insert("GDictionary", t);

    t = TypeMutation();
    build_input(t);
    table->insert("Input", t);

    t = TypeMutation();
    build_input_event(t);
    table->insert("InputEvent", t);

    t = TypeMutation();
    build_node(t);
    table->insert("Node", t);

    t = TypeMutation();
    build_object(t);
    table->insert(c("Object"), t); // [names.get_class("Object")] key

    t = TypeMutation();
    build_class_db(t);
    table->insert("ClassDB", t);

    t = TypeMutation();
    build_packed_to_array(t, "PackedByteArray", "int8", "PackedByteArray", true);
    table->insert("PackedByteArray", t);

    struct PackedEntry {
        const char *cls;
        const char *js_type;
        const char *element_type;
    };
    static const PackedEntry packed_entries[9] = {
        { "PackedInt32Array", "int32", "int32" },
        { "PackedInt64Array", "int64", "int64" },
        { "PackedFloat32Array", "float32", "float32" },
        { "PackedFloat64Array", "float64", "float64" },
        { "PackedStringArray", "string", "string" },
        { "PackedVector2Array", "godot.Vector2", "Vector2" },
        { "PackedVector3Array", "godot.Vector3", "Vector3" },
        { "PackedColorArray", "godot.Color", "Color" },
        { "PackedVector4Array", "godot.Vector4", "Vector4" },
    };
    for (int i = 0; i < 9; ++i) {
        const PackedEntry &entry = packed_entries[i];
        t = TypeMutation();
        build_packed_to_array(t, entry.js_type, entry.element_type, entry.cls, false);
        table->insert(entry.cls, t);
    }

    t = TypeMutation();
    build_packed_scene(t);
    table->insert("PackedScene", t);

    t = TypeMutation();
    build_resource(t);
    table->insert("Resource", t);

    t = TypeMutation();
    build_resource_loader(t);
    table->insert("ResourceLoader", t);

    t = TypeMutation();
    build_signal(t);
    table->insert("Signal", t);

    t = TypeMutation();
    build_undo_redo(t);
    table->insert("UndoRedo", t);

    s_direct_table = table;
}

const HashMap<String, TypeMutation> *s_inherited_table = nullptr;

void build_inherited_table() {
    HashMap<String, TypeMutation> *table = new HashMap<String, TypeMutation>();

    {
        TypeMutation t;
        t.generic_parameters.push_back({ "NodeMap", gp("NodePathMap", "any") });
        t.generic_parameters.push_back({ "LibraryMap", gp("AnimationMixerPathMap", "any") });
        t.super_generic_arguments = { "NodeMap", "LibraryMap" };
        table->insert("AnimationMixer", t);
    }
    {
        TypeMutation t;
        t.generic_parameters.push_back({ "Map", gp("NodePathMap", "any") });
        t.super_generic_arguments = { "Map" };
        table->insert("Node", t);
    }

    s_inherited_table = table;
}

} // namespace

const TypeMutation *find_direct_mutation(const String &p_name) {
    if (s_direct_table == nullptr) {
        build_direct_table();
    }
    const TypeMutation *it = s_direct_table->getptr(p_name);
    return it;
}

const TypeMutation *find_inherited_mutation(const String &p_name) {
    if (s_inherited_table == nullptr) {
        build_inherited_table();
    }
    const TypeMutation *it = s_inherited_table->getptr(p_name);
    return it;
}

TypeMutation build_intrinsic_mutation(const String &p_name, const TypeDB *p_types) {
    TypeMutation mutation;
    if (p_types == nullptr) {
        return mutation;
    }
    const PrimitiveClassDecl *primitive = p_types->find_primitive(p_name);
    if (primitive == nullptr) {
        // class_type_mutation only applies to primitive class infos in TS
        return mutation;
    }

    if (primitive->has_element_type) {
        const String element_type_name = p_types->get_variant_to_name(primitive->element_type);
        mutation.intro.push_back("set_indexed(index: number, value: " + element_type_name + "): void");
        mutation.intro.push_back("get_indexed(index: number): " + element_type_name);
    }
    if (primitive->is_keyed) {
        mutation.intro.push_back("set_keyed(index: any, value: any): void");
        mutation.intro.push_back("get_keyed(index: any): any");
    }
    return mutation;
}

} // namespace codegen
} // namespace jsb
