/************************************************************************/
/*  test_jsb_int64_conv.h                                               */
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
#if JSB_WITH_STATIC_BINDINGS
#	include "../../static_binding/thunks/thunks_common.h"
#endif // JSB_WITH_STATIC_BINDINGS

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

// A BigInt's whole existence in this suite depends on `JSB_WITH_BIGINT`: with the
// switch off, BigInt is not a value the engine build can produce or receive, and
// every reader (`to_int64` / `to_uint64` / `to_double` / `to_bool` / `JSToGD<T>`)
// compiles without its BigInt arm. So each BigInt-shaped case is written against
// this predicate instead of sprinkling `#if` through the assertions:
//
//     if (constexpr bool bigint_case = bigint_inputs_supported; bigint_case) { ... }
//
// The alternative -- asserting BigInt behavior unconditionally -- fails on that
// build, which is a configuration the project supports.
inline constexpr bool bigint_inputs_supported =
#if JSB_WITH_BIGINT
		true;
#else
		false;
#endif

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
		// `JSB_WITH_BIGINT=0` drops that arm by design (the value leaves as
		// the lossy Number instead), so this is the one part that is
		// configuration-dependent.
#if JSB_WITH_BIGINT
		CHECK(impl::Helper::new_integer(isolate, JSB_MAX_SAFE_INTEGER + 1)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, -JSB_MAX_SAFE_INTEGER - 1)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, INT64_MIN)->IsBigInt());
		CHECK(impl::Helper::new_integer(isolate, INT64_MAX)->IsBigInt());
#else
		CHECK(impl::Helper::new_integer(isolate, JSB_MAX_SAFE_INTEGER + 1)->IsNumber());
		CHECK(impl::Helper::new_integer(isolate, -JSB_MAX_SAFE_INTEGER - 1)->IsNumber());
		CHECK(impl::Helper::new_integer(isolate, INT64_MIN)->IsNumber());
		CHECK(impl::Helper::new_integer(isolate, INT64_MAX)->IsNumber());
#endif
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
#if JSB_WITH_BIGINT
			CHECK(jv->IsBigInt());
			// Reading it back is the round-trip the ObjectID handoff depends on.
			CHECK(jv.As<v8::BigInt>()->Int64Value() == value);
#else
			// The switch is off: the value leaves as a Number and the low bits
			// above 2^53 are gone. Only the negative sign is still observable.
			CHECK(jv->IsNumber());
			CHECK(jv.As<v8::Number>()->Value() == (double)value);
#endif
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
#if JSB_WITH_BIGINT
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
#else
		// Switch off: the unsigned arm is gone, so a high-bit value leaves as the
		// lossy Number. `Number::New((double)v)` is non-negative, so bit 63 shows
		// up via the magnitude rather than the sign.
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)JSB_MAX_SAFE_INTEGER + 1)->IsNumber());
		CHECK(impl::Helper::new_unsigned_integer(isolate, (uint64_t)1 << 63)->IsNumber());
		CHECK(impl::Helper::new_unsigned_integer(isolate, UINT64_MAX)->IsNumber());
		CHECK(impl::Helper::new_unsigned_integer(isolate, UINT64_MAX).As<v8::Number>()->Value() > 0.0);
#endif
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
		// With `JSB_WITH_BIGINT=0` the readers have no BigInt arm at all, so a
		// BigInt is rejected here as any other non-numeric value.
#if JSB_WITH_BIGINT
		{
			int64_t out = 0;
			const int64_t value = int64_conv_detail::kRefCountedObjectId;
			CHECK(impl::Helper::to_int64(int64_conv_detail::new_bigint(isolate, value), out));
			CHECK(out == value);
		}
#else
		{
			int64_t out = 0;
			CHECK(!impl::Helper::to_int64(int64_conv_detail::new_bigint(isolate, int64_conv_detail::kRefCountedObjectId), out));
		}
#endif

		// uint64 reads the same bits, so a negative int64 view maps onto the
		// unsigned one the engine's `Variant::operator uint64_t()` would give.
		{
			uint64_t out = 0;
#if JSB_WITH_BIGINT
			CHECK(impl::Helper::to_uint64(int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX), out));
			CHECK(out == UINT64_MAX);
			CHECK(impl::Helper::to_uint64(int64_conv_detail::new_bigint(isolate, -1), out));
			CHECK(out == UINT64_MAX);
#else
			CHECK(!impl::Helper::to_uint64(int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX), out));
			CHECK(!impl::Helper::to_uint64(int64_conv_detail::new_bigint(isolate, -1), out));
