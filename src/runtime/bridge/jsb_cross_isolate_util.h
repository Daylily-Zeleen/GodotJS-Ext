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