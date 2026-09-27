/************************************************************************/
/*  jsb_type_convert.h                                                  */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
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

#include "jsb_bridge_pch.h"
#include "jsb_class_info.h"
#include "jsb_object_handle.h"

namespace jsb {
struct TypeConvert {
	/**
	 * Returns a string representation of a JavaScript type. For primitives, equivalent to the typeof operator in
	 * JS. For JS objects, we perform additional inspections and display if the object is an array, the Godot
	 * variant type, or the constructor name, when available.
	 */
	static String js_debug_typeof(v8::Isolate *isolate, const v8::Local<v8::Value> &p_jval);

	/**
	 * Translate a Godot object into a javascript object. The type of `p_object_obj` will be automatically exposed to the context if not existed.
	 * @param p_godot_obj non-null godot object pointer
	 */
	static bool gd_obj_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, Object *p_godot_obj, v8::Local<v8::Object> &r_jval);

	/**
	 * return false if strict type check fails.
	 * dead objects return true with a nullptr.
	 * NOTE: ensure p_jval is not empty before calling this function.
	 */
	static bool js_to_gd_obj(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Object *&r_godot_obj);

	_FORCE_INLINE_ static bool gd_var_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const Variant &p_cvar, v8::Local<v8::Value> &r_jval) { return gd_var_to_js(isolate, context, p_cvar, p_cvar.get_type(), GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE, r_jval); }
	// `p_meta` carries the width/signedness of an INT slot, in both directions:
	//
	//   - Godot -> JS: a uint64 return must leave through the unsigned writer,
	//     otherwise an ObjectID (bit 63 = `is_ref_counted`) comes back negative.
	//   - JS -> Godot: a *number* in [2^63, 2^64) has to be read as uint64. The
	//     Variant INT slot holds the same bits either way, but getting the bits
	//     from the double requires the unsigned conversion -- reading it as
	//     int64 is undefined for that range and yields the INT64_MIN sentinel
	//     (e.g. `put_u64(1e19)` landing as 0x8000000000000000).
	static bool gd_var_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const Variant &p_cvar, Variant::Type p_type, GDExtensionClassMethodArgumentMetadata p_meta, v8::Local<v8::Value> &r_jval);
	static bool js_to_gd_var(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Variant::Type p_type, GDExtensionClassMethodArgumentMetadata p_meta, Variant &r_cvar);

	// Metadata-less overloads: callers with no slot metadata to supply (vararg
	// tails, container elements, callbacks). Same as passing NONE.
	_FORCE_INLINE_ static bool gd_var_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const Variant &p_cvar, Variant::Type p_type, v8::Local<v8::Value> &r_jval) { return gd_var_to_js(isolate, context, p_cvar, p_type, GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE, r_jval); }
	_FORCE_INLINE_ static bool js_to_gd_var(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Variant::Type p_type, Variant &r_cvar) { return js_to_gd_var(isolate, context, p_jval, p_type, GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE, r_cvar); }

	/**
	 * Translate js val into gd variant without any type hint
	 */
	static bool js_to_gd_var(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Variant &r_cvar);

	/**
	 * Check if a javascript value `p_val` could be converted into the expected primitive type `p_type`
	 */
	static bool can_convert_strict(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_val, Variant::Type p_type);

	// variant fast check (without checking NativeClassInfo)
	_FORCE_INLINE_ static bool is_variant(const v8::Local<v8::Object> &p_obj) {
		/**
		 * TODO: v8 能够根据编译配置改变 v8::Promise，v8::ArrayBuffer，v8::ArrayBufferView 的内嵌字段数量
		 * 		直接判断并不安全，但是一直依赖工作都很安全，Promise 0, v8::ArrayBuffer 专门用于 PackedByteArray 的处理，v8::ArrayBufferView 只存在于 v8，未在 GodotJS 中使用
		 * Node.js 则是 Promise 为 1
		 */
#if JSB_WITH_NODE
		if (p_obj->IsPromise()) return false;
#endif // JSB_WITH_NODE
		return p_obj->InternalFieldCount() == IF_VariantFieldCount;
	}

	// object fast check (without checking NativeClassInfo)
	_FORCE_INLINE_ static bool is_object(const v8::Local<v8::Object> &p_obj) {
		/**
		 * TODO: v8 能够根据编译配置改变 v8::Promise，v8::ArrayBuffer，v8::ArrayBufferView 的内嵌字段数量
		 * 		直接判断并不安全，但是一直依赖工作都很安全，Promise 0, v8::ArrayBuffer 专门用于 PackedByteArray 的处理，v8::ArrayBufferView 只存在于 v8，未在 GodotJS 中使用
		 * Node.js 则是 Promise 为 1
		 */
		return p_obj->InternalFieldCount() == IF_ObjectFieldCount;
	}

	// object fast check (without checking NativeClassInfo)
	_FORCE_INLINE_ static bool is_object(const v8::Local<v8::Object> &p_obj, NativeClassType::Type p_type) {
		if (!is_object(p_obj)) return false;
		return (NativeClassType::Type)(uintptr_t)p_obj->GetAlignedPointerFromInternalField(IF_ClassType) == p_type;
	}
};
} //namespace jsb