#endif
			// A negative Number wraps the same way, so `put_u64(-1)` and
			// `put_u64(0xffffffffffffffffn)` agree. Number input is unaffected by
			// the switch.
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
			v8::Local<v8::Value> unsigned_js;
			CHECK(TypeConvert::gd_var_to_js(isolate, context, id_variant, Variant::INT, GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64, unsigned_js));
#if JSB_WITH_BIGINT
			CHECK(signed_js->IsBigInt());
			CHECK(signed_js.As<v8::BigInt>()->Int64Value() == int64_conv_detail::kRefCountedObjectId);
			CHECK(unsigned_js->IsBigInt());
			CHECK(unsigned_js.As<v8::BigInt>()->Uint64Value() == (uint64_t)int64_conv_detail::kRefCountedObjectId);
			// Both carry the same bits; only the sign of the JS value differs.
			CHECK(unsigned_js.As<v8::BigInt>()->Uint64Value() == signed_js.As<v8::BigInt>()->Uint64Value());
#else
			// Switch off: the slot leaves as a lossy Number. The metadata still
			// selects the unsigned view, which is what keeps the value positive
			// -- that is the part worth asserting here.
			CHECK(signed_js->IsNumber());
			CHECK(unsigned_js->IsNumber());
			CHECK(unsigned_js.As<v8::Number>()->Value() > 0.0);
#endif
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

		// to_double: numbers pass through; a BigInt follows the int64 view, but
		// only when the build has BigInt at all.
		{
			double out = 0;
			CHECK(impl::Helper::to_double(v8::Number::New(isolate, 1.5), out));
			CHECK(out == 1.5);
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(impl::Helper::to_double(int64_conv_detail::new_bigint(isolate, (int64_t)1 << 40), out));
				CHECK(out == (double)((int64_t)1 << 40));
			}
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
			// BigInt: 0n is false, 1n is true (build-dependent).
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(impl::Helper::to_bool(isolate, int64_conv_detail::new_bigint(isolate, 0), out));
				CHECK(!out);
				CHECK(impl::Helper::to_bool(isolate, int64_conv_detail::new_bigint(isolate, 1), out));
				CHECK(out);
			}
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
			// Through a BigInt, the exact-bit form (build-dependent).
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(JSToGD<uint64_t>::convert(isolate, context, int64_conv_detail::new_bigint_unsigned(isolate, value), out));
				CHECK(out == value);
			}
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
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(JSToGD<uint64_t>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, -1), out));
				CHECK(out == UINT64_MAX);
			}
		}

		// The `StaticBindingUtil` path (used by the reflect/dynamic constructor
		// route) must accept the same surface, and write back unsigned.
		{
			uint64_t out = 0;
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(StaticBindingUtil<uint64_t>::get(isolate, context, int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX), out));
				CHECK(out == UINT64_MAX);
			}
			v8::Local<v8::Value> jv;
			CHECK(StaticBindingUtil<uint64_t>::set(isolate, context, UINT64_MAX, jv));
#if JSB_WITH_BIGINT
			CHECK(jv->IsBigInt());
			CHECK(jv.As<v8::BigInt>()->Uint64Value() == UINT64_MAX);
#else
			// Switch off: the unsigned writer falls back to a Number, so the
			// observable contract here is only "positive, not the signed view".
			CHECK(jv->IsNumber());
			CHECK(jv.As<v8::Number>()->Value() > 0.0);
