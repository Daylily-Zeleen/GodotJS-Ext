#pragma once

#include <limits>
#include <type_traits>

// Direct JS value -> strongly-typed Godot value converters (design doc §3,
// Level-1). Static-binding thunks call these directly, so parameters never
// materialize as Variant; TypeConvert::js_to_gd_var delegates here as well,
// keeping one conversion source of truth for both binding paths.
//
// Conversion semantics mirror the corresponding cases inside
// TypeConvert::js_to_gd_var (jsb_type_convert.cpp) so both paths behave
// identically.

#include "jsb_bridge_pch.h"
#include "jsb_class_info.h"
#include "jsb_object_handle.h"
#include "jsb_environment.h"
#include "jsb_type_convert.h"

namespace jsb {

// unwrap JS Proxy before any conversion (same as js_to_gd_var's prologue)
inline bool js_unwrap_proxy(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
        const v8::Local<v8::Value> &p_val, v8::Local<v8::Value> &r_unwrapped) {
#if JSB_WITH_V8
    if (p_val->IsProxy())
#else
    if (p_val->IsObject())
#endif
    {
        v8::Local<v8::Object> object = p_val.As<v8::Object>();
        v8::MaybeLocal<v8::Value> maybe_target =
                object->Get(p_context, Environment::wrap(p_isolate)->get_symbol(Symbols::ProxyTarget));
        v8::Local<v8::Value> target;
        if (maybe_target.ToLocal(&target) && !target->IsUndefined()) {
            r_unwrapped = target;
            return true;
        }
    }
    return false;
}

template <typename T>
struct JSToGD;

// untyped: delegate to the dynamic converter (used for vararg tails and
// json-typed "Variant" parameters)
template <>
struct JSToGD<godot::Variant> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, godot::Variant &r_out) {
        return TypeConvert::js_to_gd_var(p_isolate, p_context, p_jval, r_out);
    }
};

#define JSB_DIRECT_SCALAR(CppType, Check, Extract)                                        \
    template <>                                                                           \
    struct JSToGD<CppType> {                                                              \
        static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, \
                const v8::Local<v8::Value> &p_jval, CppType &r_out) {                     \
            (void)p_context;                                                              \
            if (p_jval -> Check) {                                                        \
                r_out = Extract;                                                          \
                return true;                                                              \
            }                                                                             \
            return false;                                                                 \
        }                                                                                 \
    };

JSB_DIRECT_SCALAR(bool, IsBoolean(), p_jval.As<v8::Boolean>()->Value())
JSB_DIRECT_SCALAR(double, IsNumber(), p_jval.As<v8::Number>()->Value())
#undef JSB_DIRECT_SCALAR

template <>
struct JSToGD<int64_t> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, int64_t &r_out) {
        (void)p_isolate;
        (void)p_context;
        return impl::Helper::to_int64(p_jval, r_out);
    }
};

// Exact-width integer targets: convert through int64 then range-check, so a
// JS value that does not fit the declared meta width (int8..uint64) is a
// clean conversion failure instead of a silent truncation.
template <typename CppT>
inline bool js_to_fixed_width_int(v8::Isolate *p_isolate,
        const v8::Local<v8::Context> &p_context,
        const v8::Local<v8::Value> &p_jval, CppT &r_out) {
	int64_t wide = 0;
	if (!JSToGD<int64_t>::convert(p_isolate, p_context, p_jval, wide)) {
		return false;
	}
	if constexpr (std::is_same_v<CppT, uint64_t>) {
		// int64 cannot represent uint64's max; validate in unsigned domain.
		if (wide < 0) {
			return false;
		}
		if (static_cast<uint64_t>(wide) > std::numeric_limits<uint64_t>::max()) {
			return false;
		}
	} else if constexpr (std::is_unsigned_v<CppT>) {
		if (wide < 0 || wide > static_cast<int64_t>(std::numeric_limits<CppT>::max())) {
			return false;
		}
	} else {
		if (wide < static_cast<int64_t>(std::numeric_limits<CppT>::min()) ||
				wide > static_cast<int64_t>(std::numeric_limits<CppT>::max())) {
			return false;
		}
	}
	r_out = static_cast<CppT>(wide);
	return true;
}

