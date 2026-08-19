/************************************************************************/
/*  jsb_web_catch.cpp                                                   */
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

#include "jsb_web_catch.h"
#include "jsb_web_ext.h"
#include "jsb_web_isolate.h"

namespace jsb::impl {
bool TryCatch::has_caught() const {
	return jsbi_HasError(isolate_->rt());
}

void TryCatch::get_message(String *r_message, String *r_stacktrace) const {
	const JSRuntime ctx = isolate_->rt();
	jsb_check(!jsbi_IsNullOrUndefined(ctx, StackBase::Error));

	const StackPosition err_message = jsbi_GetPropertyAtomID(ctx, StackBase::Error, JS_ATOM_message);
	const StackPosition err_stack = jsbi_GetPropertyAtomID(ctx, StackBase::Error, JS_ATOM_stack);

	{
		const String message = BrowserJS::GetString(ctx, err_message);
		const String stack = BrowserJS::GetString(ctx, err_stack);

		if (r_message) *r_message = message;
		if (r_stacktrace) *r_stacktrace = stack;
	}

	// reset current exception
	jsbi_StackSet(ctx, StackBase::Error, StackBase::Undefined);
}

} //namespace jsb::impl
