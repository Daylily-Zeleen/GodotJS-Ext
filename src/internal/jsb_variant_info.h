/************************************************************************/
/*  jsb_variant_info.h                                                  */
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
#include "api_tool/api_tool_types.h"
#include "jsb_macros.h"
#include "jsb_variant_util.h"

namespace jsb::internal {
struct FMethodInfoBase {
	bool is_vararg;
	Variant::Type return_type;
	Vector<Variant::Type> argument_types;

#if JSB_DEBUG
	// only for debug
	StringName name_;
	_FORCE_INLINE_ void set_debug_name(const StringName &p_name) { name_ = p_name; }
#else
	_FORCE_INLINE_ void set_debug_name(const StringName &p_name) {}
#endif
};

struct FBuiltinMethodInfo : FMethodInfoBase {
	const api_tool::ApiBuiltInMethod *method_info = nullptr;

	_FORCE_INLINE_ bool check_argc(int p_argc) const {
		return VariantUtil::check_argc(is_vararg, p_argc, method_info->method.default_arguments.size(), argument_types.size());
	}
};

struct FUtilityMethodInfo : FMethodInfoBase {
	const api_tool::ApiUtilityFunction *utility_func = nullptr;

	_FORCE_INLINE_ bool check_argc(int p_argc) const {
		return is_vararg ? p_argc >= argument_types.size() : p_argc == argument_types.size();
	}
};

struct FPrimitiveMemberInfo {
	const api_tool::ApiMemberInfo *member_info = nullptr;
};

struct FConstructorVariantInfo {
	const api_tool::ApiConstructorInfo *constructor_info = nullptr;

	// argument types are cached here for better performance at runtime.
	Vector<Variant::Type> argument_types;
};

struct FConstructorInfo {
	// overloaded constructors for a primitive type.
	// they are matched at runtime by num/type of arguments
	Vector<FConstructorVariantInfo> variants;
};

struct FPropertyInfo2 {
	const api_tool::ApiClassMethod *getter_func;
	const api_tool::ApiClassMethod *setter_func;
	int index;
};

// necessary reflection info for JS func callback (transferred as index with info.Data)
struct VariantInfoCollection {
	// constructors of Variant types
	Vector<FConstructorInfo> constructors;

	// all global utility function in godot (lerp/ease/type_string/print.. etc.)
	Vector<FUtilityMethodInfo> utility_funcs;

	// methods of Variant types
	Vector<FBuiltinMethodInfo> methods;

	// properties of Variant types
	Vector<FPrimitiveMemberInfo> primitive_members;

	// for godot properties which have an implicit (hidden) parameter for getter/setter calls
	Vector<FPropertyInfo2> object_properties;
};
} //namespace jsb::internal
