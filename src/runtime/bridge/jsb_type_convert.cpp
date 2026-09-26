/************************************************************************/
/*  jsb_type_convert.cpp                                                */
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

#include "jsb_type_convert.h"
#include "jsb_environment.h"

// Not ideal. Need to clean up access.
#include "../weaver/jsb_script.h"
#include "../weaver/jsb_script_instance.h"
#include "../weaver/jsb_script_language.h"
namespace jsb {

// Warn (debug builds only) when a value is about to be narrowed into an INT
// slot narrower than the 64-bit Variant storage.
//
// The type list mirrors the static leg's `JSB_DIRECT_FIXED_INT`: both legs must
// report the same slots, or a debug build would flag a truncation on one and
// stay silent on the other. Metadata that does not name a narrow width (INT64 /
// UINT64 / none) is ignored.
//
// Diagnostics only -- the conversion truncates unconditionally either way, which
// is what the engine does. See `js_to_fixed_width_int` in
// jsb_type_convert_direct.h for the measurements behind that.
inline void verify_narrow_int_slot(GDExtensionClassMethodArgumentMetadata p_meta, int64_t p_val) {
#if JSB_DEBUG
	int bits = 0;
	int64_t lo = 0, hi = 0;
	switch (p_meta) {
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT8:
			bits = 8; lo = INT8_MIN; hi = INT8_MAX; break;
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT8:
			bits = 8; lo = 0; hi = UINT8_MAX; break;
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT16:
			bits = 16; lo = INT16_MIN; hi = INT16_MAX; break;
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT16:
			bits = 16; lo = 0; hi = UINT16_MAX; break;
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT32:
			bits = 32; lo = INT32_MIN; hi = INT32_MAX; break;
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT32:
			bits = 32; lo = 0; hi = UINT32_MAX; break;
		// char32_t is a 32-bit code unit; the engine takes the whole uint32
		// range and never checks the Unicode bound.
		case GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_CHAR32:
			bits = 32; lo = 0; hi = UINT32_MAX; break;
		default:
			return;
	}
	if (p_val < lo || p_val > hi) {
		JSB_LOG(Warning, "narrow slot argument: %d does not fit the declared %d-bit slot, truncating", (int)p_val, bits);
	}
#else
	jsb_unused(p_meta);
	jsb_unused(p_val);
#endif
}

template <typename ElemTy, typename PackedTy>
static bool try_convert_array(v8::Isolate *isolate, const v8::Local<v8::Context> &context, v8::Local<v8::Value> p_val, Variant &r_packed) {
	if constexpr (GetTypeInfo<ElemTy>::METADATA == GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT8) {
		if (p_val->IsArrayBuffer()) {
			r_packed = impl::Helper::to_packed_byte_array(isolate, p_val.As<v8::ArrayBuffer>());
			return true;
		}
	}

#if JSB_IMPLICIT_PACKED_ARRAY_CONVERSION
	if (!p_val->IsArray()) {
		return false;
	}

	const v8::Local<v8::Array> array = p_val.As<v8::Array>();
	const uint32_t len = array->Length();

	PackedTy packed;
	packed.resize((int)len);
	ElemTy *ptrw = packed.ptrw();
	for (uint32_t index = 0; index < len; ++index) {
		v8::Local<v8::Value> element;
		Variant element_var;
		if (array->Get(context, index).ToLocal(&element) && TypeConvert::js_to_gd_var(isolate, context, element, (Variant::Type)GetTypeInfo<ElemTy>::VARIANT_TYPE, element_var)) {
			ptrw[index] = element_var;
		} else {
			// be cautious here, we silently omit conversion failures
			ptrw[index] = ElemTy{};
			JSB_LOG(Warning, "failed to convert array element %d (strictly, as %s), it'll be left as the default value", index, Variant::get_type_name((Variant::Type)GetTypeInfo<ElemTy>::VARIANT_TYPE));
		}
	}

	r_packed = packed;
	return true;
#else
	return false;
#endif
}

static bool try_convert_array_any(v8::Isolate *isolate, const v8::Local<v8::Context> &context, v8::Local<v8::Value> p_val, Variant &r_packed) {
#if JSB_IMPLICIT_PACKED_ARRAY_CONVERSION
	if (!p_val->IsArray()) {
		return false;
	}

	const v8::Local<v8::Array> array = p_val.As<v8::Array>();
	const uint32_t len = array->Length();
	Array packed;
	packed.resize((int)len);
	for (uint32_t index = 0; index < len; ++index) {
		v8::Local<v8::Value> element;
		Variant element_var;
		if (array->Get(context, index).ToLocal(&element) && TypeConvert::js_to_gd_var(isolate, context, element, element_var)) {
			packed.set((int)index, element_var);
		} else {
			// be cautious here, we silently omit conversion failures
			packed.set((int)index, {});
			JSB_LOG(Warning, "failed to convert array element %d (loosely), it'll be left as the default value", index);
		}
	}
	r_packed = packed;
	return true;
#else
	return false;
#endif
}

String TypeConvert::js_debug_typeof(v8::Isolate *isolate, const v8::Local<v8::Value> &p_jval) {
	if (p_jval.IsEmpty()) {
		return "<empty>";
	}
	if (p_jval->IsUndefined()) {
		return "undefined";
	}
	if (p_jval->IsNull()) {
		return "null";
	}
	if (p_jval->IsBoolean()) {
		return "boolean";
	}
	if (p_jval->IsNumber()) {
		return "number";
	}
	if (p_jval->IsString()) {
		return "string";
	}
	if (p_jval->IsArray()) {
		return "array";
	}
	if (p_jval->IsFunction()) {
		return "function";
	}
	if (p_jval->IsObject()) {
		v8::Local<v8::Object> object = p_jval.As<v8::Object>();

		if (is_variant(object)) {
			const Variant *variant = (Variant *)object->GetAlignedPointerFromInternalField(IF_Pointer);
			return jsb_format("variant (%s)", Variant::get_type_name(variant->get_type()));
		}

		v8::Local<v8::String> constructor_name = object->GetConstructorName();

		if (constructor_name.IsEmpty()) {
			return "object";
		}

		return jsb_format("object (%s)", impl::Helper::to_string(isolate, constructor_name));
	}

	return "<unknown>";
}

// translate js val into gd variant with an expected type
bool TypeConvert::js_to_gd_var(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Variant::Type p_type, GDExtensionClassMethodArgumentMetadata p_meta, Variant &r_cvar) {
#if JSB_WITH_V8
	if (p_jval->IsProxy())
#else
	if (p_jval->IsObject())
#endif
	{
		v8::Local<v8::Object> object = p_jval.As<v8::Object>();
		v8::MaybeLocal<v8::Value> maybe_target = object->Get(context, Environment::wrap(isolate)->get_symbol(Symbols::ProxyTarget));
		v8::Local<v8::Value> target;

		if (maybe_target.ToLocal(&target) && !target->IsUndefined()) {
			return js_to_gd_var(isolate, context, target, p_type, p_meta, r_cvar);
		}
	}

	switch (p_type) {
		case Variant::FLOAT:
			if (p_jval->IsNumber()) {
				r_cvar = p_jval.As<v8::Number>()->Value();
				return true;
			}
			break;
		case Variant::INT:
			// The Variant INT slot is 64 bits either way, so both reads produce
			// the same storage -- but only the unsigned one gets the right bits
			// from a number in [2^63, 2^64). Reading such a double as int64 is
			// undefined and yields the INT64_MIN sentinel on x86, so
			// `put_u64(1e19)` would land as 0x8000000000000000.
			if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64) {
				if (uint64_t val; impl::Helper::to_uint64(p_jval, val)) {
					// Cast back for the signed slot: the bit pattern is what the
					// engine reads out of it, so this is the value it wants.
					r_cvar = (int64_t)val;
					return true;
				}
				break;
			}
			// strict?
			if (int64_t val; impl::Helper::to_int64(p_jval, val)) {
				// A narrow slot truncates, exactly as the engine does -- but this is
				// the last point that still sees the untruncated value, so this is
				// where the debug warning belongs (no-op in release).
				verify_narrow_int_slot(p_meta, val);
				r_cvar = val;
				return true;
			}
			break;
		case Variant::OBJECT: {
			if (!p_jval->IsObject()) {
				//NOTE we return true because object is usually nullable in most situations
				if (p_jval->IsNullOrUndefined()) {
					r_cvar = (Object *)nullptr;
					return true;
				}
				break;
			}
			const v8::Local<v8::Object> self = p_jval.As<v8::Object>();

			if (!TypeConvert::is_object(self)) {
				break;
			}

			void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
			r_cvar = Environment::wrap(isolate)->verify_object(pointer) ? (Object *)pointer : nullptr;
			return true;
		}
		case Variant::BOOL:
			// Same surface as `can_convert_strict<BOOL>` and `JSToGD<bool>`
			// (number / bigint / null / undefined; a string is still rejected),
			// so the reflect path and the direct path agree.
			if (bool val; impl::Helper::to_bool(isolate, p_jval, val)) {
				r_cvar = val;
				return true;
			}
			break;
		case Variant::STRING:
			if (p_jval->IsString()) {
				StringName sn;
				if (Environment::wrap(isolate)->get_string_name_cache().try_get_string_name(isolate, p_jval, sn)) {
					r_cvar = (String)sn;
					return true;
				}
				r_cvar = impl::Helper::to_string(isolate, p_jval);
				return true;
			}
			break;
		case Variant::STRING_NAME:
			// cache the JSValue and StringName pair because the expected type is StringName
			if (p_jval->IsString()) {
				r_cvar = Environment::wrap(isolate)->get_string_name(p_jval.As<v8::String>());
				return true;
			}
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::NODE_PATH:
			if (p_jval->IsString()) {
				StringName sn;
				if (Environment::wrap(isolate)->get_string_name_cache().try_get_string_name(isolate, p_jval, sn)) {
					r_cvar = NodePath((String)sn);
					return true;
				}
				r_cvar = NodePath(impl::Helper::to_string(isolate, p_jval));
				return true;
			}
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_BYTE_ARRAY:
			if (try_convert_array<uint8_t, PackedByteArray>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_INT32_ARRAY:
			if (try_convert_array<int32_t, PackedInt32Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_INT64_ARRAY:
			if (try_convert_array<int64_t, PackedInt64Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_FLOAT32_ARRAY:
			if (try_convert_array<float, PackedFloat32Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_FLOAT64_ARRAY:
			if (try_convert_array<double, PackedFloat64Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_STRING_ARRAY:
			if (try_convert_array<String, PackedStringArray>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_VECTOR2_ARRAY:
			if (try_convert_array<Vector2, PackedVector2Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_VECTOR3_ARRAY:
			if (try_convert_array<Vector3, PackedVector3Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_COLOR_ARRAY:
			if (try_convert_array<Color, PackedColorArray>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::PACKED_VECTOR4_ARRAY:
			if (try_convert_array<Vector4, PackedVector4Array>(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::ARRAY:
			if (try_convert_array_any(isolate, context, p_jval, r_cvar)) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		// math types
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::TRANSFORM2D:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::PLANE:
		case Variant::QUATERNION:
		case Variant::AABB:
		case Variant::BASIS:
		case Variant::TRANSFORM3D:
		case Variant::PROJECTION:

		// misc types
		case Variant::COLOR:
		case Variant::RID:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::DICTIONARY: {
		FALLBACK_TO_VARIANT:
			if (!p_jval->IsObject()) {
				//TODO should auto convert a null/undefined value to a default (variant) counterpart?
				break;
			}
			const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
			if (!is_variant(self)) {
				break;
			}

			/* 进行严格检查，避免退化为无类型提示的 js 到 gd 的类型转换 */
			Variant *pointer = (Variant *)self->GetAlignedPointerFromInternalField(IF_Pointer);
			const Variant::Type type = pointer->get_type();
			if (type == p_type || (type == Variant::NIL && p_type == Variant::OBJECT)) {
				// 类型完全匹配 或为 空的 godot Object（符合 godot 类型语义）
				r_cvar = *pointer;
				return true;
			} else if (Variant::can_convert_strict(pointer->get_type(), p_type)) {
				// 可严格转换时进行转换
				r_cvar = UtilityFunctions::type_convert(*pointer, p_type);
				return true;
			}

			break;
		}
		case Variant::NIL:
			//NOTE (instead of prompting a nil value) the type NIL usually means a Variant parameter accepted by a godot method
			return js_to_gd_var(isolate, context, p_jval, r_cvar);
		default:
			return false;
	}

	JSB_LOG(Warning, "Failed to convert JS variable to Variant. Expected: %s. Found: %s", Variant::get_type_name(p_type), js_debug_typeof(isolate, p_jval));
	return false;
}

bool TypeConvert::gd_var_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const Variant &p_cvar, Variant::Type p_type, GDExtensionClassMethodArgumentMetadata p_meta, v8::Local<v8::Value> &r_jval) {
	switch (p_type) {
		case Variant::FLOAT: {
			r_jval = v8::Number::New(isolate, p_cvar);
			return true;
		}
		case Variant::INT: {
			// A uint64 slot has to leave through the unsigned writer. The stored
			// bits are the same either way, but an ObjectID sets bit 63
			// (`is_ref_counted`), so writing it signed yields a negative BigInt
			// and `instance_from_id()` then receives a different id than
			// `get_instance_id()` returned.
			//
			// Everything else (int64, and the no-meta case: eval results, Variant
			// hand-offs, container elements) goes through the signed writer. Both
			// pick `Number` or `BigInt` by magnitude, so the JS type is not part
			// of the contract -- a caller that needs one narrows it itself.
			if (p_meta == GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_UINT64) {
				r_jval = impl::Helper::new_unsigned_integer(isolate, (uint64_t)(int64_t)p_cvar);
				return true;
			}
			r_jval = impl::Helper::new_integer(isolate, p_cvar);
			return true;
		}
		case Variant::OBJECT: {
			Object *gd_obj = (Object *)p_cvar;
			if (unlikely(!gd_obj)) {
				r_jval = v8::Null(isolate);
				return true;
			}
			if (v8::Local<v8::Object> obj;
					gd_obj_to_js(isolate, context, gd_obj, obj)) {
				r_jval = obj;
				return true;
			}
			return false;
		}
		case Variant::BOOL:
			r_jval = v8::Boolean::New(isolate, p_cvar);
			return true;
		case Variant::STRING: {
			const String str = p_cvar;
			r_jval = impl::Helper::new_string(isolate, str);
			return true;
		}
		case Variant::STRING_NAME: {
			const StringName name = p_cvar;
			r_jval = Environment::wrap(isolate)->get_string_value(name);
			return true;
		}
		// math types
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::TRANSFORM2D:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::PLANE:
		case Variant::QUATERNION:
		case Variant::AABB:
		case Variant::BASIS:
		case Variant::TRANSFORM3D:
		case Variant::PROJECTION:

		// misc types
		case Variant::COLOR:
		case Variant::NODE_PATH:
		case Variant::RID:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::DICTIONARY:
		case Variant::ARRAY:

		// typed arrays
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY: {
			// nil var is considered as acceptable here (in the case of set_script_property_value called from godot scene state restoring)
			jsb_checkf(p_cvar.get_type() == Variant::NIL || Variant::can_convert(p_cvar.get_type(), p_type),
					"variant type can't convert to %s from %s",
					Variant::get_type_name(p_type),
					Variant::get_type_name(p_cvar.get_type()));
			Environment *env = Environment::wrap(isolate);
			NativeClassID class_id;
			if (const NativeClassInfoPtr class_info = env->expose_godot_primitive_class(p_type, &class_id)) {
				jsb_check(class_id && class_info->type == NativeClassType::GodotPrimitive);
				r_jval = class_info->clazz.NewInstance(context);
				const v8::Local<v8::Object> instance = r_jval.As<v8::Object>();
				jsb_check(TypeConvert::is_variant(instance));
				env->bind_valuetype(env->alloc_variant(p_cvar), instance);
				return true;
			}
			return false;
		}
		case Variant::NIL:
			if (const Variant::Type var_type = p_cvar.get_type(); var_type != Variant::NIL) {
				return gd_var_to_js(isolate, context, p_cvar, var_type, r_jval);
			}
			r_jval = v8::Null(isolate);
			return true;
		default: {
			JSB_LOG(Error, "unhandled type %s (enum=%d, var_type=%d)", Variant::get_type_name(p_type), (int)p_type, (int)p_cvar.get_type());
			return false;
		}
	}
}

bool TypeConvert::gd_obj_to_js(v8::Isolate *isolate, const v8::Local<v8::Context> &context, Object *p_godot_obj, v8::Local<v8::Object> &r_jval) {
	jsb_check(p_godot_obj);
	Environment *environment = Environment::wrap(isolate);
	if (environment->try_get_object(p_godot_obj, r_jval)) {
		return true;
	}

	// freshly bind existing gd object (not constructed in javascript)

	if (GodotJSScriptInstanceBase *si = ScriptInstance::get_script_instance<GodotJSScriptInstanceBase>(p_godot_obj)) {
		// If the script_instance is NOT a shadow instance, then we're trying to access a GodotJS scripted object
		// from another thread than the one that owns it. That's not permitted.
		jsb_check(si->is_shadow());

		Ref<GodotJSScript> script = si->get_script();
		ScriptInstance *non_shadow_instance = script->instance_construct(p_godot_obj, false);
		jsb_check(!non_shadow_instance->is_shadow() && !non_shadow_instance->is_placeholder());

		if (non_shadow_instance && environment->try_get_object(p_godot_obj, r_jval)) {
			return true;
		}
	}

	const StringName class_name = p_godot_obj->get_class();
	if (NativeClassID class_id;
			NativeClassInfoPtr class_info = environment->expose_godot_object_class(class_name, &class_id)) {
		// class_info ptr will be invalid after escape()
		// to avoid possible side effects during `NewInstance`
		r_jval = class_info.escape()->clazz.NewInstance(context);
		jsb_check(TypeConvert::is_object(r_jval));

		// the lifecycle will be managed by javascript runtime, DO NOT DELETE it externally
		NativeObjectID obj_id = environment->bind_godot_object(class_id, p_godot_obj, r_jval.As<v8::Object>());
		jsb_check(obj_id);
		return true;
	}
	JSB_LOG(Error, "failed to expose godot class '%s'", class_name);
	return false;
}

bool TypeConvert::js_to_gd_var(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Variant &r_cvar) {
	if (p_jval.IsEmpty() || p_jval->IsNullOrUndefined()) {
		r_cvar = {};
		return true;
	}
	if (p_jval->IsBoolean()) {
		r_cvar = p_jval.As<v8::Boolean>()->Value();
		return true;
	}
	if (p_jval->IsInt32()) {
		r_cvar = p_jval.As<v8::Int32>()->Value();
		return true;
	}
	if (p_jval->IsUint32()) {
		r_cvar = p_jval.As<v8::Uint32>()->Value();
		return true;
	}
	if (p_jval->IsNumber()) {
		r_cvar = p_jval.As<v8::Number>()->Value();
		return true;
	}
	if (p_jval->IsString()) {
		// directly return from cached StringName only if it exists
		StringName sn;
		if (Environment::wrap(isolate)->get_string_name_cache().try_get_string_name(isolate, p_jval, sn)) {
			r_cvar = sn;
			return true;
		}
		r_cvar = impl::Helper::to_string(isolate, p_jval);
		return true;
	}
	// is it proper to convert a ArrayBuffer into Vector<uint8_t>?
	if (p_jval->IsArrayBuffer()) {
		r_cvar = impl::Helper::to_packed_byte_array(isolate, p_jval.As<v8::ArrayBuffer>());
		return true;
	}
	//TODO
	// if (p_jval->IsFunction())
	// {
	// }

	if (p_jval->IsObject()) {
		const v8::Local<v8::Object> self = p_jval.As<v8::Object>();

#if JSB_WITH_V8
		if (p_jval->IsProxy())
#else
		if (p_jval->IsObject())
#endif
		{
			v8::MaybeLocal<v8::Value> maybe_target = self->Get(context, Environment::wrap(isolate)->get_symbol(Symbols::ProxyTarget));
			v8::Local<v8::Value> target;

			if (maybe_target.ToLocal(&target) && !target->IsUndefined()) {
				return js_to_gd_var(isolate, context, target, r_cvar);
			}
		}

		switch (self->InternalFieldCount()) {
			case IF_VariantFieldCount: {
#if JSB_WITH_NODE
				/** HACK: Node 的 Promise 会有一个内嵌字段。*/
				/** TODO: 用更优雅的方式处理 */
				if (self->IsPromise()) {
					JSB_LOG(Error, "js_to_gd_var: unhandled type: Promise");
					return false;
				}
#endif // JSB_WITH_NODE
				r_cvar = *(Variant *)self->GetAlignedPointerFromInternalField(IF_Pointer);
				return true;
			}
			case IF_ObjectFieldCount: {
				if ((NativeClassType::Type)(uintptr_t)self->GetAlignedPointerFromInternalField(IF_ClassType) != NativeClassType::GodotObject) {
					return false;
				}
				void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
				r_cvar = Environment::wrap(isolate)->verify_object(pointer) ? (Object *)pointer : nullptr;
				return true;
			}
			default:
				return false;
		}
	}
#if JSB_WITH_BIGINT
	if (p_jval->IsBigInt()) {
		r_cvar = p_jval.As<v8::BigInt>()->Int64Value();
		return true;
	}
#endif
	JSB_LOG(Error, "js_to_gd_var: unhandled type");
	return false;
}

bool TypeConvert::can_convert_strict(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_val, Variant::Type p_type) {
	switch (p_type) {
		case Variant::BOOL: {
			// Mirrors the engine's own BOOL surface: `Variant::can_convert_strict`
			// lists INT / FLOAT / NIL (STRING is commented out there), and
			// `JSToGD<bool>` / `to_bool` are the marshallers on the other side.
			return p_val->IsBoolean() || p_val->IsNumber()
#if JSB_WITH_BIGINT
					|| p_val->IsBigInt()
#endif
					|| p_val->IsNullOrUndefined();
		}
		case Variant::FLOAT: // return p_val->IsNumber();
		case Variant::INT: {
			//TODO find a better way to check integer type?
			return p_val->IsNumber()
#if JSB_WITH_BIGINT
					|| p_val->IsBigInt()
#endif
					;
		}
		case Variant::STRING:
		case Variant::STRING_NAME: {
			return p_val->IsString();
		}
		case Variant::NIL:
			// "Variant" slot: accepts ANY JS value (mirrors js_to_gd_var's
			// NIL handling -- a bare JS null/undefined/primitive/object all
			// fit a godot Variant parameter).
			return true;
		case Variant::NODE_PATH:
			if (p_val->IsString()) {
				return true;
			}
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
		case Variant::OBJECT: {
			if (!p_val->IsObject()) return false;
			const v8::Local<v8::Object> self = p_val.As<v8::Object>();
			if (!TypeConvert::is_object(self)) return false;

			//NOTE dead object is now treated as null, so we don't need to check it here anymore
			return true;
		}

		case Variant::ARRAY:
		// typed arrays
		case Variant::PACKED_BYTE_ARRAY:
			if (p_val->IsArrayBuffer()) {
				return true;
			}
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
#if JSB_IMPLICIT_PACKED_ARRAY_CONVERSION
			//TODO is loose conversion check for JS primitive array as Godot array a bad idea?
			if (p_val->IsArray()) return true;
			goto FALLBACK_TO_VARIANT; // NOLINT(cppcoreguidelines-avoid-goto, hicpp-avoid-goto)
#endif
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::TRANSFORM2D:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::PLANE:
		case Variant::QUATERNION:
		case Variant::AABB:
		case Variant::BASIS:
		case Variant::TRANSFORM3D:
		case Variant::PROJECTION:

		// misc types
		case Variant::COLOR:
		case Variant::RID:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::DICTIONARY: {
		FALLBACK_TO_VARIANT:
			if (!p_val->IsObject()) {
				//NOTE a null/undefined value is not considered convertible!
				return false;
			}
			const v8::Local<v8::Object> self = p_val.As<v8::Object>();
			if (!TypeConvert::is_variant(self)) {
				return false;
			}
			const Variant *target = (const Variant *)self->GetAlignedPointerFromInternalField(IF_Pointer);
			if (!target) {
				return Variant::can_convert_strict(Variant::NIL, p_type);
			}
			return Variant::can_convert_strict(target->get_type(), p_type);
		}
		default:
			return false;
	}
}

bool TypeConvert::js_to_gd_obj(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::Local<v8::Value> &p_jval, Object *&r_godot_obj) {
	if (!p_jval->IsObject()) {
		return false;
	}

#if JSB_WITH_V8
	if (p_jval->IsProxy())
#else
	if (p_jval->IsObject())
#endif
	{
		v8::Local<v8::Object> object = p_jval.As<v8::Object>();
		v8::MaybeLocal<v8::Value> maybe_target = object->Get(context, Environment::wrap(isolate)->get_symbol(Symbols::ProxyTarget));
		v8::Local<v8::Value> target;

		if (maybe_target.ToLocal(&target) && !target->IsUndefined()) {
			return js_to_gd_obj(isolate, context, target, r_godot_obj);
		}
	}

	// return false if strict type check fails
	const v8::Local<v8::Object> self = p_jval.As<v8::Object>();
	if (!TypeConvert::is_object(self)
			|| (NativeClassType::Type)(uintptr_t)self->GetAlignedPointerFromInternalField(IF_ClassType) != NativeClassType::GodotObject) {
		return false;
	}

	// dead objects return true with a nullptr
	void *pointer = self->GetAlignedPointerFromInternalField(IF_Pointer);
	r_godot_obj = Environment::wrap(isolate)->verify_object(pointer) ? (Object *)pointer : nullptr;
	return true;
}

} //namespace jsb
