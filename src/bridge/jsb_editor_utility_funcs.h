#pragma once
#include "jsb_bridge_pch.h"

namespace jsb {
struct EditorUtilityFuncs {
	static void expose(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj);
};
} //namespace jsb
