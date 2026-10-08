/************************************************************************/
/*  jsb_error_record.cpp                                                */
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

#include "jsb_error_record.h"
#include "jsb_bridge_helper.h"

namespace jsb::error_record {
namespace {
/** payload 键（只在内部使用；跨 isolate 传的就是这个普通对象） */
constexpr const char *kKeyPrimitive = "primitive";
constexpr const char *kKeyName = "name";
constexpr const char *kKeyMessage = "message";
constexpr const char *kKeyStack = "stack";
constexpr const char *kKeyExtra = "extra";
constexpr const char *kKeyUntransferred = "untransferred";
constexpr const char *kKeyCause = "cause";

/** 采集/重建的上界 */
struct Limits {
	int max_depth = 8; // `cause` 链的最大深度
	int max_nodes = 4096;
	int max_string_length = 64 * 1024;
	int max_untransferred = 64;
};

/** JS 基础类型 -> JS 值。
 *  NOTE 不走 `TypeConvert`：后者会触达惰性原生化类暴露，在 evaluate 的嵌套 isolate 作用域里会崩（实测 SIGSEGV）。 */
v8::Local<v8::Value> _primitive_to_js(v8::Isolate *p_isolate, const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return v8::Null(p_isolate);
		case Variant::BOOL:
			return v8::Boolean::New(p_isolate, (bool)p_value);
		case Variant::INT:
			return impl::Helper::new_integer(p_isolate, (int64_t)p_value);
		case Variant::FLOAT:
			return v8::Number::New(p_isolate, (double)p_value);
		case Variant::STRING:
			return impl::Helper::new_string(p_isolate, (String)p_value);
		default:
			return {};
	}
}

/** JS 值 -> JS 基础类型；不是基础类型返回 false。undefined 与 null 都记成 NIL。 */
bool _js_to_primitive(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_value, Variant *r_out) {
	if (p_value.IsEmpty()) {
		return false;
	}
	if (p_value->IsUndefined() || p_value->IsNull()) {
		*r_out = Variant();
		return true;
	}
	if (p_value->IsBoolean()) {
		*r_out = p_value->BooleanValue(p_isolate);
		return true;
	}
	if (p_value->IsInt32()) {
		int32_t int_value = 0;
		if (!p_value->Int32Value(p_context).To(&int_value)) {
			return false;
		}
		*r_out = (int64_t)int_value;
		return true;
	}
	if (p_value->IsNumber()) {
		double number_value = 0;
		if (!p_value->NumberValue(p_context).To(&number_value)) {
			return false;
		}
		*r_out = number_value;
		return true;
	}
	if (p_value->IsString()) {
		*r_out = impl::Helper::to_string(p_isolate, p_value.As<v8::String>());
		return true;
	}
	return false;
}

/** 自定义字段 -> 普通对象（值只写 JS 基础类型） */
v8::Local<v8::Object> _extra_to_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const Dictionary &p_extra) {
	const v8::Local<v8::Object> object = v8::Object::New(p_isolate);
	for (const Variant &key : p_extra.keys()) {
		const v8::Local<v8::Value> value = _primitive_to_js(p_isolate, p_extra[key]);
		if (!value.IsEmpty()) {
			object->Set(p_context, impl::Helper::new_string(p_isolate, (String)key), value).Check();
		}
	}
	return object;
}

/** 普通对象 -> 自定义字段（只收 JS 基础类型的值） */
void _extra_from_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_value, Dictionary *r_extra) {
	if (p_value.IsEmpty() || !p_value->IsObject()) {
		return;
	}
	const v8::Local<v8::Object> object = p_value.As<v8::Object>();
	v8::MaybeLocal<v8::Array> maybe_names = object->GetOwnPropertyNames(p_context, v8::PropertyFilter::SKIP_SYMBOLS, v8::KeyConversionMode::kNoNumbers);
	if (maybe_names.IsEmpty()) {
		return;
	}
	const v8::Local<v8::Array> names = maybe_names.ToLocalChecked();
	const uint32_t count = names->Length();
	for (uint32_t index = 0; index < count; ++index) {
		v8::Local<v8::Value> name;
		v8::Local<v8::Value> item;
		Variant converted;
		if (names->Get(p_context, index).ToLocal(&name) && name->IsString()
				&& object->Get(p_context, name).ToLocal(&item) && _js_to_primitive(p_isolate, p_context, item, &converted)) {
			(*r_extra)[impl::Helper::to_string(p_isolate, name.As<v8::String>())] = converted;
		}
	}
}

