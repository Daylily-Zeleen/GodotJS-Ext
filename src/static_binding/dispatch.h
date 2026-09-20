/************************************************************************/
/*  dispatch.h                                                          */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

/**
 * Builtin 下标与键访问不提供静态 thunk，仍走反射访问器。
 * JS 的 obj[index/key] 语法需要 Proxy 才能模拟；这里仅暴露显式访问方法。
 */

#if JSB_WITH_STATIC_BINDINGS

#	include <cstdint>
#	include <godot_cpp/variant/variant.hpp>

// forward declarations only -- pulling in the full v8 header here would leak
// engine-specific include paths into every translation unit including this one.
namespace v8 {
template <class T>
class FunctionCallbackInfo;
class Value;
} // namespace v8
#	include <godot_cpp/variant/string_name.hpp>

namespace godot {
class StringName;
} // namespace godot

namespace jsb::static_binding {

using ThunkFn = void (*)(const v8::FunctionCallbackInfo<v8::Value> &);

// Generated operator-table declarations open their own namespace and require ThunkFn.
#	include "gen/builtin_operator_tables.gen.h"

// A single indexed property lookup yields BOTH accessor thunks: the getter
// and setter of one property always share the same (class, property) entry,
// so resolving them separately would run the class search twice.
struct IndexedPropertyThunks {
	ThunkFn getter = nullptr;
	ThunkFn setter = nullptr;
};

// Signature hashes are not unique within a builtin type; include the method name.
const ThunkFn find_builtin_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name, uint32_t p_hash);

// Member accessors are keyed by base Variant type and member name (e.g. Vector2::"x").
const ThunkFn find_builtin_member_getter_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name);
const ThunkFn find_builtin_member_setter_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name);

// Returns the per-type constructor callback, which resolves argc/argument types
// at runtime and invokes a matching builtin_ctor_thunk or throws.
const ThunkFn find_ctor_adapter(godot::Variant::Type p_vt);

// Same for utility functions.
const ThunkFn find_utility_thunk(const godot::StringName &p_name, uint32_t p_hash);

// Object-derived class methods: p_class is the engine class name
// (e.g. "Node"), p_name disambiguates same-hash overloads, hash is the
// official method hash.
const ThunkFn find_class_method_thunk(const godot::StringName &p_class,
		const godot::StringName &p_name,
		uint32_t p_hash);

// Indexed property accessors: one thunk per property side; the
// constant index lives on the accessor, not on the shared backing method.
// p_name is the PROPERTY name as exposed in the api json. Either side may be
// null when the api json does not provide the corresponding accessor method.
const IndexedPropertyThunks find_indexed_property_thunk(const godot::StringName &p_class,
		const godot::StringName &p_name);

// Default values of the class method carried as a class thunk's data payload.
//
// A class thunk holds no default literal of its own (the engine MethodBind fills
// trailing omitted arguments), so a defaulted position the caller covered with an
// explicit `undefined` is resolved through the method record that registration
// attaches for methods which have defaults. The thunk substitutes the value into
// its own argument slot and keeps calling on the static path.
//
// Returns the record's default-value array and, through r_count, its length;
// nullptr when there is nothing to substitute from.
//
// Defined in the runtime bridge (jsb_object_bindings.cpp), which keeps the
// api_tool types out of the static-binding headers.
const godot::Variant *class_method_defaults(const void *p_method_info, uint32_t &r_count);

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
