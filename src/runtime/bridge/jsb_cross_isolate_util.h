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

/**
 * 把一个 isolate 里的异常搬成与 isolate 无关的字节（记录 → 序列化）。跨 isolate 只能搬纯数据，
 * 异常的 `Local` 离开源 isolate 即失效，所以中间必须过一层记录缓冲。
 * NOTE 调用方必须已处于**源** isolate/context 的作用域内（本函数不自带作用域）。失败返回 `{nullptr, 0}`；
 *      缓冲所有权交给调用方，用 `impl::Helper::free` 释放。
 */
std::pair<uint8_t *, size_t> serialize_exception(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_exception);

/**
 * 在目标 realm 里从 {@link serialize_exception} 的字节重建错误值（记录 → 本 realm 的 `Error` /
 * 原样还原原始值）。NOTE 调用方必须已处于**目标** isolate/context 的作用域内；返回空表示记录为空/无效。
 */
v8::Local<v8::Value> rebuild_error_from_bytes(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const uint8_t *p_data, size_t p_size);

/** 在 `p_isolate`/`p_context` 里造一个 `Error`。 */
v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message);

/**
 * 重建 Error 上"未携带字段清单"所用的注册表 symbol 键（宿主脚本用 `Symbol.for(...)` 读回）。
 * 只为把 `error_record` 收在这个模块里（`jsb_bridge_module_loader.cpp` 需要把这个键暴露给 JS）。
 */
const char *untransferred_symbol_key();

/**
 * 按 JS 语义在 `p_isolate`/`p_context` 里抛出：`p_error` 非空就抛它，否则按 `p_message` 抛。
 * NOTE 不用各腿的 `Isolate::ThrowException(value)`（会把值寄存进 TryCatch 槽，污染后续 `has_caught()`）。
 */
void throw_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message, const v8::Local<v8::Value> &p_error);

/**
 * 把**当前** isolate/context 里的 `p_exception` 搬到 `p_target_isolate`/`p_target_context` 并抛出，一步到位。
 * 调用点在源作用域内把异常值交出来即可（如 shadow realm 的 `evaluate`）。
 */
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

	/** NOTE:
		我们无法为用户收集所有内嵌的 godot 对象，他们可能嵌套在 Array, Dictioanry, 子节点，非 godot 属性，meta data，静态变量...等等
		不应该提供一个不完备的功能，应该由用户自己处理转移对象，
	*/
	// for (const Variant &explicit_transfer : explicit_node_transfers) {
	// 	if (explicit_transfer.get_type() == Variant::OBJECT) {
	// 		Object *object = explicit_transfer;

	// 		if (const Node *node = Object::cast_to<Node>(object)) {
	// 			append_node_descendants_for_transfer(from_env, transfers, node);
	// 		}
	// 	}
	// }

	return true;
}
} //namespace jsb::cross_isolate