/** 未携带清单（内部数据）-> 字符串数组 */
v8::Local<v8::Array> _untransferred_to_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const PackedStringArray &p_paths) {
	const v8::Local<v8::Array> array = v8::Array::New(p_isolate);
	for (int index = 0; index < p_paths.size(); ++index) {
		array->Set(p_context, (uint32_t)index, impl::Helper::new_string(p_isolate, p_paths[index])).Check();
	}
	return array;
}

/** 字符串数组 -> 未携带清单 */
void _untransferred_from_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_value, PackedStringArray *r_paths) {
	if (p_value.IsEmpty() || !p_value->IsArray()) {
		return;
	}
	const v8::Local<v8::Array> array = p_value.As<v8::Array>();
	const uint32_t count = array->Length();
	for (uint32_t index = 0; index < count; ++index) {
		v8::Local<v8::Value> item;
		if (array->Get(p_context, index).ToLocal(&item) && item->IsString()) {
			r_paths->push_back(impl::Helper::to_string(p_isolate, item.As<v8::String>()));
		}
	}
}

struct WalkState {
	const Limits &limits;
	int nodes = 0;
	bool budget_exhausted = false;
	bool truncated = false;

	explicit WalkState(const Limits &p_limits) : limits(p_limits) {}
};

void note(WalkState &r_state, PackedStringArray &r_untransferred, const String &p_path) {
	if (r_untransferred.size() < r_state.limits.max_untransferred) {
		r_untransferred.push_back(p_path);
	} else if (!r_state.truncated) {
		r_state.truncated = true;
		r_untransferred.push_back("...");
	}
}

/** 读自有属性（只读描述符，不触发 getter/setter）；返回 false 表示"没有可用的值" */
bool read_own_data_property(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_object, const v8::Local<v8::Name> &p_key, v8::Local<v8::Value> *r_value) {
	v8::Local<v8::Value> descriptor_val;
	if (!p_object->GetOwnPropertyDescriptor(p_context, p_key).ToLocal(&descriptor_val) || !descriptor_val->IsObject()) {
		return false;
	}
	const v8::Local<v8::Object> descriptor = descriptor_val.As<v8::Object>();
	for (const char *accessor : { "get", "set" }) {
		v8::Local<v8::Value> accessor_val;
		if (descriptor->Get(p_context, impl::Helper::new_string(p_isolate, accessor)).ToLocal(&accessor_val) && accessor_val->IsFunction()) {
			return false; // accessor：不读（读它会执行用户代码）
		}
	}
	return descriptor->Get(p_context, impl::Helper::new_string(p_isolate, "value")).ToLocal(r_value);
}

/** 把一个 JS 值降级成 JS 基础类型；不是基础类型（数组/对象/函数/symbol/BigInt/Godot 类型）时记入清单并返回 false */
bool carry_value(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_value, Variant *r_out, const String &p_path, WalkState &r_state, PackedStringArray &r_untransferred) {
	if (r_state.budget_exhausted) {
		return false;
	}
	if (++r_state.nodes > r_state.limits.max_nodes) {
		r_state.budget_exhausted = true;
		note(r_state, r_untransferred, p_path);
		return false;
	}
	if (p_value->IsString()) {
		const String text = impl::Helper::to_string(p_isolate, p_value.As<v8::String>());
		if (text.length() > r_state.limits.max_string_length) {
			note(r_state, r_untransferred, p_path);
			return false;
		}
		*r_out = text;
		return true;
	}
	if (!_js_to_primitive(p_isolate, p_context, p_value, r_out)) {
		note(r_state, r_untransferred, p_path);
		return false;
	}
	return true;
}

