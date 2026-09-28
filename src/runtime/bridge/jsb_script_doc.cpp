/************************************************************************/
/*  jsb_script_doc.cpp                                                  */
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

#include "jsb_script_doc.h"

#include <godot_cpp/classes/engine.hpp>

namespace jsb::internal {

#if JSB_TOOLS
namespace ScriptDocStore {
namespace {
// 进程级暂存。**编辑器主线程独占**：写入发生在编辑器安装/重扫，读取发生在脚本加载，
// 两者都在主线程（`_load` 由 `ResourceLoader` 在主线程调用；后台扫描不读文档）。
HashMap<String, ScriptDocEntry> g_docs;
} //namespace

void merge(const Dictionary &p_docs_by_source_path) {
	// `Dictionary` 的范围 for 在 godot-cpp 里需要非 const 迭代器；显式取 key 列表更直白。
	const Array keys = p_docs_by_source_path.keys();
	for (int index = 0; index < keys.size(); ++index) {
		const String source_path = keys[index];
		const Variant value = p_docs_by_source_path.get(source_path, Variant());
		if (source_path.is_empty()) {
			continue;
		}
		if (value.get_type() != Variant::DICTIONARY) {
			// 工具对该文件回了 null（没有文档）：**删掉旧条目**而不是留着 —— 作者把注释删掉后
			// 必须能看到文档消失（design.md §4.2 L1）。
			g_docs.erase(source_path);
			continue;
		}
		const Dictionary doc = value;
		ScriptDocEntry entry;
		const Variant class_value = doc.get("class", Variant());
		if (class_value.get_type() == Variant::DICTIONARY) {
			const Dictionary class_doc = class_value;
			entry.class_brief = class_doc.get("brief", String());
			entry.class_description = class_doc.get("description", String());
		}
		const Variant members_value = doc.get("members", Variant());
		if (members_value.get_type() == Variant::ARRAY) {
			entry.members = members_value;
		}
		g_docs.insert(source_path, entry);
	}
}

bool find(const String &p_source_path, ScriptDocEntry &r_entry) {
	const ScriptDocEntry *hit = g_docs.getptr(p_source_path);
	if (hit == nullptr) {
		return false;
	}
	r_entry = *hit;
	return true;
}

void clear() {
	g_docs.clear();
}
} //namespace ScriptDocStore
#endif // JSB_TOOLS

} //namespace jsb::internal
