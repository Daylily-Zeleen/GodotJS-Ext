#ifndef GODOTJS_ISOLATE_H
#define GODOTJS_ISOLATE_H

#include "jsb_bridge_pch.h"

namespace jsb {

class ShadowRealm_ {
public:
	static void register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_self);
	// release all shadow_realms, call from main thread (GodotJSScriptLanguage::finish)
	static void finish_all();
};

} //namespace jsb

#endif
