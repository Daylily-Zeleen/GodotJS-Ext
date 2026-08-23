/************************************************************************/
/*  thunks_common.h                                                     */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Shared scaffolding for static-binding thunks:                       */
/*    - FixedString: C++20 structural class NTTP carrier                */
/*    - sn<Lit>: per-literal StringName singleton (magic static,        */
/*      constructed after GDExtension init -- AGENTS.md hard rule)      */
/*    - default_value<VT, Lit>: deduplicated-by-value singleton         */
/*      parsed via UtilityFunctions::str_to_var, same rule as the       */
/*      dynamic path (api_tool_parser.cpp) -- single source of truth    */
/*    - Arg/Ret descriptors and the fixed-arity marshaling helpers      */
/*      (design doc §4.0-A: unrolled per-parameter, no loops; §4.6)     */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include <cstddef>
#include <cstdint>
#include <utility>

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "runtime/internal/jsb_macros.h"
#include "runtime/bridge/jsb_type_convert.h"
#include "api_tool/core/api_tool_internal.h"

namespace jsb::static_binding {

// ---------------------------------------------------------------------------
// C++20 structural string constant usable as a non-type template parameter.
template <size_t N>
struct FixedString {
	char value[N]{};
	static constexpr size_t capacity = N;
	static constexpr size_t length = N - 1; // excluding terminator
	static constexpr bool empty = N <= 1;

