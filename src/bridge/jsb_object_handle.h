/************************************************************************/
/*  jsb_object_handle.h                                                 */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_bit_field.h"
#include "jsb_bridge_pch.h"

namespace jsb {
enum EInternalFields {
	IF_Pointer = 0, // pointer to object (used by object/variant both)
	IF_ClassType = 1, // class type for non-valuetype objects (not a Variant)

	IF_VariantFieldCount = 1,
	IF_ObjectFieldCount = 2,
};

enum ObjectBindingFlags : uint8_t {
	OBF_PERSIST = 1 << 0,
	OBF_GD_OBJ = 1 << 1,
	OBF_GD_REFCOUNTED = 1 << 2,
	OBF_JS_OWNED = 1 << 3,
};

// godot Object classes or c++ native wrapped classes are registered in an object registry in Environment.
// godot Variant (valuetype) DO NOT have it's ObjectHandle.
struct ObjectHandle {
	NativeClassID class_id;

#if JSB_DEBUG
	// The raw pointer to the native object.
	// It must be a unique pointer which implies that different objects have different addresses.
	//NOTE it's useless at runtime now. we hold it here to validate the object binding for debugging only.
	void *pointer;
#endif

	// this reference is initially weak and hooked on v8 gc callback.
	// it becomes a strong reference after the `ref_count_` explicitly increased.
	v8::Global<v8::Object> ref_;

	templates::BitField<ObjectBindingFlags> flags;

	// // True when a non-refcounted Godot Object is explicitly JS-owned
	// // (constructed from JS via native class constructor path).
	// // Engine-owned non-refcounted objects (for example sub-objects returned
	// // from APIs) must stay false so GC finalization only unbinds them.
	// bool js_owned_non_ref_ = false;

	_FORCE_INLINE_ bool is_persist() const { return flags.has_flag(OBF_PERSIST); }
	_FORCE_INLINE_ bool is_gd_obj() const { return flags.has_flag(OBF_GD_OBJ); }
	_FORCE_INLINE_ bool is_gd_refcounted() const { return flags.has_flag(OBF_GD_REFCOUNTED); }
	_FORCE_INLINE_ bool is_js_owned() const { return flags.has_flag(OBF_JS_OWNED); }
};

} //namespace jsb

