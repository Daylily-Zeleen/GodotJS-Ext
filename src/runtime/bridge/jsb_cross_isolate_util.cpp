#include "jsb_cross_isolate_util.h"

#include "jsb_string_names.h"

namespace jsb::cross_isolate {
namespace {

/** 采集上限 */
struct Limits {
	int max_depth = 8;
	int max_nodes = 4096;
	int max_string_length = 64 * 1024;
	int max_untransferred = 64;
};

struct WalkState {
	const Limits &limits;
	int nodes = 0;
	bool budget_exhausted = false;
	bool truncated = false;

	explicit WalkState(const Limits &p_limits) : limits(p_limits) {}
};

/** 与 realm 无关的错误记录：纯数据（String / PackedStringArray / 只含基础类型的 Dictionary / 递归 record）。 */
struct ErrorRecord : public MessageRawData {
	bool is_primitive = false;
	Variant primitive;
	bool primitive_is_undefined = false;

	String name = "Error";
	String message;
	String stack;
	/** 只用来拼 `message` 前缀（`"worker: <cause>"`），不暴露成属性。 */
	String source_realm;

	Dictionary extra;
	PackedStringArray untransferred;

	bool has_cause = false;
	bool cause_is_error = false;
	Variant cause_value;
	std::shared_ptr<ErrorRecord> cause_record;
};

/** 基础类型 -> JS 值。不走 `TypeConvert`：它会触达惰性原生化类暴露，在嵌套 isolate 作用域里会崩。 */
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

/** JS 值 -> 基础类型；非基础类型返回 false（undefined 与 null 都记成 NIL）。 */
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

void note(WalkState &r_state, PackedStringArray &r_untransferred, const String &p_path) {
	if (r_untransferred.size() < r_state.limits.max_untransferred) {
		r_untransferred.push_back(p_path);
	} else if (!r_state.truncated) {
		r_state.truncated = true;
		r_untransferred.push_back("...");
	}
}

/** 读自有属性：只读描述符，accessor 一律不读（读它会执行用户代码）。 */
bool read_own_data_property(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_object, const v8::Local<v8::Name> &p_key, v8::Local<v8::Value> *r_value) {
	v8::Local<v8::Value> descriptor_val;
	if (!p_object->GetOwnPropertyDescriptor(p_context, p_key).ToLocal(&descriptor_val) || !descriptor_val->IsObject()) {
		return false;
	}
	const v8::Local<v8::Object> descriptor = descriptor_val.As<v8::Object>();
	for (const char *accessor : { "get", "set" }) {
		v8::Local<v8::Value> accessor_val;
		if (descriptor->Get(p_context, impl::Helper::new_string(p_isolate, accessor)).ToLocal(&accessor_val) && accessor_val->IsFunction()) {
			return false;
		}
	}
	return descriptor->Get(p_context, impl::Helper::new_string(p_isolate, "value")).ToLocal(r_value);
}

/** 降级成基础类型；带不走时记进清单并返回 false。 */
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
	// `stack` 先按自有 data 属性读；v8 里它是实例上的 accessor（栈惰性格式化），所以补一次受 `TryCatch`
	// 保护的真读——这是采集里唯一可能执行 getter 的地方，拿不到就留空。
	r_record.stack = read_text("stack");
	if (r_record.stack.is_empty()) {
		const impl::TryCatch try_catch(p_isolate);
		v8::Local<v8::Value> stack_value;
		if (p_error->Get(p_context, impl::Helper::new_string(p_isolate, "stack").As<v8::Name>()).ToLocal(&stack_value)
				&& stack_value->IsString()) {
			r_record.stack = impl::Helper::to_string(p_isolate, stack_value.As<v8::String>());
		}
	}

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
			continue;
		}
		v8::Local<v8::Value> property;
		if (!read_own_data_property(p_isolate, p_context, p_error, name_val.As<v8::Name>(), &property)) {
			note(r_state, r_record.untransferred, key);
			continue;
		}
		if (key == "untransferred") {
			// 我们自己挂在 `CrossEnvError` 上的清单（元素都是字符串）：并进记录，让错误再跨一层隔离区时不丢。
			if (property->IsArray()) {
				const v8::Local<v8::Array> lost = property.As<v8::Array>();
				for (uint32_t i = 0, len = lost->Length(); i < len; ++i) {
					v8::Local<v8::Value> item;
					if (lost->Get(p_context, i).ToLocal(&item) && item->IsString()) {
						r_record.untransferred.push_back(impl::Helper::to_string(p_isolate, item.As<v8::String>()));
					}
				}
			}
			continue;
		}
		if (key == "cause") {
			if (property->IsObject()) {
				// 只递归 Error 形态的 cause；其它对象要用户自己转。
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

std::unique_ptr<ErrorRecord> capture(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_error) {
	std::unique_ptr<ErrorRecord> record = std::make_unique<ErrorRecord>();
	if (Environment *env = Environment::wrap(p_isolate)) {
		record->source_realm = env->is_worker() ? String("worker") : (env->is_shadow_realm() ? String("shadowRealm") : String());
	}
	if (p_error.IsEmpty()) {
		return record;
	}
	WalkState state{ Limits() };
	if (!p_error->IsObject()) {
		record->is_primitive = true;
		if (p_error->IsUndefined()) {
			record->primitive_is_undefined = true;
		} else if (!carry_value(p_isolate, p_context, p_error, &record->primitive, "", state, record->untransferred)) {
			// 基础类型之外（symbol / BigInt / ...）：退回文本
			record->is_primitive = false;
			record->message = impl::Helper::to_string(p_isolate, p_error);
		}
		return record;
	}
	capture_error_fields(p_isolate, p_context, p_error.As<v8::Object>(), *record, state, 0);
	if (record->message.is_empty() && record->stack.is_empty()) {
		record->message = impl::Helper::to_string(p_isolate, p_error);
	}
	return record;
}

/** 本 realm 里的 `Error`（`CallAsConstructor`：各腿 shim 都有）。 */
v8::Local<v8::Value> make_error_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message) {
	v8::Local<v8::Value> ctor;
	if (!p_context->Global()->Get(p_context, impl::Helper::new_string(p_isolate, "Error")).ToLocal(&ctor) || !ctor->IsFunction()) {
		return {};
	}
	v8::Local<v8::Value> argv[] = { impl::Helper::new_string(p_isolate, p_message) };
	v8::Local<v8::Value> error;
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
	v8::Local<v8::Value> error = make_error_js(p_isolate, p_context, p_record.message);
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
	// 未携带清单挂在外层 wrapper 上（`make_cross_env_error`），不在这里。
	return error;
}

struct CrossEnvErrorImpl {
	constexpr static const char *kClassName = "CrossEnvError";

	_FORCE_INLINE_ static ErrorRecord *get_record(const v8::Local<v8::Object> &p_self) {
		if (p_self->InternalFieldCount() != IF_ObjectFieldCount) {
			return nullptr;
		}
		return (ErrorRecord *)p_self->GetAlignedPointerFromInternalField(IF_Pointer);
	}

	/** 原始值异常的文案（`message` 前缀后面那一段）。 */
	_FORCE_INLINE_ static String primitive_text(const ErrorRecord &p_record) {
		if (p_record.primitive_is_undefined) {
			return String("undefined");
		}
		return p_record.primitive.operator String();
	}

	/** `cause`：首访才 `rebuild`，然后写成自有属性遮住这个访问器。 */
	static void _get_cause(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::Local<v8::Context> context = isolate->GetCurrentContext();
		const v8::Local<v8::Object> self = info.This();
		const ErrorRecord *record = get_record(self);
		if (record == nullptr) {
			info.GetReturnValue().SetUndefined();
			return;
		}
		v8::Local<v8::Value> cause = rebuild(isolate, context, *record);
		if (cause.IsEmpty()) {
			cause = v8::Undefined(isolate);
		}
		// 必须用 `DefineOwnProperty`：这个访问器只有 getter，`Set` 会被 JS `[[Set]]` 静默拒绝。
		self->DefineOwnProperty(context, impl::Helper::new_string_ascii(isolate, "cause"), cause).Check();
		info.GetReturnValue().Set(cause);
	}

	static void _finalizer(Environment * /*p_env*/, void *p_pointer, FinalizationType /*p_finalize*/) {
		delete (ErrorRecord *)p_pointer; // 与 `std::make_unique` 配对（不用 `memnew`/`memdelete`）
	}

	/** `new CrossEnvError(msg)`：只落 `name` / `message`。 */
	static void _constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::Local<v8::Context> context = isolate->GetCurrentContext();
		const v8::Local<v8::Object> self = info.This();
		self->Set(context, impl::Helper::new_string_ascii(isolate, "name"), impl::Helper::new_string_ascii(isolate, kClassName)).Check();
		if (info.Length() >= 1 && !info[0]->IsUndefined()) {
			self->Set(context, impl::Helper::new_string_ascii(isolate, "message"), info[0]->ToString(context).ToLocalChecked()).Check();
		}
	}
};

/** 物化 `CrossEnvError`：记录挂到实例内部字段（GC 时由 finalizer 释放）。 */
v8::Local<v8::Value> make_cross_env_error(Environment *p_env, std::unique_ptr<ErrorRecord> p_record) {
	NativeClassID class_id;
	const NativeClassInfoPtr class_info = p_env->find_native_class(jsb_string_name(CrossEnvError), &class_id);
	jsb_check(class_info);
	if (!class_info) {
		return {};
	}
	v8::Isolate *isolate = p_env->get_isolate();
	const v8::Local<v8::Context> context = p_env->get_context();
	const v8::Local<v8::Object> object = class_info->clazz.NewInstance(context);

	ErrorRecord *record = p_record.release();
	p_env->bind_js_owned_pointer(class_id, NativeClassType::Custom, record, object);

	const auto set = [&](const char *p_key, const v8::Local<v8::Value> &p_value) {
		object->Set(context, impl::Helper::new_string_ascii(isolate, p_key), p_value).Check();
	};
	set("name", impl::Helper::new_string_ascii(isolate, CrossEnvErrorImpl::kClassName));
	const String cause_text = record->is_primitive ? CrossEnvErrorImpl::primitive_text(*record) : record->message;
	const String message = record->source_realm.is_empty() ? cause_text : record->source_realm + String(": ") + cause_text;
	set("message", impl::Helper::new_string(isolate, message));
	if (!record->stack.is_empty()) {
		set("stack", impl::Helper::new_string(isolate, record->stack));
	}
	if (!record->untransferred.is_empty()) {
		const v8::Local<v8::Array> lost = v8::Array::New(isolate, (size_t)record->untransferred.size());
		for (int i = 0; i < record->untransferred.size(); ++i) {
			lost->Set(context, (uint32_t)i, impl::Helper::new_string(isolate, record->untransferred[i])).Check();
		}
		set("untransferred", lost);
	}
	return object;
}

} //namespace

