#pragma once

// Node.js (libnode) engine layer.
//
// libnode bundles V8, so the bridge compiles against the plain V8 API exactly
// like the v8 engine layer. We therefore reuse the v8 impl's helper/catch/class
// components (all pure V8 API) and only replace the process/isolate bootstrap
// with the node-specific `GlobalInitialize` / `NodeRuntime` / `NodeBridge`.

#include "jsb_node_pch.h"

// v8 engine components (pure V8 API, provided by libnode's bundled v8 headers)
#include "../v8/jsb_v8_pch.h"
#include "jsb_node_helper.h"
#include "../v8/jsb_v8_catch.h"
#include "../v8/jsb_v8_class.h"
#include "../v8/jsb_v8_class_builder.h"

#include "jsb_node_typedef.h"

#include "jsb_node_global_init.h"
#include "jsb_node_runtime.h"
#include "jsb_node_bridge.h"
