/************************************************************************/
/*  jsb_class_info.cpp                                                  */
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

#include "jsb_class_info.h"
#include "jsb_object_bindings.h"
#include "jsb_shared_statics.h"
#include "jsb_type_convert.h"
#include <godot_cpp/classes/resource_loader.hpp>

//TODO it breaks the isolation of 'bridge'
#include "../weaver/jsb_script.h"
namespace jsb {
#if JSB_TOOLS
void _parse_script_doc(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const v8::MaybeLocal<v8::Value> holder, ScriptBaseDoc &r_doc) {
	if (v8::Local<v8::Value> tv; holder.IsEmpty() || !holder.ToLocal(&tv) || !tv->IsObject()) {
		// invalid
	} else {
		const v8::Local<v8::Object> obj = tv.As<v8::Object>();
		Environment *environment = Environment::wrap(isolate);

		// (@deprecated)
		if (v8::Local<v8::Value> val; obj->Get(context, jsb_name(environment, deprecated)).ToLocal(&val) && val->IsString()) {
			r_doc.is_deprecated = true;
			r_doc.deprecated_message = impl::Helper::to_string(isolate, val);
		} else {
			r_doc.is_deprecated = false;
		}
		// (@experimental)
		if (v8::Local<v8::Value> val; obj->Get(context, jsb_name(environment, experimental)).ToLocal(&val) && val->IsString()) {
			r_doc.is_experimental = true;
			r_doc.experimental_message = impl::Helper::to_string(isolate, val);
		} else {
			r_doc.is_experimental = false;
		}
		// (@help)
		if (v8::Local<v8::Value> val; obj->Get(context, jsb_name(environment, help)).ToLocal(&val) && val->IsString()) {
			r_doc.brief_description = impl::Helper::to_string(isolate, val);
		} else {
			r_doc.brief_description.resize(0);
		}
	}
}
#endif

namespace {
enum class EnumParseResult {
	None,
	Numeric,
	Stringy,
};

// `Array::make_read_only()` / `Dictionary::make_read_only()` are shallow (they only flag the shared
// `_p`), so the inner containers have to be visited explicitly to match the depth of a GDScript
// `const` container fetched through a script object (see research/phase0-findings.md §0.3 / §0.6).
void _apply_read_only_recursive(Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::ARRAY: {
			Array array = p_value;
			array.make_read_only();
			const int64_t size = array.size();
			for (int64_t index = 0; index < size; ++index) {
				// the copy shares `_p` with the stored element, so flagging it freezes the stored one too
				Variant element = array[index];
				_apply_read_only_recursive(element);
			}
		} break;
		case Variant::DICTIONARY: {
			Dictionary dictionary = p_value;
			dictionary.make_read_only();
			const Array keys = dictionary.keys();
			const int64_t size = keys.size();
			for (int64_t index = 0; index < size; ++index) {
				Variant element = dictionary.get(keys[index], Variant());
				_apply_read_only_recursive(element);
			}
		} break;
		default:
			break;
	}
}

// R2.3 whitelist: only these 8 Variant types may become a constant. Godot value types (`Vector2`,
// `Color`, ...) and the `Packed*Array` / `Object` / `Callable` / `Signal` wrappers are all rejected
// here -- note they convert *successfully* via the `IF_VariantFieldCount` branch of `js_to_gd_var`,
// so a conversion failure alone would not filter them.
bool _is_constant_value_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::ARRAY:
		case Variant::DICTIONARY:
			return true;
		default:
			return false;
	}
}

