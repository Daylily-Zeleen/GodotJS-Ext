#pragma once

#include "../../internal/jsb_internal.h"
#include "../../jsb.gen.h"
#include "../shared/jsb_custom_field.h"

#include "../../compat/jsb_ring_buffer.h"

//TODO WARNING: ONLY FOR DEV, NOT SUPPORTED TO BUILD. REMOVE IT AFTER jsc.impl IS READY.
#if !defined(API_AVAILABLE) && !defined(MACOS_ENABLED) && !defined(IOS_ENABLED)
#	define API_AVAILABLE(...)
#endif

#include <JavaScriptCore/JavaScriptCore.h>

//NOTE the header file for WeakRef is private in webkit, we copy it here. hope it's a viable plan :)
#include "JSWeakPrivate.h"

// Apple headers define `nil` as a macro; this leaks into C++ code and can break
// identifiers named `nil` in non-ObjC translation units.
#ifdef nil
#	undef nil
#endif

#include <cstdint>
#include <memory>

#define JSB_JSC_LOG(Severity, Format, ...) JSB_LOG_IMPL(jsc, Severity, Format, ##__VA_ARGS__)