void register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_global) {
	Environment *env = Environment::wrap(p_context);
	jsb_check(env != nullptr);
	v8::Isolate *isolate = env->get_isolate();
	const StringName &class_name = jsb_string_name(CrossEnvError);

	const NativeClassID class_id = env->add_native_class(NativeClassType::Custom, class_name);
	impl::ClassBuilder builder = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, class_name, &CrossEnvErrorImpl::_constructor, *class_id);
	builder.Instance().Property("cause", &CrossEnvErrorImpl::_get_cause, nullptr, (void *)nullptr);

	const NativeClassInfoPtr class_info = env->get_native_class(class_id);
	class_info->finalizer = &CrossEnvErrorImpl::_finalizer;
	class_info->clazz = builder.Build();
	jsb_check(!class_info->clazz.IsEmpty());

	const v8::Local<v8::Function> ctor = class_info->clazz.Get(isolate);
	jsb_check(!ctor.IsEmpty());
	// `extends Error`：把原型接到本 realm 的 `Error.prototype` 上（等价于 JS `class X extends Error {}`）。
	v8::Local<v8::Value> error_ctor;
	v8::Local<v8::Value> error_proto;
	v8::Local<v8::Value> own_proto;
	const v8::Local<v8::String> kPrototype = impl::Helper::new_string_ascii(isolate, "prototype");
	if (p_context->Global()->Get(p_context, impl::Helper::new_string_ascii(isolate, "Error")).ToLocal(&error_ctor)
			&& error_ctor->IsObject()
			&& error_ctor.As<v8::Object>()->Get(p_context, kPrototype).ToLocal(&error_proto)
			&& error_proto->IsObject()
			&& ctor->Get(p_context, kPrototype).ToLocal(&own_proto)
			&& own_proto->IsObject()) {
		own_proto.As<v8::Object>()->SetPrototype(p_context, error_proto).Check();
	}
	// 只作**用户**用途（`instanceof CrossEnvError` / 自行构造）；C++ 侧只用类表里那份。
	p_context->Global()->Set(p_context, env->get_string_value(class_name), ctor).Check();
}