void capture_error_fields(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_error, ErrorRecord &r_record, WalkState &r_state, int p_depth) {
	const auto read_text = [&](const char *p_key) -> String {
		v8::Local<v8::Value> value;
		if (!read_own_data_property(p_isolate, p_context, p_error, impl::Helper::new_string(p_isolate, p_key).As<v8::Name>(), &value) || !value->IsString()) {
			return String();
		}
		return impl::Helper::to_string(p_isolate, value.As<v8::String>());
	};
	const String name = read_text("name");
	if (!name.is_empty()) {
		r_record.name = name;
	}
	r_record.message = read_text("message");
	r_record.stack = read_text("stack");

	v8::MaybeLocal<v8::Array> maybe_names = p_error->GetOwnPropertyNames(p_context, v8::PropertyFilter::SKIP_SYMBOLS, v8::KeyConversionMode::kNoNumbers);
	if (maybe_names.IsEmpty()) {
		return;
	}
	const v8::Local<v8::Array> names = maybe_names.ToLocalChecked();
	const uint32_t count = names->Length();
	for (uint32_t index = 0; index < count; ++index) {
		if (r_state.budget_exhausted) {
			break;
		}
		v8::Local<v8::Value> name_val;
		if (!names->Get(p_context, index).ToLocal(&name_val) || !name_val->IsString()) {
			continue;
		}
		const String key = impl::Helper::to_string(p_isolate, name_val.As<v8::String>());
		if (key == "name" || key == "message" || key == "stack") {
			continue; // 已单独处理
		}
		v8::Local<v8::Value> property;
		if (!read_own_data_property(p_isolate, p_context, p_error, name_val.As<v8::Name>(), &property)) {
			note(r_state, r_record.untransferred, key);
			continue;
		}
		if (key == "cause") {
			if (property->IsObject()) {
				// 只递归收集 Error 形态的 cause（有自有 `stack` 或 `message`；v8 的 stack 可能是 accessor，
				// 所以只看"有没有"，不看描述符类型）；其它对象是用户自己的类型，需要用户显式转换
				const v8::Local<v8::Object> cause_object = property.As<v8::Object>();
				const auto has_own = [&](const char *p_name) {
					return cause_object->HasOwnProperty(p_context, impl::Helper::new_string(p_isolate, p_name).As<v8::Name>()).ToChecked();
				};
				const bool error_like = has_own("stack") || has_own("message");
				if (!error_like || p_depth + 1 >= r_state.limits.max_depth || r_state.budget_exhausted
						|| ++r_state.nodes > r_state.limits.max_nodes) {
					note(r_state, r_record.untransferred, key);
				} else {
					std::shared_ptr<ErrorRecord> cause = std::make_shared<ErrorRecord>();
					WalkState cause_state(r_state.limits);
					cause_state.nodes = r_state.nodes;
					cause_state.truncated = r_state.truncated;
					capture_error_fields(p_isolate, p_context, property.As<v8::Object>(), *cause, cause_state, p_depth + 1);
					r_state.nodes = cause_state.nodes;
					r_state.truncated = cause_state.truncated;
					r_state.budget_exhausted = cause_state.budget_exhausted;
					r_record.has_cause = true;
					r_record.cause_is_error = true;
					r_record.cause_record = cause;
				}
			} else if (carry_value(p_isolate, p_context, property, &r_record.cause_value, key, r_state, r_record.untransferred)) {
				r_record.has_cause = true;
				r_record.cause_is_error = false;
			}
			continue;
		}
		Variant carried;
		if (carry_value(p_isolate, p_context, property, &carried, key, r_state, r_record.untransferred)) {
			r_record.extra[key] = carried;
		}
	}
}

} // namespace

/** 记录 -> payload 对象（`cause` 递归走同一套规则） */
v8::Local<v8::Object> record_to_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record) {
	const v8::Local<v8::Object> payload = v8::Object::New(p_isolate);
	const auto set = [&](const char *p_key, const v8::Local<v8::Value> &p_value) {
		payload->Set(p_context, impl::Helper::new_string(p_isolate, p_key), p_value).Check();
	};
	set(kKeyName, impl::Helper::new_string(p_isolate, p_record.name));
	set(kKeyMessage, impl::Helper::new_string(p_isolate, p_record.message));
	set(kKeyStack, impl::Helper::new_string(p_isolate, p_record.stack));
	if (p_record.is_primitive) {
		v8::Local<v8::Value> primitive;
		if (p_record.primitive_is_undefined) {
			primitive = v8::Undefined(p_isolate);
		} else {
			primitive = _primitive_to_js(p_isolate, p_record.primitive);
		}
		set(kKeyPrimitive, primitive);
	} else {
		set(kKeyExtra, _extra_to_js(p_isolate, p_context, p_record.extra));
		set(kKeyUntransferred, _untransferred_to_js(p_isolate, p_context, p_record.untransferred));
		if (p_record.has_cause) {
			if (p_record.cause_is_error && p_record.cause_record) {
				set(kKeyCause, record_to_js(p_isolate, p_context, *p_record.cause_record));
			} else {
				set(kKeyCause, _primitive_to_js(p_isolate, p_record.cause_value));
			}
		}
	}
	return payload;
}