// Recognize a TypeScript enum object (design.md §4.3).
// A numeric enum carries the `E[E["A"] = 0] = "A"` reverse mapping and is normalized into
// `Dictionary{name: int}`; a string enum (no reverse mapping) is exposed as a plain
// `Dictionary{name: String}`. Anything else is rejected so it can fall back to `js_to_gd_var`.
EnumParseResult _try_parse_enum(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_obj, Variant &r_value) {
	// a Godot value wrapper is a Proxy; a TS enum object is a plain object, so this also keeps
	// containers (`GArray`/`GDictionary`) out of the enum path.
	//NOTE `IsProxy()` only exists on V8 (same guard as `TypeConvert::js_to_gd_var`).
#if JSB_WITH_V8
	if (p_obj->IsProxy()) {
		return EnumParseResult::None;
	}
#endif // JSB_WITH_V8
	v8::MaybeLocal<v8::Array> maybe_names = p_obj->GetOwnPropertyNames(p_context, v8::PropertyFilter::SKIP_SYMBOLS, v8::KeyConversionMode::kConvertToString);
	if (maybe_names.IsEmpty()) {
		return EnumParseResult::None;
	}
	const v8::Local<v8::Array> names = maybe_names.ToLocalChecked();
	const uint32_t len = names->Length();
	if (len == 0) {
		return EnumParseResult::None;
	}

	const v8::Local<v8::String> key_value = impl::Helper::new_string_ascii(p_isolate, "value");
	const v8::Local<v8::String> key_get = impl::Helper::new_string_ascii(p_isolate, "get");
	const v8::Local<v8::String> key_set = impl::Helper::new_string_ascii(p_isolate, "set");

	// Identifier keys are the enum member names; numeric keys carry the reverse mapping of a numeric
	// enum (`E[E["A"] = 0] = "A"` yields both `E["A"] === 0` and `E[0] === "A"`), so the value type
	// alone does not classify the object -- the key shape has to be taken into account.
	Vector<String> member_names;
	Vector<Variant> member_values;
	HashMap<int64_t, String> reverse_names;
	bool has_numeric_key = false;

	for (uint32_t index = 0; index < len; ++index) {
		v8::Local<v8::Value> name_val;
		if (!names->Get(p_context, index).ToLocal(&name_val) || !name_val->IsString()) {
			return EnumParseResult::None;
		}
		v8::Local<v8::Value> descriptor_val;
		if (!p_obj->GetOwnPropertyDescriptor(p_context, name_val.As<v8::Name>()).ToLocal(&descriptor_val) || !descriptor_val->IsObject()) {
			return EnumParseResult::None;
		}
		const v8::Local<v8::Object> descriptor = descriptor_val.As<v8::Object>();
		// enum members are plain data properties; an accessor means this is not an enum object
		if (descriptor->HasOwnProperty(p_context, key_get).ToChecked() || descriptor->HasOwnProperty(p_context, key_set).ToChecked()) {
			return EnumParseResult::None;
		}
		v8::Local<v8::Value> value;
		if (!descriptor->Get(p_context, key_value).ToLocal(&value)) {
			return EnumParseResult::None;
		}

		const String entry_name = impl::Helper::to_string(p_isolate, name_val);
		if (entry_name.is_valid_int()) {
			// a numeric key must hold the member name it maps back to
			if (!value->IsString()) {
				return EnumParseResult::None;
			}
			has_numeric_key = true;
			reverse_names.insert(entry_name.to_int(), impl::Helper::to_string(p_isolate, value));
		} else if (value->IsNumber()) {
			member_names.push_back(entry_name);
			member_values.push_back((int64_t)value.As<v8::Number>()->Value());
		} else if (value->IsString()) {
			member_names.push_back(entry_name);
			member_values.push_back(impl::Helper::to_string(p_isolate, value));
		} else {
			return EnumParseResult::None;
		}
	}

	if (member_names.is_empty()) {
		return EnumParseResult::None;
	}

	// numeric enum: every member value is an int and the reverse mapping must agree with it
	//NOTE the reverse-mapping agreement check also rejects an enum with *duplicate* values
	//     (`enum E { A = 0, B = 0 }`): TypeScript emits a single reverse entry (`{0: "B"}`), so
	//     `A` has no agreeing reverse name and the whole object is rejected. A float-valued enum
	//     (`enum E { A = 1.5 }`) is rejected the same way - its keys are not valid ints, so no
	//     reverse mapping is recorded and the enum falls through to the string branch, which
	//     requires string values. Both cases degrade to "ignored with a warning" rather than a
	//     wrong constant, which is the intended failure mode for an unrepresentable enum.
	bool is_numeric_enum = true;
	for (int index = 0; index < member_names.size(); ++index) {
		if (member_values[index].get_type() != Variant::INT) {
			is_numeric_enum = false;
			break;
		}
	}
	if (is_numeric_enum) {
		Dictionary dictionary;
		for (int index = 0; index < member_names.size(); ++index) {
			if (!member_names[index].is_valid_identifier()) {
				return EnumParseResult::None;
			}
			const int64_t number = member_values[index];
			const String *reverse = reverse_names.getptr(number);
			if (reverse == nullptr || *reverse != member_names[index]) {
				return EnumParseResult::None;
			}
			dictionary[StringName(member_names[index])] = number;
		}
		r_value = dictionary;
		return EnumParseResult::Numeric;
	}

	// string enum: every member value is a string and there is no reverse mapping at all
	if (!has_numeric_key) {
		bool is_string_enum = true;
		for (int index = 0; index < member_names.size(); ++index) {
			if (member_values[index].get_type() != Variant::STRING) {
				is_string_enum = false;
				break;
			}
		}
		if (is_string_enum) {
			Dictionary dictionary;
			for (int index = 0; index < member_names.size(); ++index) {
				if (!member_names[index].is_valid_identifier()) {
					return EnumParseResult::None;
				}
				dictionary[StringName(member_names[index])] = member_values[index];
			}
			r_value = dictionary;
			return EnumParseResult::Stringy;
		}
	}

	return EnumParseResult::None;
}

// The accessor pair installed for a `@bind.exposed.shared()` static member. Its `info.Data()` is a
// two-element array `[module_id, member_name]`; carrying both in the closure keeps the accessor
// independent of `info.This()` (a derived class would otherwise resolve to the wrong module).
//NOTE the module id travels as a JS string, not a cached StringNameID: the cache may evict an entry
//     at any time, which would leave the closure pointing at a freed id.
v8::Local<v8::Value> _shared_static_key(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const StringName &p_module_id, const StringName &p_name) {
	Environment *environment = Environment::wrap(p_isolate);
	const v8::Local<v8::Array> key = v8::Array::New(p_isolate, 2);
	key->Set(p_context, 0, environment->get_string_value(p_module_id)).Check();
	key->Set(p_context, 1, environment->get_string_value(p_name)).Check();
	return key;
}

bool _shared_static_key_parts(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_data, StringName &r_module_id, StringName &r_name) {
	if (!p_data->IsArray()) {
		return false;
	}
	const v8::Local<v8::Array> key = p_data.As<v8::Array>();
	v8::Local<v8::Value> module_id_val;
	v8::Local<v8::Value> name_val;
	if (!key->Get(p_context, 0).ToLocal(&module_id_val) || !module_id_val->IsString()
			|| !key->Get(p_context, 1).ToLocal(&name_val) || !name_val->IsString()) {
		return false;
	}
	Environment *environment = Environment::wrap(p_isolate);
	r_module_id = environment->get_string_name(module_id_val.As<v8::String>());
	r_name = environment->get_string_name(name_val.As<v8::String>());
	return true;
}

