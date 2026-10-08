#pragma once

#include "jsb_bridge_pch.h"

#include <memory>

namespace jsb::error_record {
/**
 * 跨隔离区错误：**记录 -> 复制 -> 重建**（全部用 v8 API 实现，不依赖 JS 侧脚本）。
 *
 * 异常对象不能跨 isolate（跨 isolate 使用 `Local`/`Global` 是 UB），所以错误必须先降级成与 realm 无关的
 * 数据（`ErrorRecord`），复制到目标 isolate 后再用目标 realm 自己的 `Error` 构造器重建。
 *
 * 只搬运**错误自身的字段**与 **JS 基础类型**：
 * - `name` / `message` / `stack`，以及 `cause`（`cause` 本身是 Error 时递归收集其字段）；
 * - 自定义字段只带 JS 基础类型：string / number / boolean / null / undefined；
 * - 数组、普通对象、函数、symbol、BigInt、Godot 类型（`Object` / `Array` / `Dictionary` / ...）**一律不搬运**，
 *   只把路径登记进 `untransferred`。这类数据需要用户自己在发送侧显式转换（`String(x)` / `JSON.stringify(x)` /
 *   摊平成基础类型）之后再挂到 Error 上；
 * - 原始值异常（`throw "boom"` / `throw 42` / `throw null` / `throw undefined`）原样送达；
 * - 遍历只读自有属性的**描述符**（不触发 getter/setter），并且有界（深度 / 节点数 / 字符串长度 / 清单条数）。
 */

/** 未携带清单在重建 Error 上所用的 symbol 键：宿主脚本用 `Symbol.for(kSymbolKey)` 读回 */
constexpr const char *kSymbolKey = "jsb.untransferred";

/** 错误 payload 的包装键：`web` 腿的 worker 消息通道直接搬 JS 值（没有字节流），
 *  用这个键把"错误记录"和普通 `onmessage` 数据区分开。 */
constexpr const char *kErrorPayloadKey = "__jsbError";

/** 与 realm 无关的错误记录 */
struct ErrorRecord {
	/** true 表示异常本身就是原始值（此时只有 `primitive` / `primitive_is_undefined` 有效） */
	bool is_primitive = false;
	Variant primitive;
	bool primitive_is_undefined = false;

	String name = "Error";
	String message;
	String stack;

	/** 自定义字段；**值只可能是 JS 基础类型**（其它类型在采集阶段就被写进 `untransferred`） */
	Dictionary extra;
	/** 未能携带的字段路径 */
	PackedStringArray untransferred;

	/** `cause`（`has_cause` 为 true 时有效）：是 Error 就递归进 `cause_record`，否则是 JS 基础类型 */
	bool has_cause = false;
	bool cause_is_error = false;
	Variant cause_value;
	std::shared_ptr<ErrorRecord> cause_record;
};

/** 采集：p_error 为任意异常值；失败返回的记录里至少带上 message。 */
ErrorRecord capture(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Value> &p_error);

/** 序列化记录（在源 isolate 的作用域内调用）；失败返回 { nullptr, 0 }。 */
std::pair<uint8_t *, size_t> serialize(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record);

/** 反序列化记录（在目标 isolate 的作用域内调用）；失败返回的记录的 message 为空。缓冲需由调用方用 `impl::Helper::free` 释放。 */
ErrorRecord deserialize(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const uint8_t *p_data, size_t p_size);

/** 重建：在 p_context 所属 realm 里还原异常（原始值原样返回；否则是一个 `Error`）。失败返回空。 */
v8::Local<v8::Value> rebuild(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record);

/** 记录 <-> 普通对象（各腿通用的 payload 形态；`serialize`/`deserialize` 在它之上套字节流）。 */
v8::Local<v8::Object> record_to_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const ErrorRecord &p_record);
bool record_from_js(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_payload, ErrorRecord *r_record);

/** 只带 message 的 Error（退化路径 / 兜底文案）。失败返回空。 */
v8::Local<v8::Value> make_error(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context, const String &p_message);
} // namespace jsb::error_record
