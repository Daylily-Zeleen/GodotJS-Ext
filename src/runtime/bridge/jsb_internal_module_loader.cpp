/************************************************************************/
/*  jsb_internal_module_loader.cpp                                      */
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

#include "jsb_internal_module_loader.h"

#include "jsb_amd_module_loader.h"
#include "jsb_environment.h"
#include "jsb_runtime_preset.h"

namespace jsb {
// The module id this loader is registered for resolves to an embedded preset
// bundle entry, so evaluate that entry as an AMD source -- the same path the
// runtime bundle itself takes (`AMDModuleLoader::load_source`), just for a
// module that the runtime asks for by id instead of at startup.
//
// A missing/empty entry is a hard failure rather than a silent success: the
// caller (`Environment::_load_module`) treats false as "module unavailable" and
// reports it, while returning true with an empty module would hand back a
// module whose exports were never evaluated.
bool InternalModuleLoader::load(Environment *p_env, JavaScriptModule &p_module) {
	const internal::PresetSource source = GodotJSRuntimePreset::get_source(file_name_);
	if (!source.is_valid()) {
		JSB_LOG(Error, "internal module source not found in the preset bundle: %s", file_name_);
		return false;
	}
	return AMDModuleLoader::load_source(p_env, source) == OK;
}

} //namespace jsb
