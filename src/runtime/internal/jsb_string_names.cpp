/************************************************************************/
/*  jsb_string_names.cpp                                                */
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

#include "jsb_string_names.h"
#include "jsb_string_names.def.h"

#include "../compat/jsb_compat.h"
#include "api_tool/api_tool.h"
#include "jsb_macros.h"
#include "jsb_class_visibility.h"
#include "jsb_naming_util.h"
#include <godot_cpp/classes/engine.hpp>

namespace jsb::internal {
StringNames *StringNames::singleton_ = nullptr;

StringNames &StringNames::get_singleton() {
	// Lazy-create: the editor extension compiles this TU too but never runs the
	// runtime's language bootstrap, so its copy must self-initialize. The
	// replacement table stays empty on that side (pure identifier names only).
	if (singleton_ == nullptr) {
		create();
	}
	return *singleton_;
}

void StringNames::create() {
	jsb_check(singleton_ == nullptr);
	singleton_ = memnew(StringNames);
}

void StringNames::free() {
	jsb_check(singleton_);
	memdelete(singleton_);
	singleton_ = nullptr;
}

StringNames::StringNames() {
#pragma push_macro("DEF")
#undef DEF
#define DEF(KeyName) sn_##KeyName = StringName(#KeyName);
#include "jsb_string_names.def.h"
#pragma pop_macro("DEF")
	sn_godot_typeloader = StringName("godot.typeloader");
	sn_godot_postbind = StringName("_post_bind_");

	ignored_.insert(sn_name);
}


void StringNames::populate_replacements(const PackedStringArray &p_reserved_words) {
	// builtin variant types (Array/Dictionary/...) are bound with their exposed
	// names too; they never appear in ClassDB's class list, so add them first.
	for (int vt = 1; vt < (int)Variant::VARIANT_MAX; ++vt) {
		const StringName original = Variant::get_type_name((Variant::Type)vt);
		const StringName exposed = NamingUtil::get_class_name(original);
		if (original != exposed) {
			add_replacement(original, exposed);
		}
	}

	LocalVector<StringName> exposed_class_list;
	// 不排除被忽略的类，它们只是不生成 .d.ts 声明代码，仍然可能从其他接口中获得这些类的对象并获得绑定，因此类名映射仍然是必须的。
	ClassVisibility::get_exposed_original_class_list(exposed_class_list, false);

	for (const StringName &class_name : exposed_class_list) {
		StringName exposed_name = NamingUtil::get_class_name(class_name);
		if (class_name != exposed_name) {
			add_replacement(class_name, exposed_name);
		}
	}

	PackedStringArray singleton_list = Engine::get_singleton()->get_singleton_list();
	for (const StringName &singleton_name : singleton_list) {
		StringName exposed_name = NamingUtil::get_class_name(singleton_name);
		if (exposed_name != singleton_name) {
			add_replacement(singleton_name, exposed_name);
		}
	}

	for (const StringName &func_name : api_tool::list_utility_functions()) {
		StringName exposed_name = func_name;
		if (p_reserved_words.has(exposed_name)) {
			exposed_name = NamingUtil::get_member_name("godot_" + exposed_name);
		}
		if (exposed_name != func_name) {
			add_replacement(func_name, exposed_name);
		}
	}
}
} //namespace jsb::internal
