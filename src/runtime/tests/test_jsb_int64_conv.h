/************************************************************************/
/*  test_jsb_int64_conv.h                                               */
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

// Numeric conversion primitives (`src/runtime/impl/jsb_primitive_conv.h`) and
// the metadata-aware Variant conversion that carries them to the dynamic
// binding path.
//
// The regressions these guard are silent-value ones: a wrong 64-bit threshold
// or a signed write of an ObjectID still returns a plausible value, it is just
// the wrong one. So every case asserts the concrete bits, not "it converted".

#include "../bridge/jsb_static_binding_util.h"
#include "../bridge/jsb_type_convert.h"
#include "../bridge/jsb_type_convert_direct.h"
#include "jsb_test_helpers.h"

namespace jsb::tests {

namespace int64_conv_detail {

// RefCounted ObjectID shape: bit 63 set (`is_ref_counted`), so a signed read is
// negative. Taken from the layout documented in the parent task design
// (`OBJECTDB_REFERENCE_BIT = 1 << 63`).
constexpr int64_t kRefCountedObjectId = (int64_t)0x80000006890005ecULL;

inline v8::Local<v8::BigInt> new_bigint(v8::Isolate *isolate, int64_t value) {
	return v8::BigInt::New(isolate, value);
}

inline v8::Local<v8::BigInt> new_bigint_unsigned(v8::Isolate *isolate, uint64_t value) {
	return v8::BigInt::NewFromUnsigned(isolate, value);
}

} //namespace int64_conv_detail

TEST_CASE("[runtime] [jsb.int64] new_integer threshold is two-sided") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();

		// int32 fast path is unchanged: a value that fits stays an Int32.
		CHECK(impl::Helper::new_integer(isolate, 0)->IsInt32());
		CHECK(impl::Helper::new_integer(isolate, INT32_MAX)->IsInt32());
		CHECK(impl::Helper::new_integer(isolate, INT32_MIN)->IsInt32());
		CHECK(impl::Helper::new_integer(isolate, (int64_t)INT32_MAX + 1)->IsNumber());

		// Exactly at the safe bound stays a Number, on BOTH sides.
		CHECK(impl::Helper::new_integer(isolate, JSB_MAX_SAFE_INTEGER)->IsNumber());
		CHECK(impl::Helper::new_integer(isolate, -JSB_MAX_SAFE_INTEGER)->IsNumber());

		// One past it becomes a BigInt, on BOTH sides. The negative side is the
		// regression: it used to fall through to `Number::New((double)v)`.
		CHECK(impl::Helper::new_integer(isolate, JSB_MAX_SAFE_INTEGER + 1)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, -JSB_MAX_SAFE_INTEGER - 1)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, INT64_MIN)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, INT64_MAX)->IsBigInt());
	}

	env.reset();
}

TEST_CASE("[runtime] [jsb.int64] new_integer preserves negative bits above 2^53") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();

		// Each of these is "negative with a magnitude above 2^53", the exact
		// shape that used to be rounded through `(double)`.
		const int64_t cases[] = {
			-((int64_t)1 << 53),
			int64_conv_detail::kRefCountedObjectId,
			INT64_MIN + 1,
			INT64_MIN,
		};
		for (const int64_t value : cases) {
			const v8::Local<v8::Value> jv = impl::Helper::new_integer(isolate, value);
			CHECK(jv->IsBigInt());
			// Reading it back is the round-trip the ObjectID handoff depends on.
			CHECK(jv.As<v8::BigInt>()->Int64Value() == value);
		}
	}

	env.reset();
}

TEST_CASE("[runtime] [jsb.int64] new_unsigned_integer writes high-bit values unsigned") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();

		CHECK(impl::Helper::new_unsigned_integer(isolate, 0)->IsInt32());
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)INT32_MAX)->IsInt32());
		// Above the int32 fast path but within the safe range: still a Number.
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)INT32_MAX + 1)->IsNumber());
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)JSB_MAX_SAFE_INTEGER)->IsNumber());
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)JSB_MAX_SAFE_INTEGER + 1)->IsBigInt());

		// The point of this writer: a value whose bit 63 is set must read back
		// positive. Written signed it would come back negative, and
		// `instance_from_id()` would then be handed a different id than
		// `get_instance_id()` produced.
		const uint64_t unsigned_cases[] = {
			(uint64_t)1 << 63,
			(uint64_t)int64_conv_detail::kRefCountedObjectId,
			UINT64_MAX,
		};
		for (const uint64_t value : unsigned_cases) {
			const v8::Local<v8::Value> jv = impl::Helper::new_unsigned_integer(isolate, value);
			CHECK(jv->IsBigInt());
			CHECK(jv.As<v8::BigInt>()->Uint64Value() == value);
		}
	}

	env.reset();
}

