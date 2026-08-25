/************************************************************************/
/*  jsb_statistics.h                                                    */
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

#include "../impl/shared/jsb_custom_field.h"
#include <godot_cpp/templates/vector.hpp>
namespace jsb {
struct Statistics {
	// num of traced objects
	int objects;

	// num of registered native classes
	int native_classes;

	// num of registered script classes
	int script_classes;

	int cached_string_names;
	uint32_t persistent_objects;

	// allocated num of Variants in pool (only valid in debug mode)
	uint32_t allocated_variants;

	// approximate byte usage of the object table (objects * slot size),
	// precomputed by the runtime side so consumers never touch runtime internals
	int64_t objects_bytes = 0;

	// impl-specific fields
	Vector<impl::CustomField> custom_fields;

	impl::CustomField get_custom_field(const String &name) const {
		for (const impl::CustomField &it : custom_fields) {
			if (it.name == name) {
				return it;
			}
		}
		return {};
	}
};
} //namespace jsb
