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
namespace jsb::internal {
StringNames *StringNames::singleton_ = nullptr;

StringNames &StringNames::get_singleton() {
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

} //namespace jsb::internal