TEST_CASE("[runtime] [jsb.int64] to_int64 and to_uint64 read bit patterns") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		// Numbers.
		{
			int64_t out = 0;
			CHECK(impl::Helper::to_int64(v8::Number::New(isolate, 42.0), out));
			CHECK(out == 42);
			CHECK(impl::Helper::to_int64(v8::Int32::New(isolate, -7), out));
			CHECK(out == -7);
			// A fractional number truncates toward zero, matching the engine's
			// own `T(_data._float)` cast.
			CHECK(impl::Helper::to_int64(v8::Number::New(isolate, 2.75), out));
			CHECK(out == 2);
		}

		// A BigInt above 2^53 keeps its exact bits (the read-side regression).
		{
			int64_t out = 0;
			const int64_t value = int64_conv_detail::kRefCountedObjectId;
			CHECK(impl::Helper::to_int64(int64_conv_detail::new_bigint(isolate, value), out));
			CHECK(out == value);
		}

		// uint64 reads the same bits, so a negative int64 view maps onto the
		// unsigned one the engine's `Variant::operator uint64_t()` would give.
		{
			uint64_t out = 0;
			CHECK(impl::Helper::to_uint64(int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX), out));
			CHECK(out == UINT64_MAX);
			CHECK(impl::Helper::to_uint64(int64_conv_detail::new_bigint(isolate, -1), out));
			CHECK(out == UINT64_MAX);
			// A negative Number wraps the same way, so `put_u64(-1)` and
			// `put_u64(0xffffffffffffffffn)` agree.
			CHECK(impl::Helper::to_uint64(v8::Number::New(isolate, -1.0), out));
			CHECK(out == UINT64_MAX);
		}

		// Rejections: values that are not numeric at all.
		{
			int64_t int_out = 0;
			uint64_t uint_out = 0;
			CHECK(!impl::Helper::to_int64(v8::String::NewFromUtf8Literal(isolate, "12"), int_out));
			CHECK(!impl::Helper::to_int64(v8::Null(isolate), int_out));
			CHECK(!impl::Helper::to_int64(v8::Undefined(isolate), int_out));
			CHECK(!impl::Helper::to_uint64(v8::String::NewFromUtf8Literal(isolate, "12"), uint_out));
			CHECK(!impl::Helper::to_uint64(v8::Null(isolate), uint_out));
		}

		// Same reading through the Variant entry point, with and without the
		// uint64 metadata. The metadata is what makes the high bit read as a
		// positive value on the way out.
		{
			const Variant id_variant = (int64_t)int64_conv_detail::kRefCountedObjectId;
			v8::Local<v8::Value> signed_js;
			CHECK(TypeConvert::gd_var_to_js(isolate, context, id_variant, Variant::INT, GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE, signed_js));
			CHECK(signed_js->IsBigInt());
			CHECK(signed_js.As<v8::BigInt>()->Int64Value() == int64_conv_detail::kRefCountedObjectId);

			v8::Local<v8::Value> unsigned_js;
			CHECK(TypeConvert::gd_var_to_js(isolate, context, id_variant, Variant::INT, GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64, unsigned_js));
			CHECK(unsigned_js->IsBigInt());
			CHECK(unsigned_js.As<v8::BigInt>()->Uint64Value() == (uint64_t)int64_conv_detail::kRefCountedObjectId);
			// Both carry the same bits; only the sign of the JS value differs.
			CHECK(unsigned_js.As<v8::BigInt>()->Uint64Value() == signed_js.As<v8::BigInt>()->Uint64Value());
		}
	}

	env.reset();
}

TEST_CASE("[runtime] [jsb.int64] to_double and to_bool accept the engine's numeric surface") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();

		// to_double: numbers pass through, BigInt follows the int64 view.
		{
			double out = 0;
			CHECK(impl::Helper::to_double(v8::Number::New(isolate, 1.5), out));
			CHECK(out == 1.5);
			CHECK(impl::Helper::to_double(int64_conv_detail::new_bigint(isolate, (int64_t)1 << 40), out));
			CHECK(out == (double)((int64_t)1 << 40));
			CHECK(!impl::Helper::to_double(v8::String::NewFromUtf8Literal(isolate, "1"), out));
			CHECK(!impl::Helper::to_double(v8::Null(isolate), out));
		}

		// to_bool: mirrors the engine's BOOL surface (INT / FLOAT / NIL), with
		// STRING still rejected -- `Variant::can_convert_strict` comments it out.
		{
			bool out = true;
			CHECK(impl::Helper::to_bool(isolate, v8::Boolean::New(isolate, true), out));
			CHECK(out);
			CHECK(impl::Helper::to_bool(isolate, v8::Boolean::New(isolate, false), out));
			CHECK(!out);
			// Numbers: 0 is false, everything else true.
			CHECK(impl::Helper::to_bool(isolate, v8::Number::New(isolate, 0.0), out));
			CHECK(!out);
			CHECK(impl::Helper::to_bool(isolate, v8::Number::New(isolate, 1.0), out));
			CHECK(out);
			CHECK(impl::Helper::to_bool(isolate, v8::Number::New(isolate, -1.0), out));
			CHECK(out);
			// BigInt: 0n is false, 1n is true.
			CHECK(impl::Helper::to_bool(isolate, int64_conv_detail::new_bigint(isolate, 0), out));
			CHECK(!out);
			CHECK(impl::Helper::to_bool(isolate, int64_conv_detail::new_bigint(isolate, 1), out));
			CHECK(out);
			// null / undefined are false, not rejected.
			CHECK(impl::Helper::to_bool(isolate, v8::Null(isolate), out));
			CHECK(!out);
			CHECK(impl::Helper::to_bool(isolate, v8::Undefined(isolate), out));
			CHECK(!out);
			// A string is still rejected rather than coerced.
			CHECK(!impl::Helper::to_bool(isolate, v8::String::NewFromUtf8Literal(isolate, "true"), out));
			CHECK(!impl::Helper::to_bool(isolate, v8::String::NewFromUtf8Literal(isolate, ""), out));
		}
	}

	env.reset();
}

