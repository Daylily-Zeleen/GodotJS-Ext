/************************************************************************/
/*  jsb_primitive_conv.h                                                */
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

#include <cstdint>

#include "../../jsb.config.h"

// Engine-agnostic numeric conversion primitives.
//
// The four engine shims (`impl/{v8,node,quickjs,jsc,web}/jsb_*_helper.h`) used
// to carry verbatim copies of `to_int64` / `new_integer`. Those copies drifted:
// the 64-bit read threshold was one-sided, so every negative value with a
// magnitude above 2^53 was silently rounded through `(double)`. That broke
// `ObjectID` round-tripping -- `RefCounted` ids carry `is_ref_counted` in bit
// 63, so they are negative when read as int64, and `instance_from_id()` then
// received a different id than `get_instance_id()` had returned.
//
// This header is the single implementation of the policy; each shim only
// contributes the primitives it alone can express (`v8::BigInt::Uint64Value`,
// and `v8::Value::BooleanValue`). Only the API subset shared by all four shims
// is used, so the same code compiles against every engine.
//
// Contract (matches the engine's own bit-preserving view of 64-bit integers,
// where `Variant::operator uint64_t()` is `static_cast<uint64_t>(operator
// int64_t())`, and `ObjectID::is_ref_counted()` tests bit 63):
//
//   - Reading a 64-bit integer is a bit-pattern read, never a range check.
//     Values outside the slot wrap modulo 2^64, which is what quickjs-ng / jsc
//     / web do natively and what `instance_from_id()` needs.
//   - Writing uses a two-sided threshold: `|v| <= JSB_MAX_SAFE_INTEGER` stays a
//     `Number` (int32 when it fits), anything else becomes a `BigInt`. uint64 is
//     written unsigned, so `get_instance_id()` reads back positive.
//   - Only v8 can report whether a `BigInt` was read losslessly (its accessors
//     take a `lossless` out-parameter). Nothing here branches on it for
//     equality of behavior; it is consulted only in `to_double`, to tell a
//     representable value from one that exceeds 64 bits.
//
// Every reader takes `v8::Local<v8::Value>`. This header must be included
// AFTER the engine's own pch, which is what brings in `v8::Local` / `Int32` /
// `Number` / `BigInt` and the `_FORCE_INLINE_` macro.