/** payload 对象 -> 记录（`cause` 递归走同一套规则） */
bool record_from_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_payload, ErrorRecord *r_record) {
	// NOTE 只有**存在**的键才算数：`Get()` 对不存在的键也会成功返回 undefined，
	//      照它判断会把每个 payload 都当成"原始值 undefined"。
	const auto get = [&](const char *p_key, v8::Local<v8::Value> *r_value) {
		const v8::Local<v8::String> key = impl::Helper::new_string(p_isolate, p_key);
		return p_payload->HasOwnProperty(p_context, key.As<v8::Name>()).ToChecked() && p_payload->Get(p_context, key).ToLocal(r_value);
	};
	v8::Local<v8::Value> value;
	if (get(kKeyName, &value) && value->IsString()) {
		r_record->name = impl::Helper::to_string(p_isolate, value.As<v8::String>());
	}
	if (get(kKeyMessage, &value) && value->IsString()) {
		r_record->message = impl::Helper::to_string(p_isolate, value.As<v8::String>());
	}
	if (get(kKeyStack, &value) && value->IsString()) {
		r_record->stack = impl::Helper::to_string(p_isolate, value.As<v8::String>());
	}
	if (get(kKeyPrimitive, &value)) {
		r_record->is_primitive = true;
		if (value->IsUndefined()) {
			r_record->primitive_is_undefined = true;
		} else {
			_js_to_primitive(p_isolate, p_context, value, &r_record->primitive);
		}
		return true;
	}
	if (get(kKeyExtra, &value)) {
		_extra_from_js(p_isolate, p_context, value, &r_record->extra);
	}
	if (get(kKeyUntransferred, &value)) {
		_untransferred_from_js(p_isolate, p_context, value, &r_record->untransferred);
	}
	if (get(kKeyCause, &value)) {
		if (value->IsObject()) {
			ErrorRecord cause;
			if (record_from_js(p_isolate, p_context, value.As<v8::Object>(), &cause)) {
				r_record->has_cause = true;
				r_record->cause_is_error = true;
				r_record->cause_record = std::make_shared<ErrorRecord>(cause);
			}
		} else if (_js_to_primitive(p_isolate, p_context, value, &r_record->cause_value)) {
			r_record->has_cause = true;
			r_record->cause_is_error = false;
		}
	}
	return true;
}

ErrorRecord capture(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_error) {
	ErrorRecord record;
	if (p_error.IsEmpty()) {
		return record;
	}
	WalkState state{ Limits() };
	if (!p_error->IsObject()) {
		// 原始值异常原样送达（`throw "boom"` / `throw 42` / `throw null` / `throw undefined`）
		record.is_primitive = true;
		if (p_error->IsUndefined()) {
			record.primitive_is_undefined = true;
		} else if (!carry_value(p_isolate, p_context, p_error, &record.primitive, "", state, record.untransferred)) {
			// 基础类型之外（symbol / BigInt / ...）：退回文本
			record.is_primitive = false;
			record.message = impl::Helper::to_string(p_isolate, p_error);
		}
		return record;
	}
	capture_error_fields(p_isolate, p_context, p_error.As<v8::Object>(), record, state, 0);
	if (record.message.is_empty() && record.stack.is_empty()) {
		record.message = impl::Helper::to_string(p_isolate, p_error);
	}
	return record;
}

