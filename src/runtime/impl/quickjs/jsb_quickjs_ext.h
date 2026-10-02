/************************************************************************/
/*  jsb_quickjs_ext.h                                                   */
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
#include "jsb_quickjs_pch.h"

#if !JSB_PREFER_QUICKJS_NG
#	include <string.h>
#endif

namespace v8 {
class Isolate;
}

namespace jsb::impl {
class QuickJS {
public:
	struct Atom {
	private:
		JSContext *ctx_;
		JSAtom atom_;

	public:
		Atom(JSContext *ctx, JSValueConst value)
				: ctx_(ctx), atom_(JS_ValueToAtom(ctx, value)) {
		}

		~Atom() {
			JS_FreeAtom(ctx_, atom_);
		}

		operator JSAtom() const { return atom_; }
	};

	static void MarkExceptionAsTrivial(JSContext *ctx) {
		const JSValue val = JS_GetException(ctx);
#if JSB_DEBUG
		if (!IsNotErrorThrown(val)) {
			JSB_QUICKJS_LOG(Verbose, "removed a trivial exception: %s", GetString(ctx, val));
		}
#endif
		JS_FreeValue(ctx, val);
	}

	static bool IsNotErrorThrown(JSValueConst value) {
#if JSB_PREFER_QUICKJS_NG
		return JS_IsNull(value) || JS_IsUninitialized(value);
#else
		return JS_IsNull(value);
#endif
	}

	static bool IsNullish(JSValueConst value) {
		return JS_IsNull(value) || JS_IsUndefined(value);
	}

	static bool IsArray(JSContext *ctx, JSValueConst value) {
#if JSB_PREFER_QUICKJS_NG
		return JS_IsArray(value);
#else
		return JS_IsArray(ctx, value);
#endif
	}

	static bool IsBigInt(JSContext *ctx, JSValueConst value) {
#if JSB_PREFER_QUICKJS_NG
		return JS_IsBigInt(value);
#else
		return JS_IsBigInt(ctx, value);
#endif
	}

	static bool IsError(JSContext *ctx, JSValueConst value) {
#if JSB_PREFER_QUICKJS_NG
		return JS_IsError(value);
#else
		return JS_IsError(ctx, value);
#endif
	}

	static String GetString(JSContext *ctx, JSValueConst value) {
		size_t len;
		if (const char *str = JS_ToCStringLen(ctx, &len, value)) {
			const String rval = String::utf8(str, (int)len);
			JS_FreeCString(ctx, str);
			return rval;
		}

		// silently ignore the error
		const JSValue val = JS_GetException(ctx);
		JS_FreeValue(ctx, val);
		return String();
	}

	// 严格相等。
	static bool Equals(JSContext *ctx, JSValueConst p_a, JSValueConst p_b) {
#if JSB_PREFER_QUICKJS_NG
		return JS_IsStrictEqual(ctx, p_a, p_b);
#else
		const auto content_eq = [ctx](JSValueConst a, JSValueConst b) {
			const char *text_a = JS_ToCString(ctx, a);
			if (!text_a) {
				// leaves an exception pending, matching the rest of the shim
				return false;
			}
			const char *text_b = JS_ToCString(ctx, b);
			if (!text_b) {
				JS_FreeCString(ctx, text_a);
				return false;
			}
			const bool equal = strcmp(text_a, text_b) == 0;
			JS_FreeCString(ctx, text_a);
			JS_FreeCString(ctx, text_b);
			return equal;
		};

		const int tag_a = JS_VALUE_GET_NORM_TAG(p_a);
		const int tag_b = JS_VALUE_GET_NORM_TAG(p_b);
		switch (tag_a) {
			case JS_TAG_BOOL:
				return tag_a == tag_b && JS_VALUE_GET_INT(p_a) == JS_VALUE_GET_INT(p_b);
			case JS_TAG_NULL:
			case JS_TAG_UNDEFINED:
				return tag_a == tag_b;
			case JS_TAG_STRING:
				return tag_b == JS_TAG_STRING && content_eq(p_a, p_b);
			case JS_TAG_SYMBOL:
				// interned atoms: identity is the pointer
				return tag_a == tag_b && JS_VALUE_GET_PTR(p_a) == JS_VALUE_GET_PTR(p_b);
			case JS_TAG_OBJECT:
				return tag_b == JS_TAG_OBJECT && JS_VALUE_GET_PTR(p_a) == JS_VALUE_GET_PTR(p_b);
			case JS_TAG_INT:
				// int vs float64 compares numerically, not by tag or by bits
				if (tag_b == JS_TAG_INT) return JS_VALUE_GET_INT(p_a) == JS_VALUE_GET_INT(p_b);
				if (JS_TAG_IS_FLOAT64(tag_b)) return (double)JS_VALUE_GET_INT(p_a) == JS_VALUE_GET_FLOAT64(p_b);
				return false;
			case JS_TAG_FLOAT64:
				if (tag_b == JS_TAG_FLOAT64) return JS_VALUE_GET_FLOAT64(p_a) == JS_VALUE_GET_FLOAT64(p_b);
				if (tag_b == JS_TAG_INT) return JS_VALUE_GET_FLOAT64(p_a) == (double)JS_VALUE_GET_INT(p_b);
				return false;
			case JS_TAG_BIG_INT:
				return tag_b == JS_TAG_BIG_INT && content_eq(p_a, p_b);
			default:
				return tag_a == tag_b && JS_VALUE_GET_PTR(p_a) == JS_VALUE_GET_PTR(p_b);
		}
#endif
	}

	static int _RefCount(JSValueConst value) {
		if (!JS_VALUE_HAS_REF_COUNT(value)) return 0;
#if JSB_PREFER_QUICKJS_NG
		// unsafe
		typedef struct JSRefCountHeader {
			int ref_count;
		} JSRefCountHeader;
#endif
		const JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(value);
		return p ? p->ref_count : 0;
	}

	static JSValue NoopCallback(JSContext *_ctx, JSValueConst _this_val, int _argc, JSValueConst *_argv);
};
} //namespace jsb::impl