#endif
		}

		// Non-numeric inputs are still rejected.
		{
			uint64_t out = 0;
			CHECK(!JSToGD<uint64_t>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "1"), out));
			CHECK(!JSToGD<uint64_t>::convert(isolate, context, v8::Null(isolate), out));
		}

		// Narrow slots truncate, matching the engine's own behaviour for a narrow
		// parameter. The engine never range-checks the width -- `put_8(300)` writes
		// 44 in plain GDScript (measured) -- and rejecting here would put the
		// static leg at odds with the dynamic one, whose class-method path IS the
		// engine's conversion. See `js_to_fixed_width_int` for the rationale.
		{
			uint8_t u8 = 0;
			int8_t i8 = 0;
			char32_t c32 = 0;

			// In range: unchanged.
			CHECK(JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, 255), u8));
			CHECK(u8 == 255);
			// Out of range: accepted, narrowed mod 2^8 exactly like `(uint8_t)v`.
			CHECK(JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, 256), u8));
			CHECK(u8 == 0);
			CHECK(JSToGD<uint8_t>::convert(isolate, context, v8::Int32::New(isolate, -1), u8));
			CHECK(u8 == 255);

			CHECK(JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, -128), i8));
			CHECK(i8 == -128);
			CHECK(JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, 128), i8));
			CHECK(i8 == -128);
			CHECK(JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, -129), i8));
			CHECK(i8 == 127);
			// The measured engine case: `put_8(300)` stores 44.
			CHECK(JSToGD<int8_t>::convert(isolate, context, v8::Int32::New(isolate, 300), i8));
			CHECK(i8 == 44);

			CHECK(JSToGD<char32_t>::convert(isolate, context, v8::Int32::New(isolate, 0x10FFFF), c32));
			CHECK(c32 == 0x10FFFF);
			// char32_t is a 32-bit code unit: -1 lands on UINT32_MAX, not a rejection.
			CHECK(JSToGD<char32_t>::convert(isolate, context, v8::Int32::New(isolate, -1), c32));
			CHECK(c32 == (char32_t)UINT32_MAX);
		}

		// A BigInt that cannot fit the slot narrows the same way (the read is a
		// bit-pattern read, then the C++ cast).
		if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
			int8_t i8 = 0;
			CHECK(JSToGD<int8_t>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, 300), i8));
			CHECK(i8 == 44);
		}

		// A non-numeric value is still a conversion failure, not a truncation.
		{
			uint8_t u8 = 0;
			CHECK(!JSToGD<uint8_t>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "1"), u8));
			CHECK(!JSToGD<uint8_t>::convert(isolate, context, v8::Null(isolate), u8));
		}
	}

	env.reset();
}

#if JSB_WITH_STATIC_BINDINGS
// `probe_vt` and the operator thunks live in the static-binding layer, so these
// two cases are gated the same way the header is.
//
// A JS BigInt belongs to the engine's INT surface, so it has to probe as INT:
// without that, every builtin constructor overload filter rejects it before any
// marshaller runs (measured: `new Vector2i(2n, 3)` -> "no suitable constructor").
TEST_CASE("[runtime] [jsb.numeric] probe_vt maps a BigInt onto the INT slot") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();

		CHECK(static_binding::probe_vt(v8::Int32::New(isolate, 2)) == Variant::INT);
		if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
			CHECK(static_binding::probe_vt(int64_conv_detail::new_bigint(isolate, 2)) == Variant::INT);
			CHECK(static_binding::probe_vt(int64_conv_detail::new_bigint_unsigned(isolate, UINT64_MAX)) == Variant::INT);
		}
		CHECK(static_binding::probe_vt(v8::Number::New(isolate, 2.5)) == Variant::FLOAT);
		CHECK(static_binding::probe_vt(v8::Boolean::New(isolate, true)) == Variant::BOOL);
		CHECK(static_binding::probe_vt(v8::Null(isolate)) == Variant::NIL);
		// Both probe orders must agree -- binary operator dispatch probes its left
		// operand object-first and its right operand primitive-first.
		if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
			CHECK(static_binding::probe_vt<static_binding::probe_prefer_object_types>(int64_conv_detail::new_bigint(isolate, 2)) == Variant::INT);
		}
	}

	env.reset();
}
#endif // JSB_WITH_STATIC_BINDINGS

