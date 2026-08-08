#pragma once

#include "jsb_node_pch.h"

// libnode bundles V8, expose the Node.js version in the runtime info.
#define JSB_IMPL_VERSION_STRING "node-" JSB_STRINGIFY(NODE_MAJOR_VERSION) "." JSB_STRINGIFY(NODE_MINOR_VERSION) "." JSB_STRINGIFY(NODE_PATCH_VERSION)
