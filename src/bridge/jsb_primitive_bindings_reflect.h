#pragma once

#include "jsb_bridge_pch.h"

#if !JSB_WITH_STATIC_BINDINGS
namespace jsb {
class Environment;

void register_primitive_bindings_reflect(Environment *p_env);
} //namespace jsb
#endif

