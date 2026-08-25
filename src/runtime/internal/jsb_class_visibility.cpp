/************************************************************************/
/*  jsb_class_visibility.cpp                                            */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_class_visibility.h"

#include "api_tool/api_tool.h"
#include "jsb_internal.h"
#include "jsb_macros.h"
#include "jsb_settings.h"

namespace jsb::internal {

const HashSet<StringName> &ClassVisibility::get_omitted_original_classes() {
	static const HashSet<StringName> table = {
		"IPUnix",
		"ScriptEditorDebugger",
		"Thread",
		"Semaphore",

		// GodotJS related clases
		"GodotJSEditorPlugin",
		"GodotJSExportPlugin",
		"GodotJSREPL",
		"GodotJSScript",
		"GodotJSEditorHelper",

		// GDScript related classes
		"GDScript",
		"GDScriptEditorTranslationParserPlugin",
		"GDScriptNativeClass",
		"GDScriptSyntaxHighlighter",
	};
	return table;
}

void ClassVisibility::get_exposed_original_class_list(LocalVector<StringName> &r_list, bool p_exclude_ignored_classes) {
	const PackedStringArray ignored_classes = p_exclude_ignored_classes ? Settings::get_ignored_classes() : PackedStringArray{};
	const PackedStringArray all_class_names = ClassDB::get_class_list();

	r_list.clear();
	r_list.reserve(all_class_names.size());

	const HashSet<StringName> &omitted_original_classes = get_omitted_original_classes();

	for (int i = 0; i < all_class_names.size(); i++) {
		StringName class_name = all_class_names[i];

		if (omitted_original_classes.has(class_name)) {
			JSB_LOG(Verbose, "Omitted class '%s' as it's currently not usable from JavaScript", class_name);
			continue;
		}

		if (p_exclude_ignored_classes && !is_original_class_exposed(class_name, ignored_classes)) {
			JSB_LOG(Verbose, "Ignoring class '%s' because it's in the ignored classes list", class_name);
			continue;
		}

		ClassDB::APIType api_type = ClassDB::class_get_api_type(class_name);

		if (api_type == ClassDB::API_NONE) {
			JSB_LOG(Verbose, "Ignoring class '%s' because it's marked as API_NONE", class_name);
			continue;
		}

		// GDExtension 获取到的 Class 只能是 exposed
		if (!api_tool::has_class(class_name)) {
			JSB_LOG(Verbose, "Ignoring class '%s' because it's not exposed", class_name);
			continue;
		}

		if (!ClassDB::is_class_enabled(class_name)) {
			JSB_LOG(Verbose, "Ignoring class '%s' because it's not enabled", class_name);
			continue;
		}

		r_list.push_back(class_name);
	}
}

bool ClassVisibility::is_original_class_exposed(const StringName &p_original_name, const PackedStringArray &p_ignored_classes) {
	const HashSet<StringName> &omitted_original_classes = get_omitted_original_classes();

	if (omitted_original_classes.has(p_original_name)) {
		return false;
	}

	ClassDB::APIType api_type = ClassDB::class_get_api_type(p_original_name);

	if (api_type == ClassDB::API_NONE) {
		return false;
	}

	// GDExtension 获取到的 CLass 只能是 exposed
	// if (!ClassDB::is_class_exposed(p_original_name))
	// {
	// 	return false;
	// }

	if (!ClassDB::is_class_enabled(p_original_name)) {
		return false;
	}

	// ignored classs 可以指定父类，连同子类一起禁用
	for (const String &ignored_class : (p_ignored_classes.is_empty() ? jsb::internal::Settings::get_ignored_classes() : p_ignored_classes)) {
		if (ignored_class == p_original_name || ClassDB::is_parent_class(p_original_name, ignored_class))
			return false;
	}

	return true;
}

StringName ClassVisibility::find_exposed_base_class(const StringName &p_unexposed_original_class) {
	const PackedStringArray ignored_classes = jsb::internal::Settings::get_ignored_classes();
	StringName base = ClassDB::get_parent_class(p_unexposed_original_class);
	while (!base.is_empty() && !is_original_class_exposed(base, ignored_classes)) {
		base = ClassDB::get_parent_class(base);
	}
	return base;
}

} //namespace jsb::internal