std::unique_ptr<MessageRawData> capture_error(Environment *p_env, const v8::Local<v8::Value> &p_exception) {
	return capture(p_env->get_isolate(), p_env->get_context(), p_exception);
}

v8::Local<v8::Value> take_error(Environment *p_env, Message &p_message, const String &p_fallback_message) {
	std::unique_ptr<MessageRawData> raw = p_message.take_rawdata();
	std::unique_ptr<ErrorRecord> record;
	if (raw) {
		record.reset(static_cast<ErrorRecord *>(raw.release()));
	} else {
		record = std::make_unique<ErrorRecord>();
	}
	if (record->name.is_empty() && record->message.is_empty() && !record->is_primitive) {
		record->message = p_fallback_message;
	}
	const v8::Local<v8::Value> error = make_cross_env_error(p_env, std::move(record));
	if (error.IsEmpty()) {
		return make_error(p_env->get_isolate(), p_env->get_context(), p_fallback_message);
	}
	return error;
}

v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message) {
	return make_error_js(p_isolate, p_context, p_message);
}

void throw_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message, const v8::Local<v8::Value> &p_error) {
	if (p_error.IsEmpty()) {
		jsb_throw(p_isolate, p_message);
		return;
	}
	impl::Helper::throw_value(p_isolate, p_context, p_error);
}

