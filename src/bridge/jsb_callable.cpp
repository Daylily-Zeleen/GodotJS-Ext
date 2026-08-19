/************************************************************************/
/*  jsb_callable.cpp                                                    */
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

#include "jsb_callable.h"

namespace jsb {
String JSCallable::get_as_text() const {
	return vformat("[JSFunction: object_id=%s, callback_id=%s]", object_id_.operator uint64_t(), callback_id_.to_string());
}

JSCallable::~JSCallable() {
	if (callback_id_) {
		if (const std::shared_ptr<jsb::Environment> env = jsb::Environment::_access(env_id_)) {
			env->release_function(callback_id_);
		}
	}
}

void JSCallable::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, GDExtensionCallError &r_call_error) const {
	const std::shared_ptr<jsb::Environment> env = jsb::Environment::_access(env_id_);
	if (!env) {
		r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}

	Object *object_ptr = object_id_.is_null() ? nullptr : godot::ObjectDB::get_instance(object_id_);
	r_return_value = env->call_function(object_ptr, callback_id_, p_arguments, p_argcount, r_call_error);
}
} //namespace jsb
