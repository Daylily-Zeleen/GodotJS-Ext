#include "string_ext.h"

#if JSB_USE_JS_TYPE_EXTENSION

#	include "jsb_type_convert.h"
#	include "v8-platform.h"

namespace jsb::type_extension {
namespace string_ext {

#	define PREPARE(info, this_name)                                                                                                 \
		v8::Isolate *isolate = info.GetIsolate();                                                                                    \
		JSB_ISOLATE_SCOPE(isolate);                                                                                                  \
		const v8::HandleScope handle_scope(isolate);                                                                                 \
		v8::Local<v8::String> __this_name##_arg = info.This().As<v8::String>();                                                      \
		if (__this_name##_arg.IsEmpty() || !__this_name##_arg->IsObject()) {                                                         \
			jsb_throw(isolate, vformat("BadThis, desire a object, but %s", 0, impl::Helper::to_string(isolate, __this_name##_arg))); \
			return;                                                                                                                  \
		}                                                                                                                            \
		String this_name = impl::Helper::to_string(isolate, __this_name##_arg.As<v8::String>());

#	define GET_STR_ARG(name, idx)                                                                                                                           \
		v8::Local<v8::Value> __##name##_arg = info[idx];                                                                                                     \
		if (__##name##_arg.IsEmpty() || !(__##name##_arg)->IsString()) {                                                                                     \
			jsb_throw(isolate, vformat("Invalid argument at index %s, desire a string, but %s", idx + 1, impl::Helper::to_string(isolate, __##name##_arg))); \
			return;                                                                                                                                          \
		}                                                                                                                                                    \
		String name = impl::Helper::to_string(isolate, __##name##_arg.As<v8::String>());

// Path
void getExtension(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, path.get_extension()));
}

void getBasename(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, path.get_basename()));
}
void pathJoin(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, base);
	GET_STR_ARG(path, 0);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, base.path_join(path)));
}
void hasExtension(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	GET_STR_ARG(ext, 0);
	info.GetReturnValue().Set(v8::Boolean::New(isolate, path.get_extension().to_lower() == ext));
}

void isAbsolutePath(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(v8::Boolean::New(isolate, path.is_absolute_path()));
}
void isRelativePath(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(v8::Boolean::New(isolate, path.is_relative_path()));
}
void isResourceFile(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(v8::Boolean::New(isolate, path.begins_with("res://") && path.find("::") == -1));
}
void getBaseDir(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, path.get_base_dir()));
}
void getFile(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, path.get_file()));
}
void simplifyPath(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(impl::Helper::new_string(isolate, path.simplify_path()));
}
void isNetworkSharePath(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, path);
	info.GetReturnValue().Set(v8::Boolean::New(isolate, path.begins_with("//") || path.begins_with("\\\\")));
}

void hash(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, str);
	info.GetReturnValue().Set(v8::Number::New(isolate, str.hash()));
}
void hash64(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, str);

	const char32_t *chr = str.length() ? str.ptr() : []() {static constexpr char32_t _null = 0; return &_null; }();
	uint64_t hashv = 5381;
	uint64_t c = *chr++;

	while (c) {
		hashv = ((hashv << 5) + hashv) + c; /* hash * 33 + c */
		c = *chr++;
	}

	info.GetReturnValue().Set(v8::BigInt::NewFromUnsigned(isolate, hashv));
}

void format(const v8::FunctionCallbackInfo<v8::Value> &info) {
	PREPARE(info, str);

	v8::Local<v8::Value> values_arg = info[0];
	if (values_arg.IsEmpty()) {
		jsb_throw(isolate, vformat("Invalid argument at index %s, desire a dictionary, but %s", 2, impl::Helper::to_string(isolate, values_arg)));
		return;
	}
	Variant values;
	if (!TypeConvert::js_to_gd_var(isolate, isolate->GetCurrentContext(), values_arg, values)) {
		jsb_throw(isolate, vformat("Invalid argument at index %s, can not convert to Variant: %s", 1, impl::Helper::to_string(isolate, values_arg)));
		return;
	}

	if (info.Length() <= 1) {
		info.GetReturnValue().Set(impl::Helper::new_string(isolate, str.format(values)));
	} else {
		v8::Local<v8::Value> placeholder_arg = info[1];
		if (placeholder_arg.IsEmpty() || !placeholder_arg->IsString()) {
			jsb_throw(isolate, vformat("Invalid argument at index %s, desire a GArray, Dictionary or undefine, but %s", 2, impl::Helper::to_string(isolate, placeholder_arg)));
			return;
		}
		String placeholder = impl::Helper::to_string(isolate, placeholder_arg.As<v8::String>());
		info.GetReturnValue().Set(impl::Helper::new_string(isolate, str.format(values, placeholder)));
	}
}

void register_string_extension(const v8::Local<v8::Context> &context, const v8::Local<v8::Object> &self) {
	v8::Isolate *isolate = context->GetIsolate();
	JSB_ISOLATE_SCOPE(isolate);
	const v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Value> string_constructor;
	if (!self->Get(context, v8::String::NewFromUtf8Literal(isolate, "String")).ToLocal(&string_constructor)
			|| !string_constructor->IsFunction()) {
		JSB_LOG(Warning, "Can not get '%s'.", jsb_typename(String));
		return;
	}
	v8::Local<v8::Value> str_prototype_val;
	if (!string_constructor.As<v8::Function>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "prototype")).ToLocal(&str_prototype_val)
			|| !str_prototype_val->IsObject()) {
		JSB_LOG(Warning, "Can not get prototype of '%s'.", jsb_typename(String));
		return;
	}
	v8::Local<v8::Object> str_prototype = str_prototype_val.As<v8::Object>();

#	define SET_EXTENSION_FUNCTION(name, function)                                                 \
		{                                                                                          \
			const v8::Local<v8::String> key = v8::String::NewFromUtf8Literal(isolate, (name));     \
			bool has_existing = false;                                                             \
			if (self->HasOwnProperty(context, key).To(&has_existing) && has_existing) {            \
				JSB_LOG(Warning, "'%s' is existing in String.");                                   \
			} else {                                                                               \
				str_prototype->Set(context, key, JSB_NEW_FUNCTION(context, function, {})).Check(); \
			}                                                                                      \
		};

	SET_EXTENSION_FUNCTION("getExtension", getExtension);
	SET_EXTENSION_FUNCTION("getBasename", getBasename);
	SET_EXTENSION_FUNCTION("pathJoin", pathJoin);
	SET_EXTENSION_FUNCTION("hasExtension", hasExtension);
	SET_EXTENSION_FUNCTION("isAbsolutePath", isAbsolutePath);
	SET_EXTENSION_FUNCTION("isRelativePath", isRelativePath);
	SET_EXTENSION_FUNCTION("isResourceFile", isResourceFile);
	SET_EXTENSION_FUNCTION("getBaseDir", getBaseDir);
	SET_EXTENSION_FUNCTION("getFile", getFile);
	SET_EXTENSION_FUNCTION("simplifyPath", simplifyPath);
	SET_EXTENSION_FUNCTION("isNetworkSharePath", isNetworkSharePath);
	SET_EXTENSION_FUNCTION("hash", hash);
	SET_EXTENSION_FUNCTION("hash64", hash64);
	SET_EXTENSION_FUNCTION("format", format);
}

} //namespace string_ext

} //namespace jsb::type_extension

#endif // JSB_USE_JS_TYPE_EXTENSION