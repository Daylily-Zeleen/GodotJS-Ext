/************************************************************************/
/*  api_tool_doc_types.h                                                */
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

#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/local_vector.hpp>

namespace api_tool {

// ============================================================================
// Document sub-structures (stored in .doc files, generated during parsing)
// ============================================================================
namespace internal {
struct ApiNameDescriptionDocument {
	godot::String name;
	godot::String description;
};
} //namespace internal

using ApiMethodDocument = internal::ApiNameDescriptionDocument;

struct ApiSignalDocument {
	godot::LocalVector<godot::PropertyInfo> arguments;
	godot::String name;
	godot::String description;
};

using ApiPropertyDocument = internal::ApiNameDescriptionDocument;

using ApiMemberDocument = internal::ApiNameDescriptionDocument;

using ApiConstantDocument = internal::ApiNameDescriptionDocument;

using ApiEnumValueDocument = internal::ApiNameDescriptionDocument;

struct ApiEnumDocument {
	godot::LocalVector<ApiEnumValueDocument> values;
	godot::String name;
};

using ApiOperatorDocument = internal::ApiNameDescriptionDocument;

struct ApiConstructorDocument {
	godot::String description;
};

// ============================================================================
// Top-level document structures (one .doc file per entity)
// ============================================================================

struct ApiClassDocument {
	godot::LocalVector<ApiMethodDocument> methods;
	godot::LocalVector<ApiSignalDocument> signals;
	godot::LocalVector<ApiPropertyDocument> properties;
	godot::LocalVector<ApiEnumDocument> enums;
	// BuiltInClass-specific fields (empty for regular classes)
	godot::LocalVector<ApiConstantDocument> constants;
	godot::LocalVector<ApiOperatorDocument> operators;
	godot::LocalVector<ApiConstructorDocument> constructors; // 解析时已按 index 排序
	godot::String name;
	godot::String brief_description;
	godot::String description;
};

using ApiUtilityFunctionDocument = internal::ApiNameDescriptionDocument;

struct ApiGlobalEnumDocument {
	godot::LocalVector<ApiEnumValueDocument> values;
	godot::String name;
};

using ApiGlobalConstantDocument = internal::ApiNameDescriptionDocument;
} //namespace api_tool