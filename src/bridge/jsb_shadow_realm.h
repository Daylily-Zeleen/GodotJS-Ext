#pragma once

#include "jsb_bridge_pch.h"

namespace jsb {

class ShadowRealm {
public:
	static void register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_self);
	// release all shadow_realms, call from main thread (GodotJSScriptLanguage::finish)
	static void finish_all();
};

} //namespace jsb

