#pragma once

#include "../../jsb.gen.h"

#include "../shared/jsb_custom_field.h"
#if JSB_PREFER_QUICKJS_NG
#	include "../../third/quickjs-ng/quickjs.h"
#else
#	include "../../third/quickjs/quickjs.h"
#endif
#include "../../internal/jsb_internal.h"

#include <cstdint>
#include <memory>

#define JSB_QUICKJS_LOG(Severity, Format, ...) JSB_LOG_IMPL(quickjs, Severity, Format, ##__VA_ARGS__)

