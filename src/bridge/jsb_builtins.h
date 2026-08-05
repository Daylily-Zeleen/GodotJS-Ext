#pragma once
#include "jsb_bridge_pch.h"

namespace jsb {
class Builtins {
public:
	static void _require(const v8::FunctionCallbackInfo<v8::Value> &info);
	static void _define(const v8::FunctionCallbackInfo<v8::Value> &info);
};
} //namespace jsb