std::pair<uint8_t *, size_t> serialize(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record) {
	const v8::Local<v8::Object> payload = record_to_js(p_isolate, p_context, p_record);

	v8::ValueSerializer serializer(p_isolate);
	serializer.WriteHeader();
	impl::TryCatch try_catch(p_isolate);
	if (serializer.WriteValue(p_context, payload).IsNothing()) {
		JSB_LOG(Warning, "failed to serialize the error record: %s", BridgeHelper::get_exception(try_catch));
		return { nullptr, 0 };
	}
	return serializer.Release();
}

ErrorRecord deserialize(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const uint8_t *p_data, size_t p_size) {
	ErrorRecord record;
	if (p_data == nullptr || p_size == 0) {
		return record;
	}
	v8::ValueDeserializer deserializer(p_isolate, p_data, p_size);
	bool header_ok = false;
	if (!deserializer.ReadHeader(p_context).To(&header_ok) || !header_ok) {
		return record;
	}
	v8::Local<v8::Value> payload;
	if (!deserializer.ReadValue(p_context).ToLocal(&payload) || !payload->IsObject()) {
		return record;
	}
	record_from_js(p_isolate, p_context, payload.As<v8::Object>(), &record);
	return record;
}

v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message) {
	v8::Local<v8::Value> ctor;
	if (!p_context->Global()->Get(p_context, impl::Helper::new_string(p_isolate, "Error")).ToLocal(&ctor) || !ctor->IsFunction()) {
		return {};
	}
	v8::Local<v8::Value> argv[] = { impl::Helper::new_string(p_isolate, p_message) };
	v8::Local<v8::Value> error;
	// NOTE 用 `CallAsConstructor`（各腿 shim 都有；`Function::NewInstance` 只在真 v8 上有）
	if (!ctor.As<v8::Function>()->CallAsConstructor(p_context, 1, argv).ToLocal(&error) || !error->IsObject()) {
		return {};
	}
	return error;
}

v8::Local<v8::Value> rebuild(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record) {
	if (p_record.is_primitive) {
		if (p_record.primitive_is_undefined) {
			return v8::Undefined(p_isolate);
		}
		const v8::Local<v8::Value> primitive = _primitive_to_js(p_isolate, p_record.primitive);
		if (!primitive.IsEmpty()) {
			return primitive;
		}
	}
	v8::Local<v8::Value> error = make_error(p_isolate, p_context, p_record.message);
	if (error.IsEmpty()) {
		return {};
	}
	const v8::Local<v8::Object> object = error.As<v8::Object>();
	const auto set_js = [&](const v8::Local<v8::Name> &p_key, const v8::Local<v8::Value> &p_value) {
		object->Set(p_context, p_key, p_value).Check();
	};
	if (!p_record.name.is_empty() && p_record.name != "Error") {
		set_js(impl::Helper::new_string(p_isolate, "name"), impl::Helper::new_string(p_isolate, p_record.name));
	}
	if (!p_record.stack.is_empty()) {
		set_js(impl::Helper::new_string(p_isolate, "stack"), impl::Helper::new_string(p_isolate, p_record.stack));
	}
	for (const Variant &key_var : p_record.extra.keys()) {
		const v8::Local<v8::Value> value = _primitive_to_js(p_isolate, p_record.extra[key_var]);
		if (!value.IsEmpty()) {
			set_js(impl::Helper::new_string(p_isolate, (String)key_var), value);
		}
	}
	if (p_record.has_cause) {
		v8::Local<v8::Value> cause;
		if (p_record.cause_is_error && p_record.cause_record) {
			cause = rebuild(p_isolate, p_context, *p_record.cause_record);
		} else {
			cause = _primitive_to_js(p_isolate, p_record.cause_value);
		}
		if (!cause.IsEmpty()) {
			set_js(impl::Helper::new_string(p_isolate, "cause"), cause);
		}
	}
	if (!p_record.untransferred.is_empty()) {
		// 未携带清单：挂在 symbol 键上（注册表 symbol，目标 realm 本地就能算出同一个）
		const v8::Local<v8::Symbol> symbol = v8::Symbol::For(p_isolate, impl::Helper::new_string(p_isolate, kSymbolKey));
		set_js(symbol, _untransferred_to_js(p_isolate, p_context, p_record.untransferred));
	}
	return error;
}
} // namespace jsb::error_record
