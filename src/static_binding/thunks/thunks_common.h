#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "runtime/internal/jsb_macros.h"
#include "runtime/bridge/jsb_type_convert_direct.h"

namespace jsb::static_binding {

// ---------------------------------------------------------------------------
// C++20 structural string constant usable as a non-type template parameter.
template <size_t N>
struct FixedString {
	char value[N]{};
	static constexpr size_t length = N - 1; // excluding terminator

	constexpr FixedString(const char (&s)[N]) {
		for (size_t i = 0; i < N; ++i) {
			value[i] = s[i];
		}
	}
};

// ---------------------------------------------------------------------------
// Default value for optional parameters, keyed by (C++ type, literal).
// Parsed once per instantiation (magic static); STRING literals are used
// verbatim, everything else goes through str_to_var -- the same rule the
// dynamic path follows (api_tool_parser.cpp).
template <typename T, FixedString Lit>
inline const T &default_as() {
	static const T value = []() -> T {
		if constexpr (std::is_same_v<T, godot::String>) {
			return T(Lit.value);
		} else {
			godot::Variant parsed = godot::UtilityFunctions::str_to_var(Lit.value);
			return parsed;
		}
	}();
	return value;
}

// ---------------------------------------------------------------------------
// Parameter descriptor emitted by the code generator:
//   Arg<godot::Vector2>            required parameter, converted directly to
//                                  the strongly-typed value via try_js_to_gd
//   Arg<int64_t, "-1">             optional, filled from a per-(type,literal)
//                                  default singleton when missing
template <typename T_, FixedString Def = FixedString("")>
struct Arg {
	using gd_type = T_;
	static constexpr bool has_default = Def.length > 0;
	static constexpr FixedString def = Def;
};

// ---------------------------------------------------------------------------
// Return descriptor: numeric hint is the GDExtensionVariantType value
// (0 == NIL == void); RetAny converts without a type hint (json "Variant").
struct RetAny {
	static constexpr bool has_return = true;
};

template <int VT_ = 0>
struct Ret {
	static constexpr godot::Variant::Type vt = (godot::Variant::Type)VT_;
	static constexpr bool has_return = VT_ != 0;
};

// ---------------------------------------------------------------------------
// Marshaling helpers.

// Produce one strongly-typed value from the JS arguments (conversion,
// default-fill, or error). This is the single conversion entry point shared
// by every thunk shape.
template <class ArgT>
inline bool produce_value(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info, int i,
		typename ArgT::gd_type &out, int provided) {
	if (i < provided) {
		if (!try_js_to_gd(p_isolate, p_context, info[i], out)) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d: got %s", i,
					TypeConvert::js_debug_typeof(p_isolate, info[i]).utf8().get_data()));
			return false;
		}
		return true;
	}
	if constexpr (ArgT::has_default) {
		out = default_as<typename ArgT::gd_type, ArgT::def>();
		return true;
	}
	jsb_throw(p_isolate, jsb_errorf("missing argument %d", i));
	return false;
}

// ptrcall flavor: produce the value and encode it into a raw argument slot
// through godot-cpp's ptrcall contract. SlotOf<T> is PtrToArg<T>::EncodeT --
// the authoritative slot layout (bool -> uint8_t, narrow ints widen to
// int64_t, Object* -> engine object pointer). Never hand raw addresses of
// semantic values to the engine: some encodes are NOT trivial writes.
template <typename T>
using SlotOf = typename godot::PtrToArg<T>::EncodeT;

template <class ArgT>
inline bool marshal_one(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info, int i,
		SlotOf<typename ArgT::gd_type> &slot, int provided) {
	typename ArgT::gd_type value{};
	if (!produce_value<ArgT>(p_isolate, p_context, info, i, value, provided)) {
		return false;
	}
	godot::PtrToArg<typename ArgT::gd_type>::encode(value, &slot);
	return true;
}

// Untyped tail loop for vararg methods -- the only allowed loop (§4.0-B).
inline bool marshal_tail_args(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const v8::FunctionCallbackInfo<v8::Value> &info,
		godot::Variant *tail, int from, int provided) {
	for (int i = from; i < provided; ++i) {
		if (!TypeConvert::js_to_gd_var(p_isolate, p_context, info[i], tail[i - from])) {
			jsb_throw(p_isolate, jsb_errorf("bad argument %d", i));
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
inline bool translate_return(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
		const godot::Variant &ret, const v8::FunctionCallbackInfo<v8::Value> &info) {
	if constexpr (!RetT::has_return) {
		return true;
	} else {
		v8::Local<v8::Value> jrval;
		bool ok;
		if constexpr (requires { RetT::vt; }) {
			ok = TypeConvert::gd_var_to_js(p_isolate, p_context, ret, RetT::vt, jrval);
		} else {
			ok = TypeConvert::gd_var_to_js(p_isolate, p_context, ret, jrval);
		}
		if (!ok) {
			jsb_throw(p_isolate, "failed to translate godot variant to v8 value");
			return false;
		}
		info.GetReturnValue().Set(jrval);
		return true;
	}
}

// Per-instantiation lazy native-pointer cache (magic static, thread-safe on
// first call). Returns nullptr on engine/generated-tables version mismatch.
template <godot::Variant::Type VTC, uint32_t HashC, FixedString NameLit>
GDExtensionPtrBuiltInMethod resolve_builtin_method() {
		static GDExtensionPtrBuiltInMethod fn = [&] {
		// one-shot lookup: a temporary StringName suffices, nothing retains it
		const godot::StringName method_name(NameLit.value);
		return ::godot::gdextension_interface::variant_get_ptr_builtin_method(
				(GDExtensionVariantType)VTC,
				method_name._native_ptr(),
				(GDExtensionInt)HashC);
	}();
	return fn;
}

template <uint32_t HashC, FixedString NameLit>
GDExtensionPtrUtilityFunction resolve_utility_function() {
	static GDExtensionPtrUtilityFunction fn = [&] {
		const godot::StringName function_name(NameLit.value);
		return ::godot::gdextension_interface::variant_get_ptr_utility_function(
				function_name._native_ptr(),
				(GDExtensionInt)HashC);
	}();
	return fn;
}

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