#define JSB_DIRECT_FIXED_INT(CppType)                                                     	template <>                                                                           	struct JSToGD<CppType> {                                                              		static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, 				const v8::Local<v8::Value> &p_jval, CppType &r_out) {                     			return js_to_fixed_width_int<CppType>(p_isolate, p_context, p_jval, r_out);   		}                                                                                 	};

JSB_DIRECT_FIXED_INT(int8_t)
JSB_DIRECT_FIXED_INT(int16_t)
JSB_DIRECT_FIXED_INT(int32_t)
JSB_DIRECT_FIXED_INT(uint8_t)
JSB_DIRECT_FIXED_INT(uint16_t)
JSB_DIRECT_FIXED_INT(uint32_t)
JSB_DIRECT_FIXED_INT(uint64_t)
JSB_DIRECT_FIXED_INT(char32_t)
#undef JSB_DIRECT_FIXED_INT

template <>
struct JSToGD<godot::String> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, godot::String &r_out) {
        (void)p_context;
        if (!p_jval->IsString()) {
            return false;
        }
        godot::StringName sn;
        if (Environment::wrap(p_isolate)->get_string_name_cache().try_get_string_name(p_isolate, p_jval, sn)) {
            r_out = (godot::String)sn;
            return true;
        }
        r_out = impl::Helper::to_string(p_isolate, p_jval);
        return true;
    }
};

template <>
struct JSToGD<godot::StringName> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, godot::StringName &r_out) {
        (void)p_context;
        if (p_jval->IsString()) {
            r_out = Environment::wrap(p_isolate)->get_string_name(p_jval.As<v8::String>());
            return true;
        }
        // same fallback semantics as the dynamic path: accept a variant-backed wrapper
        godot::Variant v;
        if (!JSToGD<godot::Variant>::convert(p_isolate, p_context, p_jval, v)) {
            return false;
        }
        r_out = v;
        return true;
    }
};

template <>
struct JSToGD<godot::NodePath> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, godot::NodePath &r_out) {
        (void)p_context;
        if (p_jval->IsString()) {
            godot::StringName sn;
            if (Environment::wrap(p_isolate)->get_string_name_cache().try_get_string_name(p_isolate, p_jval, sn)) {
                r_out = godot::NodePath((godot::String)sn);
                return true;
            }
            r_out = godot::NodePath(impl::Helper::to_string(p_isolate, p_jval));
            return true;
        }
        godot::Variant v;
        if (!JSToGD<godot::Variant>::convert(p_isolate, p_context, p_jval, v)) {
            return false;
        }
        r_out = v;
        return true;
    }
};

template <>
struct JSToGD<godot::Object *> {
    static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
            const v8::Local<v8::Value> &p_jval, godot::Object *&r_out) {
        (void)p_context;
        if (!p_jval->IsObject()) {
            // objects are usually nullable
            if (p_jval->IsNullOrUndefined()) {
                r_out = nullptr;
                return true;
            }
            return false;
        }
        const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
        if (!TypeConvert::is_object(self)) {
            return false;
        }
        void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
        r_out = Environment::wrap(p_isolate)->verify_object(pointer) ? (godot::Object *)pointer : nullptr;
        return true;
    }
};

// variant-backed types: the JS wrapper stores a full Variant internally; read
// it and convert to the requested type via Variant's implicit conversion.
template <typename T>
inline bool extract_variant_backed(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
        const v8::Local<v8::Value> &p_jval, T &r_out) {
    (void)p_isolate;
    (void)p_context;
    if (!p_jval->IsObject()) {
        return false;
    }
    const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
    if (!TypeConvert::is_variant(self)) {
        return false;
    }
    void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
    r_out = *(godot::Variant *)pointer;
    return true;
}

#define JSB_DIRECT_VARIANT_BACKED(CppType)                                                \
    template <>                                                                           \
    struct JSToGD<CppType> {                                                              \
        static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, \
                const v8::Local<v8::Value> &p_jval, CppType &r_out) {                     \
            return extract_variant_backed(p_isolate, p_context, p_jval, r_out);           \
        }                                                                                 \
    };

