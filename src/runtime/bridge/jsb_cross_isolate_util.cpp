#include "jsb_cross_isolate_util.h"

#include "jsb_error_record.h"

namespace jsb::cross_isolate {
namespace internal {
/**
 * 按 JS 语义在 `p_context` 所属 realm 里抛出一个值。
 *
 * NOTE 这里**不用**各腿 shim 的 `Isolate::ThrowException(value)`：jsc/quickjs 的实现会把值"寄存"到
 *      `TryCatch` 用的内部槽（`set_stack_steal(StackPos::Exception, ...)`），之后任何 `has_caught()`
 *      都会读到脏状态 —— quickjs 直接 `jsb_checkf` 断言 "stack.exception is dirty"，并触发错误打印风暴。
 *      按 JS 语义抛出（让该 realm 自己 `throw`）只留下正常的 pending exception，槽位保持干净。
 */
_FORCE_INLINE_ static void throw_value_in_context(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_value) {
	const v8::Local<v8::String> source = impl::Helper::new_string(p_isolate, "(function (e) { throw e; })");
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(p_context, source).ToLocal(&script)) {
		return;
	}
	v8::Local<v8::Value> func_value;
	if (!script->Run(p_context).ToLocal(&func_value) || !func_value->IsFunction()) {
		return;
	}
	v8::Local<v8::Value> argv[] = { p_value };
	func_value.As<v8::Function>()->Call(p_context, v8::Undefined(p_isolate), 1, argv);
}
} //namespace internal

std::pair<uint8_t *, size_t> serialize_exception(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_exception) {
	return jsb::error_record::serialize(p_isolate, p_context, jsb::error_record::capture(p_isolate, p_context, p_exception));
}

v8::Local<v8::Value> rebuild_error_from_bytes(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const uint8_t *p_data, size_t p_size) {
	if (p_data == nullptr || p_size == 0) {
		return {};
	}
	const jsb::error_record::ErrorRecord record = jsb::error_record::deserialize(p_isolate, p_context, p_data, p_size);
	if (!record.name.is_empty() || !record.message.is_empty() || record.is_primitive) {
		return jsb::error_record::rebuild(p_isolate, p_context, record);
	}
	return {}; // 记录为空/不合理（无效 payload）
}

v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message) {
	return jsb::error_record::make_error(p_isolate, p_context, p_message);
}

const char *untransferred_symbol_key() {
	return jsb::error_record::kSymbolKey;
}

void throw_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message, const v8::Local<v8::Value> &p_error) {
	if (p_error.IsEmpty()) {
		jsb_throw(p_isolate, p_message);
		return;
	}
	internal::throw_value_in_context(p_isolate, p_context, p_error);
}

void throw_cross_isolate_error(v8::Isolate *p_target_isolate, const v8::Local<v8::Context> &p_target_context, const v8::Local<v8::Value> &p_exception, const String &p_fallback_message) {
	// 源侧：在**当前**（源）作用域内采集。各腿 shim 只有 `TryGetCurrent`，没有 `GetCurrent`。
	v8::Isolate *source_isolate = v8::Isolate::TryGetCurrent();
	jsb_checkf(source_isolate != nullptr, "throw_cross_isolate_error requires an active source isolate scope.");
	const v8::Local<v8::Context> source_context = source_isolate->GetCurrentContext();
	const std::pair<uint8_t *, size_t> data = serialize_exception(source_isolate, source_context, p_exception);

	// 目标侧：重建 + 抛出（缓冲是源 isolate 分配器的产物，用完即还）
	JSB_ISOLATE_SCOPE(p_target_isolate);
	const v8::HandleScope handle_scope(p_target_isolate);
	const v8::Context::Scope context_scope(p_target_context);
	v8::Local<v8::Value> error;
	if (data.first != nullptr) {
		error = rebuild_error_from_bytes(p_target_isolate, p_target_context, data.first, data.second);
		impl::Helper::free(data.first); // 与 `ValueSerializer::Release` 的分配器配对
	}
	if (error.IsEmpty()) {
		error = make_error(p_target_isolate, p_target_context, p_fallback_message);
	}
	throw_error(p_target_isolate, p_target_context, p_fallback_message, error);
}

} //namespace jsb::cross_isolate
