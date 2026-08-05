#ifndef GODOTJS_WEB_PCH_H
#define GODOTJS_WEB_PCH_H

#include "../../internal/jsb_internal.h"
#include "../../jsb.gen.h"

#include <cstdint>
#include <memory>

#define JSB_WEB_LOG(Severity, Format, ...) JSB_LOG_IMPL(web, Severity, Format, ##__VA_ARGS__)

#include "../shared/jsb_custom_field.h"
#include "jsb_web_interop.h"

#endif
