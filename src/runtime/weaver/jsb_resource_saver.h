/************************************************************************/
/*  jsb_resource_saver.h                                                */
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

#include "../compat/jsb_compat.h"
#include <godot_cpp/classes/resource_format_saver.hpp>

class ResourceFormatSaverGodotJSScript : public ResourceFormatSaver {
	GDCLASS(ResourceFormatSaverGodotJSScript, ResourceFormatSaver)

protected:
	static void _bind_methods() {};

private:
	bool add_uid_to_source(String &p_r_source, const String &p_path, int64_t p_uid = -1) const;

public:
	virtual Error _save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) override;
	virtual PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
	virtual bool _recognize(const Ref<Resource> &p_resource) const override;
	virtual Error _set_uid(const String &p_path, int64_t p_uid) override;
};

