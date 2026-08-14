#pragma once

#include <jsb_bridge_pch.h>

#if JSB_USE_JS_TYPE_EXTENSION

namespace jsb::type_extension {
namespace string_ext {
void register_string_extension(const v8::Local<v8::Context> &context, const v8::Local<v8::Object> &self);
} //namespace string_ext

} //namespace jsb::type_extension

#endif // JSB_USE_JS_TYPE_EXTENSION