TEST_CASE("[runtime] [jsb.numeric] numeric JSToGD acceptance matches the engine surface") {
	GodotJSScriptLanguageIniter initer;
	std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
	{
		JSB_TESTS_EXECUTION_SCOPE(env.get());
		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		// double / float: the engine's FLOAT surface is BOOL / INT / NIL, and a
		// BigInt probes as INT, so all of them have to marshal -- otherwise the
		// overload filter picks the numeric constructor and the marshaller then
		// rejects it ("selected, then bad argument N").
		{
			double d = 0;
			CHECK(JSToGD<double>::convert(isolate, context, v8::Number::New(isolate, 1.5), d));
			CHECK(d == 1.5);
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(JSToGD<double>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, 3), d));
				CHECK(d == 3.0);
				// A BigInt above 2^53 loses low bits in a double slot -- inherent
				// to the slot, not an error.
				CHECK(JSToGD<double>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, (int64_t)1 << 53), d));
				CHECK(d == (double)((int64_t)1 << 53));
			}
			CHECK(JSToGD<double>::convert(isolate, context, v8::Boolean::New(isolate, true), d));
			CHECK(d == 1.0);
			CHECK(JSToGD<double>::convert(isolate, context, v8::Boolean::New(isolate, false), d));
			CHECK(d == 0.0);
			CHECK(!JSToGD<double>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "1"), d));
			CHECK(!JSToGD<double>::convert(isolate, context, v8::Null(isolate), d));

			float f = 0;
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(JSToGD<float>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, 2), f));
				CHECK(f == 2.0f);
			}
			CHECK(JSToGD<float>::convert(isolate, context, v8::Boolean::New(isolate, true), f));
			CHECK(f == 1.0f);
			CHECK(!JSToGD<float>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "2"), f));
		}

		// int64: BOOL is on the engine's INT surface too (`true` -> 1).
		{
			int64_t v = 0;
			CHECK(JSToGD<int64_t>::convert(isolate, context, v8::Boolean::New(isolate, true), v));
			CHECK(v == 1);
			CHECK(JSToGD<int64_t>::convert(isolate, context, v8::Boolean::New(isolate, false), v));
			CHECK(v == 0);
			CHECK(JSToGD<int64_t>::convert(isolate, context, v8::Number::New(isolate, 2.75), v));
			CHECK(v == 2);
			// null / undefined are NIL, which the engine's INT surface does NOT
			// list -- only BOOL / FLOAT / NIL... NIL is listed, but the direct
			// converter declines a bare null for an int slot (no numeric value).
			CHECK(!JSToGD<int64_t>::convert(isolate, context, v8::Null(isolate), v));
		}

		// bool: the engine's BOOL surface (INT / FLOAT / NIL) with STRING still
		// commented out on the engine side, so a string stays rejected.
		{
			bool b = true;
			CHECK(JSToGD<bool>::convert(isolate, context, v8::Boolean::New(isolate, false), b));
			CHECK(!b);
			CHECK(JSToGD<bool>::convert(isolate, context, v8::Number::New(isolate, 1.0), b));
			CHECK(b);
			CHECK(JSToGD<bool>::convert(isolate, context, v8::Number::New(isolate, 0.0), b));
			CHECK(!b);
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(JSToGD<bool>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, 1), b));
				CHECK(b);
				CHECK(JSToGD<bool>::convert(isolate, context, int64_conv_detail::new_bigint(isolate, 0), b));
				CHECK(!b);
			}
			CHECK(JSToGD<bool>::convert(isolate, context, v8::Null(isolate), b));
			CHECK(!b);
			CHECK(JSToGD<bool>::convert(isolate, context, v8::Undefined(isolate), b));
			CHECK(!b);
			CHECK(!JSToGD<bool>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, "true"), b));
			CHECK(!JSToGD<bool>::convert(isolate, context, v8::String::NewFromUtf8Literal(isolate, ""), b));
		}

		// The reflect constructor path uses StaticBindingUtil, which must agree
		// with JSToGD (it delegated to it).
		{
			bool b = true;
			CHECK(StaticBindingUtil<bool>::get(isolate, context, v8::Number::New(isolate, 3.0), b));
			CHECK(b);
			CHECK(StaticBindingUtil<bool>::get(isolate, context, v8::Null(isolate), b));
			CHECK(!b);
			CHECK(!StaticBindingUtil<bool>::get(isolate, context, v8::String::NewFromUtf8Literal(isolate, "1"), b));

			double d = 0;
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(StaticBindingUtil<double>::get(isolate, context, int64_conv_detail::new_bigint(isolate, 4), d));
				CHECK(d == 4.0);
			}
			float f = 0;
			CHECK(StaticBindingUtil<float>::get(isolate, context, v8::Boolean::New(isolate, true), f));
			CHECK(f == 1.0f);
		}

		// `can_convert_strict<BOOL>` is the predicate the dynamic path checks
		// before converting; it has to accept exactly what JSToGD<bool> does.
		{
			CHECK(TypeConvert::can_convert_strict(isolate, context, v8::Boolean::New(isolate, true), Variant::BOOL));
			CHECK(TypeConvert::can_convert_strict(isolate, context, v8::Number::New(isolate, 0.0), Variant::BOOL));
			if (constexpr bool bigint_case = int64_conv_detail::bigint_inputs_supported; bigint_case) {
				CHECK(TypeConvert::can_convert_strict(isolate, context, int64_conv_detail::new_bigint(isolate, 1), Variant::BOOL));
			}
			CHECK(TypeConvert::can_convert_strict(isolate, context, v8::Null(isolate), Variant::BOOL));
			CHECK(TypeConvert::can_convert_strict(isolate, context, v8::Undefined(isolate), Variant::BOOL));
			CHECK(!TypeConvert::can_convert_strict(isolate, context, v8::String::NewFromUtf8Literal(isolate, "true"), Variant::BOOL));
		}
	}

	env.reset();
}

} //namespace jsb::tests
