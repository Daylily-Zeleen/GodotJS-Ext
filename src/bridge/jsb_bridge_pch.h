#pragma once

#include <cstdint>

#include "../jsb.config.h"
#include "../jsb.gen.h"
#include "../jsb_version.h"

#include "../compat/jsb_compat.h"

#if JSB_WITH_WEB
#	include "../impl/web/jsb_web.h"
#elif JSB_WITH_NODE
	// node mode also defines JSB_WITH_V8=1 (libnode bundles V8), keep this branch first
#	include "../impl/node/jsb_node.h"
#elif JSB_WITH_V8
#	include "../impl/v8/jsb_v8.h"
#elif JSB_WITH_QUICKJS
#	include "../impl/quickjs/jsb_quickjs.h"
#elif JSB_WITH_JAVASCRIPTCORE
#	include "../impl/jsc/jsb_jsc.h"
#else
#	error unknown javascript runtime
#endif

#include "../impl/shared/jsb_isolate_scope.h"
#include "../internal/jsb_internal.h"

