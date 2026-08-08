#pragma once

#include "../../jsb.config.h"

// two-level indirection so __LINE__ is expanded before token pasting
#define JSB_CONCAT2_(a, b) a##b
#define JSB_CONCAT_(a, b) JSB_CONCAT2_(a, b)
#define JSB_LINE_NAME_(prefix) JSB_CONCAT_(prefix, __LINE__)

#if JSB_USE_V8_LOCKER_PER_ISOLATE_SCOPE && JSB_WITH_V8
#	include <v8-locker.h>
#	define JSB_ISOLATE_SCOPE(isolate) \
		v8::Locker JSB_LINE_NAME_(jsb_locker_)(isolate); \
		v8::Isolate::Scope JSB_LINE_NAME_(jsb_isolate_scope_)(isolate)
#else
#	define JSB_ISOLATE_SCOPE(isolate) v8::Isolate::Scope JSB_LINE_NAME_(jsb_isolate_scope_)(isolate)
#endif
