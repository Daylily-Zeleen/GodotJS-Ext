/************************************************************************/
/*  jsb_jsc_ext.h                                                       */
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
#include "jsb_jsc_pch.h"
#include "jsb_jsc_typedef.h"

namespace v8 {
class Isolate;
}

namespace jsb::impl {
class JavaScriptCore {
public:
	// [unsafe] forcibly cast JSValueRef to JSObjectRef
	static JSObjectRef AsObject(JSContextRef ctx, JSValueRef val) {
		// return JSValueToObject(ctx, val, nullptr);
		if (val) return JSValueToObject(ctx, val, nullptr);
		// if (JSValueIsObject(ctx, val)) return (JSObjectRef) val;
		return nullptr;
	}

	static void SetContextOpaque(JSContextRef ctx, void *data) {
		const JSObjectRef globalObject = JSContextGetGlobalObject(ctx);
		JSObjectSetPrivate(globalObject, data);
	}

	static void *GetContextOpaque(JSContextRef ctx) {
		const JSObjectRef globalObject = JSContextGetGlobalObject(ctx);
		return JSObjectGetPrivate(globalObject);
	}

	static void MarkExceptionAsTrivial(JSContextRef ctx, JSValueRef error) {
		if (!error) {
			return;
		}
		if (const JSStringRef str = JSValueToStringCopy(ctx, error, nullptr)) {
			const size_t cap = JSStringGetMaximumUTF8CStringSize(str);
			char *buf = (char *)memalloc(cap);
			const size_t len = JSStringGetUTF8CString(str, buf, cap);
			jsb_unused(len);
			JSB_JSC_LOG(Verbose, "ignoring trivial error: %s", buf);
			JSStringRelease(str);
		}
	}

	template <bool kProtected>
	static JSValueRef MakeUTF8String(JSContextRef ctx, const char *p_str) {
		const JSStringRef str = JSStringCreateWithUTF8CString(p_str);
		const JSValueRef val = JSValueMakeString(ctx, str);
		JSStringRelease(str);
		if constexpr (kProtected) {
			JSValueProtect(ctx, val);
		}
		return val;
	}

	static String GetString(JSContextRef ctx, JSValueRef value) {
		if (value) {
			if (const JSStringRef str = JSValueToStringCopy(ctx, value, nullptr)) {
				const size_t cap = JSStringGetMaximumUTF8CStringSize(str);
				char *buf = (char *)memalloc(cap);
				const size_t len = JSStringGetUTF8CString(str, buf, cap);
				JSStringRelease(str);
				jsb_check(len > 0 && (size_t)(int)len == len);
				const String parsed = String::utf8(buf, (int)(len - 1));
				memfree(buf);
				return parsed;
			}
		}
		return String();
	}
};
} //namespace jsb::impl
