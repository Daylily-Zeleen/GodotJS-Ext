#pragma once

#include "jsb_environment.h"
#include "jsb_message.h"

namespace jsb::cross_isolate {
namespace internal {
_FORCE_INLINE_ static void insert_transfer_variant(
		Environment *from_env,
		jsb::internal::ReferentialVariantMap<TransferData> &transfers,
		const Variant &variant) {
	if (transfers.getptr(variant)) {
		return;
	}

	TransferData transfer_data;
	from_env->prepare_transfer_out(NativeObjectID::none(), transfers.size(), variant, transfer_data);
	transfers.insert(variant, transfer_data);
}
} //namespace internal

/** 注册 `CrossEnvError` 类（`Environment::init()` 调用）。 */
void register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_global);

/** 采集 `p_exception` 成消息附加数据（`TYPE_ERROR`）。调用方须处在**源** env 作用域内、异常还热着。 */
std::unique_ptr<MessageRawData> capture_error(Environment *p_env, const v8::Local<v8::Value> &p_exception);

/** 取出 `p_message` 里的错误物化成 `CrossEnvError`；没有记录时用 `p_fallback_message` 造一个（`TYPE_ERROR`）。 */
v8::Local<v8::Value> take_error(Environment *p_env, Message &p_message, const String &p_fallback_message);

/** 本 realm 自己的失败：一个普通 `Error`。 */
v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message);

/** 抛出一个值（`p_error` 为空则按 `p_message` 抛）。 */
void throw_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message, const v8::Local<v8::Value> &p_error);

/** 把当前源作用域里的异常搬到目标 realm 抛出。 */
void throw_cross_isolate_error(v8::Isolate *p_target_isolate, const v8::Local<v8::Context> &p_target_context, const v8::Local<v8::Value> &p_exception, const String &p_fallback_message);

// shared master -> worker/shadowRealm postMessage transfer-list parsing.
// Worker and TransferableShadowRealm both send Godot objects over postMessage,
// so the side-channel variant/object transfer list must be parsed identically.
inline bool parse_transfer_list(
		v8::Isolate *isolate,
		const v8::Local<v8::Context> &context,
		Environment *from_env,
		const v8::FunctionCallbackInfo<v8::Value> &info,
		jsb::internal::ReferentialVariantMap<TransferData> &transfers) {
	std::vector<Variant> explicit_node_transfers;

	if (info.Length() <= 1 || info[1]->IsUndefined()) {
		return true; // no transfer list, not an error
	}

	v8::Local<v8::Value> transfer_arg = info[1];

	if (!transfer_arg->IsArray() && !transfer_arg->IsObject()) {
		jsb_throw(isolate, "transfer list must be an array");
		return false;
	}

	if (transfer_arg->IsArray()) {
		v8::Local<v8::Array> transfer_array = transfer_arg.As<v8::Array>();

		for (uint32_t i = 0, len = transfer_array->Length(); i < len; i++) {
			v8::HandleScope transfer_item_scope(isolate);
			v8::Local<v8::Value> item = transfer_array->Get(context, i).ToLocalChecked();

			if (!item->IsObject()) {
				// JS primitive, no underlying Variant exists to transfer. Since JS primitives are automatically
				// coerced to variants, it's more consistent if we permit (but ignore) them.
				continue;
			}

			Variant variant;

			if (!TypeConvert::js_to_gd_var(isolate, context, item.As<v8::Object>(), variant)) {
				jsb_throw(isolate, "transfer list must contain Godot object/variant types only");
				return false;
			}

			internal::insert_transfer_variant(from_env, transfers, variant);
			explicit_node_transfers.push_back(variant);
		}
	} else {
		Variant transfer_var;

		if (!TypeConvert::js_to_gd_var(isolate, context, transfer_arg.As<v8::Object>(), Variant::Type::ARRAY, transfer_var)) {
			jsb_throw(isolate, "transfer list must be an array");
			return false;
		}

		if (transfer_var.get_type() != Variant::ARRAY) {
			jsb_throw(isolate, "transfer list must be an array");
			return false;
		}

		Array transfer_arr = transfer_var;

		for (int i = 0, size = transfer_arr.size(); i < size; i++) {
			Variant &variant = transfer_arr[i];
			internal::insert_transfer_variant(from_env, transfers, variant);
			explicit_node_transfers.push_back(variant);
		}
	}

	// 只搬用户显式列出的东西：内嵌的 Godot 对象（数组/字典/子节点/自定义属性里）不替用户递归收集。
	return true;
}
} //namespace jsb::cross_isolate
