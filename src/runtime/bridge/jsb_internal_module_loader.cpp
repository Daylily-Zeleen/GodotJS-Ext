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

namespace jsb {
bool InternalModuleLoader::load(Environment *p_env, JavaScriptModule &p_module) {
	// 这个 loader 目前**没有任何注册点**（全仓只有本文件与它的头文件引用它，
	// `git grep -n InternalModuleLoader -- src`），所以「从 preset 里按 file_name_ 求值源码」
	// 没有消费者：`file_name_` 只在构造函数里被存下，从没被读过。
	// 真要做需要先决定它服务于谁（历史上对应「把内置 bundle 当模块加载」，而那条路径现在
	// 由 `AMDModuleLoader` + `GodotJSRuntimePreset` 承担，见 `jsb_script_language.cpp` 的
	// `AMDModuleLoader::load_source` 调用）。在此之前保留空实现，不再挂 TODO。
	return true;
}

} //namespace jsb
