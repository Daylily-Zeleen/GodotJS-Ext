/************************************************************************/
/*  test_jsb_quickjs_runtime.h                                          */
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

#pragma once

#include "../bridge/jsb_builtins.h"
#include "../bridge/jsb_essentials.h"
#include "jsb_test_helpers.h"

#if JSB_WITH_QUICKJS
// all quickjs.impl specific test cases
namespace jsb::tests {
struct QuickJSBindings {
	static JSValue magic_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
		CHECK(magic == 1);
		return JS_UNDEFINED;
	}
};

TEST_CASE("[runtime] [jsb] quickjs.minimal") {
	JSRuntime *rt = JS_NewRuntime();
	JSContext *ctx = JS_NewContext(rt);
	{
		const JSValue this_obj = JS_NewObject(ctx);
		const JSValue func = JS_NewCFunctionMagic(ctx, QuickJSBindings::magic_call, "magic_call", 0, JS_CFUNC_generic_magic, 1);
		const JSAtom prop = JS_NewAtom(ctx, "prop");

		CHECK(JS_IsFunction(ctx, func));
		CHECK(prop != JS_ATOM_NULL);
		CHECK(impl::QuickJS::_RefCount(func) == 1);
		constexpr int flags = JS_PROP_HAS_ENUMERABLE | JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_GET;
		CHECK(JS_DefineProperty(ctx, this_obj, prop, JS_UNDEFINED, func, JS_UNDEFINED, flags) == 1);
		CHECK(impl::QuickJS::_RefCount(func) == 2);

		JS_FreeValue(ctx, func);
		JS_FreeAtom(ctx, prop);
		JS_FreeValue(ctx, this_obj);
	}
	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);
}
} //namespace jsb::tests
#endif