void _shared_static_getter(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	StringName module_id;
	StringName name;
	if (!_shared_static_key_parts(isolate, context, info.Data(), module_id, name)) {
		info.GetReturnValue().SetUndefined();
		return;
	}

	Variant value;
	if (!SharedStatics::get(module_id, name, value)) {
		info.GetReturnValue().SetUndefined();
		return;
	}

	v8::Local<v8::Value> js_value;
	if (TypeConvert::gd_var_to_js(isolate, context, value, js_value)) {
		info.GetReturnValue().Set(js_value);
	}
}

void _shared_static_setter(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	StringName module_id;
	StringName name;
	if (!_shared_static_key_parts(isolate, context, info.Data(), module_id, name)) {
		return;
	}

	//NOTE an unconvertible value is dropped instead of coerced: `undefined`/`symbol`/functions have
	//     no Variant counterpart, and a plain JS object has none either - storing the default NIL
	//     that `js_to_gd_var` leaves behind would silently corrupt the shared value.
	if (info.Length() < 1 || info[0]->IsUndefined() || info[0]->IsSymbol() || info[0]->IsFunction()) {
		return;
	}
	Variant value;
	bool converted = TypeConvert::js_to_gd_var(isolate, context, info[0], value);
	if (!converted && info[0]->IsArray()) {
		// the hinted overload is the only path that accepts a JS native array
		value = Variant();
		converted = TypeConvert::js_to_gd_var(isolate, context, info[0], Variant::ARRAY, value);
	}
	if (!converted) {
		JSB_LOG(Warning, "(script-parser) write to shared static %s is ignored (unconvertible value)", name);
		return;
	}
	SharedStatics::set(module_id, name, value);
}

// Replace the plain data property with an accessor pair backed by the process-wide store, so that
// every JS environment and the GDScript side read and write one value.
void _install_shared_static_accessor(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_class_obj, const v8::Local<v8::Name> &p_name, const StringName &p_module_id, const StringName &p_static_name) {
	const v8::Local<v8::Value> key = _shared_static_key(p_isolate, p_context, p_module_id, p_static_name);
	const v8::Local<v8::Function> getter = JSB_NEW_FUNCTION(p_context, _shared_static_getter, key);
	const v8::Local<v8::Function> setter = JSB_NEW_FUNCTION(p_context, _shared_static_setter, key);
	p_class_obj->SetAccessorProperty(p_name, getter, setter);
}
} //namespace