namespace jsb::impl::internal {

// Number / Int32 / BigInt -> int64, bit-preserving (mod 2^64).
_FORCE_INLINE_ bool to_int64(const v8::Local<v8::Value> p_val, int64_t &r_val) {
	if (p_val->IsInt32()) {
		r_val = p_val.As<v8::Int32>()->Value();
		return true;
	}
	if (p_val->IsNumber()) {
		r_val = (int64_t)p_val.As<v8::Number>()->Value();
		return true;
	}
#if JSB_WITH_BIGINT
	if (p_val->IsBigInt()) {
		r_val = p_val.As<v8::BigInt>()->Int64Value();
		return true;
	}
#endif
	return false;
}

// Number / Int32 / BigInt -> uint64, bit-preserving (mod 2^64).
_FORCE_INLINE_ bool to_uint64(const v8::Local<v8::Value> p_val, uint64_t &r_val) {
	if (p_val->IsInt32()) {
		r_val = (uint64_t)(int64_t)p_val.As<v8::Int32>()->Value();
		return true;
	}
	if (p_val->IsNumber()) {
		const double v = p_val.As<v8::Number>()->Value();
		// Two ranges, because one cast cannot cover both:
		//   - [0, 2^64) casts directly. Routing it through int64 is undefined
		//     for [2^63, 2^64) -- `1e19` lives there, and the x86 conversion
		//     returns the INT64_MIN sentinel (0x8000000000000000) instead of
		//     the real value.
		//   - negative goes through int64, so it wraps the way the engine's own
		//     `Variant::operator uint64_t()` does (`-1` -> 0xffff...ffff).
		if (v >= 0.0 && v < 18446744073709551616.0) {
			r_val = (uint64_t)v;
		} else if (v < 0.0 && v >= -9223372036854775808.0) {
#			if JSB_DEBUG
			// A negative `Number` reaches this slot as its two's-complement bit
			// pattern, not as a value: `-1` becomes 0xffffffffffffffff. That is
			// what the engine itself does (`Variant::operator uint64_t()` is
			// `static_cast<uint64_t>(operator int64_t())`), and rejecting it here
			// would split the static and dynamic legs. Warn instead, in debug
			// builds only, because silently accepting `-1` for an unsigned slot
			// is an easy typo to stare past. `%d` + a cast: Godot's
			// `String::sprintf` has no `%lld`.
			JSB_LOG(Warning, "negative number %d fed to a uint64 slot: its bits are reinterpreted, not its value", (int)v);
#			endif
			r_val = (uint64_t)(int64_t)v;
		} else {
			// A double outside the 64-bit range has no representable value in
			// this slot. Reject instead of invoking undefined behavior; every
			// value that does fit is still written bit-exactly.
			return false;
		}
		return true;
	}
#if JSB_WITH_BIGINT
	if (p_val->IsBigInt()) {
		r_val = p_val.As<v8::BigInt>()->Uint64Value();
		return true;
	}
#endif
	return false;
}

// Number / BigInt -> double.
//
// A BigInt is read through its 64-bit accessor rather than the engine's
// `NumberValue`, for two reasons:
//
//   - v8's `NumberValue` is `ToNumber()` semantics, and `ToNumber` throws a
//     TypeError on a BigInt (only the `Number()` function special-cases it).
//     Using it would leave a pending exception behind a failed conversion.
//   - the 64-bit accessor is exact and identical on all four engines, and a
//     correctly-rounded double of a 64-bit integer is what `Number()` produces
//     for the same value anyway.
//
// The read is the *signed* one, matching `to_int64` and the INT slot (which
// `probe_vt` routes a BigInt to), so the same argument behaves the same way in
// an int slot and a float slot. The single divergence from `Number()` is a
// BigInt in [2^63, 2^64) -- i.e. a uint64 whose bit 63 is set, such as an
// ObjectID. Reading it as int64 keeps its bit pattern but flips the sign, which
// is the same interpretation the engine's own `Variant::operator uint64_t()`
// applies (`static_cast<uint64_t>(operator int64_t())`). Telling that case apart
// would need the `lossless` out-parameter, which only v8 provides, so branching
// on it would make the engines disagree.
_FORCE_INLINE_ bool to_double(const v8::Local<v8::Value> p_val, double &r_val) {
	if (p_val->IsNumber()) {
		r_val = p_val.As<v8::Number>()->Value();
		return true;
	}
#if JSB_WITH_BIGINT
	if (p_val->IsBigInt()) {
		r_val = (double)p_val.As<v8::BigInt>()->Int64Value();
		return true;
	}
#endif
	return false;
}

// Number / Boolean / BigInt / null / undefined -> bool, following `Boolean()`.
//
// Mirrors the engine's own strict-conversion table for BOOL
// (`Variant::can_convert_strict`: INT / FLOAT / NIL, with STRING commented
// out), so a string is still rejected rather than coerced.
_FORCE_INLINE_ bool to_bool(v8::Isolate *p_isolate, const v8::Local<v8::Value> p_val, bool &r_val) {
	if (p_val->IsNullOrUndefined()) {
		r_val = false;
		return true;
	}
	if (p_val->IsBoolean() || p_val->IsNumber()
#if JSB_WITH_BIGINT
			|| p_val->IsBigInt()
#endif
	) {
		r_val = p_val->BooleanValue(p_isolate);
		return true;
	}
	return false;
}

// int64 -> JS, value-dependent: `int32` when it fits, `BigInt` beyond +-2^53-1,
// `Number` otherwise. `JSB_WITH_BIGINT=0` drops the BigInt arm (the value
// leaves as the lossy `Number`, the pre-BigInt behaviour).
//
// This is the writer for every int64-shaped value: class-method returns,
// property getters, the untyped `Variant::INT` fallback, narrow 32-bit uses,
// eval results, `Variant` hand-offs. The result type is decided by MAGNITUDE
// alone, so a caller that needs a definite type narrows it itself.
_FORCE_INLINE_ v8::Local<v8::Value> new_integer(v8::Isolate *p_isolate, const int64_t p_val) {
	if (const int32_t downscale = (int32_t)p_val;
			(int64_t)downscale == p_val) {
		return v8::Int32::New(p_isolate, downscale);
	}
#if JSB_WITH_BIGINT
	if (p_val > JSB_MAX_SAFE_INTEGER || p_val < -JSB_MAX_SAFE_INTEGER) {
		// Godot's `String::sprintf` has no `%lld`; a 64-bit value goes through
		// `%d` with a cast (the project-wide convention), or the log line is dropped.
		JSB_LOG(VeryVerbose, "represented as bigint %d", (int)p_val);
		return v8::BigInt::New(p_isolate, p_val);
	}
#endif
	return v8::Number::New(p_isolate, (double)p_val);
}

// uint64 -> JS: unsigned, so a value with bit 63 set (an ObjectID) stays
// positive. Same magnitude rule: `int32` when it fits, `BigInt` beyond
// 2^53-1, `Number` otherwise.
_FORCE_INLINE_ v8::Local<v8::Value> new_unsigned_integer(v8::Isolate *p_isolate, const uint64_t p_val) {
	if (p_val <= (uint64_t)INT32_MAX) {
		return v8::Int32::New(p_isolate, (int32_t)p_val);
	}
#if JSB_WITH_BIGINT
	if (p_val > (uint64_t)JSB_MAX_SAFE_INTEGER) {
		return v8::BigInt::NewFromUnsigned(p_isolate, p_val);
	}
#endif
	return v8::Number::New(p_isolate, (double)p_val);
}

} // namespace jsb::impl::internal