JSB_DIRECT_VARIANT_BACKED(godot::Vector2)
JSB_DIRECT_VARIANT_BACKED(godot::Vector2i)
JSB_DIRECT_VARIANT_BACKED(godot::Rect2)
JSB_DIRECT_VARIANT_BACKED(godot::Rect2i)
JSB_DIRECT_VARIANT_BACKED(godot::Vector3)
JSB_DIRECT_VARIANT_BACKED(godot::Vector3i)
JSB_DIRECT_VARIANT_BACKED(godot::Transform2D)
JSB_DIRECT_VARIANT_BACKED(godot::Vector4)
JSB_DIRECT_VARIANT_BACKED(godot::Vector4i)
JSB_DIRECT_VARIANT_BACKED(godot::Plane)
JSB_DIRECT_VARIANT_BACKED(godot::Quaternion)
JSB_DIRECT_VARIANT_BACKED(godot::AABB)
JSB_DIRECT_VARIANT_BACKED(godot::Basis)
JSB_DIRECT_VARIANT_BACKED(godot::Transform3D)
JSB_DIRECT_VARIANT_BACKED(godot::Projection)
JSB_DIRECT_VARIANT_BACKED(godot::Color)
JSB_DIRECT_VARIANT_BACKED(godot::RID)
JSB_DIRECT_VARIANT_BACKED(godot::Callable)
JSB_DIRECT_VARIANT_BACKED(godot::Signal)

#undef JSB_DIRECT_VARIANT_BACKED

// Container-family targets: accept BOTH a variant-backed wrapper (Godot
// Array/Dictionary/Packed*) AND raw JS values (native Array, object literal,
// null) by falling back to the full typed dynamic converter. The strict
// variant-backed path above would reject native JS values that the dynamic
// path happily converts (e.g. OS.execute("sh", ["-v"], output)).
#define JSB_DIRECT_CONTAINER(CppType, GDType)                                             \
	template <>                                                                           \
	struct JSToGD<CppType> {                                                              \
		static bool convert(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, \
				const v8::Local<v8::Value> &p_jval, CppType &r_out) {                     \
			if (extract_variant_backed(p_isolate, p_context, p_jval, r_out)) {            \
				return true;                                                              \
			}                                                                             \
			godot::Variant v;                                                             \
			if (!TypeConvert::js_to_gd_var(p_isolate, p_context, p_jval, GDType, v)) {    \
				return false;                                                             \
			}                                                                             \
			r_out = v;                                                                    \
			return true;                                                                  \
		}                                                                                 \
	};

JSB_DIRECT_CONTAINER(godot::Array, godot::Variant::ARRAY)
JSB_DIRECT_CONTAINER(godot::Dictionary, godot::Variant::DICTIONARY)
JSB_DIRECT_CONTAINER(godot::PackedByteArray, godot::Variant::PACKED_BYTE_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedInt32Array, godot::Variant::PACKED_INT32_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedInt64Array, godot::Variant::PACKED_INT64_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedFloat32Array, godot::Variant::PACKED_FLOAT32_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedFloat64Array, godot::Variant::PACKED_FLOAT64_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedStringArray, godot::Variant::PACKED_STRING_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector2Array, godot::Variant::PACKED_VECTOR2_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector3Array, godot::Variant::PACKED_VECTOR3_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedColorArray, godot::Variant::PACKED_COLOR_ARRAY)
JSB_DIRECT_CONTAINER(godot::PackedVector4Array, godot::Variant::PACKED_VECTOR4_ARRAY)

#undef JSB_DIRECT_CONTAINER

// entry point with Proxy unwrapping
template <typename T>
inline bool try_js_to_gd(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context,
        const v8::Local<v8::Value> &p_jval, T &r_out) {
    v8::Local<v8::Value> unwrapped;
    if (js_unwrap_proxy(p_isolate, p_context, p_jval, unwrapped)) {
        return try_js_to_gd(p_isolate, p_context, unwrapped, r_out);
    }
    return JSToGD<T>::convert(p_isolate, p_context, p_jval, r_out);
}

} // namespace jsb