	constexpr FixedString(const char (&s)[N]) {
		for (size_t i = 0; i < N; ++i) {
			value[i] = s[i];
		}
	}
};

// Per-literal StringName singleton. First instantiation happens at wrapper
// build time or first call, i.e. after GDExtension CORE init.
template <FixedString Lit>
const godot::StringName &sn() {
	static const godot::StringName name{Lit.value};
	return name;
}

// ---------------------------------------------------------------------------
// Default value singleton keyed by (type, literal) -- NOT by method hash:
// identical defaults across methods share one parse and one storage (§4.6).
// STRING-typed literals are used verbatim; everything else goes through
// UtilityFunctions::str_to_var, exactly like api_tool_parser.cpp:239-247.
template <int VT_, FixedString Lit>
const godot::Variant &default_value() {
	static const godot::Variant value = [] {
		if constexpr (VT_ == godot::Variant::STRING) {
			return godot::Variant(godot::String(Lit.value));
		} else {
			return godot::UtilityFunctions::str_to_var(Lit.value);
		}
	}();
	return value;
}

// ---------------------------------------------------------------------------
// Parameter/return descriptors emitted by the code generator.
//
//   Arg<VT>            required argument of variant type VT
//   Arg<VT, "lit">     optional argument filled with default_value<VT, "lit">
//   ArgAny             untyped argument (json type "Variant"), no hint
//   Ret<VT>            return value converted with type hint VT
//                      (VT == NIL means void: nothing is set on the return)
//   RetAny             return value converted without hint (json "Variant")
struct RetAny {
	static constexpr bool has_return = true;
};

template <int VT_ = 0> // VT_ is a GDExtensionVariantType value; 0 == NIL == void
struct Ret {
	static constexpr godot::Variant::Type vt = (godot::Variant::Type)VT_;
	static constexpr bool has_return = VT_ != 0;
};

template <int VT_, FixedString Def = FixedString("")>
struct Arg {
	static constexpr bool has_type_hint = true;
	static constexpr bool has_default = Def.length > 0;
	static constexpr godot::Variant::Type vt = (godot::Variant::Type)VT_;
	static constexpr FixedString def = Def;
};

template <FixedString Def = FixedString("")>
struct ArgAny {
	static constexpr bool has_type_hint = false;
	static constexpr bool has_default = Def.length > 0;
	static constexpr FixedString def = Def;
};

// Untyped default (json type "Variant"): parsed via str_to_var only.
template <FixedString Lit>
const godot::Variant &default_value_any() {
	static const godot::Variant value = godot::UtilityFunctions::str_to_var(Lit.value);
	return value;
}

// ---------------------------------------------------------------------------
// Marshaling helpers.

template <class ArgT>
inline bool marshal_one(v8::Isolate *isolate, const v8::Local<v8::Context> &context,
		const v8::FunctionCallbackInfo<v8::Value> &info, int i, godot::Variant &out, int provided) {
	if (i < provided) {
		bool ok;
		if constexpr (ArgT::has_type_hint) {
			ok = TypeConvert::js_to_gd_var(isolate, context, info[i], ArgT::vt, out);
		} else {
			ok = TypeConvert::js_to_gd_var(isolate, context, info[i], out);
		}
		if (!ok) {
			if constexpr (requires { ArgT::vt; }) {
				jsb_throw(isolate, jsb_errorf("bad argument %d: expected %s, got %s",
						i, godot::Variant::get_type_name(ArgT::vt),
						TypeConvert::js_debug_typeof(isolate, info[i]).utf8().get_data()));
			} else {
				jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			}
			return false;
		}
		return true;
	}
	if constexpr (ArgT::has_default) {
		if constexpr (requires { ArgT::vt; }) {
			out = default_value<ArgT::vt, ArgT::def>();
		} else {
			out = default_value_any<ArgT::def>();
		}
		return true;
	}
	// unreachable for well-formed dispatch entries (argc is validated first);
	// kept defensive so a generator bug cannot pass garbage to the engine.
	jsb_throw(isolate, jsb_errorf("missing argument %d", i));
	return false;
}

template <class... ArgsT, size_t... I>
inline bool marshal_fixed_args(v8::Isolate *isolate, const v8::Local<v8::Context> &context,
		const v8::FunctionCallbackInfo<v8::Value> &info,
		godot::Variant *args, int provided, std::index_sequence<I...>) {
	bool ok = true;
	(void)((ok = ok && marshal_one<ArgsT>(isolate, context, info, (int)I, args[I], provided)) && ...);
	return ok;
}

// Untyped tail loop for vararg methods -- the only allowed loop (§4.0-B).
inline bool marshal_tail_args(v8::Isolate *isolate, const v8::Local<v8::Context> &context,
		const v8::FunctionCallbackInfo<v8::Value> &info,
		godot::Variant *tail, int from, int provided) {
	for (int i = from; i < provided; ++i) {
		if (!TypeConvert::js_to_gd_var(isolate, context, info[i], tail[i - from])) {
			jsb_throw(isolate, jsb_errorf("bad argument %d", i));
			return false;
		}
	}
	return true;
}

template <class RetT>
inline void init_return(godot::Variant &r_ret) {
	if constexpr (RetT::has_return) {
		if constexpr (requires { RetT::vt; }) {
			r_ret = godot::UtilityFunctions::type_convert(godot::Variant(), RetT::vt);
		}
		// RetAny: leave NIL; the engine overwrites it on return.
	}
}

template <class RetT>
inline bool translate_return(v8::Isolate *isolate, const v8::Local<v8::Context> &context,
		const godot::Variant &ret, const v8::FunctionCallbackInfo<v8::Value> &info) {
	if constexpr (!RetT::has_return) {
		return true;
	} else {
		v8::Local<v8::Value> jrval;
		bool ok;
		if constexpr (requires { RetT::vt; }) {
			ok = TypeConvert::gd_var_to_js(isolate, context, ret, RetT::vt, jrval);
		} else {
			ok = TypeConvert::gd_var_to_js(isolate, context, ret, jrval);
		}
		if (!ok) {
			jsb_throw(isolate, "failed to translate godot variant to v8 value");
			return false;
		}
		info.GetReturnValue().Set(jrval);
		return true;
	}
}

// Runtime-indexed declared type of the fixed prefix (used by vararg thunks).
// One trailing NIL pad keeps the array well-formed for empty packs.
template <class ArgT>
inline constexpr godot::Variant::Type pack_one_type() {
	if constexpr (requires { ArgT::vt; }) {
		return ArgT::vt;
	} else {
		return godot::Variant::NIL;
	}
}

template <class... ArgsT>
inline godot::Variant::Type pack_type_at(int i) {
	const godot::Variant::Type types[] = { pack_one_type<ArgsT>()..., godot::Variant::NIL };
	return types[i];
}

// Per-instantiation lazy native-pointer cache (magic static, thread-safe on
// first call). Returns nullptr when the engine has no matching entry
// (engine/generated-tables version mismatch); callers log once and throw.
template <uint16_t VTC, uint64_t HashC, FixedString NameLit>
GDExtensionPtrBuiltInMethod resolve_builtin_method() {
	static GDExtensionPtrBuiltInMethod fn =
			::godot::gdextension_interface::variant_get_ptr_builtin_method(
					(GDExtensionVariantType)VTC,
					sn<NameLit>()._native_ptr(),
					(GDExtensionInt)HashC);
	return fn;
}

template <uint64_t HashC, FixedString NameLit>
GDExtensionPtrUtilityFunction resolve_utility_function() {
	static GDExtensionPtrUtilityFunction fn =
			::godot::gdextension_interface::variant_get_ptr_utility_function(
					sn<NameLit>()._native_ptr(),
					(GDExtensionInt)HashC);
	return fn;
}

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
