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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
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

#include <cmath>
#include <cstdio>
#include <string>

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
// `Equals` stands in for the engine`s strict equality, so it has to agree with JS on
// the cases where a bitwise or pointer compare does not: quickjs normalises 0.0 to an
// int but keeps -0.0 a float64 (equal values, different tags), and NaN has one bit
// pattern while being unequal to itself.
// `Equals` stands in for the engine`s strict equality, so it must agree with JS where a
// tag+pointer compare does not: quickjs normalises 0.0 to an int but keeps -0.0 a float64
// (equal values, different tags), and NaN has a single bit pattern while being unequal to
// itself. Values are built through the C API so the case is exactly the pair Equals sees.
// `Equals` is the backend`s `===` (see Data::strict_eq). A tag + pointer compare is not:
// it gets `-0.0 === 0.0` and `NaN === NaN` backwards, and it compares strings and BigInts
// by address where JS compares by value. Expectations are written out rather than taken
// from the engine, so an Equals that tracked the engine bug instead of the language would
// still fail here.
TEST_CASE("[runtime] [jsb] quickjs strict equality matches JS") {
	JSRuntime *rt = JS_NewRuntime();
	JSContext *ctx = JS_NewContext(rt);
	{
		struct Case {
			JSValue a;
			JSValue b;
			const char *label;
			bool equal;
		};

		// Numbers: quickjs normalises 0.0 to an int but keeps -0.0 a float64, so strictly
		// equal numbers can carry different tags.
		const JSValue minus_zero = JS_NewFloat64(ctx, -0.0);
		const JSValue zero = JS_NewFloat64(ctx, 0.0);
		const JSValue int_zero = JS_NewInt32(ctx, 0);
		const JSValue one = JS_NewInt32(ctx, 1);
		const JSValue one_f = JS_NewFloat64(ctx, 1.0);
		const JSValue two = JS_NewInt32(ctx, 2);
		const JSValue three = JS_NewInt32(ctx, 3);
		const JSValue infinity = JS_NewFloat64(ctx, INFINITY);
		const JSValue nan = JS_NewFloat64(ctx, NAN);
		const JSValue half = JS_NewFloat64(ctx, 1.5);
		const JSValue two_half = JS_NewFloat64(ctx, 2.5);
		// Booleans, null and undefined are compared by their own payload.
		const JSValue js_true = JS_NewBool(ctx, true);
		const JSValue js_false = JS_NewBool(ctx, false);
		// Strings and BigInts are compared by value, and each call builds a distinct
		// allocation, so an address compare would call every pair unequal.
		const JSValue str_a = JS_NewString(ctx, "a");
		const JSValue str_a2 = JS_NewString(ctx, "a");
		const JSValue str_ab = JS_NewString(ctx, "ab");
		const JSValue str_ac = JS_NewString(ctx, "ac");
		const JSValue big_1 = JS_Eval(ctx, "1n", 2, "<v>", JS_EVAL_TYPE_GLOBAL);
		const JSValue big_1b = JS_Eval(ctx, "1n", 2, "<v>", JS_EVAL_TYPE_GLOBAL);
		const JSValue big_2 = JS_Eval(ctx, "2n", 2, "<v>", JS_EVAL_TYPE_GLOBAL);
		const JSValue big_huge_a = JS_Eval(ctx, "12345678901234567890123n", 24, "<v>", JS_EVAL_TYPE_GLOBAL);
		const JSValue big_huge_b = JS_Eval(ctx, "12345678901234567890123n", 24, "<v>", JS_EVAL_TYPE_GLOBAL);
		// Objects are compared by identity.
		const JSValue array_a = JS_NewArray(ctx);
		const JSValue array_b = JS_NewArray(ctx);

		const Case cases[] = {
			{ minus_zero, zero, "-0.0 === 0.0 (different tags, equal value)", true },
			{ int_zero, minus_zero, "0 === -0.0 (int vs float64)", true },
			{ nan, nan, "NaN === NaN (one bit pattern, still unequal)", false },
			{ one, one_f, "1 === 1.0 (int vs float64)", true },
			{ two, three, "2 === 3", false },
			{ two, two, "2 === 2", true },
			{ infinity, infinity, "Infinity === Infinity", true },
			{ half, two_half, "1.5 === 2.5", false },
			{ js_true, js_false, "true === false", false },
			{ js_true, one, "true === 1 (no coercion)", false },
			{ js_false, int_zero, "false === 0 (no coercion)", false },
			{ JS_NULL, JS_NULL, "null === null", true },
			{ JS_UNDEFINED, JS_UNDEFINED, "undefined === undefined", true },
			{ JS_NULL, JS_UNDEFINED, "null === undefined", false },
			{ JS_NULL, int_zero, "null === 0", false },
			{ str_a, str_a2, "'a' === 'a' (distinct allocations, equal value)", true },
			{ str_ab, str_ac, "'ab' === 'ac'", false },
			{ str_a, one, "'a' === 1", false },
			{ big_1, big_1b, "1n === 1n (distinct BigInts)", true },
			{ big_1, big_2, "1n === 2n", false },
			{ big_huge_a, big_huge_b, "a BigInt beyond int64 === itself", true },
			{ big_1, one, "1n === 1", false },
			{ array_a, array_b, "[] === [] (distinct objects)", false },
		};

		for (const Case &c : cases) {
			CHECK_MESSAGE(impl::QuickJS::Equals(ctx, c.a, c.b) == c.equal,
					c.label, " - Equals disagrees with JS ===");
		}

		JS_FreeValue(ctx, big_huge_b);
		JS_FreeValue(ctx, big_huge_a);
		JS_FreeValue(ctx, big_2);
		JS_FreeValue(ctx, big_1b);
		JS_FreeValue(ctx, big_1);
		JS_FreeValue(ctx, str_ac);
		JS_FreeValue(ctx, str_ab);
		JS_FreeValue(ctx, str_a2);
		JS_FreeValue(ctx, str_a);
		JS_FreeValue(ctx, array_b);
		JS_FreeValue(ctx, array_a);
	}
	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);
}
// 回归：well-known symbol 查询在 quickjs shim 里曾返回悬垂句柄（函数内局部 `HandleScope` 把返回值槽
//       连同所有权一起释放），并且对"借用的 Symbol 构造器槽"多做了 `JS_FreeValue`（引用计数欠账）。
//       两者都只在反复查询后才显形，所以这里连续查 256 次并要求身份恒定。
TEST_CASE("[runtime] [jsb] quickjs well-known symbols stay stable and owned") {
	GodotJSScriptLanguageIniter initer;
	jsb::Environment *env = GodotJSScriptLanguage::get_singleton()->get_environment().get();
	REQUIRE(env != nullptr);
	JSB_TESTS_EXECUTION_SCOPE(env);
	v8::Isolate *isolate = env->get_isolate();

	const v8::Local<v8::Symbol> iterator = v8::Symbol::GetIterator(isolate);
	const v8::Local<v8::Symbol> to_primitive = v8::Symbol::GetToPrimitive(isolate);
	REQUIRE(!iterator.IsEmpty());
	REQUIRE(!to_primitive.IsEmpty());
	const int iterator_hash = iterator->GetIdentityHash();
	const int to_primitive_hash = to_primitive->GetIdentityHash();
	CHECK(iterator_hash != 0);
	CHECK(to_primitive_hash != 0);
	CHECK(iterator_hash != to_primitive_hash);

	for (int i = 0; i < 256; ++i) {
		const v8::Local<v8::Symbol> again_iterator = v8::Symbol::GetIterator(isolate);
		const v8::Local<v8::Symbol> again_to_primitive = v8::Symbol::GetToPrimitive(isolate);
		REQUIRE(!again_iterator.IsEmpty());
		REQUIRE(!again_to_primitive.IsEmpty());
		CHECK(again_iterator->GetIdentityHash() == iterator_hash);
		CHECK(again_to_primitive->GetIdentityHash() == to_primitive_hash);
	}
}

#endif // JSB_WITH_QUICKJS

} //namespace jsb::tests