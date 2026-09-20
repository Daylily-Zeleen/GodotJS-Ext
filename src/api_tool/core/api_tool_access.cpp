/************************************************************************/
/*  api_tool_access.cpp                                                 */
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

#include "api_tool/core/api_tool_access.h"

using namespace godot;

namespace api_tool::internal {

void internal::ApiMethodAccess::setup(ApiMethodBase &r_method, const StringName &p_name, uint32_t p_hash, uint32_t p_flags, bool p_has_returns, Variant::Type p_return_type, GDExtensionClassMethodArgumentMetadata p_return_meta, uint16_t p_arg_count) {
	r_method.name_ = p_name;
	r_method.hash_ = p_hash;
	r_method.flags_ = p_has_returns ? p_flags : (p_flags | internal::METHOD_FLAG_NO_RETURN);
	r_method.ret_ = internal::ApiMethodArg{ static_cast<VariantType>(p_return_type),
		static_cast<VariantType>(p_return_meta) };
	r_method.arg_count_ = p_arg_count;
}

void internal::ApiMethodAccess::set_index(ApiMethodBase &r_method, uint16_t p_index) {
	r_method.method_index_ = p_index;
}

void internal::ApiMethodAccess::set_args(ApiMethodBase &r_method, const internal::ApiMethodArg *p_args) {
	r_method.args_ = p_args;
}

void internal::ApiMethodAccess::set_storage(ApiMethodBase &r_method, internal::ApiMethodDetailStorage *p_storage) {
	r_method.storage_ = p_storage;
}

void internal::ApiMethodAccess::set_default_count(ApiMemberMethodBase &r_method, uint16_t p_default_count) {
	r_method.default_count_ = p_default_count;
}

uint32_t internal::ApiMethodAccess::get_flags_raw(const ApiMethodBase &p_method) {
	// The internal NO_RETURN bit has no other home, so it must survive a store
	// rewrite. get_flags() would mask it out.
	return p_method.flags_;
}

} //namespace api_tool::internal
