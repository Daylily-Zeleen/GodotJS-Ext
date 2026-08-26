/************************************************************************/
/*  jsb_resource_loader.h                                               */
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

#include <common/compat/jsb_compat.h>
#include <godot_cpp/classes/resource_format_loader.hpp>

class ResourceFormatLoaderGodotJSScript : public ResourceFormatLoader {
	GDCLASS(ResourceFormatLoaderGodotJSScript, ResourceFormatLoader)

	PackedStringArray recognized_extensions_;

protected:
	static void _bind_methods() {};

public:
	ResourceFormatLoaderGodotJSScript();

	virtual bool _recognize_path(const String &p_path, const StringName &p_type) const override;
	virtual String _get_resource_type(const String &p_path) const override;
	virtual Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
	virtual PackedStringArray _get_recognized_extensions() const override;
	virtual bool _handles_type(const StringName &p_type) const override;
	virtual PackedStringArray _get_dependencies(const String &p_path, bool p_add_types) const override;
	virtual int64_t _get_resource_uid(const String &p_path) const override;
	// virtual bool has_custom_uid_support() const override;
public:
	static bool is_not_godot_resource_script(const String &p_path);
};