void throw_cross_isolate_error(v8::Isolate *p_target_isolate, const v8::Local<v8::Context> &p_target_context, const v8::Local<v8::Value> &p_exception, const String &p_fallback_message) {
	// 源侧采集必须热着做（异常 `Local` 随帧消失，跨 isolate 存 `Global` 是 UB）。
	v8::Isolate *source_isolate = v8::Isolate::TryGetCurrent();
	jsb_checkf(source_isolate != nullptr, "throw_cross_isolate_error requires an active source isolate scope.");
	const v8::Local<v8::Context> source_context = source_isolate->GetCurrentContext();
	std::unique_ptr<ErrorRecord> record = capture(source_isolate, source_context, p_exception);

	JSB_ISOLATE_SCOPE(p_target_isolate);
	const v8::HandleScope handle_scope(p_target_isolate);
	const v8::Context::Scope context_scope(p_target_context);
	v8::Local<v8::Value> error;
	if (Environment *env = Environment::wrap(p_target_isolate)) {
		if (record->name.is_empty() && record->message.is_empty() && !record->is_primitive) {
			record->message = p_fallback_message;
		}
		error = make_cross_env_error(env, std::move(record));
	}
	if (error.IsEmpty()) {
		error = make_error(p_target_isolate, p_target_context, p_fallback_message);
	}
	throw_error(p_target_isolate, p_target_context, p_fallback_message, error);
}

} //namespace jsb::cross_isolate
