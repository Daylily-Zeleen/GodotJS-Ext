/************************************************************************/
/*  jsb_script_doc.h                                                    */
/*                                                                      */
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

#include "jsb_class_info.h"

namespace jsb::internal {

#if JSB_TOOLS
/** 一个脚本的源文件注释（类 + 成员）。 */
struct ScriptDocEntry {
	String class_brief;
	String class_description;
	/** `{kind, name, brief, description}` 的数组，kind ∈ method/property/signal/constant。 */
	Array members;
};

/**
 * 源文件注释文档的进程级暂存：**键是脚本的 `res://` 源路径**（`GodotJSScript::get_path()`）。
 *
 * 为什么经由全局暂存而不是直接写进某个 `GodotJSScript`：
 *  - 文档由**编辑器**（另一个 DLL）在工具进程应答后推入，而 `GodotJSScript` 对象在运行时侧
 *    （跨 DLL 无 C++ 符号可调用，只能经 `JsbBridgeTable`）；
 *  - 推入的时刻通常**早于**脚本被加载（编辑器安装/重扫时只处理文件，不实例化脚本），
 *    所以必须有一个"先存着、等脚本加载时再取"的地方。
 *
 * 消费点是 `GodotJSScript::load_module_immediately()`：类信息就绪后按 `get_path()` 取一次。
 */
namespace ScriptDocStore {
/** 合并一批 `res://<源路径> -> {class, members}`（同路径覆盖）。主线程调用。 */
void merge(const Dictionary &p_docs_by_source_path);

/** 取一个脚本路径的文档；没有则返回 false。不删除（模块可能重载多次）。 */
bool find(const String &p_source_path, ScriptDocEntry &r_entry);

/** 清空（引擎关闭时调用，避免进程级容器持有 String 活过 DLL 卸载）。 */
void clear();
} //namespace ScriptDocStore
#endif // JSB_TOOLS

} //namespace jsb::internal
