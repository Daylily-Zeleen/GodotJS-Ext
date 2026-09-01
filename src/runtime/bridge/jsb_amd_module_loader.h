/************************************************************************/
/*  jsb_amd_module_loader.h                                             */
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
#include <internal/jsb_preset_source.h>
#include "jsb_bridge_pch.h"
#include "jsb_module_loader.h"

namespace jsb {
// `AMDModuleLoader` follows the fundamental guidelines of the `AsynchronousModuleDefinition`, but not really async.
// it's currently only used to load the `compiled` editor script bundle.
class AMDModuleLoader : public IModuleLoader {
private:
	PackedStringArray deps_;
	v8::Global<v8::Function> evaluator_;
	bool internal_;

public:
	AMDModuleLoader(const PackedStringArray &p_deps, v8::Global<v8::Function> &&p_evaluator)
			: deps_(p_deps), evaluator_(std::move(p_evaluator)) {}

	virtual ~AMDModuleLoader() override { evaluator_.Reset(); }

	virtual bool load(Environment *p_env, JavaScriptModule &p_module) override;

	void set_internal(bool internal) {
		internal_ = internal;
	}

	static Error load_source(Environment *p_env, const internal::PresetSource &p_source);
	static void load_source(Environment *p_env, const char *p_source, int p_len, const String &p_name, bool p_internal = false);
};

} //namespace jsb