namespace internal {
//NOTE ensure the address of p_class_info being locked during this procedure
bool _parse_script_class_iterate(const v8::Local<v8::Context> &p_context, const ScriptClassInfoPtr &p_class_info, const v8::Local<v8::Object> &class_obj) {
	v8::Isolate *isolate = p_context->GetIsolate();
	Environment *environment = Environment::wrap(isolate);

	//TODO collect methods/signals/properties
	v8::Local<v8::Value> prototype_val;
	if (!class_obj->Get(p_context, jsb_name(environment, prototype)).ToLocal(&prototype_val) || !prototype_val->IsObject()) {
		JSB_LOG(Warning, "(script-parser) class prototype is missing or invalid");
		return false;
	}
	const v8::Local<v8::Object> prototype = prototype_val.As<v8::Object>();

	jsb_check(prototype->IsObject());
	// reset CDO of the legacy JS class
	// p_class_info->js_default_object.Reset();

	// update the latest script class info
	p_class_info->native_class_name = environment->get_native_class(p_class_info->native_class_id)->name;
	jsb_check(internal::VariantUtil::is_valid_name(p_class_info->native_class_name));
	p_class_info->js_class.Reset(isolate, class_obj);
	{
		v8::Local<v8::Value> class_name_val;
		if (class_obj->Get(p_context, jsb_name(environment, name)).ToLocal(&class_name_val) && class_name_val->IsString()) {
			p_class_info->js_class_name = environment->get_string_name(class_name_val.As<v8::String>());
		} else {
			p_class_info->js_class_name = StringName();
		}
	}
	p_class_info->methods.clear();
	p_class_info->signals.clear();
	p_class_info->properties.clear();
	p_class_info->constants.clear();
	p_class_info->static_variables.clear();
	p_class_info->rpc_config.clear();
	p_class_info->method_cache.clear();
	p_class_info->flags = ScriptClassFlags::None;

	JSB_LOG(VeryVerbose, "godot js class name %s (native: %s)", p_class_info->js_class_name, p_class_info->native_class_name);

#if JSB_TOOLS
	// class doc
	v8::Local<v8::Map> doc_map;
	if (v8::Local<v8::Value> val; prototype->HasOwnProperty(p_context, jsb_symbol(environment, MemberDocMap)).ToChecked() && prototype->Get(p_context, jsb_symbol(environment, MemberDocMap)).ToLocal(&val) && val->IsMap()) {
		doc_map = val.As<v8::Map>();
	}
	_parse_script_doc(isolate, p_context, prototype->Get(p_context, jsb_symbol(environment, Doc)), p_class_info->doc);
#endif

	// class rpc config
	v8::Local<v8::Map> rpc_config_map;
	if (v8::Local<v8::Value> val; prototype->Get(p_context, jsb_symbol(environment, ClassRPCConfig)).ToLocal(&val) && val->IsMap()) {
		rpc_config_map = val.As<v8::Map>();
	}

	// methods
	{
		// const v8::Local<v8::Array> property_names = prototype->GetPropertyNames(p_context, v8::KeyCollectionMode::kOwnOnly, v8::PropertyFilter::ALL_PROPERTIES, v8::IndexFilter::kSkipIndices, v8::KeyConversionMode::kNoNumbers).ToLocalChecked();
		constexpr v8::PropertyFilter property_filter = v8::PropertyFilter::SKIP_SYMBOLS;
		v8::MaybeLocal<v8::Array> maybe_property_names = prototype->GetOwnPropertyNames(p_context, property_filter, v8::KeyConversionMode::kNoNumbers);
		if (maybe_property_names.IsEmpty()) {
			JSB_LOG(Warning, "(script-parser) failed to enumerate class properties");
			return false;
		}
		const v8::Local<v8::Array> property_names = maybe_property_names.ToLocalChecked();

		const uint32_t len = property_names->Length();
		for (uint32_t index = 0; index < len; ++index) {
			v8::HandleScope loop_scope(isolate);
			v8::Local<v8::Value> prop_name_val;
			if (!property_names->Get(p_context, index).ToLocal(&prop_name_val) || !prop_name_val->IsString()) {
				continue;
			}
			const v8::Local<v8::Name> prop_name = prop_name_val.As<v8::Name>();
			const String name_s = impl::Helper::to_string(isolate, prop_name);
			if (name_s.is_empty() || name_s == "constructor") continue;

			// check property type with 'GetOwnPropertyDescriptor' instead of direct 'Get' to avoid triggering code execution
			v8::Local<v8::Value> prop_descriptor;
			if (prototype->GetOwnPropertyDescriptor(p_context, prop_name).ToLocal(&prop_descriptor) && prop_descriptor->IsObject()) {
				v8::Local<v8::Value> prop_val;
				if (prop_descriptor.As<v8::Object>()->Get(p_context, jsb_name(environment, value)).ToLocal(&prop_val) && prop_val->IsFunction()) {
					//TODO property categories
					ScriptMethodInfo method_info{};
#if JSB_TOOLS
					if (v8::Local<v8::Value> val; !doc_map.IsEmpty() && doc_map->Get(p_context, prop_name).ToLocal(&val) && val->IsObject()) {
						_parse_script_doc(isolate, p_context, val, method_info.doc);
					}
#endif // JSB_TOOLS
					p_class_info->methods.insert((StringName)name_s, method_info);

					// check rpc config
					if (v8::Local<v8::Value> rpc_val;
							!rpc_config_map.IsEmpty() && rpc_config_map->Get(p_context, prop_name).ToLocal(&rpc_val) && rpc_val->IsObject()) {
						v8::Local<v8::Object> rpc_obj = rpc_val.As<v8::Object>();
						Dictionary rpc_config;

						// for `rpc_config[name]`, a StringName has an identical hash of it's underlying String
						// and returns true for equality check. So, it's safe to use jsb_string_name here
						if (v8::Local<v8::Value> config_val; rpc_obj->Get(p_context, jsb_name(environment, rpc_mode)).ToLocal(&config_val)) {
							rpc_config[jsb_string_name(rpc_mode)] = config_val.As<v8::Int32>()->Value();
						}
						if (v8::Local<v8::Value> config_val; rpc_obj->Get(p_context, jsb_name(environment, call_local)).ToLocal(&config_val)) {
							rpc_config[jsb_string_name(call_local)] = config_val.As<v8::Boolean>()->Value();
						}
						if (v8::Local<v8::Value> config_val; rpc_obj->Get(p_context, jsb_name(environment, transfer_mode)).ToLocal(&config_val)) {
							rpc_config[jsb_string_name(transfer_mode)] = config_val.As<v8::Int32>()->Value();
						}
						if (v8::Local<v8::Value> config_val; rpc_obj->Get(p_context, jsb_name(environment, channel)).ToLocal(&config_val)) {
							rpc_config[jsb_string_name(channel)] = config_val.As<v8::Int32>()->Value();
						}
						jsb_check(!p_class_info->rpc_config.has(name_s));
						p_class_info->rpc_config[name_s] = rpc_config;
					}
					JSB_LOG(VeryVerbose, "... method %s", name_s);
				}
			}
		}
	}

	// tool (@tool_)
	{
		const bool is_tool = class_obj->HasOwnProperty(p_context, jsb_symbol(environment, ClassToolScript)).FromMaybe(false);
		if (is_tool) {
			p_class_info->flags.set_flag(ScriptClassFlags::Tool);
		}
	}

	// icon (@icon)
	{
		if (v8::Local<v8::Value> val; class_obj->Get(p_context, jsb_symbol(environment, ClassIcon)).ToLocal(&val)) {
			p_class_info->icon = impl::Helper::to_string(isolate, val);
		}
	}

	// signals (@signal_)
	{
		v8::Local<v8::Value> val_test;
		if (prototype->HasOwnProperty(p_context, jsb_symbol(environment, ClassSignals)).ToChecked() && prototype->Get(p_context, jsb_symbol(environment, ClassSignals)).ToLocal(&val_test) && val_test->IsArray()) {
			v8::Local<v8::Array> collection = val_test.As<v8::Array>();
			const uint32_t len = collection->Length();
			for (uint32_t index = 0; index < len; ++index) {
				v8::HandleScope loop_scope(isolate);
				v8::Local<v8::Value> signal_name_js;
				if (!collection->Get(p_context, index).ToLocal(&signal_name_js) || !signal_name_js->IsString()) {
					continue;
				}
				const StringName signal_name = environment->get_string_name_cache().get_string_name(isolate, signal_name_js.As<v8::String>());
				p_class_info->signals.insert(signal_name, {});

				// instantiate a fake Signal property
				//NOTE: we use JS string representation of signal name for info.Data() to avoid persistent StringNameID requirement.
				//      therefore any cached string name could be cleaned up on demand (e.g. on memory low) without additional lifetime control.
				v8::Local<v8::Function> signal_func = JSB_NEW_FUNCTION(p_context, ObjectReflectBindingUtil::_godot_object_signal_get, signal_name_js);
				prototype->SetAccessorProperty(signal_name_js.As<v8::Name>(), signal_func);
				JSB_LOG(VeryVerbose, "... signal %s", signal_name);
			}
		}
	}

	// properties (@export_)
	// detect all exported properties (which annotated with @export_)
	{
		v8::Local<v8::Value> val_test;
		if (prototype->HasOwnProperty(p_context, jsb_symbol(environment, ClassProperties)).ToChecked()
				&& prototype->Get(p_context, jsb_symbol(environment, ClassProperties)).ToLocal(&val_test)
				&& val_test->IsArray()) {
			const v8::Local<v8::Array> collection = val_test.As<v8::Array>();
			const uint32_t len = collection->Length();
			for (uint32_t index = 0; index < len; ++index) {
				v8::HandleScope loop_scope(isolate);
				v8::Local<v8::Value> element;
				if (!collection->Get(p_context, index).ToLocal(&element)) {
					continue;
				}
				const v8::Local<v8::Context> &context = p_context;
				if (!element->IsObject()) {
					continue;
				}
				v8::Local<v8::Object> obj = element.As<v8::Object>();
				ScriptPropertyInfo property_info;
				PropertyInfo &property_details = property_info.details;
				v8::Local<v8::Value> prop_name;
				if (!obj->Get(context, jsb_name(environment, name)).ToLocal(&prop_name)) {
					continue;
				}
				property_details.name = impl::Helper::to_string(isolate, prop_name); // string
				v8::Local<v8::Value> type_val;
				if (!obj->Get(context, jsb_name(environment, type)).ToLocal(&type_val)) {
					continue;
				}
				int32_t type_int = 0;
				if (!type_val->Int32Value(context).To(&type_int)) {
					continue;
				}
				property_details.type = (Variant::Type)type_int; // int
				property_details.hint = BridgeHelper::to_enum<PropertyHint>(context, obj->Get(context, jsb_name(environment, hint)), PROPERTY_HINT_NONE);
				v8::Local<v8::Value> hint_string_val;
				if (obj->Get(context, jsb_name(environment, hint_string)).ToLocal(&hint_string_val)) {
					property_details.hint_string = impl::Helper::to_string(isolate, hint_string_val);
				}
				property_details.usage = BridgeHelper::to_enum<PropertyUsageFlags>(context, obj->Get(context, jsb_name(environment, usage)), PROPERTY_USAGE_DEFAULT) | PROPERTY_USAGE_SCRIPT_VARIABLE;

				v8::Local<v8::Value> cache;

				if (obj->Get(context, jsb_name(environment, cache)).ToLocal(&cache)) {
					property_info.cache = cache->BooleanValue(isolate);
				}

#if JSB_TOOLS
				if (v8::Local<v8::Value> val; !doc_map.IsEmpty() && doc_map->Get(p_context, prop_name).ToLocal(&val) && val->IsObject()) {
					_parse_script_doc(isolate, p_context, val, property_info.doc);
				}
#endif // JSB_TOOLS
				p_class_info->properties.insert(property_details.name, property_info);
				JSB_LOG(VeryVerbose, "... property %s: %s", property_details.name, Variant::get_type_name(property_details.type));
			}
		}
	}

	// constants (@bind.exposed.const()) and shared statics (@bind.exposed.shared())
	//NOTE static members are not inherited, so only the own properties of `class_obj` are visited;
	//     the base chain is walked by the `base` recursion on the GodotJSScript side.
	//NOTE only annotated members are collected (D2 / R4.4): the two symbols below are the sole gate.
	{
		const v8::Local<v8::String> key_value = impl::Helper::new_string_ascii(isolate, "value");
		const v8::Local<v8::String> key_get = impl::Helper::new_string_ascii(isolate, "get");
		const v8::Local<v8::String> key_set = impl::Helper::new_string_ascii(isolate, "set");

		// Collected across the shared-static loop so the store can be pruned of names whose
		// annotation is gone once the whole set is known.
		HashSet<StringName> shared_static_names;

		// Names annotated as constants, collected before the value is inspected so the conflict
		// checked in the shared-static loop is reported even when the constant itself is rejected by
		// the type whitelist - the two annotations contradict each other either way.
		HashSet<StringName> annotated_constant_names;

		// constants
		{
			v8::Local<v8::Value> val_test;
			if (class_obj->HasOwnProperty(p_context, jsb_symbol(environment, ClassConstants)).ToChecked()
					&& class_obj->Get(p_context, jsb_symbol(environment, ClassConstants)).ToLocal(&val_test)
					&& val_test->IsArray()) {
				const v8::Local<v8::Array> collection = val_test.As<v8::Array>();
				const uint32_t len = collection->Length();
				for (uint32_t index = 0; index < len; ++index) {
					v8::HandleScope loop_scope(isolate);
					v8::Local<v8::Value> name_val;
					if (!collection->Get(p_context, index).ToLocal(&name_val) || !name_val->IsString()) {
						continue;
					}
					const String constant_name = impl::Helper::to_string(isolate, name_val);
					if (constant_name.is_empty()) {
						continue;
					}
					const StringName constant_name_sn = environment->get_string_name(name_val.As<v8::String>());
					if (!internal::VariantUtil::is_valid_name(constant_name_sn)) {
						continue;
					}
					// recorded before the value is inspected: the shared-static loop reports a
					// const+shared conflict regardless of whether the constant itself is accepted
					annotated_constant_names.insert(constant_name_sn);

					// read through the descriptor to avoid triggering a getter
					v8::Local<v8::Value> descriptor_val;
					if (!class_obj->GetOwnPropertyDescriptor(p_context, name_val.As<v8::Name>()).ToLocal(&descriptor_val) || !descriptor_val->IsObject()) {
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (not an own property of the class)", constant_name);
						continue;
					}
					const v8::Local<v8::Object> descriptor = descriptor_val.As<v8::Object>();
					if (descriptor->HasOwnProperty(p_context, key_get).ToChecked() || descriptor->HasOwnProperty(p_context, key_set).ToChecked()) {
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (accessor property)", constant_name);
						continue;
					}
					v8::Local<v8::Value> value;
					if (!descriptor->Get(p_context, key_value).ToLocal(&value)) {
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (failed to read the value)", constant_name);
						continue;
					}

					// `typeof` prefilter (R1.2): `undefined` / `symbol` / `function` are dropped before
					// reaching `js_to_gd_var` (which logs an Error for `symbol`, polluting the log)
					if (value->IsUndefined() || value->IsSymbol() || value->IsFunction()) {
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (unsupported value type)", constant_name);
						continue;
					}

					ScriptConstantInfo constant_info;
					constant_info.name = constant_name_sn;

					// a TS enum object is recognized first: it is normalized into a freshly built
					// Dictionary instead of going through `js_to_gd_var` (which cannot convert a plain JS object)
					if (value->IsObject()) {
						switch (_try_parse_enum(isolate, p_context, value.As<v8::Object>(), constant_info.value)) {
							case EnumParseResult::Numeric:
								constant_info.kind = ScriptConstantKind::Enum;
								_apply_read_only_recursive(constant_info.value);
								p_class_info->constants.insert(constant_name_sn, constant_info);
								JSB_LOG(VeryVerbose, "... constant %s: enum", constant_name);
								continue;
							case EnumParseResult::Stringy:
								// a string enum has no reverse mapping, so it is exposed as a plain
								// Dictionary constant (design.md §4.3)
								constant_info.kind = ScriptConstantKind::Container;
								_apply_read_only_recursive(constant_info.value);
								p_class_info->constants.insert(constant_name_sn, constant_info);
								JSB_LOG(VeryVerbose, "... constant %s: string enum", constant_name);
								continue;
							case EnumParseResult::None:
								break;
						}
					}

					Variant converted;
					if (!TypeConvert::js_to_gd_var(isolate, p_context, value, converted)) {
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (unconvertible value)", constant_name);
						continue;
					}
					if (!_is_constant_value_type(converted.get_type())) {
						// R2.3 whitelist: Godot value wrappers (`Vector2`/`Color`/`Packed*`/Object/...) all
						// convert *successfully*, so they must be rejected by their Variant type
						JSB_LOG(Warning, "(script-parser) annotated constant %s is ignored (type %s is not allowed)", constant_name, Variant::get_type_name(converted.get_type()));
						continue;
					}

					constant_info.value = converted;
					constant_info.kind = converted.get_type() == Variant::ARRAY || converted.get_type() == Variant::DICTIONARY
							? ScriptConstantKind::Container
							: ScriptConstantKind::Value;
					if (constant_info.kind == ScriptConstantKind::Container) {
						// the container shares `_p` with the JS side, so a recursive read-only flag
						// freezes both sides at once (R2.3)
						_apply_read_only_recursive(constant_info.value);
					}

					p_class_info->constants.insert(constant_name_sn, constant_info);
					JSB_LOG(VeryVerbose, "... constant %s: %s", constant_name, Variant::get_type_name(converted.get_type()));
				}
			}
		}

		// shared statics
		{
			v8::Local<v8::Value> val_test;
			if (class_obj->HasOwnProperty(p_context, jsb_symbol(environment, ClassSharedStatics)).ToChecked()
					&& class_obj->Get(p_context, jsb_symbol(environment, ClassSharedStatics)).ToLocal(&val_test)
					&& val_test->IsArray()) {
				const v8::Local<v8::Array> collection = val_test.As<v8::Array>();
				const uint32_t len = collection->Length();
				for (uint32_t index = 0; index < len; ++index) {
					v8::HandleScope loop_scope(isolate);
					v8::Local<v8::Value> name_val;
					if (!collection->Get(p_context, index).ToLocal(&name_val) || !name_val->IsString()) {
						continue;
					}
					const String static_name = impl::Helper::to_string(isolate, name_val);
					if (static_name.is_empty()) {
						continue;
					}
					const StringName static_name_sn = environment->get_string_name(name_val.As<v8::String>());
					if (!internal::VariantUtil::is_valid_name(static_name_sn)) {
						continue;
					}

					// A member annotated as *both* a constant and a shared static has no coherent
					// semantics: the constant is a frozen parse-time snapshot read through `_get`,
					// while the shared static is a writable store reached through `_set` and the
					// installed accessor. The constant wins and this annotation is dropped, so
					// `_get` / `_set` / `_get_property_list` all agree on one interpretation.
					if (annotated_constant_names.has(static_name_sn)) {
						JSB_LOG(Warning, "(script-parser) %s is annotated as both a constant and a shared static; the shared static annotation is ignored", static_name);
						continue;
					}
					// Read the declared value and seed the process-wide store with it. On a re-parse
					// the property is already the accessor installed by the previous pass, so the
					// store is consulted instead (reading through the getter would return the stored
					// value anyway, and `ensure` never overwrites an existing entry).
					Variant initial_value;
					bool has_initial_value = false;
					bool is_accessor = false;
					v8::Local<v8::Value> descriptor_val;
					if (class_obj->GetOwnPropertyDescriptor(p_context, name_val.As<v8::Name>()).ToLocal(&descriptor_val) && descriptor_val->IsObject()) {
						const v8::Local<v8::Object> descriptor = descriptor_val.As<v8::Object>();
						is_accessor = descriptor->HasOwnProperty(p_context, key_get).ToChecked() || descriptor->HasOwnProperty(p_context, key_set).ToChecked();
						if (is_accessor) {
							has_initial_value = SharedStatics::get(p_class_info->module_id, static_name_sn, initial_value);
						} else {
							v8::Local<v8::Value> value;
							if (descriptor->Get(p_context, key_value).ToLocal(&value)
									&& !value->IsUndefined() && !value->IsSymbol() && !value->IsFunction()) {
								if (!TypeConvert::js_to_gd_var(isolate, p_context, value, initial_value) && value->IsArray()) {
									// the hinted overload is the only path that accepts a JS native array
									// (`try_convert_array_any`); the plain path rejects it (`InternalFieldCount() == 0`).
									// It builds a fresh Array, which is correct here: the store is authoritative,
									// the JS side never aliases the author's literal.
									TypeConvert::js_to_gd_var(isolate, p_context, value, Variant::ARRAY, initial_value);
								}
								has_initial_value = true;
							}
						}
					}

					// a slot must exist even for a member with no derivable initial value (`static x;`
					// is `undefined`), otherwise it could not be assigned from either side
					//NOTE guarded on a valid module id: `_parse_script_class_iterate` is also driven
					//     directly by the test suite with a bare class info, and keying the store on an
					//     empty name would create an entry nothing can ever prune (retain is guarded too).
					if (internal::VariantUtil::is_valid_name(p_class_info->module_id)) {
						SharedStatics::ensure(p_class_info->module_id, static_name_sn, has_initial_value ? initial_value : Variant());
					}

					ScriptStaticVariableInfo static_info;
					static_info.name = static_name_sn;
					// GDScript parity for the reported usage: a static variable carries
					// `SCRIPT_VARIABLE` only, without `STORAGE`/`EDITOR`. `DataType::to_property_info`
					// starts from `PROPERTY_USAGE_NONE` (`gdscript_parser.cpp:5418`) and the compiler
					// adds `SCRIPT_VARIABLE` (`gdscript_compiler.cpp:2902`). Measured on the engine:
					// GDScript's own `static var sv` reports usage 4096 on the script resource, this
					// one reported 4102 (= `PROPERTY_USAGE_DEFAULT | SCRIPT_VARIABLE`). The value
					// lives in the shared store rather than in the resource, so neither bit is
					// accurate for it.
					static_info.details = PropertyInfo(has_initial_value ? initial_value.get_type() : Variant::NIL, static_name_sn, PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_SCRIPT_VARIABLE);
					p_class_info->static_variables.insert(static_name_sn, static_info);
					shared_static_names.insert(static_name_sn);
					JSB_LOG(VeryVerbose, "... shared static %s: %s", static_name, Variant::get_type_name(static_info.details.type));

					// route every later read/write through the store, so all environments and the
					// GDScript side observe one value (design.md §5.2)
					if (!is_accessor) {
						_install_shared_static_accessor(isolate, p_context, class_obj, name_val.As<v8::Name>(), p_class_info->module_id, static_name_sn);
					}
				}
			}
		}

		// Drop names whose annotation is gone (idempotent, so a plain re-parse keeps every value).
		if (internal::VariantUtil::is_valid_name(p_class_info->module_id)) {
			SharedStatics::retain(p_class_info->module_id, shared_static_names);
		}
	}
	return true;
}
} //namespace internal

