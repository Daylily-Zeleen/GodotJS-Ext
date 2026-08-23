/************************************************************************/
/*  static_binding.h                                                    */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Compile-time static bindings entry point.                           */
/*  Design: docs/design/static-bindings.md                              */
/*                                                                      */
/*  Everything in this module is compiled only when the `static_binding` */
/*  scons option is enabled (JSB_WITH_STATIC_BINDINGS). With the option */
/*  off, no generated source enters the build and the runtime behaves   */
/*  bit-identically to the dynamic-binding-only configuration.          */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include "string_names.h"
#include "gen/registry.gen.h"

namespace jsb::static_binding {

// P1+: thunk families (builtin/utility/class methods), default-value
// singletons, registration hooks and the static->dynamic fallback land here.

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