TEST_CASE("[runtime] [jsb.int64] JSToGD<uint64_t> writes high-bit values instead of rejecting them") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		// The regression: `js_to_fixed_width_int<uint64_t>` carried a
		// `wide < 0 -> false` early return, so every value >= 2^63 was rejected --
		// including plain numbers -- while the dynamic path wrote those bytes.
		// The static and dynamic legs must now agree on the bytes.
		const uint64_t accepted[] = {
			0,
			(uint64_t)JSB_MAX_SAFE_INTEGER,
			(uint64_t)JSB_MAX_SAFE_INTEGER + 1,
			(uint64_t)1 << 63,
			(uint64_t)1 << 63 | 1,
			UINT64_MAX,
		};
		for (const uint64_t value : accepted) {
			uint64_t out = 0;
			// Through a BigInt, the exact-bit form.
			CHECK(JSToGD<uint64_t>::convert(isolate, context, int64_conv_detail::new_bigint_unsigned(isolate, value), out));
			CHECK(out == value);
			// And through a plain Number, which is what the old code rejected.
			// Above 2^53 a double cannot represent every integer, so the number
			// form is only checked where it is exact.
			if (value <= (uint64_t)JSB_MAX_SAFE_INTEGER) {
				CHECK(JSToGD<uint64_t>::convert(isolate, context, v8::Number::New(isolate, (double)value), out));
				CHECK(out == value);
			}
		}

		// `put_u64(-1)` writes 0xffffffffffffffff on both legs: a negative number
		// wraps through the signed read, exactly like the engine's own
		// `Variant::operator uint64_t()`.
		{
			uint64_t out = 0;
			CHECK(JSToGD<uint64_t>::convert(isolate, context, v8::Number::New(isolate, -1.0), out));
			CHECK(out == UINT64_MAX);
			CHECK(JSToGD<uint64_t>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, -1), out));
			CHECK(out == UINT64_MAX);
		}

		// The `StaticBindingUtil` path (used by the reflect/dynamic constructor
		// route) must accept the same surface, and write back unsigned.
		{
			uint64_t out = 0;
			CHECK(StaticBindingUtil<uint64_t>::get(isolate, context, int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX), out));
			CHECK(out == UINT64_MAX);
			v8::Local<v8::Value> jv;
			CHECK(StaticBindingUtil<uint64_t>::set(isolate, context, UINT64_MAX, jv));
			CHECK(jv->IsBigInt());
			CHECK(jv.As<v8::BigInt>()->Uint64Value() == UINT64_MAX);
		}

		// Non-numeric inputs are still rejected.
		{
			uint64_t out = 0;
			CHECK(!JSToGD<uint64_t>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "1"), out));
			CHECK(!JSToGD<uint64_t>::convert(isolate, context, v8::Null(isolate), out));
		}

		// Narrow slots keep their range checks: they are genuinely narrow, so a
		// silent truncation would be the defect. `uint8_t` is the representative
		// unsigned case, `int8_t` the signed one, `char32_t` the wide-but-narrow one.
		{
			uint8_t u8 = 0;
			int8_t i8 = 0;
			char32_t c32 = 0;
			CHECK(JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, 255), u8));
			CHECK(u8 == 255);
			CHECK(!JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, 256), u8));
			CHECK(!JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, -1), u8));

			CHECK(JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, -128), i8));
			CHECK(i8 == -128);
			CHECK(!JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, 128), i8));
			CHECK(!JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, -129), i8));

			CHECK(JSToGD<char32_t>::convert(isolate, context, v8::Int32::New(isolate, 0x10FFFF), c32));
			CHECK(c32 == 0x10FFFF);
			CHECK(!JSToGD<char32_t>::convert(isolate, context, v8::Int32::New(isolate, -1), c32));
		}
	}

	env.reset();
}

} //namespace jsb::tests