void ScriptClassInfo::instantiate(Environment *p_env, const StringName &p_module_id, const v8::Local<v8::Object> &p_self) {
	const String source_path = internal::PathUtil::convert_javascript_path(p_module_id);
	const Ref<GodotJSScript> script = ResourceLoader::get_singleton()->load(source_path, jsb_typename(GodotJSScript));
	if (script.is_valid()) {
		jsb_unused(script->_can_instantiate()); // make it loaded immediately
		const ScriptInstance *script_instance = script->instance_and_native_object_create(p_self, p_env->flags_ & Environment::EnvironmentFlags::EF_Shadow);
		jsb_unused(script_instance);
		jsb_check(script_instance);
	}
}

bool ScriptClassInfo::_parse_script_class(const v8::Local<v8::Context> &p_context, JavaScriptModule &p_module) {
	if (p_module.exports.IsEmpty()) {
		JSB_LOG(VeryVerbose, "(script-parser) no exports %s", p_module.source_info.source_filepath);
		return false;
	}
	// only classes in files of godot package system could be used as godot js script
	if (!p_module.source_info.source_filepath.begins_with("res://")
			|| p_module.source_info.source_filepath.begins_with("res://node_modules")) {
		JSB_LOG(VeryVerbose, "(script-parser) non-res module %s", p_module.source_info.source_filepath);
		return false;
	}
	v8::Isolate *isolate = p_context->GetIsolate();
	const v8::Local<v8::Value> exports = p_module.exports.Get(isolate);
	if (!exports->IsObject()) {
		JSB_LOG(VeryVerbose, "(script-parser) non-standard module %s", p_module.source_info.source_filepath);
		return false;
	}
	Environment *environment = Environment::wrap(isolate);
	v8::Local<v8::Value> default_val;
	if (!exports.As<v8::Object>()->Get(p_context, jsb_name(environment, default)).ToLocal(&default_val)
			|| !default_val->IsObject()) {
#if JSB_WITH_WEB
		JSB_LOG(VeryVerbose, "(script-parser) no default object %s exports(%d)", p_module.source_info.source_filepath, p_module.exports.get_internal_id());
#else
		JSB_LOG(VeryVerbose, "(script-parser) no default object %s", p_module.source_info.source_filepath);
#endif
		return false;
	}

	// the JS class object itself
	const v8::Local<v8::Object> class_obj = default_val.As<v8::Object>();
	v8::Local<v8::Value> class_id_val;
	if (!class_obj->Get(p_context, jsb_symbol(environment, ClassId)).ToLocal(&class_id_val) || !class_id_val->IsUint32()) {
		// ignore a javascript which does not inherit from a native class (directly and indirectly both)
		JSB_LOG(VeryVerbose, "(script-parser) base class is non-godot object class %s", p_module.source_info.source_filepath);
		return false;
	}

	// unsafe
	const NativeClassID native_class_id = (NativeClassID)class_id_val.As<v8::Uint32>()->Value();
	jsb_check(internal::VariantUtil::is_valid_name(environment->get_native_class(native_class_id)->name));

	//TODO maybe we should always add new GodotJS class instead of refreshing the existing one (for simpler reloading flow, such as directly replacing prototype of a existing instance javascript object)
	ScriptClassInfoPtr existed_class_info = environment->find_script_class(p_module.script_class_id);
	if (!existed_class_info) {
		ScriptClassID script_class_id;
		existed_class_info = environment->add_script_class(script_class_id);
		p_module.script_class_id = script_class_id;
		existed_class_info->module_id = p_module.id;
	}

	// trick: save godot class id for convenience of getting it in JS class constructor
	if (!class_obj->Set(p_context, jsb_symbol(environment, ClassModuleId), environment->get_string_value(p_module.id)).FromMaybe(false)) {
		JSB_LOG(Warning, "(script-parser) failed to set class module id for %s", p_module.source_info.source_filepath);
		return false;
	}

	v8::Local<v8::Value> prototype_val;
	if (!class_obj->Get(p_context, jsb_name(environment, prototype)).ToLocal(&prototype_val) || !prototype_val->IsObject()) {
		JSB_LOG(Warning, "(script-parser) class prototype is missing for %s", p_module.source_info.source_filepath);
		return false;
	}
	v8::Local<v8::Value> base_proto_val;
	if (!prototype_val.As<v8::Object>()->Get(p_context, jsb_name(environment, __proto__)).ToLocal(&base_proto_val) || !base_proto_val->IsObject()) {
		JSB_LOG(Warning, "(script-parser) base prototype is missing for %s", p_module.source_info.source_filepath);
		return false;
	}
	v8::Local<v8::Value> dt_base_obj_val;
	if (!base_proto_val.As<v8::Object>()->Get(p_context, jsb_name(environment, constructor)).ToLocal(&dt_base_obj_val) || !dt_base_obj_val->IsObject()) {
		JSB_LOG(Warning, "(script-parser) base constructor is missing for %s", p_module.source_info.source_filepath);
		return false;
	}
	const v8::Local<v8::Object> dt_base_obj = dt_base_obj_val.As<v8::Object>();
	jsb_check(class_obj != dt_base_obj);

	v8::Local<v8::Value> dt_base_tag;
	if (!dt_base_obj->Get(p_context, jsb_symbol(environment, ClassModuleId)).ToLocal(&dt_base_tag)) {
		JSB_LOG(Warning, "(script-parser) failed to read base class module id for %s", p_module.source_info.source_filepath);
		return false;
	}
	existed_class_info->base_script_module_id = dt_base_tag->IsString() ? environment->get_string_name(dt_base_tag.As<v8::String>()) : StringName();
	JSB_LOG(Verbose, "%s script %d inherits script module %s native: %d", p_module.source_info.source_filepath, p_module.script_class_id, existed_class_info->base_script_module_id, *native_class_id);

	jsb_check(existed_class_info->base_script_module_id != p_module.id);
	jsb_check(existed_class_info->module_id == p_module.id);
	existed_class_info->native_class_id = native_class_id;

	return internal::_parse_script_class_iterate(p_context, existed_class_info, class_obj);
}

} //namespace jsb
