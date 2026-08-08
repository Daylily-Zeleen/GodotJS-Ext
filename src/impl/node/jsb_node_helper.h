#pragma once

// Node engine layer helper.
//
// libnode bundles V8, so the plain-V8 helper implementation from the v8
// engine layer works as-is (the V8 API surface comes from libnode's bundled
// v8 headers). We keep this file as the node layer's canonical helper
// provider so the node impl stays self-contained (and can override/append
// helpers here if the node runtime ever needs them).

#include "../v8/jsb_v8_helper.h"
