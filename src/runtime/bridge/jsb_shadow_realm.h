/************************************************************************/
/*  jsb_shadow_realm.h                                                  */
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

#include "jsb_bridge_pch.h"
namespace jsb {

class Environment;

class ShadowRealm {
public:
	static void register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_self);
	// release all shadow_realms, call from main thread (GodotJSScriptLanguage::finish)
	static void finish_all();

	/** 推进一个 realm 环境的 `update`：帧保护只对 realm 有意义（只有 realm 会被它自己 JS 帧内的 `terminate()` 销毁）。 */
	static void update_env(Environment *p_env, uint64_t p_delta_msecs);
};

} //namespace jsb
