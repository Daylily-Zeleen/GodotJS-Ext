/************************************************************************/
/*  jsb_quickjs_catch.cpp                                               */
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

#include "jsb_quickjs_catch.h"
#include "jsb_quickjs_ext.h"
#include "jsb_quickjs_isolate.h"

namespace jsb::impl {
bool TryCatch::has_caught() const {
	return isolate_->try_catch();
}

void TryCatch::get_message(String *r_message, String *r_stacktrace) const {
	JSContext *ctx = isolate_->ctx();
	const JSValue ex = JS_DupValue(ctx, isolate_->stack_val(StackPos::Exception));
	jsb_check(!JS_IsNull(ex));

	// reset current exception
	isolate_->set_stack_copy(StackPos::Exception, StackPos::Null);

	if (jsb::impl::QuickJS::IsError(ctx, ex)) {
		const JSValue err_message = JS_GetProperty(ctx, ex, JS_ATOM_message);
		const JSValue err_stack = JS_GetProperty(ctx, ex, JS_ATOM_stack);

		{
			// const String filename = QuickJS::IsNullish(err_filename) ? String("native") : QuickJS::GetString(ctx, err_filename);
			// const String line = QuickJS::IsNullish(err_line) ? String("") : QuickJS::GetString(ctx, err_line);
			const String message = QuickJS::GetString(ctx, err_message);
			const String stack = QuickJS::GetString(ctx, err_stack);

			if (r_message) *r_message = message;
			if (r_stacktrace) *r_stacktrace = stack;
		}

		// JS_FreeValue(ctx, err_filename);
		// JS_FreeValue(ctx, err_line);
		JS_FreeValue(ctx, err_message);
		JS_FreeValue(ctx, err_stack);
	} else {
		JSB_LOG(Error, "the thrown exception is not an Error");
	}
	JS_FreeValue(ctx, ex);
}

} //namespace jsb::impl
